#include "zectrix_display_state.h"
#include <cassert>
using namespace zectrix::display;
int main() {
    StateModel model;
    assert(!model.CanUsePartial() && !model.ShouldRequestFullClean());
    model.OnPartial1BppSuccess({1,1,2,2}); assert(model.state().partial_refresh_count == 0);
    model.OnFull1BppSuccess(); assert(model.CanUsePartial());
    model.OnPartial1BppSuccess({10,20,5,6}); model.OnPartial1BppSuccess({0,0,4,8});
    assert(model.state().partial_refresh_count == 2 && model.state().dirty_region.x == 0 && model.state().dirty_region.y == 0 && model.state().dirty_region.width == 15 && model.state().dirty_region.height == 26);
    for (int i=2;i<8;++i) model.OnPartial1BppSuccess({1,1,1,1});
    assert(model.ShouldRequestFullClean()); model.OnFull4BppSuccess(); assert(!model.CanUsePartial() && !model.state().has_dirty_region);
    model.OnFull1BppSuccess(); model.OnPartial1BppSuccess({1,1,1,1}); model.OnRefreshError();
    assert(!model.CanUsePartial() && model.state().partial_refresh_count == 0);
}
