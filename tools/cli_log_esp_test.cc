#include "zectrix_cli_log.h"
#include "esp_log.h"

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

int main() {
    using namespace zectrix::cli;
    Emit("boot");
    assert(fallback_calls == 1);
    StartMaintenanceLogCapture();
    const auto in_flight_sink = current_sink.load();
    Emit("W (%d) test: %s\n", 42, "hello");
    assert(fallback_calls == 1);
    LogRecord record;
    assert(MaintenanceLogs().Pop(&record));
    assert(record.level == LogLevel::kWarn);
    assert(std::strcmp(record.text.data(), "W (42) test: hello\n") == 0);
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
}
