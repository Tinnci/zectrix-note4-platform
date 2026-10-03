#include "note4_self_test.h"

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "acoustic_selftest.h"
#include "audio_codec.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "note4_board.h"
#include "note4_board_config.h"
#include "note4_nfc.h"
#include "note4_input_service.h"
#include "note4_power_service.h"
#include "note4_storage_service.h"
#include "note4_system_service.h"
#include "note4_time_service.h"
#include "sdkconfig.h"
#if CONFIG_NOTE4_ENABLE_WIFI
#include "note4_wifi_esp_driver.h"
#endif

namespace {

constexpr char kTag[] = "note4_test";
constexpr int kRfRequiredHits = 3;
constexpr int64_t kInteractiveTimeoutUs = 60LL * 1000 * 1000;

void SetText(char* target, size_t size, const char* format, ...) {
    va_list args;
    va_start(args, format);
    std::vsnprintf(target, size, format, args);
    va_end(args);
}

Note4TestUpdate MakeUpdate(Note4TestId id, Note4TestState state,
                             const char* hint) {
    Note4TestUpdate update;
    update.id = id;
    update.state = state;
    SetText(update.title, sizeof(update.title), "%s TEST",
            Note4SelfTest::Name(id));
    SetText(update.hint, sizeof(update.hint), "%s", hint ? hint : "");
    return update;
}

void Publish(const Note4SelfTest::UpdateCallback& callback,
             const Note4TestUpdate& update) {
    if (callback) {
        callback(update);
    }
}

bool IsCancelOrShutdown(note4::input::InputService* input,
                        TickType_t timeout,
                        Note4TestResult* result,
                        note4::input::InputEvent* received = nullptr) {
    note4::input::InputEvent event;
    if (input == nullptr || !input->Wait(&event, timeout)) {
        return false;
    }
    if (received != nullptr) {
        *received = event;
    }
    if (event.button == note4::input::Button::Down &&
        event.action == note4::input::Action::LongPress) {
        *result = Note4TestResult::kShutdown;
        return true;
    }
    if (event.button == note4::input::Button::Ok &&
        event.action == note4::input::Action::LongPress) {
        *result = Note4TestResult::kCancelled;
        return true;
    }
    return false;
}

std::vector<uint8_t> BuildUriNdef(const std::string& uri) {
    uint8_t prefix = 0x00;
    std::string suffix = uri;
    if (uri.rfind("https://www.", 0) == 0) {
        prefix = 0x02;
        suffix = uri.substr(12);
    } else if (uri.rfind("https://", 0) == 0) {
        prefix = 0x04;
        suffix = uri.substr(8);
    } else if (uri.rfind("http://www.", 0) == 0) {
        prefix = 0x01;
        suffix = uri.substr(11);
    } else if (uri.rfind("http://", 0) == 0) {
        prefix = 0x03;
        suffix = uri.substr(7);
    }
    std::vector<uint8_t> payload = {prefix};
    payload.insert(payload.end(), suffix.begin(), suffix.end());
    std::vector<uint8_t> message = {
        0xD1, 0x01, static_cast<uint8_t>(payload.size()), 'U'};
    message.insert(message.end(), payload.begin(), payload.end());
    return message;
}

std::vector<uint8_t> BuildNdefStorage(const std::vector<uint8_t>& message) {
    std::vector<uint8_t> data = {0x03, static_cast<uint8_t>(message.size())};
    data.insert(data.end(), message.begin(), message.end());
    data.push_back(0xFE);
    return data;
}

}  // namespace

const char* Note4SelfTest::Name(Note4TestId id) {
    switch (id) {
        case Note4TestId::kRf: return "WI-FI RF";
        case Note4TestId::kAudio: return "AUDIO";
        case Note4TestId::kRtc: return "RTC";
        case Note4TestId::kCharge: return "POWER";
        case Note4TestId::kLed: return "LED";
        case Note4TestId::kButtons: return "BUTTONS";
        case Note4TestId::kNfc: return "NFC";
        default: return "UNKNOWN";
    }
}

