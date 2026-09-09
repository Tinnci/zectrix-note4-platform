#pragma once

#include "zectrix_canvas.h"
#include "zectrix_reader.h"

namespace zectrix::ui {
void DrawGlyph(ZectrixCanvas& canvas, int x, int y, uint32_t codepoint,
               reader::FontSize font, bool inverted = false);
void DrawUtf8Line(ZectrixCanvas& canvas, int x, int y, const char* text,
                  int width, bool inverted = false);
}  // namespace zectrix::ui
