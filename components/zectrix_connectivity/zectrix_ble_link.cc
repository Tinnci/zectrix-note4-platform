#include "zectrix_ble_link.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <new>

#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_store.h"
#include "host/util/util.h"
#include "nimble/ble.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "os/os_mbuf.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "zectrix_companion_protocol.h"

#include "esp_log.h"

extern "C" void ble_store_config_init(void);

namespace zectrix::connectivity {
namespace {

constexpr char kTag[] = "zectrix_ble";
constexpr char kDeviceName[] = "Zectrix Note4";
constexpr int32_t kDefaultPairingWindowMs = 120000;
constexpr int32_t kReconnectAdvertisingForever = BLE_HS_FOREVER;
constexpr uint16_t kNoConnection = BLE_HS_CONN_HANDLE_NONE;
constexpr std::size_t kReceivedQueueCapacity = 2;
constexpr int kMaximumBondedPeers = 8;
constexpr std::size_t kMinimumPacketCapacity =
    companion::kFragmentHeaderSize + 1;

const ble_uuid128_t kServiceUuid = BLE_UUID128_INIT(
    0x9d, 0x29, 0x8f, 0x67, 0x4a, 0xe2, 0x47, 0xbb,
    0x95, 0x08, 0xa9, 0x15, 0x3f, 0x5e, 0xc1, 0x10);
const ble_uuid128_t kPhoneToNoteUuid = BLE_UUID128_INIT(
    0x9d, 0x29, 0x8f, 0x67, 0x4a, 0xe2, 0x47, 0xbb,
    0x95, 0x08, 0xa9, 0x15, 0x3f, 0x5e, 0xc1, 0x11);
const ble_uuid128_t kNoteToPhoneUuid = BLE_UUID128_INIT(
    0x9d, 0x29, 0x8f, 0x67, 0x4a, 0xe2, 0x47, 0xbb,
    0x95, 0x08, 0xa9, 0x15, 0x3f, 0x5e, 0xc1, 0x12);

int NotifyOnlyAccess(uint16_t, uint16_t, ble_gatt_access_ctxt* context,
                     void*) {
    if (context == nullptr) return BLE_ATT_ERR_UNLIKELY;
    switch (context->op) {
        case BLE_GATT_ACCESS_OP_READ_CHR:
            return BLE_ATT_ERR_READ_NOT_PERMITTED;
        case BLE_GATT_ACCESS_OP_WRITE_CHR:
            return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
        default:
            return BLE_ATT_ERR_UNLIKELY;
    }
}

bool IsBondedPeer(uint16_t connection_handle) {
    ble_gap_conn_desc descriptor{};
    if (ble_gap_conn_find(connection_handle, &descriptor) != 0) return false;
    std::array<ble_addr_t, kMaximumBondedPeers> peers{};
    int peer_count = 0;
    if (ble_store_util_bonded_peers(peers.data(), &peer_count,
                                    peers.size()) != 0) {
        return false;
    }
    for (int index = 0; index < peer_count; ++index) {
        if (peers[index].type == descriptor.peer_id_addr.type &&
            std::memcmp(peers[index].val, descriptor.peer_id_addr.val,
                        sizeof(peers[index].val)) == 0) {
            return true;
        }
    }
    return false;
}

}  // namespace

struct BleLink::Impl {
    struct QueuedFrame {
        std::array<uint8_t, companion::kMaximumFrameSize> data{};
        std::size_t size = 0;
        uint32_t session_id = 0;
        bool occupied = false;
    };

    enum class AdvertiseIntent : uint8_t {
        kNone = 0,
        kReconnect,
        kPairing,
    };

    static Impl* instance;
    SemaphoreHandle_t lock = nullptr;
    SemaphoreHandle_t session_event = nullptr;
    SemaphoreHandle_t advertise_mutex = nullptr;
    std::atomic<AdvertiseIntent> pending_advertise{AdvertiseIntent::kNone};
    std::atomic<int32_t> advertise_window_ms{kDefaultPairingWindowMs};
    BleState state = BleState::kStopped;
    bool initialized = false;
    bool synchronized = false;
    bool pairing_requested = false;
    bool pairing_authorized = false;
    bool subscribed = false;
    bool transmit_active = false;
    bool received_borrowed = false;
    uint8_t own_address_type = BLE_OWN_ADDR_PUBLIC;
    uint16_t connection_handle = kNoConnection;
    uint16_t notify_value_handle = 0;
    uint32_t session_id = 0;
    uint16_t transmit_frame_id = 1;
    std::size_t transmit_fragment = 0;
    std::size_t transmit_packet_capacity = 20;
    uint32_t passkey = 0;
    bool passkey_pending = false;
    bool encrypted = false;
    bool authenticated = false;
    bool bonded = false;
    companion::FragmentReassembler reassembler;
    std::array<uint8_t, companion::kMaximumFrameSize> transmit_frame{};
    std::size_t transmit_size = 0;
    std::array<QueuedFrame, kReceivedQueueCapacity> received{};
    std::size_t received_read = 0;
    std::size_t received_write = 0;
    std::size_t received_count = 0;
    bool discard_received_after_release = false;

