#pragma once

#include <array>
#include <cstddef>

#include "canvas.h"

namespace note4::ui {

struct ViewPort {
    Canvas::Clip bounds{0, 0, 0, 0};
    void (*draw)(void* context, Canvas& canvas) = nullptr;
    void* context = nullptr;
};

// A fixed set of regions shares one framebuffer and one physical commit.
// A null draw callback retains content already drawn inside that viewport.
class ViewPortScheduler {
public:
    static constexpr std::size_t kCapacity = 4;
    struct Update {
        bool pending = false;
        bool quality = false;
        Canvas::Clip dirty{0, 0, 0, 0};
    };

    bool Configure(std::size_t id, const ViewPort& viewport);
    bool Invalidate(std::size_t id, bool quality = false);
    bool Enable(std::size_t id, bool enabled);
    Update Compose(Canvas& canvas);
    void Complete(bool success);
    struct Inspection {
        Canvas::Clip bounds{};
        bool configured = false, enabled = false, dirty = false, quality = false;
    };
    std::array<Inspection, kCapacity> Inspect() const {
        std::array<Inspection, kCapacity> result{};
        for (std::size_t i = 0; i < slots_.size(); ++i) {
            const auto& s = slots_[i];
            result[i] = {s.viewport.bounds, s.configured, s.enabled, s.dirty, s.quality};
        }
        return result;
    }

private:
    struct Slot {
        ViewPort viewport{};
        bool configured = false;
        bool enabled = false;
        bool dirty = false;
        bool quality = false;
        bool composed = false;
    };
    std::array<Slot, kCapacity> slots_{};
    bool composing_ = false;
};

}  // namespace note4::ui
