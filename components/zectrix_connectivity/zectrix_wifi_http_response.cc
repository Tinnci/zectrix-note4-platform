#include "zectrix_wifi_http.h"

#include <cstring>
#include <string_view>

namespace zectrix::connectivity {
namespace {

using View = std::string_view;

View Trim(View value) {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
        value.remove_prefix(1);
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
        value.remove_suffix(1);
    }
    return value;
}

bool Equal(View first, View second) {
    if (first.size() != second.size()) return false;
    for (std::size_t index = 0; index < first.size(); ++index) {
        char value = first[index];
        if (value >= 'A' && value <= 'Z') value += 'a' - 'A';
        if (value != second[index]) return false;
    }
    return true;
}

bool IsHeaderName(View name) {
    if (name.empty()) return false;
    for (const unsigned char value : name) {
        if ((value >= 'a' && value <= 'z') ||
            (value >= 'A' && value <= 'Z') ||
            (value >= '0' && value <= '9') ||
            std::strchr("!#$%&'*+-.^_`|~", value) != nullptr) continue;
        return false;
    }
    return true;
}

bool PlainText(View value) {
    const auto separator = value.find(';');
    if (!Equal(Trim(value.substr(0, separator)), "text/plain")) return false;
    if (separator == View::npos) return true;
    value = Trim(value.substr(separator + 1));
    const auto equals = value.find('=');
    if (equals == View::npos ||
        !Equal(Trim(value.substr(0, equals)), "charset")) return false;
    value = Trim(value.substr(equals + 1));
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        value.remove_prefix(1);
        value.remove_suffix(1);
    }
    return Equal(value, "utf-8") || Equal(value, "us-ascii");
}

bool Utf8(const uint8_t* data, std::size_t size) {
    std::size_t index = 0;
    while (index < size) {
        const uint8_t first = data[index++];
        if (first < 0x80) continue;
        unsigned count = 0;
        uint32_t code = 0;
        uint32_t minimum = 0;
        if (first >= 0xc2 && first <= 0xdf) {
            count = 1; code = first & 0x1fU; minimum = 0x80;
        } else if (first >= 0xe0 && first <= 0xef) {
            count = 2; code = first & 0x0fU; minimum = 0x800;
        } else if (first >= 0xf0 && first <= 0xf4) {
            count = 3; code = first & 0x07U; minimum = 0x10000;
        } else {
            return false;
        }
        if (count > size - index) return false;
        while (count-- != 0) {
            const uint8_t next = data[index++];
            if ((next & 0xc0U) != 0x80U) return false;
            code = (code << 6U) | (next & 0x3fU);
        }
        if (code < minimum || code > 0x10ffff ||
            (code >= 0xd800 && code <= 0xdfff)) return false;
    }
    return true;
}

}  // namespace

bool WifiHttpResponse::Begin(uint8_t* body, std::size_t capacity) {
    *this = {};
    if (body == nullptr || capacity == 0 || capacity > kMaximumWifiResourceBytes) {
        return false;
    }
    body_ = body;
    body_capacity_ = capacity;
    state_ = State::kStatus;
    return true;
}

WifiDriverResult WifiHttpResponse::Feed(const uint8_t* data, std::size_t size) {
    if (data == nullptr && size != 0) Fail();
    for (std::size_t index = 0; index < size && state_ != State::kFailed; ++index) {
        const uint8_t value = data[index];
        if (state_ == State::kDone) {
            Fail();
        } else if (state_ == State::kFixedBody || state_ == State::kCloseBody ||
                   state_ == State::kChunkBody) {
            if (body_size_ == body_capacity_) {
                Fail(WifiDriverResult::kResponseTooLarge);
                break;
            }
            body_[body_size_++] = value;
            if (state_ != State::kCloseBody && --remaining_ == 0) {
                if (state_ == State::kChunkBody) state_ = State::kChunkCr;
                else Complete();
            }
        } else if (state_ == State::kChunkCr) {
            if (value != '\r') Fail();
            else state_ = State::kChunkLf;
        } else if (state_ == State::kChunkLf) {
            if (value != '\n') Fail();
            else state_ = State::kChunkSize;
        } else {
            // Count chunk metadata and trailers as well as response headers.
            if (++header_bytes_ > kMaximumHeaderBytes) {
                Fail();
            } else if (line_cr_) {
                line_cr_ = false;
                if (value != '\n') { Fail(); continue; }
                ProcessLine();
                line_size_ = 0;
            } else if (value == '\r') {
                line_cr_ = true;
            } else if (value == '\n' || value == 0 ||
                       (value < 0x20 && value != '\t') || value == 0x7f ||
                       line_size_ == kMaximumHeaderLineBytes) {
                Fail();
            } else {
                line_[line_size_++] = static_cast<char>(value);
            }
        }
    }
    return Result();
}

