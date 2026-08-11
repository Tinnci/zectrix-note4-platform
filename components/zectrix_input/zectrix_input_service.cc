#include "zectrix_input_service.h"

#include "freertos/FreeRTOS.h"
#include "zectrix_board.h"

namespace zectrix::input {

esp_err_t InputService::Attach(ZectrixBoard& board, InputService** out_service) {
    if (out_service == nullptr) return ESP_ERR_INVALID_ARG;
    *out_service = new InputService(board);
    return ESP_OK;
}

InputService::~InputService() = default;

bool InputService::Wait(InputEvent* event, TickType_t timeout_ticks) {
    if (event == nullptr || board_ == nullptr) return false;
    ZectrixButtonEvent board_event;
    if (!board_->WaitButton(&board_event, timeout_ticks)) {
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
    if (board_ != nullptr) board_->DrainButtons();
}

}  // namespace zectrix::input
