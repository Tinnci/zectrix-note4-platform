#include "zectrix_input_service.h"

#include "freertos/FreeRTOS.h"
#include "zectrix_board.h"

namespace zectrix::input {

esp_err_t InputService::Attach(void* board, InputService** out_service) {
    if (board == nullptr || out_service == nullptr) return ESP_ERR_INVALID_ARG;
    *out_service = new InputService(board);
    return ESP_OK;
}

InputService::~InputService() = default;

bool InputService::Wait(InputEvent* event, uint32_t timeout_ms) {
    if (event == nullptr || board_ == nullptr) return false;
    ZectrixButtonEvent board_event;
    if (!static_cast<ZectrixBoard*>(board_)->WaitButton(
            &board_event, pdMS_TO_TICKS(timeout_ms))) {
        return false;
    }
    switch (board_event.button) {
        case ZectrixButton::kUp: event->button = Button::Up; break;
        case ZectrixButton::kDown: event->button = Button::Down; break;
        case ZectrixButton::kOk: event->button = Button::Ok; break;
    }
    event->action = board_event.action == ZectrixButtonAction::kLongPress
                        ? Action::LongPress
                        : Action::Click;
    return true;
}

void InputService::Drain() {
    if (board_ != nullptr) static_cast<ZectrixBoard*>(board_)->DrainButtons();
}

}  // namespace zectrix::input
