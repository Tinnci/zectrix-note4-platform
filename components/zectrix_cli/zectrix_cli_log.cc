#include "zectrix_cli_log.h"

#include <cstring>

namespace zectrix::cli {

void LogBuffer::Push(LogLevel level, const char* text, bool truncated) {
    if (text == nullptr) return;
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock()) {
        ++dropped_;
        return;
    }
    if (count_ == records_.size()) {
        head_ = (head_ + 1) % records_.size();
        --count_;
        ++dropped_;
    }
    auto& record = records_[(head_ + count_) % records_.size()];
    record.level = level;
    std::size_t size = 0;
    while (size < kMaximumOutputSize && text[size] != '\0') {
        record.text[size] = text[size];
        ++size;
    }
    record.text[size] = '\0';
    if (truncated || (size == kMaximumOutputSize && text[size] != '\0')) {
        ++truncated_;
        constexpr char marker[] = " [truncated]";
        std::memcpy(record.text.data() + kMaximumOutputSize - sizeof(marker) + 1,
                    marker, sizeof(marker));
    }
    ++count_;
}

bool LogBuffer::Pop(LogRecord* record) {
    if (record == nullptr) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (count_ == 0) return false;
    *record = records_[head_];
    head_ = (head_ + 1) % records_.size();
    --count_;
    return true;
}

LogStats LogBuffer::Stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return {static_cast<uint32_t>(count_), dropped_.load(), truncated_.load()};
}

LogLevel DetectLogLevel(const char* text) {
    if (text == nullptr) return LogLevel::kInfo;
    if (text[0] == '\x1b' && text[1] == '[') {
        text += 2;
        while (*text != '\0' && *text != 'm') ++text;
        if (*text == 'm') ++text;
    }
    switch (*text) {
        case 'E': return LogLevel::kError;
        case 'W': return LogLevel::kWarn;
        case 'D': return LogLevel::kDebug;
        case 'V': return LogLevel::kVerbose;
        default: return LogLevel::kInfo;
    }
}

}  // namespace zectrix::cli
