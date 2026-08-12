#pragma once

#include <cstddef>
#include <cstdint>

#include "zectrix_input_service.h"

namespace zectrix::app {

enum class LauncherDecision : uint8_t {
    None,
    RenderFast,
    OpenClock,
    RunLegacy,
    Shutdown,
};

struct LauncherResult {
    LauncherDecision decision = LauncherDecision::None;
    std::size_t selected = 0;
};

class LauncherController {
public:
    static constexpr std::size_t kItemCount = 6;

    LauncherResult Handle(const input::InputEvent& event);
    std::size_t selected() const { return selected_; }

private:
    std::size_t selected_ = 0;
};

enum class ClockDecision : uint8_t { None, Home, Shutdown };

ClockDecision HandleClockInput(const input::InputEvent& event);

}  // namespace zectrix::app
