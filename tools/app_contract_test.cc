#include "zectrix_app_contract.h"

#include <cassert>
#include <cstring>

int main() {
    using namespace zectrix::app;

    char mutable_id[] = "clock";
    AppCommand open_clock;
    assert(AppCommand::Open(mutable_id, &open_clock));
    mutable_id[0] = 'X';
    assert(std::strcmp(open_clock.target.c_str(), "clock") == 0);
    assert(!AppCommand::Open("", &open_clock));
    assert(!AppCommand::Open("Clock", &open_clock));
    assert(!AppCommand::Open("this-application-id-is-far-too-long", &open_clock));

    CommandArbiter commands;
    assert(commands.Submit(open_clock) == SubmitResult::Accepted);
    assert(commands.Submit(AppCommand::Back()) == SubmitResult::Conflict);
    assert(commands.Submit(AppCommand::Home()) == SubmitResult::Superseded);
    assert(commands.Submit(AppCommand::Shutdown()) == SubmitResult::Superseded);
    assert(commands.Submit(AppCommand::Home()) == SubmitResult::Conflict);
    AppCommand selected;
    assert(commands.Take(&selected));
    assert(selected.kind == CommandKind::Shutdown);
    assert(!commands.Take(&selected));

    RenderAccumulator renders;
    renders.Submit({7, {10, 20, 30, 40}, RenderIntent::Fast});
    renders.Submit({7, {20, 10, 40, 20}, RenderIntent::Quality});
    RenderRequest render;
    assert(renders.TakeFor(7, &render));
    assert(render.intent == RenderIntent::Quality);
    assert(render.dirty.x == 10 && render.dirty.y == 10);
    assert(render.dirty.width == 50 && render.dirty.height == 50);

    renders.Submit({7, {0, 0, 10, 10}, RenderIntent::Fast});
    assert(!renders.TakeFor(8, &render));
    assert(!renders.has_pending());

    renders.Submit({8, {0, 0, 10, 10}, RenderIntent::Fast});
    renders.Submit({9, {30, 30, 10, 10}, RenderIntent::Quality});
    assert(renders.TakeFor(9, &render));
    assert(render.dirty.x == 30 && render.intent == RenderIntent::Quality);
}
