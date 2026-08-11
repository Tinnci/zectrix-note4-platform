#pragma once
#include <cstdint>
namespace zectrix::display {
enum class BaselineState : uint8_t { Unknown, Valid1Bpp };
struct Rect { int x=0, y=0, width=0, height=0; bool IsEmpty() const; };
struct State { BaselineState baseline=BaselineState::Unknown; uint32_t partial_refresh_count=0; Rect dirty_region; bool has_dirty_region=false; };
class StateModel {
public:
    static constexpr uint32_t kPartialRefreshLimit = 8;
    const State& state() const { return state_; }
    bool CanUsePartial() const;
    bool ShouldRequestFullClean() const;
    void OnFull1BppSuccess();
    void OnPartial1BppSuccess(const Rect& region);
    void OnFull4BppSuccess();
    void OnRefreshError();
private:
    void SetUnknown();
    void AddDirtyRegion(const Rect& region);
    State state_;
};
}