void WifiHttpResponse::ProcessLine() {
    const View line(line_.data(), line_size_);
    if (state_ == State::kStatus) {
        if (line.size() < 12 ||
            (line.substr(0, 9) != "HTTP/1.1 " &&
             line.substr(0, 9) != "HTTP/1.0 ") ||
            (line.size() > 12 && line[12] != ' ') ||
            line[9] < '1' || line[9] > '5' ||
            line[10] < '0' || line[10] > '9' ||
            line[11] < '0' || line[11] > '9') {
            Fail();
        } else if (line.substr(9, 3) != "200") {
            Fail(line[9] >= '4' ? WifiDriverResult::kServerError
                               : WifiDriverResult::kInvalidResponse);
        } else {
            state_ = State::kHeaders;
        }
    } else if (state_ == State::kHeaders || state_ == State::kTrailers) {
        if (!line.empty()) {
            ProcessHeader(state_ == State::kTrailers);
        } else if (state_ == State::kTrailers) {
            Complete();
        } else if (!content_type_seen_ || (length_seen_ && chunked_)) {
            Fail();
        } else if (chunked_) {
            state_ = State::kChunkSize;
        } else if (length_seen_) {
            state_ = State::kFixedBody;
            if (remaining_ == 0) Complete();
        } else {
            state_ = State::kCloseBody;
        }
    } else if (state_ == State::kChunkSize) {
        const auto semicolon = line.find(';');
        const View number = line.substr(0, semicolon);
        if (number.empty()) { Fail(); return; }
        remaining_ = 0;
        for (const char digit : number) {
            unsigned value = 16;
            if (digit >= '0' && digit <= '9') value = digit - '0';
            if (digit >= 'a' && digit <= 'f') value = digit - 'a' + 10;
            if (digit >= 'A' && digit <= 'F') value = digit - 'A' + 10;
            if (value == 16) { Fail(); return; }
            const std::size_t available = body_capacity_ - body_size_;
            if (value > available || remaining_ > (available - value) / 16) {
                Fail(WifiDriverResult::kResponseTooLarge);
                return;
            }
            remaining_ = remaining_ * 16 + value;
        }
        state_ = remaining_ == 0 ? State::kTrailers : State::kChunkBody;
    }
}

void WifiHttpResponse::ProcessHeader(bool trailer) {
    const View line(line_.data(), line_size_);
    const auto colon = line.find(':');
    if (colon == View::npos || !IsHeaderName(line.substr(0, colon))) {
        Fail(); return;
    }
    const View name = line.substr(0, colon);
    const View value = Trim(line.substr(colon + 1));
    if (Equal(name, "content-type")) {
        if (trailer || content_type_seen_ || !PlainText(value)) Fail();
        else content_type_seen_ = true;
    } else if (Equal(name, "content-length")) {
        if (trailer || length_seen_ || value.empty()) { Fail(); return; }
        length_seen_ = true;
        remaining_ = 0;
        for (const char digit : value) {
            if (digit < '0' || digit > '9') { Fail(); return; }
            const unsigned number = digit - '0';
            if (number > body_capacity_ ||
                remaining_ > (body_capacity_ - number) / 10) {
                Fail(WifiDriverResult::kResponseTooLarge); return;
            }
            remaining_ = remaining_ * 10 + number;
        }
    } else if (Equal(name, "transfer-encoding")) {
        if (trailer || chunked_ || !Equal(value, "chunked")) Fail();
        else chunked_ = true;
    } else if (Equal(name, "content-encoding")) {
        if (trailer || !Equal(value, "identity")) Fail();
    }
}

void WifiHttpResponse::Complete() {
    if (body_size_ == 0 || !Utf8(body_, body_size_)) Fail();
    else state_ = State::kDone;
}

void WifiHttpResponse::Fail(WifiDriverResult result) {
    state_ = State::kFailed;
    failure_ = result;
}

WifiDriverResult WifiHttpResponse::Result() const {
    if (state_ == State::kDone) return WifiDriverResult::kReady;
    if (state_ == State::kFailed) return failure_;
    return WifiDriverResult::kPending;
}

WifiDriverResult WifiHttpResponse::EndOfStream() {
    if (state_ == State::kCloseBody) Complete();
    else if (state_ != State::kDone && state_ != State::kFailed) Fail();
    return Result();
}

}  // namespace zectrix::connectivity
