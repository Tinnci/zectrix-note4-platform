#include "zectrix_radio_arbiter.h"
#include "zectrix_book_storage.h"
#include "zectrix_book_web.h"
#include "zectrix_resource_client.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <vector>

namespace {
using namespace zectrix;
using namespace connectivity;
using namespace companion;
constexpr char kCode[] = "ABCDEFGH2345";

class Store final : public SyncStore {
public:
    std::vector<uint8_t> data;
    bool fail = false;
    StoreReadStatus Load(uint8_t* output, std::size_t capacity, std::size_t* size) override {
        if (data.empty()) return StoreReadStatus::kNotFound;
        assert(capacity >= data.size());
        std::copy(data.begin(), data.end(), output);
        *size = data.size();
        return StoreReadStatus::kOk;
    }
    bool Save(const uint8_t* input, std::size_t size) override {
        if (fail) return false;
        data.assign(input, input + size);
        return true;
    }
};

FrameView Decode(const std::vector<uint8_t>& bytes) {
    FrameView frame{};
    assert(DecodeFrame(bytes.data(), bytes.size(), kProtocolMajor, kProtocolMinor, &frame) == ProtocolStatus::kOk);
    return frame;
}

class Sender final : public SyncFrameSender {
public:
    struct Sent { uint32_t time; std::vector<uint8_t> bytes; };
    std::vector<Sent> sent;
    std::size_t delivered = 0;
    uint32_t now = 0;
    bool busy = false;
    LinkResult SendSyncFrame(const uint8_t* bytes, std::size_t size) override {
        if (busy) return LinkResult::kBusy;
        sent.push_back({now, {bytes, bytes + size}});
        return LinkResult::kOk;
    }
    void Deliver(SyncSession& to, bool drop) {
        while (delivered < sent.size()) {
            const auto frame = Decode(sent[delivered++].bytes);
            if (!drop) assert(to.Receive(frame));
        }
    }
    std::vector<uint32_t> StateTimes() const {
        std::vector<uint32_t> times;
        for (const auto& frame : sent) {
            if (Decode(frame.bytes).header.message_class == MessageClass::kDurableState) times.push_back(frame.time);
        }
        return times;
    }
};

void Queue(SyncEngine& engine, uint16_t count, uint32_t revision = 1) {
    std::array<uint8_t, kDurableValueCapacity> value{};
    value.fill(static_cast<uint8_t>(revision));
    for (uint16_t key = 1; key <= count; ++key) {
        assert(engine.PutDurableState(key, revision, value.data(), value.size()) == SyncStatus::kOk);
    }
}

struct Peers {
    Store device_store, phone_store;
    SyncEngine device, phone;
    SyncSession device_session{device}, phone_session{phone};
    Sender device_tx, phone_tx;
    RadioArbiter arbiter;
    uint32_t device_sequence = 1, phone_sequence = 1;
    Peers() {
        assert(device.Initialize(device_store) == SyncStatus::kOk);
        assert(phone.Initialize(phone_store) == SyncStatus::kOk);
    }
    void Start(uint32_t now) {
        const auto device_cursors = device.Cursors(), phone_cursors = phone.Cursors();
        assert(device_session.Start(phone_cursors, now) == SyncStatus::kOk);
        assert(phone_session.Start(device_cursors, now) == SyncStatus::kOk);
        device_sequence = phone_sequence = 1;
    }
    void Tick(uint32_t now, bool awaiting_phone = false, bool drop_device = false, bool drop_phone = false) {
        device_tx.now = phone_tx.now = now;
        arbiter.PollSync(device_session, device_tx, device_sequence, now, awaiting_phone);
        device_tx.Deliver(phone_session, drop_device);
        phone_session.Poll(phone_tx, phone_sequence, now);
        phone_tx.Deliver(device_session, drop_phone);
    }
};

class Radio final : public BookTransferRadio {
public:
    bool claimed = false, fail_stop = false;
    unsigned starts = 0, stops = 0;
    WifiDriverResult Start(BookTransferMode, const WifiCredentials&) override {
        assert(!claimed);
        ++starts; claimed = true;
        return WifiDriverResult::kPending;
    }
    WifiDriverResult Poll(BookTransferMode, char* address, std::size_t capacity) override {
        assert(claimed && capacity >= 16);
        std::strcpy(address, "192.168.4.1");
        return WifiDriverResult::kReady;
    }
    WifiDriverResult Stop() override {
        ++stops;
        if (fail_stop) return WifiDriverResult::kUnavailable;
        claimed = false;
        return WifiDriverResult::kReady;
    }
};

class Server final : public BookTransferServer {
public:
    BookWebApi api;
    bool fail_stop = false;
    bool Start(storage::BookStorage& books, const char* code, uint32_t now) override {
        api.Start(books, code, now); return true;
    }
    BookTransferProgress Progress() const override { return api.Progress(); }
    bool Stop() override { api.Cancel(); return !fail_stop; }
};

class Request final : public BookHttpRequest {
public:
    uint32_t& now;
    std::function<void()> after_io;
    std::string input, output;
    std::size_t offset = 0, reads = 0, max_read = 0;
    int status = 0;
    explicit Request(uint32_t& time, BookHttpMethod verb) : now(time) {
        method = verb;
        uri = "/api/books/radio.txt";
    }
    bool Header(const char*, char* value, std::size_t capacity) override {
        assert(capacity >= 20); std::strcpy(value, "Bearer ABCDEFGH2345"); return true;
    }
    int Read(uint8_t* data, std::size_t capacity) override {
        const auto size = std::min(capacity, input.size() - offset);
        std::memcpy(data, input.data() + offset, size);
        offset += size; ++reads; max_read = std::max(max_read, capacity);
        now += 10;
        if (after_io) after_io();
        return static_cast<int>(size);
    }
    bool Respond(int code, const char*, const char*) override { status = code; return true; }
    bool Write(const char* data, std::size_t size) override {
        output.append(data, size); now += 10;
        if (after_io) after_io();
        return true;
    }
    bool Finish() override { return true; }
    uint32_t NowMs() const override { return now; }
};

void TestStreamedBooksAndBidirectionalSync(const std::filesystem::path& root) {
    for (const auto mode : {BookTransferMode::Hotspot, BookTransferMode::Station}) {
        const auto directory = root / (mode == BookTransferMode::Hotspot ? "ap" : "sta");
        std::filesystem::create_directory(directory);
        storage::BookStorage books(directory.c_str());
        assert(books.BeginManagement() == ESP_OK);
        Radio radio; Server server; BookTransfer transfer(radio, server);
        Peers peers;
        Queue(peers.device, kDurableKeyCapacity);
        Queue(peers.phone, kDurableKeyCapacity);
        peers.Start(0);
        uint32_t now = 0;
        assert(transfer.Begin(books, mode, {}, kCode, now));
        auto tick = [&] {
            transfer.Poll(now);
            peers.arbiter.Update(WifiBackendState::kStopped, transfer.Snapshot(), radio.claimed, now);
            peers.Tick(now);
            assert(peers.device_session.Status() == SyncSessionStatus::kActive);
        };
        tick();
        assert(peers.arbiter.Mode() == RadioMode::kWifiBurst && peers.device_tx.sent.empty());
        Request upload(now, BookHttpMethod::Put);
        upload.input.assign(256 * 1024, 'x');
        upload.content_length = upload.input.size();
        upload.after_io = [&] {
            tick();
            assert(transfer.Snapshot().client_active && peers.arbiter.Mode() == RadioMode::kWifiBurst);
        };
        assert(server.api.Handle(upload) && upload.status == 200);
        assert(upload.max_read == 1024 && upload.reads == 256 && upload.offset == upload.input.size());
        std::ifstream stored(directory / "radio.txt", std::ios::binary);
        assert(std::string(std::istreambuf_iterator<char>(stored), {}) == upload.input);
        assert(peers.device_session.Converged() && peers.phone_session.Converged());
        const auto sent = peers.device_tx.StateTimes();
        assert(sent.size() == kDurableKeyCapacity && sent.front() == 250 && sent.back() == 2000);
        for (std::size_t i = 1; i < sent.size(); ++i) assert(sent[i] - sent[i - 1] >= RadioArbiter::kSyncIntervalMs);
        assert(peers.arbiter.NextSyncWakeMs(peers.device_session, now, false) == UINT32_MAX);

        // The same server progress must cover downloads, not only upload bytes.
        Queue(peers.device, kDurableKeyCapacity, 2);
        Request download(now, BookHttpMethod::Get);
        download.after_io = upload.after_io;
        assert(server.api.Handle(download) && download.status == 200 && download.output == upload.input);
        assert(peers.device_session.Converged());
        tick();
        assert(!transfer.Snapshot().client_active && peers.arbiter.Mode() == RadioMode::kWifiBurst);
        now = transfer.Snapshot().activity_ms + RadioArbiter::kWifiQuietMs;
        tick();
        assert(peers.arbiter.Mode() == RadioMode::kSharedIdle);
        Queue(peers.device, 1, 3);
        peers.Tick(now);
        assert(peers.device_session.Converged() && peers.device_tx.StateTimes().back() == now);
        assert(transfer.Stop() && !radio.claimed && radio.starts == 1 && radio.stops == 1);
        tick();
        assert(peers.arbiter.Mode() == RadioMode::kCompanion);
    }
    std::puts("PASS: 256 KiB AP/STA upload and download with bidirectional sync; eight outbound states converge in 2000 simulated ms.");
}

void TestControlBusyRetryAndReconnect() {
    Peers peers;
    Queue(peers.device, 2);
    peers.Start(0);
    peers.arbiter.Update(WifiBackendState::kTransferring, {}, true, 0);
    peers.Tick(249);
    assert(peers.device_tx.sent.empty());
    assert(peers.arbiter.NextSyncWakeMs(peers.device_session, 249, false) == 1);
    peers.device_tx.busy = true;
    peers.Tick(250);
    assert(peers.device_sequence == 2 && peers.device_tx.sent.empty());
    peers.device_tx.busy = false;
    peers.Tick(260, false, false, true);
    assert(peers.device_tx.sent.size() == 1);
    const auto original = peers.device_tx.sent.front().bytes;
    peers.Tick(3259, false, false, true);
    assert(peers.device_tx.sent.size() == 1);
    peers.Tick(3260, false, false, true);
    assert(peers.device_tx.sent.size() == 2 && peers.device_tx.sent.back().bytes == original);
    assert(peers.device.PendingDurableCount() == 2);
    peers.device_session.Disconnect();
    peers.phone_session.Disconnect();
    assert(peers.arbiter.NextSyncWakeMs(peers.device_session, 3300, false) == UINT32_MAX);
    Queue(peers.device, 1, 2);
    assert(peers.device.Initialize(peers.device_store) == SyncStatus::kOk);
    assert(peers.phone.Initialize(peers.phone_store) == SyncStatus::kOk);
    peers.Start(3400);
    peers.Tick(3400);
    peers.Tick(3650);
    assert(peers.device_session.Converged() && peers.phone_session.Converged());

    for (const bool store_failure : {false, true}) {
        Peers control;
        Queue(control.device, 1);
        Queue(control.phone, 1);
        control.Start(0);
        control.arbiter.Update(WifiBackendState::kStartingStation, {}, true, 0);
        control.device_store.fail = store_failure;
        control.Tick(1);
        control.Tick(2);
        assert(control.device_tx.sent.size() == 1);
        assert(Decode(control.device_tx.sent.front().bytes).header.message_class == MessageClass::kControl);
        assert(control.device_sequence == 1);
        assert(control.device_session.Status() == (store_failure ? SyncSessionStatus::kStoreError : SyncSessionStatus::kActive));
    }

    Peers stalled;
    Queue(stalled.device, 1);
    stalled.Start(0);
    stalled.arbiter.Update(WifiBackendState::kTransferring, {}, true, 0);
    for (const uint32_t now : {250U, 3250U, 6250U, 9250U}) stalled.Tick(now, false, true, true);
    assert(stalled.device_tx.sent.size() == 3 && stalled.device_session.Status() == SyncSessionStatus::kTimeout);
}

void TestBookCancellation(const std::filesystem::path& root) {
    std::filesystem::create_directory(root / "cancel");
    storage::BookStorage books((root / "cancel").c_str());
    for (unsigned reason = 0; reason < 6; ++reason) {
        Radio radio; Server server; BookTransfer transfer(radio, server);
        RadioArbiter arbiter;
        assert(books.BeginManagement() == ESP_OK);
        assert(transfer.Begin(books, BookTransferMode::Hotspot, {}, kCode, 0));
        transfer.Poll(0);
        arbiter.Update(WifiBackendState::kStopped, transfer.Snapshot(), radio.claimed, 0);
        assert(arbiter.Mode() == RadioMode::kWifiBurst);
        if (reason == 0) assert(transfer.Stop());
        if (reason == 1) transfer.Poll(10, false);
        if (reason == 2) transfer.Poll(10, true, false);
        if (reason == 3) transfer.Poll(BookTransfer::kIdleMs);
        if (reason >= 4) {
            radio.fail_stop = reason == 4;
            server.fail_stop = reason == 5;
            assert(!transfer.Stop());
            arbiter.Update(WifiBackendState::kStopped, transfer.Snapshot(), radio.claimed, 10);
            assert(arbiter.Mode() == RadioMode::kWifiStopping && radio.claimed && transfer.Busy());
            assert(books.BeginManagement() == ESP_ERR_INVALID_STATE);
            radio.fail_stop = server.fail_stop = false;
            transfer.Poll(20);
        }
        arbiter.Update(WifiBackendState::kStopped, transfer.Snapshot(), radio.claimed, 30);
        assert(arbiter.Mode() == RadioMode::kCompanion && !radio.claimed && !transfer.Busy());
    }
}

class Credentials final : public WifiCredentialSource {
public:
    WifiCredentialResult Load(WifiCredentials* value) override {
        std::strcpy(value->ssid.data(), "test-ap"); return WifiCredentialResult::kAvailable;
    }
};
class Wifi final : public WifiBackendDriver {
public:
    bool claimed = false, fail_stop = false;
    WifiDriverResult StartStation(const WifiCredentials&) override { claimed = true; return WifiDriverResult::kPending; }
    WifiDriverResult PollAssociation() override { return WifiDriverResult::kReady; }
    WifiDriverResult PollIp() override { return WifiDriverResult::kReady; }
    WifiDriverResult Resolve(ResourceCapability) override { return WifiDriverResult::kReady; }
    WifiDriverResult OpenTls(ResourceCapability) override { return WifiDriverResult::kReady; }
    WifiDriverResult Fetch(ResourceCapability, uint8_t* output, std::size_t capacity, std::size_t* size) override {
        assert(capacity >= 2); output[0] = 'o'; output[1] = 'k'; *size = 2;
        return WifiDriverResult::kReady;
    }
    WifiDriverResult StopStation() override {
        if (fail_stop) return WifiDriverResult::kUnavailable;
        claimed = false; return WifiDriverResult::kReady;
    }
};
class Phone final : public PhoneResourceSender {
public:
    LinkResult SendResource(uint32_t, const ResourceRequestMessage&) override { return LinkResult::kOk; }
};

void TestResourceAndPhoneOwnership() {
    for (unsigned scenario = 0; scenario < 3; ++scenario) {
        Credentials credentials; Wifi driver; Phone phone;
        WifiBackend wifi(&credentials, &driver);
        ResourceClient client(wifi, phone);
        Peers peers;
        Queue(peers.device, 2);
        peers.Start(0);
        ConnectivityConditions conditions{};
        conditions.wifi_credentials_available = true;
        conditions.battery_percent = 80;
        conditions.user_policy = UserConnectivityPolicy::kWifiOnly;
        driver.fail_stop = scenario == 1;
        assert(client.Begin(1, {}, 0));
        for (uint32_t now = 0; now <= 1000 + WifiBackend::kStopTimeoutMs; now += 10) {
            if (scenario == 2 && now >= 30) conditions.battery_percent = 5;
            client.Poll(conditions, now);
            peers.arbiter.Update(client.WifiState(), {}, driver.claimed, now);
            peers.Tick(now, client.AwaitingPhone());
            if (scenario == 1 && now == 1000) {
                ResourceResponse pending;
                assert(!client.TakeResponse(&pending));
                assert(client.Busy() && driver.claimed);
                assert(peers.arbiter.Mode() == RadioMode::kWifiStopping);
            }
        }
        ResourceResponse response;
        assert(client.TakeResponse(&response));
        assert(response.wifi_operation == (scenario == 2 ? WifiOperationResult::kCancelled : WifiOperationResult::kSuccess));
        assert(response.wifi_stop == (scenario == 1 ? WifiStopResult::kFailure : WifiStopResult::kSuccess));
        assert(peers.device_session.Converged());
        assert(peers.arbiter.Mode() == (scenario == 1 ? RadioMode::kWifiStopping : RadioMode::kCompanion));
        assert(driver.claimed == (scenario == 1));
        driver.fail_stop = false;
        assert(driver.StopStation() == WifiDriverResult::kReady);
    }

    Credentials credentials; Wifi driver; Phone phone;
    WifiBackend wifi(&credentials, &driver);
    ResourceClient client(wifi, phone);
    ConnectivityConditions conditions{};
    conditions.phone = PhoneAvailability::kConnected;
    conditions.user_policy = UserConnectivityPolicy::kPhoneOnly;
    ResourceRequestMessage request{};
    request.timeout_ms = 10000;
    assert(client.Begin(42, request, 0));
    client.Poll(conditions, 0);
    assert(client.AwaitingPhone() && !driver.claimed);
    Peers peers;
    Queue(peers.device, 1); peers.Start(0);
    peers.Tick(0, client.AwaitingPhone());
    peers.Tick(9000, client.AwaitingPhone());
    assert(peers.device_tx.sent.empty() && peers.device_session.Status() == SyncSessionStatus::kActive);
    assert(peers.arbiter.NextSyncWakeMs(peers.device_session, 9000, true) == UINT32_MAX);
    ResourceResponseMessage response{};
    response.status = ResourceStatus::kNotAuthorized;
    assert(client.AcceptPhoneResponse(42, response, 9001));
    peers.Tick(9001, client.AwaitingPhone());
    assert(peers.device_session.Converged());
}

void TestWraparoundAndModeChurn() {
    constexpr uint32_t start = UINT32_MAX - 100;
    Peers peers;
    Queue(peers.device, 2); peers.Start(start);
    BookTransferSnapshot books{};
    books.state = BookTransferState::Starting;
    peers.arbiter.Update(WifiBackendState::kStopped, books, false, start);
    peers.Tick(start + 249);
    assert(peers.device_tx.sent.empty());
    assert(peers.arbiter.NextSyncWakeMs(peers.device_session, start + 249, false) == 1);
    for (uint32_t elapsed = 250; elapsed <= 500; ++elapsed) {
        books.state = elapsed % 2 == 0 ? BookTransferState::Sharing : BookTransferState::Stopping;
        books.client_active = true;
        books.activity_ms = start + elapsed + 1;
        peers.arbiter.Update(WifiBackendState::kStopped, books, true, start + elapsed);
        peers.Tick(start + elapsed);
    }
    assert(peers.device_session.Converged() && peers.device_tx.StateTimes().size() == 2);
    books.state = BookTransferState::Sharing; books.client_active = false;
    books.activity_ms = start + 500;
    peers.arbiter.Update(WifiBackendState::kStopped, books, true, start + 999);
    assert(peers.arbiter.Mode() == RadioMode::kWifiBurst);
    peers.arbiter.Update(WifiBackendState::kStopped, books, true, start + 1000);
    assert(peers.arbiter.Mode() == RadioMode::kSharedIdle);
    peers.arbiter.Update(WifiBackendState::kStopped, {}, true, start + 1001);
    assert(peers.arbiter.Mode() == RadioMode::kWifiBurst);
    peers.arbiter.Update(WifiBackendState::kStopped, {}, false, start + 1002);
    assert(peers.arbiter.Mode() == RadioMode::kCompanion);
    assert(peers.arbiter.NextSyncWakeMs(peers.device_session, start + 1002, false) == UINT32_MAX);
}
}  // namespace

int main(int argc, char** argv) {
    assert(argc == 2);
    TestStreamedBooksAndBidirectionalSync(argv[1]);
    TestControlBusyRetryAndReconnect();
    TestBookCancellation(argv[1]);
    TestResourceAndPhoneOwnership();
    TestWraparoundAndModeChurn();
    std::puts("PASS: radio admission fairness, ACK/NACK, retries, reconnect, cancellation, power policy and timer wraparound.");
}
