#pragma once

#include "canvas.h"

namespace note4::reader { enum class FontSize : uint8_t; }

namespace note4::ui {
void DrawGlyph(Canvas& canvas, int x, int y, uint32_t codepoint,
               reader::FontSize font, bool inverted = false,
               sdk::TextStyle style = sdk::TextStyle::Regular);
void DrawUtf8Line(Canvas& canvas, int x, int y, const char* text,
                  int width, bool inverted = false,
                  sdk::TextStyle style = sdk::TextStyle::Regular);
}  // namespace note4::ui
