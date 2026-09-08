#include "zectrix_input_service.h"

#include <new>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "zectrix_board.h"

namespace zectrix::input {

esp_err_t InputService::Attach(ZectrixBoard& board, InputService** out_service) {
    if (out_service == nullptr) return ESP_ERR_INVALID_ARG;
    *out_service = new (std::nothrow) InputService(board);
    return *out_service == nullptr ? ESP_ERR_NO_MEM : ESP_OK;
}

InputService::~InputService() = default;

bool InputService::Wait(InputEvent* event, TickType_t timeout_ticks) {
    if (event == nullptr || board_ == nullptr) return false;
    const TickType_t started = xTaskGetTickCount();
    for (;;) {
        if (wait_hook_ != nullptr) wait_hook_(wait_context_);
        const TickType_t elapsed = xTaskGetTickCount() - started;
        const TickType_t remaining = timeout_ticks == portMAX_DELAY
            ? portMAX_DELAY : (elapsed < timeout_ticks ? timeout_ticks - elapsed : 0);
        ZectrixButtonEvent board_event;
        if (!board_->WaitButton(&board_event, remaining)) return false;
        if (board_event.action == ZectrixButtonAction::kWake) {
            if (remaining == 0) {
                if (wait_hook_ != nullptr) wait_hook_(wait_context_);
                return false;
            }
            continue;
        }
        if (wait_hook_ != nullptr) wait_hook_(wait_context_);
        switch (board_event.button) {
            case ZectrixButton::kUp: event->button = Button::Up; break;
            case ZectrixButton::kDown: event->button = Button::Down; break;
            case ZectrixButton::kOk: event->button = Button::Ok; break;
        }
        event->action = board_event.action == ZectrixButtonAction::kLongPress
                            ? Action::LongPress : Action::Click;
        return true;
    }
}

void InputService::Drain() {
    if (board_ != nullptr) board_->DrainButtons();
}

void InputService::SetWaitHook(WaitHook hook, void* context) {
    wait_hook_ = hook;
    wait_context_ = context;
}

void InputService::WakeWait() {
    if (board_ != nullptr) board_->WakeButtonWait();
}

}  // namespace zectrix::input
