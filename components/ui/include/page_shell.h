#pragma once

#include "layout.h"

namespace zectrix::ui {

// Configuration parameters for a standard page shell.
struct PageConfig {
    const char* title = "";
    const char* footer = "";
    bool portrait_capable = true;
};

}  // namespace zectrix::ui