    void ClearReceivedQueue() {
        if (received_borrowed) {
            discard_received_after_release = true;
            return;
        }
        for (auto& frame : received) frame = {};
        received_read = 0;
        received_write = 0;
        received_count = 0;
        discard_received_after_release = false;
    }

    // Host GAP callbacks publish a coalesced advertising intent instead of
    // restarting advertising synchronously from inside the current host event.
    // The session task owns ProcessAdvertiseRequest(); xSemaphoreGive() is a
    // non-blocking wake, so host callbacks never wait on a full event queue.
    void RequestAdvertise(bool pairing, int32_t pairing_window_ms) {
        const AdvertiseIntent intent = pairing
            ? AdvertiseIntent::kPairing
            : AdvertiseIntent::kReconnect;
        if (intent == AdvertiseIntent::kPairing) {
            advertise_window_ms.store(pairing_window_ms,
                                      std::memory_order_release);
            pending_advertise.store(intent, std::memory_order_release);
        } else {
            AdvertiseIntent expected = AdvertiseIntent::kNone;
            pending_advertise.compare_exchange_strong(
                expected, intent, std::memory_order_release,
                std::memory_order_relaxed);
        }
        xSemaphoreGive(session_event);
    }

    // Runs outside the NimBLE host task, typically from the session owner.
    // Re-checks state before touching GAP so a stale deferred request cannot
    // restart advertising after a newer connection has already been made.
    void ProcessAdvertiseRequest() {
        AdvertiseIntent intent = AdvertiseIntent::kNone;
        int32_t window_ms = kDefaultPairingWindowMs;
        const char* skip_reason = nullptr;
        xSemaphoreTake(lock, portMAX_DELAY);
        intent = pending_advertise.exchange(AdvertiseIntent::kNone,
                                            std::memory_order_acq_rel);
        window_ms = advertise_window_ms.load(std::memory_order_acquire);
        if (!synchronized) {
            skip_reason = "not_synchronized";
        } else if (state == BleState::kFault) {
            skip_reason = "fault";
        } else if (connection_handle != kNoConnection) {
            skip_reason = "connected";
        }
        xSemaphoreGive(lock);

        if (skip_reason != nullptr) {
            if (intent != AdvertiseIntent::kNone) {
                ESP_LOGI(kTag, "event=advertising_skipped reason=%s",
                         skip_reason);
            }
            return;
        }
        if (intent == AdvertiseIntent::kNone) return;
        if (ble_gap_adv_active()) {
            ESP_LOGI(kTag, "event=advertising_skipped reason=already_active");
            return;
        }
        Advertise(intent == AdvertiseIntent::kPairing, window_ms);
    }

