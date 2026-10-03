#pragma once

#include <cstdint>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "note4/sdk/input.h"
#include "note4_input_trace.h"

class Note4Board;

namespace note4::input {

using Button = sdk::Button;
using Action = sdk::InputAction;
using InputEvent = sdk::InputEvent;

class InputService {
public:
    // Attaches to initialized board support. The service does not own it.
    static esp_err_t Attach(Note4Board& board, InputService** out_service);
    ~InputService();

    InputService(const InputService&) = delete;
    InputService& operator=(const InputService&) = delete;

    // Uses native FreeRTOS ticks so portMAX_DELAY keeps its wait-forever meaning.
    bool Wait(InputEvent* event, TickType_t timeout_ticks);
    void Drain();
    TraceBatch ReadTrace(uint64_t cursor) const;

    // Install/remove on the application owner. The hook runs between blocking
    // waits, outside a display operation. WakeWait is safe from another task.
    using WaitHook = void (*)(void*);
    void SetWaitHook(WaitHook hook, void* context);
    void WakeWait();

    static constexpr InputEvent MakeEvent(Button button, Action action) {
        return InputEvent{button, action};
    }

private:
    explicit InputService(Note4Board& board) : board_(&board) {}
    Note4Board* board_;
    WaitHook wait_hook_ = nullptr;
    void* wait_context_ = nullptr;
};

}  // namespace note4::input
