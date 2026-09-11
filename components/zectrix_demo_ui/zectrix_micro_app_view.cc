#include "zectrix_micro_app_view.h"

#include <algorithm>

namespace zectrix::ui {

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
