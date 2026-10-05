#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace note4::log {

enum class LogLevel : uint8_t { kError = 1, kWarn, kInfo, kDebug, kVerbose };
inline constexpr std::size_t kLogRecords = 32;
inline constexpr std::size_t kLogTextBytes = 224;
inline constexpr std::size_t kLogTagBytes = 23;

struct LogRecord {
    std::array<char, kLogTextBytes + 1> text{};
    std::array<char, kLogTagBytes + 1> tag{};
    uint64_t sequence = 0;
    uint64_t uptime_ms = 0; // Zero if the source provided no timestamp.
    LogLevel level = LogLevel::kInfo;
};
struct LogStats {
    uint32_t queued = 0, dropped = 0, truncated = 0;
    uint64_t latest = 0;
};

class LogBuffer final {
public:
    // Task-context only. Producers never wait for a consumer or terminal I/O.
    // Overwrite the oldest record on overflow; sequence gaps remain observable.
    void Push(LogLevel level, const char* text, bool truncated = false);
    bool Pop(LogRecord* record);
    LogStats Stats() const;

private:
    mutable std::mutex mutex_;
    std::array<LogRecord, kLogRecords> records_{};
    std::size_t head_ = 0, count_ = 0;
    std::atomic<uint32_t> dropped_{0}, truncated_{0};
    std::atomic<uint64_t> latest_{0};
};

LogLevel DetectLogLevel(const char* text);
const char* LevelName(LogLevel level);
bool ParseLevel(const char* name, LogLevel* level);

class LevelControl {
public:
    virtual ~LevelControl() = default;
    virtual LogLevel Get() const = 0;
    virtual bool Set(LogLevel level) = 0;
};

// ESP adapter owns the global vprintf hook, independently of USB CLI lifetime.
// Early boot/panic keep ESP-IDF's direct path. No flash/NVS history is created.
LogBuffer& SystemLogs();
void StartCapture();
void StopCapture();
LevelControl& EspLevelControl();

} // namespace note4::log
