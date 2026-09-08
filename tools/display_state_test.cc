#include "zectrix_display_state.h"

#include <cassert>
#include <initializer_list>

using namespace zectrix::display;

void TestLifecycleAndDirtyBounds() {
    StateModel model;
    assert(!model.CanUsePartial() && !model.ShouldRequestFullClean());
    model.OnPartial1BppSuccess({1, 1, 2, 2}, 4);
    assert(model.state().partial_refresh_count == 0 && model.state().partial_changed_pixels == 0);
    model.OnFull1BppSuccess();
    assert(model.CanUsePartial());
    model.OnPartial1BppSuccess({10, 20, 5, 6}, 30);
    model.OnPartial1BppSuccess({0, 0, 4, 8}, 32);
    assert(model.state().partial_refresh_count == 2 && model.state().partial_changed_pixels == 62);
    const auto& dirty = model.state().dirty_region;
    assert(dirty.x == 0 && dirty.y == 0 && dirty.width == 15 && dirty.height == 26);
    model.OnPartial1BppSuccess({}, 5);
    model.OnPartial1BppSuccess({0, 0, 400, 300}, 0);
    assert(model.state().partial_refresh_count == 2 && model.state().partial_changed_pixels == 62);
    model.OnFull1BppSuccess();
    assert(model.CanUsePartial() && !model.ShouldRequestFullClean());
    assert(model.state().partial_refresh_count == 0 && model.state().partial_changed_pixels == 0);
    assert(!model.state().has_dirty_region);
    for (bool gray : {false, true}) {
        model.OnFull1BppSuccess();
        model.OnPartial1BppSuccess({0, 0, 400, 30}, 12000);
        if (gray) model.OnFull4BppSuccess(); else model.OnRefreshError();
        assert(!model.CanUsePartial() && !model.state().has_dirty_region);
        assert(model.state().partial_refresh_count == 0 && model.state().partial_changed_pixels == 0);
    }
}

void TestFrameBudget() {
    StateModel model;
    model.OnFull1BppSuccess();
    for (unsigned count = 0; count < 8; ++count) {
        assert(!model.ShouldRequestFullClean(1));
        model.OnPartial1BppSuccess({1, 1, 1, 1}, 1);
    }
    assert(model.ShouldRequestFullClean());
    assert(model.state().partial_changed_pixels == 8);
    for (unsigned count = 0; count < 16; ++count) {
        model.OnPartial1BppSuccess({1, 1, 1, 1}, 1);
    }
    assert(model.state().partial_refresh_count == 8 && model.ShouldRequestFullClean(1));
    model.OnFull1BppSuccess();
    assert(!model.ShouldRequestFullClean(1) && model.state().partial_changed_pixels == 0);
}

void TestPixelBudget() {
    StateModel model;
    model.OnFull1BppSuccess();
    assert(!model.ShouldRequestFullClean(29999));
    assert(model.ShouldRequestFullClean(30000));
    assert(model.ShouldRequestFullClean(120000));
    // Repeated changes to the same region count even if they cancel visually.
    for (unsigned count = 0; count < 4; ++count) {
        assert(!model.ShouldRequestFullClean(12000));
        model.OnPartial1BppSuccess({0, 0, 400, 30}, 12000);
    }
    assert(model.state().partial_refresh_count == 4 && model.state().partial_changed_pixels == 48000);
    assert(!model.ShouldRequestFullClean(11999));
    assert(model.ShouldRequestFullClean(12000));
    model.OnPartial1BppSuccess({0, 0, 400, 30}, 0);
    assert(model.state().partial_refresh_count == 4 && model.state().partial_changed_pixels == 48000);
    model.OnFull1BppSuccess();
    assert(!model.ShouldRequestFullClean(12000));
    model.OnPartial1BppSuccess({0, 0, 400, 300}, UINT32_MAX);
    model.OnPartial1BppSuccess({0, 0, 400, 300}, UINT32_MAX);
    assert(model.state().partial_changed_pixels == 60000 && model.ShouldRequestFullClean());
}

int main() {
    TestLifecycleAndDirtyBounds();
    TestFrameBudget();
    TestPixelBudget();
}
