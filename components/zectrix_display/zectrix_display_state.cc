#include "zectrix_display_state.h"
#include <algorithm>
namespace zectrix::display {
bool Rect::IsEmpty() const { return width <= 0 || height <= 0; }
bool StateModel::CanUsePartial() const { return state_.baseline == BaselineState::Valid1Bpp; }
bool StateModel::ShouldRequestFullClean() const { return state_.partial_refresh_count >= kPartialRefreshLimit; }
void StateModel::OnFull1BppSuccess() { state_.baseline=BaselineState::Valid1Bpp; state_.partial_refresh_count=0; state_.has_dirty_region=false; state_.dirty_region={}; }
void StateModel::OnPartial1BppSuccess(const Rect& region) { if (!CanUsePartial() || region.IsEmpty()) return; ++state_.partial_refresh_count; AddDirtyRegion(region); }
void StateModel::OnFull4BppSuccess() { SetUnknown(); }
void StateModel::OnRefreshError() { SetUnknown(); }
void StateModel::SetUnknown() { state_={}; }
void StateModel::AddDirtyRegion(const Rect& region) { if (!state_.has_dirty_region) { state_.dirty_region=region; state_.has_dirty_region=true; return; } int right=std::max(state_.dirty_region.x+state_.dirty_region.width, region.x+region.width); int bottom=std::max(state_.dirty_region.y+state_.dirty_region.height, region.y+region.height); int left=std::min(state_.dirty_region.x, region.x); int top=std::min(state_.dirty_region.y, region.y); state_.dirty_region={left,top,right-left,bottom-top}; }
}
