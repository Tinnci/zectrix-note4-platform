#include "zectrix_micro_app_view.h"

namespace zectrix::ui {

void DrawMicroAppFrame(ZectrixCanvas& canvas, const runtime::Frame& frame) {
    const auto clip = canvas.clip();
    constexpr int x = 12, y = 66;
    canvas.SetClip({x, y, runtime::kWidth, runtime::kHeight});
    for (std::size_t i = 0; i < frame.count; ++i) {
        const auto& command = frame.commands[i];
        switch (command.kind) {
            case runtime::DrawKind::Text:
                canvas.Text(x + command.x, y + command.y, frame.text.data() + command.text, command.scale);
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
