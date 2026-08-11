#include "zectrix_input_service.h"
#include "zectrix_board.h"
#include <cassert>
#include <type_traits>

int main() {
    using namespace zectrix::input;
    const InputEvent up = InputService::MakeEvent(Button::Up, Action::Click);
    const InputEvent hold = InputService::MakeEvent(Button::Ok, Action::LongPress);
    assert(up.button == Button::Up && up.action == Action::Click);
    assert(hold.button == Button::Ok && hold.action == Action::LongPress);

    using WaitSignature = bool (InputService::*)(InputEvent*, TickType_t);
    static_assert(std::is_same_v<decltype(&InputService::Wait), WaitSignature>);
    static_assert(portMAX_DELAY == UINT32_MAX);

    ZectrixBoard board;
    InputService* service = nullptr;
    assert(InputService::Attach(board, &service) == ESP_OK);
    assert(service != nullptr);
    InputEvent event;
    assert(!service->Wait(&event, 17));
    assert(board.last_timeout == 17);
    board.next_event = {ZectrixButton::kUp, ZectrixButtonAction::kClick};
    board.has_event = true;
    assert(service->Wait(&event, portMAX_DELAY));
    assert(board.last_timeout == portMAX_DELAY);
    assert(event.button == Button::Up && event.action == Action::Click);
    board.next_event = {ZectrixButton::kDown,
                        ZectrixButtonAction::kLongPress};
    board.has_event = true;
    assert(service->Wait(&event, 2));
    assert(event.button == Button::Down && event.action == Action::LongPress);
    service->Drain();
    assert(board.drained);
    delete service;
}
