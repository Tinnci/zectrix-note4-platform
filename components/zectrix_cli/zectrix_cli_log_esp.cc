#include "zectrix_cli_log.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>

#include "esp_log.h"

namespace zectrix::cli {
namespace {

LogBuffer logs;
std::atomic<bool> capturing{false};
std::atomic<vprintf_like_t> fallback{&std::vprintf};

int Capture(const char* format, va_list args) {
    if (!capturing.load()) return fallback.load()(format, args);
    char buffer[kMaximumOutputSize + 1]{};
    va_list copy;
    va_copy(copy, args);
    const int size = std::vsnprintf(buffer, sizeof(buffer), format, copy);
    va_end(copy);
    if (size >= 0) {
        logs.Push(DetectLogLevel(buffer), buffer,
                  size >= static_cast<int>(sizeof(buffer)));
    }
    return size;
}

}  // namespace

LogBuffer& MaintenanceLogs() { return logs; }

void StartMaintenanceLogCapture() {
    if (capturing.exchange(true)) return;
    fallback.store(esp_log_set_vprintf(Capture));
}

void StopMaintenanceLogCapture() {
    if (!capturing.exchange(false)) return;
    esp_log_set_vprintf(fallback.load());
}

}  // namespace zectrix::cli
