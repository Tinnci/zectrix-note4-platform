#include "zectrix_time_service.h"

#include <ctime>

#include "esp_timer.h"
#include "zectrix_board.h"

namespace zectrix::time {
namespace {

tm ToTm(const DateTime& value) {
    tm result = {};
    result.tm_year = value.year - 1900;
    result.tm_mon = value.month - 1;
    result.tm_mday = value.day;
    result.tm_wday = value.weekday;
    result.tm_hour = value.hour;
    result.tm_min = value.minute;
    result.tm_sec = value.second;
    return result;
}

DateTime FromTm(const tm& value) {
    return DateTime{value.tm_year + 1900, value.tm_mon + 1, value.tm_mday,
                    value.tm_wday, value.tm_hour, value.tm_min, value.tm_sec};
}

}  // namespace

esp_err_t TimeService::Attach(ZectrixBoard& board, TimeService** out_service) {
    if (out_service == nullptr) return ESP_ERR_INVALID_ARG;
    *out_service = new TimeService(board);
    return ESP_OK;
}

TimeService::~TimeService() = default;

int64_t TimeService::MonotonicMicroseconds() const {
    return esp_timer_get_time();
}

bool TimeService::RtcAvailable() const {
    return board_ != nullptr && board_->HasRtc();
}

esp_err_t TimeService::ReadRtc(DateTime* value) {
    if (value == nullptr) return ESP_ERR_INVALID_ARG;
    if (!RtcAvailable()) return ESP_ERR_NOT_FOUND;
    tm raw = {};
    if (!board_->ReadRtc(&raw)) return ESP_FAIL;
    *value = FromTm(raw);
    return ESP_OK;
}

esp_err_t TimeService::WriteRtc(const DateTime& value) {
    if (!RtcAvailable()) return ESP_ERR_NOT_FOUND;
    const tm raw = ToTm(value);
    return board_->WriteRtc(raw) ? ESP_OK : ESP_FAIL;
}

esp_err_t TimeService::StartRtcCountdown(uint8_t seconds) {
    if (!RtcAvailable()) return ESP_ERR_NOT_FOUND;
    return board_->StartRtcCountdown(seconds) ? ESP_OK : ESP_FAIL;
}

RtcTimerStatus TimeService::ReadRtcTimerStatus() {
    if (!RtcAvailable()) return {};
    return {board_->IsRtcInterruptActive(), board_->IsRtcTimerFired()};
}

esp_err_t TimeService::StopRtcCountdown() {
    if (!RtcAvailable()) return ESP_ERR_NOT_FOUND;
    return board_->StopRtcCountdown() ? ESP_OK : ESP_FAIL;
}

esp_err_t TimeService::ClearRtcTimerFlag() {
    if (!RtcAvailable()) return ESP_ERR_NOT_FOUND;
    return board_->ClearRtcTimerFlag() ? ESP_OK : ESP_FAIL;
}

}  // namespace zectrix::time
