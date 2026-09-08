#pragma once

#include <cstdint>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "zectrix/sdk/input.h"

class ZectrixBoard;

namespace zectrix::input {

using Button = sdk::Button;
using Action = sdk::InputAction;
using InputEvent = sdk::InputEvent;

class InputService {
public:
    // Attaches to initialized board support. The service does not own it.
    static esp_err_t Attach(ZectrixBoard& board, InputService** out_service);
    ~InputService();

    InputService(const InputService&) = delete;
    InputService& operator=(const InputService&) = delete;

    // Uses native FreeRTOS ticks so portMAX_DELAY keeps its wait-forever meaning.
    bool Wait(InputEvent* event, TickType_t timeout_ticks);
    void Drain();

    // Install/remove on the application owner. The hook runs between blocking
    // waits, outside a display operation. WakeWait is safe from another task.
    using WaitHook = void (*)(void*);
    void SetWaitHook(WaitHook hook, void* context);
    void WakeWait();

    static constexpr InputEvent MakeEvent(Button button, Action action) {
        return InputEvent{button, action};
    }

private:
    explicit InputService(ZectrixBoard& board) : board_(&board) {}
    ZectrixBoard* board_;
    WaitHook wait_hook_ = nullptr;
    void* wait_context_ = nullptr;
};

}  // namespace zectrix::input
