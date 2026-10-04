#pragma once

#include "canvas.h"

namespace note4::ui {

inline Canvas::UiFace FocusTextFace(bool selected) {
    return selected ? Canvas::UiFace::Selected : Canvas::UiFace::Navigation;
}

// A rail and text weight express focus without inverting the whole surface.
// Call inside an already-cleared page/row; the normal canvas clip still applies.
inline void DrawFocusRail(Canvas& canvas, Rect bounds, bool selected) {
    if (!selected || bounds.IsEmpty()) return;
    const int height = std::min(bounds.height, 24);
    canvas.FillRect(bounds.x, bounds.y + (bounds.height - height) / 2,
                    std::min(bounds.width, 3), height, true);
}

}  // namespace note4::ui
