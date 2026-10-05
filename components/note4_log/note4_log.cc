#include "note4_log.h"

#include <cstring>
#include <limits>

namespace note4::log {
namespace {
const char* SkipColor(const char* text) {
    if (!text)
        return "";
    std::size_t remaining = 256;
    while (remaining >= 2 && text[0] == '\x1b' && text[1] == '[') {
        std::size_t end = 2;
        while (end < remaining && ((text[end] >= '0' && text[end] <= '9') || text[end] == ';'))
            ++end;
        if (end >= remaining || text[end] != 'm')
            break;
        text += end + 1;
        remaining -= end + 1;
    }
    return text;
}

void Metadata(const char* text, LogRecord* record) {
    text = SkipColor(text);
    record->uptime_ms = 0;
    record->tag.fill(0);
    if (!text[0] || text[1] != ' ' || text[2] != '(')
        return;
    const char* digit = text + 3;
    uint64_t value = 0;
    while (*digit >= '0' && *digit <= '9') {
        const unsigned next = *digit++ - '0';
        if (value > (UINT64_MAX - next) / 10)
            return;
        value = value * 10 + next;
    }
    if (digit == text + 3 || digit[0] != ')' || digit[1] != ' ')
        return;
    record->uptime_ms = value;
    digit += 2;
    std::size_t size = 0;
    while (size < kLogTagBytes && digit[size] && digit[size] != ':' && digit[size] != ' ') {
        const unsigned char c = digit[size];
        if (c < 0x21 || c >= 0x7f)
            return;
        ++size;
    }
    if (digit[size] != ':' || digit[size + 1] != ' ')
        return;
    std::memcpy(record->tag.data(), digit, size);
}
} // namespace

void LogBuffer::Push(LogLevel level, const char* text, bool shortened) {
    if (!text)
        return;
    // Bound all parsing, not just copying; a malformed escape sequence or
    // timestamp must never cause an unbounded scan on the producer task.
    char input[257]{};
    std::size_t length = 0;
    while (length < 256 && text[length]) {
        input[length] = text[length];
        ++length;
    }
    shortened = shortened || (length == 256 && text[length]);
    text = input;
    const auto sequence = latest_.fetch_add(1, std::memory_order_relaxed) + 1;
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
    record.sequence = sequence;
    Metadata(text, &record);
    text = SkipColor(text);
    std::size_t in = 0, out = 0;
    // The ESP adapter supplies at most 256 bytes. Direct users are bounded here
    // too; metadata reads only the level/timestamp/tag prefix.
    while (in < 256 && text[in]) {
        if (text[in] == '\x1b' && text[in + 1] == '[') {
            const auto* next = SkipColor(text + in);
            if (next != text + in) {
                in = next - text;
                continue;
            }
        }
        const unsigned char c = text[in];
        const bool final_eol =
            (c == '\r' || c == '\n') &&
            (text[in + 1] == 0 || (c == '\r' && text[in + 1] == '\n' && text[in + 2] == 0));
        if (final_eol)
            break;
        const std::size_t need = (c < 0x20 || c == 0x7f) ? 4 : 1;
        if (out + need > kLogTextBytes) {
            shortened = true;
            break;
        }
        if (need == 1)
            record.text[out++] = c;
        else {
            constexpr char hex[] = "0123456789ABCDEF";
            record.text[out++] = '\\';
            record.text[out++] = 'x';
            record.text[out++] = hex[c >> 4];
            record.text[out++] = hex[c & 15];
        }
        ++in;
    }
    if (in == 256 && text[in])
        shortened = true;
    record.text[out] = 0;
    if (shortened) {
        ++truncated_;
        constexpr char marker[] = " [truncated]";
        const auto offset =
            out > kLogTextBytes - sizeof(marker) + 1 ? kLogTextBytes - sizeof(marker) + 1 : out;
        std::memcpy(record.text.data() + offset, marker, sizeof(marker));
    }
    ++count_;
}

bool LogBuffer::Pop(LogRecord* record) {
    if (!record)
        return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!count_)
        return false;
    *record = records_[head_];
    head_ = (head_ + 1) % records_.size();
    --count_;
    return true;
}

LogStats LogBuffer::Stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return {static_cast<uint32_t>(count_), dropped_.load(), truncated_.load(), latest_.load()};
}

LogLevel DetectLogLevel(const char* text) {
    switch (*SkipColor(text)) {
    case 'E':
        return LogLevel::kError;
    case 'W':
        return LogLevel::kWarn;
    case 'D':
        return LogLevel::kDebug;
    case 'V':
        return LogLevel::kVerbose;
    default:
        return LogLevel::kInfo;
    }
}

const char* LevelName(LogLevel level) {
    switch (level) {
    case LogLevel::kError:
        return "error";
    case LogLevel::kWarn:
        return "warn";
    case LogLevel::kInfo:
        return "info";
    case LogLevel::kDebug:
        return "debug";
    default:
        return "verbose";
    }
}

bool ParseLevel(const char* name, LogLevel* level) {
    if (!name || !level)
        return false;
    for (unsigned value = 1; value <= 4; ++value) {
        const auto candidate = static_cast<LogLevel>(value);
        if (std::strcmp(name, LevelName(candidate)) == 0) {
            *level = candidate;
            return true;
        }
    }
    return false;
}
} // namespace note4::log