Note4TestResult Note4SelfTest::Run(
    Note4TestId id, const UpdateCallback& callback) {
    if (board_ == nullptr || input_ == nullptr || power_ == nullptr ||
        time_ == nullptr || storage_ == nullptr || system_ == nullptr) {
        return Note4TestResult::kFail;
    }
    input_->Drain();
    switch (id) {
        case Note4TestId::kRf: return RunRf(callback);
        case Note4TestId::kAudio: return RunAudio(callback);
        case Note4TestId::kRtc: return RunRtc(callback);
        case Note4TestId::kCharge: return RunCharge(callback);
        case Note4TestId::kLed: return RunLed(callback);
        case Note4TestId::kButtons: return RunButtons(callback);
        case Note4TestId::kNfc: return RunNfc(callback);
        default: return Note4TestResult::kFail;
    }
}

Note4TestResult Note4SelfTest::RunRf(const UpdateCallback& callback) {
#if CONFIG_NOTE4_ENABLE_WIFI
    auto update = MakeUpdate(Note4TestId::kRf, Note4TestState::kRunning,
                             "Scanning 2.4 GHz access points...");
    Publish(callback, update);
    note4::connectivity::EspWifiBackendDriver radio;
    if (radio.StartScan() != note4::connectivity::WifiDriverResult::kPending) {
        update.state = Note4TestState::kFail;
        SetText(update.hint, sizeof(update.hint), "Wi-Fi is busy or unavailable");
        Publish(callback, update);
        return Note4TestResult::kFail;
    }

    const char* target = CONFIG_NOTE4_QUALIFICATION_RF_TARGET_SSID;
    const bool qualification = target[0] != '\0';
    int consecutive_hits = 0;
    const int64_t deadline = time_->MonotonicMicroseconds() + kInteractiveTimeoutUs;
    while (time_->MonotonicMicroseconds() < deadline) {
        note4::connectivity::WifiScanSnapshot scan{};
        const auto scan_result = radio.PollScan(target, &scan);
        if (scan_result == note4::connectivity::WifiDriverResult::kPending) {
            Note4TestResult control;
            if (IsCancelOrShutdown(input_, pdMS_TO_TICKS(50), &control)) return control;
            continue;
        }
        if (scan_result != note4::connectivity::WifiDriverResult::kReady) {
            update.state = Note4TestState::kFail;
            SetText(update.hint, sizeof(update.hint), "Wi-Fi scan failed");
            Publish(callback, update);
            return Note4TestResult::kFail;
        }
        const uint16_t count = scan.access_point_count;
        const int best_rssi = scan.best_rssi;
        const bool target_found = scan.target_found;
        const std::string best_name(scan.best_ssid.data());

        const bool hit = qualification
                             ? target_found &&
                                   best_rssi >= CONFIG_NOTE4_QUALIFICATION_RF_THRESHOLD_DBM
                             : count > 0;
        consecutive_hits = hit ? consecutive_hits + 1 : 0;
        SetText(update.details[0].data(), update.details[0].size(),
                "MODE: %s", qualification ? "QUALIFICATION" : "GENERIC SCAN");
        SetText(update.details[1].data(), update.details[1].size(),
                "AP COUNT: %u", static_cast<unsigned>(count));
        SetText(update.details[2].data(), update.details[2].size(),
                "BEST: %s", best_name.empty() ? "NOT FOUND" : best_name.c_str());
        SetText(update.details[3].data(), update.details[3].size(),
                "RSSI: %d dBm   HITS: %d/%d", best_rssi, consecutive_hits,
                qualification ? kRfRequiredHits : 1);
        Publish(callback, update);

        if ((!qualification && hit) ||
            consecutive_hits >= kRfRequiredHits) {
            if (radio.StopStation() != note4::connectivity::WifiDriverResult::kReady) {
                update.state = Note4TestState::kFail;
                SetText(update.hint, sizeof(update.hint), "Wi-Fi stop failed");
                Publish(callback, update);
                return Note4TestResult::kFail;
            }
            update.state = Note4TestState::kPass;
            SetText(update.hint, sizeof(update.hint), "Wi-Fi radio is operational");
            Publish(callback, update);
            return Note4TestResult::kPass;
        }
        Note4TestResult control;
        if (IsCancelOrShutdown(input_, pdMS_TO_TICKS(250), &control)) {
            return control;
        }
    }
    update.state = Note4TestState::kFail;
    SetText(update.hint, sizeof(update.hint), "RF test timed out");
    Publish(callback, update);
    return Note4TestResult::kFail;
#else
    Publish(callback, MakeUpdate(Note4TestId::kRf, Note4TestState::kSkipped,
                                 "Wi-Fi is disabled in this firmware"));
    return Note4TestResult::kSkipped;
#endif
}

