#include "zectrix_view_port.h"

#include <algorithm>

namespace zectrix::ui {

bool ViewPortScheduler::Configure(std::size_t id, const ViewPort& viewport) {
    const auto& r = viewport.bounds;
    if (composing_ || id >= kCapacity || r.x < 0 || r.y < 0 ||
        r.width <= 0 || r.height <= 0 || r.x > ZectrixCanvas::kWidth ||
        r.y > ZectrixCanvas::kHeight || r.width > ZectrixCanvas::kWidth - r.x ||
        r.height > ZectrixCanvas::kHeight - r.y) return false;
    slots_[id] = {viewport, true, true, true, false, false};
    return true;
}

bool ViewPortScheduler::Invalidate(std::size_t id, bool quality) {
    if (composing_ || id >= kCapacity || !slots_[id].configured) return false;
    slots_[id].dirty = true;
    slots_[id].quality |= quality;
    return true;
}

bool ViewPortScheduler::Enable(std::size_t id, bool enabled) {
    if (composing_ || id >= kCapacity || !slots_[id].configured) return false;
    if (slots_[id].enabled != enabled) {
        slots_[id].enabled = enabled;
        slots_[id].dirty = true;
    }
    return true;
}

ViewPortScheduler::Update ViewPortScheduler::Compose(ZectrixCanvas& canvas) {
    Update result;
    if (composing_) return result;
    composing_ = true;
    const auto saved_clip = canvas.clip();
    for (auto& slot : slots_) {
        slot.composed = slot.configured && slot.enabled && slot.dirty;
        if (!slot.composed) continue;
        const auto& r = slot.viewport.bounds;
        canvas.SetClip(r);
        if (slot.viewport.draw) slot.viewport.draw(slot.viewport.context, canvas);
        if (!result.pending) result.dirty = r;
        else {
            const int right = std::max(result.dirty.x + result.dirty.width, r.x + r.width);
            const int bottom = std::max(result.dirty.y + result.dirty.height, r.y + r.height);
            result.dirty.x = std::min(result.dirty.x, r.x);
            result.dirty.y = std::min(result.dirty.y, r.y);
            result.dirty.width = right - result.dirty.x;
            result.dirty.height = bottom - result.dirty.y;
        }
        result.pending = true;
        result.quality |= slot.quality;
    }
    canvas.SetClip(saved_clip);
    composing_ = false;
    return result;
}

void ViewPortScheduler::Complete(bool success) {
    if (composing_) return;
    for (auto& slot : slots_) {
        if (success && slot.composed) slot.dirty = slot.quality = false;
        slot.composed = false;
    }
}

}  // namespace zectrix::ui
