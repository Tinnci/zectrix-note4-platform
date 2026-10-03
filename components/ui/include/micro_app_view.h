#pragma once

#include "canvas.h"
#include "note4_runtime.h"
#include "note4_app_package.h"

namespace note4::ui {
// Draw only the completed host-owned command buffer, inside the guest viewport.
void DrawMicroAppFrame(Canvas& canvas, const runtime::Frame& frame);
void DrawMicroAppIcon(Canvas& canvas, const package::Metadata& meta,
                      int x, int y, int side = 16, bool inverse = false);
}  // namespace note4::ui
