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

    static constexpr InputEvent MakeEvent(Button button, Action action) {
        return InputEvent{button, action};
    }

private:
    explicit InputService(ZectrixBoard& board) : board_(&board) {}
    ZectrixBoard* board_;
};

}  // namespace zectrix::input