Note4TestResult Note4SelfTest::RunAudio(const UpdateCallback& callback) {
    auto update = MakeUpdate(Note4TestId::kAudio,
                             Note4TestState::kRunning,
                             "Playing, recording and decoding AFSK...");
    Publish(callback, update);
    std::array<uint8_t, 6> mac = {};
    if (system_ == nullptr || system_->ReadWifiMac(&mac) != ESP_OK) {
        update.state = Note4TestState::kFail;
        SetText(update.hint, sizeof(update.hint), "System identity is not available");
        Publish(callback, update);
        return Note4TestResult::kFail;
    }
    AudioCodec* codec = board_->PrepareAudio();
    const AcousticSelftestSummary summary = AcousticSelftest().Run(codec, mac);
    update.state = summary.pass ? Note4TestState::kPass
                                : Note4TestState::kFail;
    SetText(update.hint, sizeof(update.hint), "%s",
            summary.pass ? "Speaker and microphone loopback passed"
                         : "Audio loopback failed");
    SetText(update.details[0].data(), update.details[0].size(),
            "RESULT: %s", summary.pass ? "PASS" : "FAIL");
    SetText(update.details[1].data(), update.details[1].size(),
            "ROUND: %d", summary.round);
    SetText(update.details[2].data(), update.details[2].size(),
            "CENTER FREQUENCY: %d Hz", summary.fc);
    SetText(update.details[3].data(), update.details[3].size(),
            "REASON: %s", AcousticSelftest::FailureReasonToString(summary.reason));
    Publish(callback, update);
    return summary.pass ? Note4TestResult::kPass
                        : Note4TestResult::kFail;
}

Note4TestResult Note4SelfTest::RunRtc(const UpdateCallback& callback) {
    auto update = MakeUpdate(Note4TestId::kRtc,
                             Note4TestState::kRunning,
                             "Waiting for a 1 second RTC interrupt...");
    Publish(callback, update);
    if (time_ == nullptr || !time_->RtcAvailable()) {
        update.state = Note4TestState::kFail;
        SetText(update.hint, sizeof(update.hint), "PCF8563 was not detected");
        Publish(callback, update);
        return Note4TestResult::kFail;
    }

    for (int attempt = 1; attempt <= 3; ++attempt) {
        bool fired = false;
        if (time_->StartRtcCountdown(1) == ESP_OK) {
            const int64_t deadline = time_->MonotonicMicroseconds() + 2000000;
            while (time_->MonotonicMicroseconds() < deadline) {
                note4::time::RtcTimerStatus status;
                if (time_->ReadRtcTimerStatus(&status) != ESP_OK) {
                    break;
                }
                const bool gpio_hit = status.interrupt_active;
                const bool flag_hit = status.flag_set;
                SetText(update.details[0].data(), update.details[0].size(),
                        "ATTEMPT: %d/3", attempt);
                SetText(update.details[1].data(), update.details[1].size(),
                        "INT GPIO: %s", gpio_hit ? "ACTIVE" : "WAIT");
                SetText(update.details[2].data(), update.details[2].size(),
                        "TIMER FLAG: %s", flag_hit ? "SET" : "CLEAR");
                Publish(callback, update);
                if (gpio_hit || flag_hit) {
                    fired = true;
                    break;
                }
                Note4TestResult control;
                if (IsCancelOrShutdown(input_, pdMS_TO_TICKS(50), &control)) {
                    time_->StopRtcCountdown();
                    return control;
                }
            }
        }
        time_->StopRtcCountdown();
        time_->ClearRtcTimerFlag();
        if (fired) {
            update.state = Note4TestState::kPass;
            SetText(update.hint, sizeof(update.hint), "RTC timer interrupt passed");
            Publish(callback, update);
            return Note4TestResult::kPass;
        }
    }
    update.state = Note4TestState::kFail;
    SetText(update.hint, sizeof(update.hint), "RTC timer did not fire");
    Publish(callback, update);
    return Note4TestResult::kFail;
}

