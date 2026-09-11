#include "zectrix_input_service.h"
#include "zectrix_board.h"
#include "freertos/task.h"
#include <cassert>
#include <type_traits>

static zectrix::input::InputTrace trace;
zectrix::input::TraceBatch ZectrixBoard::ReadInputTrace(uint64_t cursor) { return trace.Read(cursor); }

namespace {
struct WaitProbe {
    ZectrixBoard* board;
    unsigned calls = 0;
    bool deliver_event = true;
};
void ServiceWait(void* context) {
    auto& probe = *static_cast<WaitProbe*>(context);
    ++probe.calls;
    host_ticks += 5;
    if (probe.calls == 2 && probe.deliver_event) {
        probe.board->next_event = {ZectrixButton::kUp, ZectrixButtonAction::kClick};
        probe.board->has_event = true;
    }
}
}

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
    const auto cursor = service->ReadTrace(0).cursor;
    for (unsigned i = 0; i < 40; ++i) trace.Push(i * 1000, i % 3, i % 2, i % 4 == 0);
    auto batch = service->ReadTrace(cursor);
    assert(batch.lost == 24 && batch.count == 4 && batch.records[0].sequence == 25);
    assert(batch.records[0].timestamp_us == 24000 && batch.records[0].queued);
    assert(service->ReadTrace(cursor).records[0].sequence == 25);
    assert(!board.drained && !board.has_event && board.wake_calls == 0);
    for (unsigned i = 0; i < 3; ++i) batch = service->ReadTrace(batch.cursor);
    assert(batch.records[3].sequence == 40 && service->ReadTrace(batch.cursor).count == 0);
    assert(!service->Wait(&event, 17));
    assert(board.last_timeout == 17);
    board.next_event = {ZectrixButton::kUp, ZectrixButtonAction::kClick};
    board.has_event = true;
    service->ReadTrace(0);
    assert(board.has_event);
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
    WaitProbe probe{&board};
    service->SetWaitHook(ServiceWait, &probe);
    service->WakeWait();
    service->WakeWait();
    assert(service->Wait(&event, 50));
    assert(event.button == Button::Up);
    assert(board.last_timeout == 40);
    assert(!board.wake_pending);

    // A notification in the predicate-to-block window is retained by the
    // queue, and cannot become a synthetic application input or idle event.
    probe.calls = 0;
    board.on_wait = [](ZectrixBoard& waiting) { waiting.WakeButtonWait(); };
    assert(service->Wait(&event, portMAX_DELAY));
    assert(event.button == Button::Up && board.last_timeout == portMAX_DELAY);

    // Wakes preserve the original deadline, including tick-counter wrap.
    probe.calls = 0;
    probe.deliver_event = false;
    host_ticks = UINT32_MAX - 3;
    service->WakeWait();
    assert(!service->Wait(&event, 50));
    assert(board.last_timeout == 40);
    service->SetWaitHook(nullptr, nullptr);
    delete service;
}
