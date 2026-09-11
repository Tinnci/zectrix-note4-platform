#include "zectrix_display_state.h"

#include <algorithm>

namespace zectrix::display {

bool Rect::IsEmpty() const { return width <= 0 || height <= 0; }
bool StateModel::CanUsePartial() const { return state_.baseline == BaselineState::Valid1Bpp; }

void StateModel::OnFull1BppSuccess() {
    state_ = {};
    state_.baseline = BaselineState::Valid1Bpp;
}

void StateModel::OnPartial1BppSuccess(const Rect& region, uint32_t changed_pixels) {
    if (!CanUsePartial() || region.IsEmpty() || changed_pixels == 0) return;
    if (state_.partial_refresh_count < UINT32_MAX) ++state_.partial_refresh_count;
    // These are diagnostic totals, never scheduling thresholds.
    state_.partial_changed_pixels += std::min(
        changed_pixels, UINT32_MAX - state_.partial_changed_pixels);
    AddDirtyRegion(region);
}

void StateModel::OnFull4BppSuccess() { SetUnknown(); }
void StateModel::OnRefreshError() { SetUnknown(); }
void StateModel::SetUnknown() { state_ = {}; }

void StateModel::AddDirtyRegion(const Rect& region) {
    if (!state_.has_dirty_region) {
        state_.dirty_region = region;
        state_.has_dirty_region = true;
        return;
    }
    const int right = std::max(state_.dirty_region.x + state_.dirty_region.width,
                               region.x + region.width);
    const int bottom = std::max(state_.dirty_region.y + state_.dirty_region.height,
                                region.y + region.height);
    const int left = std::min(state_.dirty_region.x, region.x);
    const int top = std::min(state_.dirty_region.y, region.y);
    state_.dirty_region = {left, top, right - left, bottom - top};
}

}  // namespace zectrix::display
