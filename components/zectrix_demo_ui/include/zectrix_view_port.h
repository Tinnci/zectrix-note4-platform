#pragma once

#include <array>
#include <cstddef>

#include "zectrix_canvas.h"

namespace zectrix::ui {

struct ViewPort {
    ZectrixCanvas::Clip bounds{0, 0, 0, 0};
    void (*draw)(void* context, ZectrixCanvas& canvas) = nullptr;
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
        ZectrixCanvas::Clip dirty{0, 0, 0, 0};
    };

    bool Configure(std::size_t id, const ViewPort& viewport);
    bool Invalidate(std::size_t id, bool quality = false);
    bool Enable(std::size_t id, bool enabled);
    Update Compose(ZectrixCanvas& canvas);
    void Complete(bool success);

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

}  // namespace zectrix::ui
