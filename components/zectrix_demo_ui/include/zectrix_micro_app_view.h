#pragma once

#include "zectrix_canvas.h"
#include "zectrix_runtime.h"
#include "zectrix_app_package.h"

namespace zectrix::ui {
// Draw only the completed host-owned command buffer, inside the guest viewport.
void DrawMicroAppFrame(ZectrixCanvas& canvas, const runtime::Frame& frame);
void DrawMicroAppIcon(ZectrixCanvas& canvas, const package::Metadata& meta,
                      int x, int y, int side = 16, bool inverse = false);
}  // namespace zectrix::ui
