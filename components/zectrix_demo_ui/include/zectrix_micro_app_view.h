#pragma once

#include "zectrix_canvas.h"
#include "zectrix_runtime.h"

namespace zectrix::ui {
// Draw only the completed host-owned command buffer, inside the guest viewport.
void DrawMicroAppFrame(ZectrixCanvas& canvas, const runtime::Frame& frame);
}  // namespace zectrix::ui
