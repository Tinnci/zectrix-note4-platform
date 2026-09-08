#pragma once

#include <cstdint>

namespace zectrix::display {

enum class BaselineState : uint8_t { Unknown, Valid1Bpp };

struct Rect {
    int x = 0, y = 0, width = 0, height = 0;
    bool IsEmpty() const;
};

struct State {
    BaselineState baseline = BaselineState::Unknown;
    uint32_t partial_refresh_count = 0;
    uint32_t partial_changed_pixels = 0;
    Rect dirty_region;
    bool has_dirty_region = false;
};

class StateModel {
public:
    static constexpr uint32_t kPartialRefreshLimit = 8;
    static constexpr uint32_t kPanelPixels = 400 * 300;
    // Full cleanup starts at 25% changed pixels in one update, or 50% summed
    // across partial updates including the pending frame. Repeated flips count.
    static constexpr uint32_t kHighContrastPixelLimit = kPanelPixels / 4;
    static constexpr uint32_t kPartialPixelLimit = kPanelPixels / 2;

    const State& state() const { return state_; }
    bool CanUsePartial() const;
    bool ShouldRequestFullClean(uint32_t changed_pixels = 0) const;
    void OnFull1BppSuccess();
    void OnPartial1BppSuccess(const Rect& region, uint32_t changed_pixels);
    void OnFull4BppSuccess();
    void OnRefreshError();
private:
    void SetUnknown();
    void AddDirtyRegion(const Rect& region);
    State state_;
};
}  // namespace zectrix::display
