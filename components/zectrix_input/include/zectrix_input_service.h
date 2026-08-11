#pragma once

#include <cstdint>

#include "esp_err.h"

namespace zectrix::input {

enum class Button : uint8_t { Up, Down, Ok };
enum class Action : uint8_t { Click, LongPress };

struct InputEvent {
    Button button = Button::Ok;
    Action action = Action::Click;
};

class InputService {
public:
    // Attaches to an already-initialised board support object. The service does not own it.
    static esp_err_t Attach(void* board, InputService** out_service);
    ~InputService();

    InputService(const InputService&) = delete;
    InputService& operator=(const InputService&) = delete;

    bool Wait(InputEvent* event, uint32_t timeout_ms);
    void Drain();

    static constexpr InputEvent MakeEvent(Button button, Action action) {
        return InputEvent{button, action};
    }

private:
    explicit InputService(void* board) : board_(board) {}
    void* board_;
};

}  // namespace zectrix::input
