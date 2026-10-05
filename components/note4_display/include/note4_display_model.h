#pragma once
#include "note4_display_physics.h"
namespace note4::display {
inline constexpr std::size_t kModelBytes = 112;
bool EncodeModel(const PhysicsParameters&, uint8_t* bytes, std::size_t size);
bool DecodeModel(const uint8_t* bytes, std::size_t size, PhysicsParameters* out);
// Transactional update; revision increments only for a valid complete edit.
bool UpdateModel(PhysicsParameters* model, const char* group, const uint32_t* values,
                 std::size_t count);
} // namespace note4::display
