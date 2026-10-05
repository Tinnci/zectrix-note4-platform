#include "esp_log.h"
#include "note4_cli_log.h"
#include "note4_log_event.h"

#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {

std::atomic<unsigned> fallback_calls{0};
int Fallback(const char*, va_list) { ++fallback_calls; return 0; }
std::atomic<vprintf_like_t> current_sink{Fallback};
unsigned level_updates = 0;
esp_log_level_t selected_level = ESP_LOG_INFO;

void Emit(const char* format, ...) {
    va_list args;
    va_start(args, format);
    current_sink.load()(format, args);
    va_end(args);
}

}  // namespace

vprintf_like_t esp_log_set_vprintf(vprintf_like_t function) {
    return current_sink.exchange(function);
}
void esp_log_level_set(const char* tag, esp_log_level_t level) {
    assert(std::strcmp(tag, "*") != 0 && std::strcmp(tag, "wifi") != 0);
    selected_level = level;
    ++level_updates;
}

int main() {
    using namespace note4::cli;
    Emit("boot");
    assert(fallback_calls == 1);
    StartMaintenanceLogCapture();
    const auto in_flight_sink = current_sink.load();
    Emit("W (%d) test: %s\n", 42, "hello");
    assert(fallback_calls == 1);
    LogRecord record;
    assert(MaintenanceLogs().Pop(&record));
    assert(record.level == LogLevel::kWarn);
    assert(std::strcmp(record.text.data(), "W (42) test: hello") == 0);
    assert(record.uptime_ms == 42 && std::strcmp(record.tag.data(), "test") == 0 &&
           record.sequence == 1);
    Emit("I %s", std::string(500, 'x').c_str());
    assert(MaintenanceLogs().Stats().truncated == 1);
    for (int index = 0; index < 40; ++index) Emit("I item %d", index);
    assert(MaintenanceLogs().Stats().dropped == 9);
    assert(fallback_calls == 1);
    StartMaintenanceLogCapture();
    StopMaintenanceLogCapture();
    assert(current_sink == Fallback);
    Emit("after stop");
    assert(fallback_calls == 2);
    // A producer that loaded the callback before shutdown uses direct fallback.
    current_sink = in_flight_sink;
    Emit("late producer");
    current_sink = Fallback;
    assert(fallback_calls == 3);
    StopMaintenanceLogCapture();
    assert(current_sink == Fallback);

    std::atomic<bool> started{false};
    std::vector<std::thread> producers;
    for (unsigned producer = 0; producer < 4; ++producer) {
        producers.emplace_back([&, producer] {
            while (!started) std::this_thread::yield();
            for (unsigned record = 0; record < 1000; ++record) {
                Emit("I producer=%u record=%u", producer, record);
            }
        });
    }
    started = true;
    for (unsigned cycle = 0; cycle < 100; ++cycle) {
        StartMaintenanceLogCapture();
        StopMaintenanceLogCapture();
    }
    for (auto& producer : producers) producer.join();
    assert(current_sink == Fallback && MaintenanceLogs().Stats().queued <= kLogRecords);
    auto& control = note4::log::EspLevelControl();
    assert(control.Get() == LogLevel::kInfo);
    assert(control.Set(LogLevel::kDebug) && selected_level == ESP_LOG_DEBUG && level_updates > 10);
    assert(!control.Set(LogLevel::kVerbose) && control.Get() == LogLevel::kDebug);
    assert(control.Set(LogLevel::kInfo));
    assert(std::strcmp(note4::log::Token("a b=\n\x1b").c_str(), "a%20b%3D%0A%1B") == 0);
    assert(std::strcmp(note4::log::Token(nullptr).c_str(), "-") == 0);
    LogBuffer bounded;
    bounded.Push(LogLevel::kInfo, "\x1b[32mI (9) epd: event=done\x1b[0m\r\n");
    assert(bounded.Pop(&record) && record.uptime_ms == 9 &&
           std::strcmp(record.tag.data(), "epd") == 0);
    assert(std::strcmp(record.text.data(), "I (9) epd: event=done\\x0D\\x0A") != 0);
    bounded.Push(LogLevel::kWarn, "W (1) test: hello\nE fake");
    assert(bounded.Pop(&record) && std::strstr(record.text.data(), "hello\\x0AE fake"));
    bounded.Push(LogLevel::kInfo, ("\x1b[" + std::string(1000, '1')).c_str());
    assert(bounded.Pop(&record) && std::strstr(record.text.data(), "[truncated]"));
    bounded.Push(LogLevel::kError, "E");
    assert(bounded.Pop(&record) && record.tag[0] == 0);
}
