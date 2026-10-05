#include "note4_log.h"

#include "esp_log.h"
#include <cstdarg>
#include <cstdio>

namespace note4::log {
namespace {
LogBuffer logs;
std::mutex lifecycle;
std::atomic<bool> capturing{false};
std::atomic<vprintf_like_t> fallback{&std::vprintf};

int Capture(const char* format, va_list args) {
    if (!capturing.load())
        return fallback.load()(format, args);
    char buffer[257]{};
    va_list copy;
    va_copy(copy, args);
    const int size = std::vsnprintf(buffer, sizeof(buffer), format, copy);
    va_end(copy);
    if (size >= 0)
        logs.Push(DetectLogLevel(buffer), buffer, size >= static_cast<int>(sizeof(buffer)));
    return size;
}

class EspControl final : public LevelControl {
public:
    LogLevel Get() const override { return level_.load(); }
    bool Set(LogLevel level) override {
        if (level < LogLevel::kError || level > LogLevel::kDebug)
            return false;
        // Never enable third-party Wi-Fi/BLE debug output via '*': those logs
        // may contain peer identifiers or credentials outside our field policy.
        constexpr const char* tags[] = {
            "application",  "platform", "update", "system", "time",       "power", "board",   "i2c",
            "i2c_lock",     "rtc",      "nfc",    "audio",  "audio_test", "epd",   "display", "ble",
            "connectivity", "storage",  "host",   "cli",    "terminal",   "edge",  "runtime"};
        const auto esp_level = static_cast<esp_log_level_t>(level);
        for (const auto* tag : tags)
            esp_log_level_set(tag, esp_level);
#if CONFIG_LOG_MASTER_LEVEL
        // Retain SDK default verbosity even when project verbosity is lower.
        esp_log_set_level_master(esp_level > CONFIG_LOG_DEFAULT_LEVEL
                                     ? esp_level
                                     : static_cast<esp_log_level_t>(CONFIG_LOG_DEFAULT_LEVEL));
#endif
        level_.store(level);
        return true;
    }

private:
    std::atomic<LogLevel> level_{LogLevel::kInfo};
};
EspControl levels;
} // namespace

LogBuffer& SystemLogs() { return logs; }
LevelControl& EspLevelControl() { return levels; }
void StartCapture() {
    std::lock_guard<std::mutex> lock(lifecycle);
    if (capturing.load())
        return;
    // ESP pthread locks may initialize lazily. Initialize before installing
    // the producer callback, outside the bounded logging path.
    (void)logs.Stats();
    // Publish the fallback before capture becomes active. Late callbacks use
    // process-lifetime storage, never a pointer into a stopped USB service.
    fallback.store(esp_log_set_vprintf(Capture));
    capturing.store(true);
}
void StopCapture() {
    std::lock_guard<std::mutex> lock(lifecycle);
    if (!capturing.exchange(false))
        return;
    esp_log_set_vprintf(fallback.load());
}
} // namespace note4::log
