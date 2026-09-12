#include "zectrix_micro_app_view.h"

#include <algorithm>

namespace zectrix::ui {

void DrawMicroAppIcon(ZectrixCanvas& canvas, const package::Metadata& meta,
                      int x, int y, int side, bool inverse) {
    if ((meta.icon_side != 16 && meta.icon_side != 32) || (side != 16 && side != 32)) return;
    const int step = std::max(1, meta.icon_side / side);
    for (int row = 0; row < side; ++row) for (int col = 0; col < side; ++col) {
        bool ink = false;
        // Keep single-pixel strokes visible when reducing a 32-pixel icon.
        for (int dy = 0; dy < step; ++dy) for (int dx = 0; dx < step; ++dx) {
            const int bit = (row * meta.icon_side / side + dy) * meta.icon_side + col * meta.icon_side / side + dx;
            ink |= (meta.icon[bit / 8] & (0x80 >> (bit % 8))) != 0;
        }
        canvas.Pixel(x + col, y + row, ink != inverse);
    }
}

void DrawMicroAppFrame(ZectrixCanvas& canvas, const runtime::Frame& frame) {
    const auto clip = canvas.clip();
    constexpr int x = 12, y = 66;
    const int left = std::max(x, clip.x), top = std::max(y, clip.y);
    const int right = std::min(x + runtime::kWidth, clip.x + clip.width);
    const int bottom = std::min(y + runtime::kHeight, clip.y + clip.height);
    canvas.SetClip({left, top, std::max(0, right - left), std::max(0, bottom - top)});
    for (std::size_t i = 0; i < frame.count; ++i) {
        const auto& command = frame.commands[i];
        switch (command.kind) {
            case runtime::DrawKind::Text:
                canvas.Text(x + command.x, y + command.y, frame.text.data() + command.text,
                            command.scale, false, command.style);
                break;
            case runtime::DrawKind::Rect:
                canvas.Rect(x + command.x, y + command.y, command.width, command.height);
                break;
            case runtime::DrawKind::Fill:
                canvas.FillRect(x + command.x, y + command.y, command.width, command.height, true);
                break;
        }
    }
    canvas.SetClip(clip);
}

}  // namespace zectrix::ui
