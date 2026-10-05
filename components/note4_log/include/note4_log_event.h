#pragma once

#include "esp_log.h"
#ifdef __cplusplus
#include <array>
#include <cstddef>

namespace note4::log {
// Untrusted identifiers/reasons stay one ASCII token, with no terminal controls.
// A trailing ~ means this field was shortened; literal ~ is percent-encoded.
class Token final {
public:
    explicit Token(const char* text) {
        if (!text || !*text) {
            bytes_[0] = '-';
            return;
        }
        constexpr char hex[] = "0123456789ABCDEF";
        std::size_t out = 0;
        for (std::size_t in = 0; text[in]; ++in) {
            const auto c = static_cast<unsigned char>(text[in]);
            const bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                               (c >= '0' && c <= '9') || c == '_' || c == '.' || c == ':' ||
                               c == '/' || c == '-';
            const std::size_t need = plain ? 1 : 3;
            if (out + need > bytes_.size() - 2) {
                bytes_[out++] = '~';
                break;
            }
            if (plain)
                bytes_[out++] = c;
            else {
                bytes_[out++] = '%';
                bytes_[out++] = hex[c >> 4];
                bytes_[out++] = hex[c & 15];
            }
        }
        bytes_[out] = 0;
    }
    const char* c_str() const { return bytes_.data(); }

private:
    std::array<char, 64> bytes_{};
};
} // namespace note4::log
#endif

// Event and field formats are literals; ESP-IDF retains compile/runtime gating.
// These macros are not for ISR, early boot or cache-disabled code.
#define NOTE4_LOGE(tag, event, format, ...) ESP_LOGE(tag, "event=" event " " format, ##__VA_ARGS__)
#define NOTE4_LOGW(tag, event, format, ...) ESP_LOGW(tag, "event=" event " " format, ##__VA_ARGS__)
#define NOTE4_LOGI(tag, event, format, ...) ESP_LOGI(tag, "event=" event " " format, ##__VA_ARGS__)
#define NOTE4_LOGD(tag, event, format, ...) ESP_LOGD(tag, "event=" event " " format, ##__VA_ARGS__)