    static int Access(uint16_t connection, uint16_t attribute,
                      ble_gatt_access_ctxt* context, void*) {
        if (instance == nullptr || context == nullptr ||
            context->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
            return BLE_ATT_ERR_UNLIKELY;
        }
        ble_gap_conn_desc descriptor{};
        if (ble_gap_conn_find(connection, &descriptor) != 0 ||
            !descriptor.sec_state.encrypted ||
            !descriptor.sec_state.authenticated || !descriptor.sec_state.bonded) {
            return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
        }
        const uint16_t packet_size = OS_MBUF_PKTLEN(context->om);
        if (packet_size <= companion::kFragmentHeaderSize || packet_size > 512) {
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        std::array<uint8_t, 512> packet{};
        uint16_t copied = 0;
        if (ble_hs_mbuf_to_flat(context->om, packet.data(), packet.size(),
                                &copied) != 0 || copied != packet_size) {
            return BLE_ATT_ERR_UNLIKELY;
        }
        if (xSemaphoreTake(instance->lock, 0) != pdTRUE) {
            return BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        const companion::ProtocolStatus status =
            instance->reassembler.Accept(packet.data(), copied);
        int result = 0;
        bool frame_queued = false;
        if (status == companion::ProtocolStatus::kFrameComplete) {
            if (instance->received_count == kReceivedQueueCapacity) {
                instance->reassembler.Reset();
                result = BLE_ATT_ERR_INSUFFICIENT_RES;
            } else {
                QueuedFrame& destination =
                    instance->received[instance->received_write];
                destination.size = instance->reassembler.Size();
                destination.session_id = instance->session_id;
                std::memcpy(destination.data.data(),
                            instance->reassembler.Data(), destination.size);
                destination.occupied = true;
                instance->received_write =
                    (instance->received_write + 1) % kReceivedQueueCapacity;
                ++instance->received_count;
                instance->reassembler.Reset();
                frame_queued = true;
            }
        } else if (status != companion::ProtocolStatus::kFragmentAccepted) {
            instance->reassembler.Reset();
            result = BLE_ATT_ERR_VALUE_NOT_ALLOWED;
        }
        xSemaphoreGive(instance->lock);
        if (frame_queued) xSemaphoreGive(instance->session_event);
        return result;
    }

    static void HostTask(void*) {
        nimble_port_run();
        nimble_port_freertos_deinit();
    }

    static void OnReset(int) {
        if (instance == nullptr) return;
        xSemaphoreTake(instance->lock, portMAX_DELAY);
        instance->synchronized = false;
        instance->state = BleState::kFault;
        xSemaphoreGive(instance->lock);
        xSemaphoreGive(instance->session_event);
        ESP_LOGE(kTag, "event=host_reset state=fault");
    }

    static void OnSync() {
        if (instance == nullptr) return;
        uint8_t address_type = BLE_OWN_ADDR_PUBLIC;
        if (ble_hs_util_ensure_addr(0) != 0 ||
            ble_hs_id_infer_auto(0, &address_type) != 0) {
            OnReset(0);
            return;
        }
        bool start_pairing = false;
        xSemaphoreTake(instance->lock, portMAX_DELAY);
        instance->own_address_type = address_type;
        instance->synchronized = true;
        if (instance->state != BleState::kFault) instance->state = BleState::kIdle;
        start_pairing = instance->pairing_requested;
        xSemaphoreGive(instance->lock);
        xSemaphoreGive(instance->session_event);
        instance->RequestAdvertise(start_pairing, kDefaultPairingWindowMs);
    }

    static int GapEvent(ble_gap_event* event, void*) {
        if (instance == nullptr || event == nullptr) return 0;
        switch (event->type) {
            case BLE_GAP_EVENT_CONNECT: {
                if (event->connect.status != 0) {
                    ESP_LOGW(kTag, "gap: connect failed: %d",
                             event->connect.status);
                    bool pairing = false;
                    xSemaphoreTake(instance->lock, portMAX_DELAY);
                    pairing = instance->pairing_requested;
                    xSemaphoreGive(instance->lock);
                    instance->RequestAdvertise(pairing, kDefaultPairingWindowMs);
                    return 0;
                }
                uint32_t session = 0;
                bool pairing_authorized = false;
                xSemaphoreTake(instance->lock, portMAX_DELAY);
                ++instance->session_id;
                if (instance->session_id == 0) ++instance->session_id;
                session = instance->session_id;
                instance->connection_handle = event->connect.conn_handle;
                instance->state = BleState::kConnectedUnsecured;
                instance->pairing_authorized = instance->pairing_requested;
                pairing_authorized = instance->pairing_authorized;
                instance->pairing_requested = false;
                xSemaphoreGive(instance->lock);
                xSemaphoreGive(instance->session_event);
                ESP_LOGI(kTag, "event=connected session=%lu",
                         static_cast<unsigned long>(session));
                if (!pairing_authorized &&
                    !IsBondedPeer(event->connect.conn_handle)) {
                    ESP_LOGW(kTag,
                             "event=connection_rejected session=%lu reason=untrusted_peer",
                             static_cast<unsigned long>(session));
                    ble_gap_terminate(event->connect.conn_handle,
                                      BLE_ERR_AUTH_FAIL);
                    return 0;
                }
                ESP_LOGI(kTag, "event=security_started session=%lu",
                         static_cast<unsigned long>(session));
                ble_gap_security_initiate(event->connect.conn_handle);
                return 0;
            }

            case BLE_GAP_EVENT_DISCONNECT: {
                uint32_t disconnected_session = 0;
                xSemaphoreTake(instance->lock, portMAX_DELAY);
                disconnected_session = instance->session_id;
                instance->connection_handle = kNoConnection;
                instance->subscribed = false;
                instance->transmit_active = false;
                instance->transmit_size = 0;
                instance->reassembler.Reset();
                instance->ClearReceivedQueue();
                instance->state = BleState::kIdle;
                instance->pairing_authorized = false;
                instance->passkey = 0;
                instance->passkey_pending = false;
                instance->encrypted = false;
                instance->authenticated = false;
                instance->bonded = false;
                xSemaphoreGive(instance->lock);
                xSemaphoreGive(instance->session_event);
                ESP_LOGI(kTag,
                         "event=disconnected session=%lu reason=%d",
                         static_cast<unsigned long>(disconnected_session),
                         event->disconnect.reason);
                instance->RequestAdvertise(false, kDefaultPairingWindowMs);
                return 0;
            }

            case BLE_GAP_EVENT_ADV_COMPLETE: {
                bool pairing_expired = false;
                xSemaphoreTake(instance->lock, portMAX_DELAY);
                pairing_expired = instance->state == BleState::kPairing;
                instance->pairing_requested = false;
                if (instance->connection_handle == kNoConnection &&
                    instance->state != BleState::kFault) {
                    instance->state = BleState::kIdle;
                }
                xSemaphoreGive(instance->lock);
                if (pairing_expired) {
                    ESP_LOGI(kTag, "event=pairing_window_closed reason=expired");
                }
                instance->RequestAdvertise(false, kDefaultPairingWindowMs);
                return 0;
            }

            case BLE_GAP_EVENT_ENC_CHANGE: {
                ble_gap_conn_desc descriptor{};
                const bool secure = event->enc_change.status == 0 &&
                    ble_gap_conn_find(event->enc_change.conn_handle,
                                      &descriptor) == 0 &&
                    descriptor.sec_state.encrypted &&
                    descriptor.sec_state.authenticated &&
                    descriptor.sec_state.bonded;
                uint32_t secure_session = 0;
                xSemaphoreTake(instance->lock, portMAX_DELAY);
                secure_session = instance->session_id;
                instance->state = secure ? BleState::kConnectedSecured
                                         : BleState::kConnectedUnsecured;
                instance->encrypted = descriptor.sec_state.encrypted;
                instance->authenticated = descriptor.sec_state.authenticated;
                instance->bonded = descriptor.sec_state.bonded;
                if (secure && instance->subscribed) {
                    instance->state = BleState::kTransportReady;
                }
                instance->pairing_authorized = false;
                xSemaphoreGive(instance->lock);
                xSemaphoreGive(instance->session_event);
                ESP_LOGI(kTag,
                         "event=security_complete session=%lu status=%d secure=%d",
                         static_cast<unsigned long>(secure_session),
                         event->enc_change.status, secure ? 1 : 0);
                if (!secure) {
                    ble_gap_terminate(event->enc_change.conn_handle,
                                      BLE_ERR_AUTH_FAIL);
                }
                return 0;
            }

            case BLE_GAP_EVENT_SUBSCRIBE: {
                if (event->subscribe.attr_handle ==
                    instance->notify_value_handle) {
                    uint32_t subscribe_session = 0;
                    ble_gap_conn_desc descriptor{};
                    const bool secure =
                        ble_gap_conn_find(event->subscribe.conn_handle,
                                          &descriptor) == 0 &&
                        descriptor.sec_state.encrypted &&
                        descriptor.sec_state.authenticated &&
                        descriptor.sec_state.bonded;
                    xSemaphoreTake(instance->lock, portMAX_DELAY);
                    subscribe_session = instance->session_id;
                    instance->subscribed = event->subscribe.cur_notify != 0;
                    if (secure) {
                        instance->state = instance->subscribed
                            ? BleState::kTransportReady
                            : BleState::kConnectedSecured;
                    }
                    xSemaphoreGive(instance->lock);
                    xSemaphoreGive(instance->session_event);
                    ESP_LOGI(kTag,
                             "event=notification_subscription session=%lu enabled=%d secure=%d",
                             static_cast<unsigned long>(subscribe_session),
                             event->subscribe.cur_notify != 0 ? 1 : 0,
                             secure ? 1 : 0);
                }
                return 0;
            }

            case BLE_GAP_EVENT_NOTIFY_TX:
                if (event->notify_tx.attr_handle ==
                        instance->notify_value_handle &&
                    event->notify_tx.status == 0) {
                    instance->PumpTransmit();
                } else if (event->notify_tx.attr_handle ==
                           instance->notify_value_handle) {
                    xSemaphoreTake(instance->lock, portMAX_DELAY);
                    instance->transmit_active = false;
                    instance->transmit_size = 0;
                    xSemaphoreGive(instance->lock);
                }
                return 0;

            case BLE_GAP_EVENT_MTU:
                xSemaphoreTake(instance->lock, portMAX_DELAY);
                instance->transmit_packet_capacity =
                    std::max<std::size_t>(kMinimumPacketCapacity,
                                          event->mtu.value - 3U);
                xSemaphoreGive(instance->lock);
                return 0;

            case BLE_GAP_EVENT_PASSKEY_ACTION:
                if (event->passkey.params.action == BLE_SM_IOACT_DISP) {
                    bool authorized = false;
                    xSemaphoreTake(instance->lock, portMAX_DELAY);
                    authorized = instance->pairing_authorized &&
                        instance->connection_handle ==
                            event->passkey.conn_handle;
                    xSemaphoreGive(instance->lock);
                    if (!authorized) return BLE_HS_EAUTHEN;
                    ESP_LOGI(kTag,
                             "event=pairing_challenge session=%lu method=display_only",
                             static_cast<unsigned long>(instance->session_id));
                    ble_sm_io io{};
                    io.action = BLE_SM_IOACT_DISP;
                    // Uniform 000000-999999 passkey: reject the tail above
                    // 10^9 to avoid modulo bias, then take mod 10^6.
                    uint32_t random_value = 0;
                    do {
                        random_value = esp_random();
                    } while (random_value >= 1000000000U);
                    io.passkey = random_value % 1000000U;
                    xSemaphoreTake(instance->lock, portMAX_DELAY);
                    instance->passkey = io.passkey;
                    instance->passkey_pending = true;
                    xSemaphoreGive(instance->lock);
                    return ble_sm_inject_io(event->passkey.conn_handle, &io);
                }
                return BLE_HS_EAUTHEN;

            case BLE_GAP_EVENT_REPEAT_PAIRING:
                // Never delete an existing bond because a remote peer asks.
                return BLE_GAP_REPEAT_PAIRING_IGNORE;

            default:
                return 0;
        }
    }

    int Advertise(bool pairing, int32_t pairing_window_ms) {
        if (advertise_mutex == nullptr) return BLE_HS_EBUSY;
        xSemaphoreTake(advertise_mutex, portMAX_DELAY);
        int result = 0;
        do {
            if (!synchronized || ble_gap_adv_active()) {
                result = 0;
                break;
            }
            ble_hs_adv_fields fields{};
            fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
            fields.name = reinterpret_cast<uint8_t*>(
                const_cast<char*>(kDeviceName));
            fields.name_len = sizeof(kDeviceName) - 1;
            fields.name_is_complete = 1;
            result = ble_gap_adv_set_fields(&fields);
            if (result != 0) {
                ESP_LOGW(kTag, "advertise: set fields failed: %d", result);
                break;
            }
            ble_hs_adv_fields response{};
            response.uuids128 = const_cast<ble_uuid128_t*>(&kServiceUuid);
            response.num_uuids128 = 1;
            response.uuids128_is_complete = 1;
            result = ble_gap_adv_rsp_set_fields(&response);
            if (result != 0) {
                ESP_LOGW(kTag, "advertise: set scan response failed: %d",
                         result);
                break;
            }
            // Reconnect advertising is intentionally non-pairable general
            // discoverability: any central may connect, but encryption and
            // bonding are required, and a new pairing only succeeds inside
            // the local pairing window. This is not a whitelist/directed
            // advertisement.
            ble_gap_adv_params parameters{};
            parameters.conn_mode = BLE_GAP_CONN_MODE_UND;
            parameters.disc_mode = BLE_GAP_DISC_MODE_GEN;
            // Advertising intervals are in 0.625 ms units: pairing 160-240
            // units (100-150 ms), reconnect 2560-3200 units (1.6-2.0 s).
            parameters.itvl_min = pairing ? 0x00a0 : 0x0a00;
            parameters.itvl_max = pairing ? 0x00f0 : 0x0c80;
            const int32_t duration = pairing ? pairing_window_ms
                                             : kReconnectAdvertisingForever;
            result = ble_gap_adv_start(own_address_type, nullptr, duration,
                                       &parameters, GapEvent, nullptr);
            if (result == 0) {
                xSemaphoreTake(lock, portMAX_DELAY);
                state = pairing ? BleState::kPairing : BleState::kAdvertising;
                xSemaphoreGive(lock);
                ESP_LOGI(kTag,
                         "event=advertising_started mode=%s duration_ms=%ld",
                         pairing ? "pairing" : "reconnect",
                         static_cast<long>(duration));
            } else {
                ESP_LOGW(kTag, "advertise: start failed: %d", result);
            }
        } while (false);
        xSemaphoreGive(advertise_mutex);
        return result;
    }

    bool PumpTransmit() {
        std::array<uint8_t, 512> packet{};
        std::size_t packet_size = 0;
        uint16_t connection = kNoConnection;
        uint16_t attribute = 0;
        xSemaphoreTake(lock, portMAX_DELAY);
        if (!transmit_active) {
            xSemaphoreGive(lock);
            return false;
        }
        const std::size_t count = companion::FragmentCount(
            transmit_size, transmit_packet_capacity);
        if (transmit_fragment >= count) {
            transmit_active = false;
            transmit_size = 0;
            xSemaphoreGive(lock);
            return true;
        }
        const auto status = companion::EncodeFragment(
            transmit_frame.data(), transmit_size, transmit_frame_id,
            transmit_fragment, transmit_packet_capacity, packet.data(),
            packet.size(), &packet_size);
        connection = connection_handle;
        attribute = notify_value_handle;
        if (status != companion::ProtocolStatus::kOk) {
            transmit_active = false;
            transmit_size = 0;
            xSemaphoreGive(lock);
            return false;
        }
        ++transmit_fragment;
        xSemaphoreGive(lock);

        os_mbuf* buffer = ble_hs_mbuf_from_flat(packet.data(), packet_size);
        const int result = buffer == nullptr
            ? BLE_HS_ENOMEM
            : ble_gatts_notify_custom(connection, attribute, buffer);
        if (result != 0) {
            xSemaphoreTake(lock, portMAX_DELAY);
            transmit_active = false;
            transmit_size = 0;
            xSemaphoreGive(lock);
            return false;
        }
        return true;
    }
};

BleLink::Impl* BleLink::Impl::instance = nullptr;

namespace {

ble_gatt_chr_def kCharacteristics[] = {
    {
        .uuid = &kPhoneToNoteUuid.u,
        .access_cb = BleLink::Impl::Access,
        .arg = nullptr,
        .descriptors = nullptr,
        .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_AUTHEN,
        .min_key_size = 16,
        .val_handle = nullptr,
        .cpfd = nullptr,
    },
    {
        .uuid = &kNoteToPhoneUuid.u,
        .access_cb = NotifyOnlyAccess,
        .arg = nullptr,
        .descriptors = nullptr,
        .flags = BLE_GATT_CHR_F_NOTIFY,
        .min_key_size = 16,
        .val_handle = nullptr,
        .cpfd = nullptr,
    },
    {},
};

ble_gatt_svc_def kServices[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &kServiceUuid.u,
        .includes = nullptr,
        .characteristics = kCharacteristics,
    },
    {},
};

}  // namespace

BleLink::BleLink() : impl_(new (std::nothrow) Impl()) {}

BleLink::~BleLink() {
    Stop();
    if (impl_ != nullptr && impl_->session_event != nullptr) {
        vSemaphoreDelete(impl_->session_event);
        impl_->session_event = nullptr;
    }
    if (impl_ != nullptr && impl_->lock != nullptr) {
        vSemaphoreDelete(impl_->lock);
        impl_->lock = nullptr;
    }
    if (impl_ != nullptr && impl_->advertise_mutex != nullptr) {
        vSemaphoreDelete(impl_->advertise_mutex);
        impl_->advertise_mutex = nullptr;
    }
    delete impl_;
}

companion::LinkResult BleLink::Initialize() {
    if (impl_ == nullptr) return companion::LinkResult::kUnavailable;
    if (impl_->initialized) return companion::LinkResult::kOk;
    if (Impl::instance != nullptr) return companion::LinkResult::kBusy;
    impl_->lock = xSemaphoreCreateMutex();
    if (impl_->lock == nullptr) return companion::LinkResult::kUnavailable;
    impl_->session_event = xSemaphoreCreateBinary();
    if (impl_->session_event == nullptr) {
        vSemaphoreDelete(impl_->lock);
        impl_->lock = nullptr;
        return companion::LinkResult::kUnavailable;
    }
    impl_->advertise_mutex = xSemaphoreCreateMutex();
    if (impl_->advertise_mutex == nullptr) {
        vSemaphoreDelete(impl_->lock);
        impl_->lock = nullptr;
        vSemaphoreDelete(impl_->session_event);
        impl_->session_event = nullptr;
        return companion::LinkResult::kUnavailable;
    }
    const auto release_primitives = [this]() {
        vSemaphoreDelete(impl_->lock);
        impl_->lock = nullptr;
        vSemaphoreDelete(impl_->session_event);
        impl_->session_event = nullptr;
        vSemaphoreDelete(impl_->advertise_mutex);
        impl_->advertise_mutex = nullptr;
    };
    Impl::instance = impl_;
    const esp_err_t result = nimble_port_init();
    if (result != ESP_OK) {
        ESP_LOGE(kTag, "initialize: nimble_port_init failed: %s",
                 esp_err_to_name(result));
        Impl::instance = nullptr;
        release_primitives();
        return companion::LinkResult::kTransportError;
    }
    ESP_LOGD(kTag, "initialize: nimble_port_init ok");
    ble_hs_cfg.reset_cb = Impl::OnReset;
    ble_hs_cfg.sync_cb = Impl::OnSync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_DISP_ONLY;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 1;
    // LE Secure Connections is enabled and preferred; legacy authenticated
    // pairing remains available as a fallback because
    // CONFIG_BT_NIMBLE_SM_SC_ONLY is not enabled. This is not an SC-only gate.
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC |
                                 BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC |
                                   BLE_SM_PAIR_KEY_DIST_ID;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    const int name_result = ble_svc_gap_device_name_set(kDeviceName);
    if (name_result != 0) {
        ESP_LOGE(kTag, "initialize: device name set failed: %d", name_result);
        nimble_port_deinit();
        Impl::instance = nullptr;
        release_primitives();
        return companion::LinkResult::kTransportError;
    }
    kCharacteristics[1].val_handle = &impl_->notify_value_handle;
    const int count_result = ble_gatts_count_cfg(kServices);
    if (count_result != 0) {
        ESP_LOGE(kTag, "initialize: gatts count failed: %d", count_result);
        nimble_port_deinit();
        Impl::instance = nullptr;
        release_primitives();
        return companion::LinkResult::kTransportError;
    }
    const int add_result = ble_gatts_add_svcs(kServices);
    if (add_result != 0) {
        ESP_LOGE(kTag, "initialize: gatts add failed: %d", add_result);
        nimble_port_deinit();
        Impl::instance = nullptr;
        release_primitives();
        return companion::LinkResult::kTransportError;
    }
    ESP_LOGD(kTag, "initialize: gatts configured");
    ble_store_config_init();
    impl_->initialized = true;
    impl_->state = BleState::kIdle;
    nimble_port_freertos_init(Impl::HostTask);
    ESP_LOGD(kTag, "initialize: host task started");
    return companion::LinkResult::kOk;
}

companion::LinkResult BleLink::Start() {
    return Start(static_cast<uint32_t>(kDefaultPairingWindowMs));
}

companion::LinkResult BleLink::Start(uint32_t pairing_window_ms) {
    if (impl_ == nullptr || !impl_->initialized) {
        return companion::LinkResult::kUnavailable;
    }
    if (pairing_window_ms == 0 || pairing_window_ms > 0x7fffffffU) {
        return companion::LinkResult::kInvalidArgument;
    }
    xSemaphoreTake(impl_->lock, portMAX_DELAY);
    if (impl_->connection_handle != kNoConnection ||
        impl_->pairing_requested || impl_->state == BleState::kPairing) {
        xSemaphoreGive(impl_->lock);
        return companion::LinkResult::kBusy;
    }
    impl_->pairing_requested = true;
    const bool synchronized = impl_->synchronized;
    xSemaphoreGive(impl_->lock);
    ESP_LOGI(kTag, "event=pairing_window_opened duration_ms=%lu",
             static_cast<unsigned long>(pairing_window_ms));
    if (!synchronized) return companion::LinkResult::kOk;
    if (ble_gap_adv_active()) {
        if (ble_gap_adv_stop() != 0) {
            xSemaphoreTake(impl_->lock, portMAX_DELAY);
            impl_->pairing_requested = false;
            xSemaphoreGive(impl_->lock);
            return companion::LinkResult::kTransportError;
        }
    }
    if (impl_->Advertise(true, static_cast<int32_t>(pairing_window_ms)) != 0) {
        xSemaphoreTake(impl_->lock, portMAX_DELAY);
        impl_->pairing_requested = false;
        xSemaphoreGive(impl_->lock);
        return companion::LinkResult::kTransportError;
    }
    return companion::LinkResult::kOk;
}

companion::LinkResult BleLink::Send(const uint8_t* frame,
                                    std::size_t frame_size) {
    return SendForSession(0, frame, frame_size);
}

companion::LinkResult BleLink::SendForSession(uint32_t expected_session_id,
                                              const uint8_t* frame,
                                              std::size_t frame_size) {
    if (impl_ == nullptr || frame == nullptr || frame_size == 0) {
        return companion::LinkResult::kInvalidArgument;
    }
    if (frame_size > companion::kMaximumFrameSize) {
        return companion::LinkResult::kPayloadTooLarge;
    }
    companion::FrameView validated{};
    if (companion::DecodeFrame(frame, frame_size, companion::kProtocolMajor,
                               companion::kProtocolMinor, &validated) !=
        companion::ProtocolStatus::kOk) {
        return companion::LinkResult::kInvalidArgument;
    }
    xSemaphoreTake(impl_->lock, portMAX_DELAY);
    if (expected_session_id != 0 &&
        impl_->session_id != expected_session_id) {
        xSemaphoreGive(impl_->lock);
        return companion::LinkResult::kUnavailable;
    }
    if (impl_->state != BleState::kTransportReady || !impl_->subscribed) {
        xSemaphoreGive(impl_->lock);
        return companion::LinkResult::kUnavailable;
    }
    if (impl_->transmit_active) {
        xSemaphoreGive(impl_->lock);
        return companion::LinkResult::kBusy;
    }
    std::memcpy(impl_->transmit_frame.data(), frame, frame_size);
    impl_->transmit_size = frame_size;
    impl_->transmit_fragment = 0;
    ++impl_->transmit_frame_id;
    if (impl_->transmit_frame_id == 0) impl_->transmit_frame_id = 1;
    impl_->transmit_active = true;
    xSemaphoreGive(impl_->lock);
    return impl_->PumpTransmit() ? companion::LinkResult::kOk
                                 : companion::LinkResult::kTransportError;
}

void BleLink::Stop() {
    if (impl_ == nullptr || !impl_->initialized) return;
    if (ble_gap_adv_active()) ble_gap_adv_stop();
    if (impl_->connection_handle != kNoConnection) {
        ble_gap_terminate(impl_->connection_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
    nimble_port_stop();
    nimble_port_deinit();
    xSemaphoreTake(impl_->lock, portMAX_DELAY);
    impl_->initialized = false;
    impl_->synchronized = false;
    impl_->pairing_requested = false;
    impl_->pairing_authorized = false;
    impl_->connection_handle = kNoConnection;
    impl_->state = BleState::kStopped;
    impl_->pending_advertise.store(Impl::AdvertiseIntent::kNone,
                                   std::memory_order_release);
    xSemaphoreGive(impl_->lock);
    Impl::instance = nullptr;
}

companion::LinkStatus BleLink::Status() const {
    const BleState state = State();
    if (state == BleState::kStopped) return companion::LinkStatus::kStopped;
    if (state == BleState::kTransportReady) {
        return companion::LinkStatus::kReady;
    }
    if (state == BleState::kFault) return companion::LinkStatus::kFailed;
    return companion::LinkStatus::kStarting;
}

BleState BleLink::State() const {
    if (impl_ == nullptr || impl_->lock == nullptr) return BleState::kStopped;
    xSemaphoreTake(impl_->lock, portMAX_DELAY);
    const BleState state = impl_->state;
    xSemaphoreGive(impl_->lock);
    return state;
}

BleSnapshot BleLink::Snapshot() const {
    BleSnapshot snapshot{};
    if (impl_ == nullptr || impl_->lock == nullptr) return snapshot;
    xSemaphoreTake(impl_->lock, portMAX_DELAY);
    snapshot.state = impl_->state;
    snapshot.session_id = impl_->session_id;
    snapshot.local_pairing_active = impl_->pairing_requested ||
                                    impl_->pairing_authorized ||
                                    impl_->state == BleState::kPairing;
    snapshot.encrypted = impl_->encrypted;
    snapshot.authenticated = impl_->authenticated;
    snapshot.bonded = impl_->bonded;
    snapshot.notifications_enabled = impl_->subscribed;
    xSemaphoreGive(impl_->lock);
    return snapshot;
}

bool BleLink::TakePairingPasskey(uint32_t* passkey) {
    if (impl_ == nullptr || passkey == nullptr || impl_->lock == nullptr) {
        return false;
    }
    xSemaphoreTake(impl_->lock, portMAX_DELAY);
    const bool available = impl_->passkey_pending;
    if (available) {
        *passkey = impl_->passkey;
        impl_->passkey = 0;
        impl_->passkey_pending = false;
    }
    xSemaphoreGive(impl_->lock);
    return available;
}

bool BleLink::WaitForSessionEvent(uint32_t timeout_ms) {
    if (impl_ == nullptr || impl_->session_event == nullptr) return false;
    const TickType_t timeout = timeout_ms == UINT32_MAX
        ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTake(impl_->session_event, timeout) == pdTRUE;
}

void BleLink::WakeSessionWaiter() {
    if (impl_ != nullptr && impl_->session_event != nullptr) {
        xSemaphoreGive(impl_->session_event);
    }
}

bool BleLink::TakeReceivedFrame(ReceivedFrame* frame) {
    if (impl_ == nullptr || frame == nullptr || impl_->lock == nullptr) {
        return false;
    }
    xSemaphoreTake(impl_->lock, portMAX_DELAY);
    if (impl_->received_count == 0 || impl_->received_borrowed) {
        xSemaphoreGive(impl_->lock);
        return false;
    }
    const Impl::QueuedFrame& source = impl_->received[impl_->received_read];
    frame->data = source.data.data();
    frame->size = source.size;
    frame->session_id = source.session_id;
    impl_->received_borrowed = true;
    xSemaphoreGive(impl_->lock);
    return true;
}

void BleLink::ReleaseReceivedFrame() {
    if (impl_ == nullptr || impl_->lock == nullptr) return;
    xSemaphoreTake(impl_->lock, portMAX_DELAY);
    if (impl_->received_borrowed && impl_->received_count != 0) {
        impl_->received[impl_->received_read] = {};
        impl_->received_read =
            (impl_->received_read + 1) % kReceivedQueueCapacity;
        --impl_->received_count;
        impl_->received_borrowed = false;
    }
    if (impl_->discard_received_after_release && !impl_->received_borrowed) {
        impl_->ClearReceivedQueue();
    }
    xSemaphoreGive(impl_->lock);
}

companion::LinkResult BleLink::ClearBonds() {
    if (impl_ == nullptr || !impl_->initialized) {
        return companion::LinkResult::kUnavailable;
    }
    if (impl_->connection_handle != kNoConnection) {
        return companion::LinkResult::kBusy;
    }
    const int result = ble_store_clear();
    ESP_LOGI(kTag, "event=bonds_cleared result=%d", result);
    return result == 0 ? companion::LinkResult::kOk
                       : companion::LinkResult::kTransportError;
}

void BleLink::ProcessAdvertiseRequest() {
    if (impl_ == nullptr || !impl_->initialized) return;
    impl_->ProcessAdvertiseRequest();
}

bool BleLink::WithCurrentTransportSession(
    uint32_t expected_session_id, const std::function<void()>& action) {
    if (impl_ == nullptr || impl_->lock == nullptr || !action) return false;
    xSemaphoreTake(impl_->lock, portMAX_DELAY);
    const bool current = impl_->state == BleState::kTransportReady &&
                         impl_->session_id == expected_session_id;
    if (current) action();
    xSemaphoreGive(impl_->lock);
    return current;
}

}  // namespace zectrix::connectivity
