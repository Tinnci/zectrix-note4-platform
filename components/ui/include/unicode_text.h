#pragma once

#include "zectrix_canvas.h"

namespace zectrix::reader { enum class FontSize : uint8_t; }

namespace zectrix::ui {
void DrawGlyph(ZectrixCanvas& canvas, int x, int y, uint32_t codepoint,
               reader::FontSize font, bool inverted = false,
               sdk::TextStyle style = sdk::TextStyle::Regular);
void DrawUtf8Line(ZectrixCanvas& canvas, int x, int y, const char* text,
                  int width, bool inverted = false,
                  sdk::TextStyle style = sdk::TextStyle::Regular);
}  // namespace zectrix::ui