Note4TestResult Note4SelfTest::RunCharge(const UpdateCallback& callback) {
    auto update = MakeUpdate(Note4TestId::kCharge,
                             Note4TestState::kRunning,
                             "Connect USB power to verify charging");
    const int64_t deadline = time_->MonotonicMicroseconds() + kInteractiveTimeoutUs;
    while (time_->MonotonicMicroseconds() < deadline) {
        const note4::power::PowerSnapshot power = power_->ReadSnapshot();
        SetText(update.details[0].data(), update.details[0].size(),
                "USB POWER: %s", power.external_power_present ? "PRESENT" : "ABSENT");
        SetText(update.details[1].data(), update.details[1].size(),
                "CHARGING: %s   FULL: %s",
                power.charging ? "YES" : "NO",
                power.charge_full ? "YES" : "NO");
        SetText(update.details[2].data(), update.details[2].size(),
                "BATTERY: %s%u%%", power.battery_valid ? "" : "-- / ",
                power.battery_valid ? power.battery_percent : 0);
        SetText(update.details[3].data(), update.details[3].size(),
                "VOLTAGE: %u mV%s", power.battery_mv,
                power.battery_absent ? "  NO BATTERY" : "");
        Publish(callback, update);
        const bool pass = power.charging ||
                          (power.charge_full && power.battery_valid &&
                           power.battery_percent > 97);
        if (pass && !power.battery_absent) {
            update.state = Note4TestState::kPass;
            SetText(update.hint, sizeof(update.hint), "Power and charging path passed");
            Publish(callback, update);
            return Note4TestResult::kPass;
        }
        if (power.charge_fault) {
            break;
        }
        Note4TestResult control;
        if (IsCancelOrShutdown(input_, pdMS_TO_TICKS(300), &control)) {
            return control;
        }
    }
    update.state = Note4TestState::kFail;
    SetText(update.hint, sizeof(update.hint), "Charging was not detected");
    Publish(callback, update);
    return Note4TestResult::kFail;
}

