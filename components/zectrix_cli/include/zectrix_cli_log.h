#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>

#include "zectrix_cli_core.h"

namespace zectrix::cli {

enum class LogLevel : uint8_t { kError = 1, kWarn, kInfo, kDebug, kVerbose };
inline constexpr std::size_t kLogRecords = 32;

struct LogRecord {
    std::array<char, kMaximumOutputSize + 1> text{};
    LogLevel level = LogLevel::kInfo;
};

struct LogStats {
    uint32_t queued = 0;
    uint32_t dropped = 0;
    uint32_t truncated = 0;
};

class LogBuffer final {
public:
    // Producers never wait for the consumer or for terminal I/O.
    void Push(LogLevel level, const char* text, bool truncated = false);
    bool Pop(LogRecord* record);
    LogStats Stats() const;

private:
    mutable std::mutex mutex_;
    std::array<LogRecord, kLogRecords> records_{};
    std::size_t head_ = 0;
    std::size_t count_ = 0;
    std::atomic<uint32_t> dropped_{0};
    std::atomic<uint32_t> truncated_{0};
};

LogLevel DetectLogLevel(const char* text);

// The ESP log hook uses process-lifetime storage so an in-flight producer
// cannot retain a pointer to a stopped USB service.
LogBuffer& MaintenanceLogs();
void StartMaintenanceLogCapture();
void StopMaintenanceLogCapture();

}  // namespace zectrix::cli
