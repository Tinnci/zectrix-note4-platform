#pragma once

#include <array>
#include <cstdint>

namespace zectrix::app {

// A copied local bookmark summary never owns an open book or a reader engine.
struct ReadingOverview {
    enum class State : uint8_t { Unavailable, Empty, Saved, Error };
    State state = State::Unavailable;
    std::array<char, 64> book_id{};
    uint16_t progress_per_mille = 0;
};

}  // namespace zectrix::app
