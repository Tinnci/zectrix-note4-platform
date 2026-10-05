#pragma once

#include "esp_err.h"
#include "note4_display_model.h"
#include "note4_epd_calibration.h"

namespace note4::storage {
class StorageService;
}
namespace note4::display {
inline constexpr char kCalibrationSettingKey[] = "epd.cal.v1";
// Default output even on error. Missing is a normal default; malformed or
// unsupported records are preserved and reported, never automatically erased.
esp_err_t LoadCalibration(const storage::StorageService& storage,
                          note4_epd_calibration_t* calibration, bool* saved = nullptr);
esp_err_t SaveCalibration(storage::StorageService& storage,
                          const note4_epd_calibration_t& calibration);
esp_err_t ResetCalibration(storage::StorageService& storage);
inline constexpr char kModelSettingKey[] = "epd.model.v1";
esp_err_t LoadModel(const storage::StorageService&, PhysicsParameters* out, bool* saved = nullptr);
esp_err_t SaveModel(storage::StorageService&, const PhysicsParameters&);
esp_err_t ResetModel(storage::StorageService&);
} // namespace note4::display
