#include "zectrix_input_service.h"
#include <cassert>

int main() {
    using namespace zectrix::input;
    const InputEvent up = InputService::MakeEvent(Button::Up, Action::Click);
    const InputEvent hold = InputService::MakeEvent(Button::Ok, Action::LongPress);
    assert(up.button == Button::Up && up.action == Action::Click);
    assert(hold.button == Button::Ok && hold.action == Action::LongPress);
}