Note4TestResult Note4SelfTest::RunLed(const UpdateCallback& callback) {
    auto update = MakeUpdate(Note4TestId::kLed,
                             Note4TestState::kRunning,
                             "Is the power LED blinking?");
    SetText(update.details[0].data(), update.details[0].size(),
            "LED GPIO: 3");
    SetText(update.details[1].data(), update.details[1].size(),
            "OK = PASS");
    SetText(update.details[2].data(), update.details[2].size(),
            "DOWN = FAIL");
    Publish(callback, update);
    bool led_on = false;
    TickType_t last_toggle = xTaskGetTickCount();
    const int64_t deadline = time_->MonotonicMicroseconds() + kInteractiveTimeoutUs;
    while (time_->MonotonicMicroseconds() < deadline) {
        const TickType_t now = xTaskGetTickCount();
        if (now - last_toggle >= pdMS_TO_TICKS(500)) {
            led_on = !led_on;
            board_->SetPowerLed(led_on);
            last_toggle = now;
        }
        note4::input::InputEvent event;
        if (!input_->Wait(&event, pdMS_TO_TICKS(50))) {
            continue;
        }
        if (event.button == note4::input::Button::Down &&
            event.action == note4::input::Action::LongPress) {
            board_->SetPowerLed(false);
            return Note4TestResult::kShutdown;
        }
        if (event.button == note4::input::Button::Ok &&
            event.action == note4::input::Action::LongPress) {
            board_->SetPowerLed(false);
            return Note4TestResult::kCancelled;
        }
        if (event.action == note4::input::Action::Click &&
            event.button == note4::input::Button::Ok) {
            board_->SetPowerLed(false);
            update.state = Note4TestState::kPass;
            SetText(update.hint, sizeof(update.hint), "LED confirmed by operator");
            Publish(callback, update);
            return Note4TestResult::kPass;
        }
        if (event.action == note4::input::Action::Click &&
            event.button == note4::input::Button::Down) {
            board_->SetPowerLed(false);
            update.state = Note4TestState::kFail;
            SetText(update.hint, sizeof(update.hint), "LED rejected by operator");
            Publish(callback, update);
            return Note4TestResult::kFail;
        }
    }
    board_->SetPowerLed(false);
    update.state = Note4TestState::kFail;
    SetText(update.hint, sizeof(update.hint), "LED confirmation timed out");
    Publish(callback, update);
    return Note4TestResult::kFail;
}

Note4TestResult Note4SelfTest::RunButtons(const UpdateCallback& callback) {
    auto update = MakeUpdate(Note4TestId::kButtons,
                             Note4TestState::kRunning,
                             "Press the requested buttons in order");
    constexpr std::array<note4::input::Button, 3> sequence = {
        note4::input::Button::Ok, note4::input::Button::Up,
        note4::input::Button::Down};
    constexpr std::array<const char*, 3> names = {"OK", "UP", "DOWN"};
    size_t stage = 0;
    const int64_t deadline = time_->MonotonicMicroseconds() + kInteractiveTimeoutUs;
    while (stage < sequence.size() && time_->MonotonicMicroseconds() < deadline) {
        for (size_t i = 0; i < names.size(); ++i) {
            SetText(update.details[i].data(), update.details[i].size(),
                    "%s  %s", i < stage ? "PASS" : (i == stage ? ">>" : "--"),
                    names[i]);
        }
        Publish(callback, update);
        note4::input::InputEvent event;
        if (!input_->Wait(&event, pdMS_TO_TICKS(200))) {
            continue;
        }
        if (event.button == note4::input::Button::Down &&
            event.action == note4::input::Action::LongPress) {
            return Note4TestResult::kShutdown;
        }
        if (event.button == note4::input::Button::Ok &&
            event.action == note4::input::Action::LongPress) {
            return Note4TestResult::kCancelled;
        }
        if (event.action != note4::input::Action::Click) {
            continue;
        }
        if (event.button == sequence[stage]) {
            ++stage;
        } else {
            stage = 0;
            SetText(update.hint, sizeof(update.hint),
                    "Wrong order - sequence restarted");
        }
    }
    update.state = stage == sequence.size() ? Note4TestState::kPass
                                            : Note4TestState::kFail;
    SetText(update.hint, sizeof(update.hint), "%s",
            stage == sequence.size() ? "All three buttons passed"
                                     : "Button test timed out");
    Publish(callback, update);
    return stage == sequence.size() ? Note4TestResult::kPass
                                    : Note4TestResult::kFail;
}

Note4TestResult Note4SelfTest::RunNfc(const UpdateCallback& callback) {
    auto update = MakeUpdate(Note4TestId::kNfc,
                             Note4TestState::kRunning,
                             "Backing up NFC memory...");
    Publish(callback, update);
    Note4Nfc* nfc = board_->nfc();
    if (nfc == nullptr) {
        update.state = Note4TestState::kFail;
        SetText(update.hint, sizeof(update.hint), "NFC device was not detected");
        Publish(callback, update);
        return Note4TestResult::kFail;
    }

    std::vector<uint8_t> backup(nfc->GetUserDataCapacity());
    if (nfc->ReadUserData(0, backup.data(), backup.size()) != ESP_OK) {
        update.state = Note4TestState::kFail;
        SetText(update.hint, sizeof(update.hint), "Could not back up NFC memory");
        Publish(callback, update);
        return Note4TestResult::kFail;
    }

    const std::string url = CONFIG_NOTE4_QUALIFICATION_NFC_URL;
    const std::vector<uint8_t> expected_ndef = BuildUriNdef(url);
    const std::vector<uint8_t> expected_raw = BuildNdefStorage(expected_ndef);
    SetText(update.hint, sizeof(update.hint), "Writing and verifying NDEF URL...");
    SetText(update.details[0].data(), update.details[0].size(), "URL: %s", url.c_str());
    Publish(callback, update);

    const esp_err_t write = nfc->WriteUriNdef(url);
    std::vector<uint8_t> actual_raw(expected_raw.size());
    std::vector<uint8_t> actual_ndef;
    const esp_err_t raw_read = write == ESP_OK
                                   ? nfc->ReadUserData(0, actual_raw.data(), actual_raw.size())
                                   : write;
    const esp_err_t ndef_read = raw_read == ESP_OK
                                    ? nfc->ReadNdef(&actual_ndef)
                                    : raw_read;
    const bool verified = write == ESP_OK && raw_read == ESP_OK &&
                          ndef_read == ESP_OK && actual_raw == expected_raw &&
                          actual_ndef == expected_ndef;
    SetText(update.details[1].data(), update.details[1].size(),
            "WRITE: %s", esp_err_to_name(write));
    SetText(update.details[2].data(), update.details[2].size(),
            "RAW + NDEF: %s", verified ? "VERIFIED" : "FAILED");
    Publish(callback, update);
    if (!verified) {
        nfc->WriteUserData(0, backup.data(), backup.size());
        update.state = Note4TestState::kFail;
        SetText(update.hint, sizeof(update.hint), "NFC write/read verification failed");
        Publish(callback, update);
        return Note4TestResult::kFail;
    }

    SetText(update.hint, sizeof(update.hint), "Tap a phone on the NFC antenna");
    const int64_t deadline = time_->MonotonicMicroseconds() + kInteractiveTimeoutUs;
    int stable = 0;
    while (time_->MonotonicMicroseconds() < deadline && stable < 3) {
        const bool field = nfc->HasField();
        stable = field ? stable + 1 : 0;
        SetText(update.details[3].data(), update.details[3].size(),
                "PHONE FIELD: %s  %d/3", field ? "DETECTED" : "WAIT", stable);
        Publish(callback, update);
        Note4TestResult control;
        if (IsCancelOrShutdown(input_, pdMS_TO_TICKS(200), &control)) {
            nfc->WriteUserData(0, backup.data(), backup.size());
            return control;
        }
    }

    const esp_err_t restore = nfc->WriteUserData(0, backup.data(), backup.size());
    const bool pass = stable >= 3 && restore == ESP_OK;
    update.state = pass ? Note4TestState::kPass
                        : Note4TestState::kFail;
    SetText(update.hint, sizeof(update.hint), "%s",
            pass ? "NFC passed; original data restored"
                 : (stable < 3 ? "Phone field was not detected"
                               : "NFC passed but restore failed"));
    SetText(update.details[3].data(), update.details[3].size(),
            "RESTORE: %s", esp_err_to_name(restore));
    Publish(callback, update);
    return pass ? Note4TestResult::kPass : Note4TestResult::kFail;
}
