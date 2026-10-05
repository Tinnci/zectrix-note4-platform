#include "note4_display_calibration_store.h"

#include "note4_storage_service.h"
#include <array>

namespace note4::display {
esp_err_t LoadModel(const storage::StorageService& storage, PhysicsParameters* out, bool* saved) {
    if (!out)
        return ESP_ERR_INVALID_ARG;
    *out = PhysicsParameters{};
    if (saved)
        *saved = false;
    std::array<uint8_t, kModelBytes> bytes{};
    size_t size = 0;
    auto error = storage.GetBlob(kModelSettingKey, nullptr, &size);
    if (error == ESP_ERR_NOT_FOUND)
        return ESP_OK;
    if (error != ESP_OK)
        return error;
    if (size != bytes.size())
        return ESP_ERR_INVALID_SIZE;
    error = storage.GetBlob(kModelSettingKey, bytes.data(), &size);
    if (error != ESP_OK)
        return error;
    if (!DecodeModel(bytes.data(), size, out))
        return ESP_ERR_INVALID_ARG;
    if (saved)
        *saved = true;
    return ESP_OK;
}
esp_err_t SaveModel(storage::StorageService& storage, const PhysicsParameters& p) {
    std::array<uint8_t, kModelBytes> bytes{};
    if (!EncodeModel(p, bytes.data(), bytes.size()))
        return ESP_ERR_INVALID_ARG;
    return storage.SetBlob(kModelSettingKey, bytes.data(), bytes.size());
}
esp_err_t ResetModel(storage::StorageService& storage) {
    const auto error = storage.Erase(kModelSettingKey);
    return error == ESP_ERR_NOT_FOUND ? ESP_OK : error;
}
esp_err_t LoadCalibration(const storage::StorageService& storage, note4_epd_calibration_t* out,
                          bool* saved) {
    if (!out)
        return ESP_ERR_INVALID_ARG;
    note4_epd_calibration_default(out);
    if (saved)
        *saved = false;
    std::array<uint8_t, NOTE4_EPD_CALIBRATION_BYTES> bytes{};
    size_t size = 0;
    auto error = storage.GetBlob(kCalibrationSettingKey, nullptr, &size);
    if (error == ESP_ERR_NOT_FOUND)
        return ESP_OK;
    if (error != ESP_OK)
        return error;
    if (size != bytes.size())
        return ESP_ERR_INVALID_SIZE;
    error = storage.GetBlob(kCalibrationSettingKey, bytes.data(), &size);
    if (error != ESP_OK)
        return error;
    if (!note4_epd_calibration_decode(bytes.data(), size, out))
        return ESP_ERR_INVALID_ARG;
    if (saved)
        *saved = true;
    return ESP_OK;
}

esp_err_t SaveCalibration(storage::StorageService& storage,
                          const note4_epd_calibration_t& calibration) {
    std::array<uint8_t, NOTE4_EPD_CALIBRATION_BYTES> bytes{};
    if (!note4_epd_calibration_encode(&calibration, bytes.data(), bytes.size()))
        return ESP_ERR_INVALID_ARG;
    // A single existing NVS blob commit covers the whole profile. NVS supplies
    // integrity and recovery; no second checksum or multi-key transaction.
    return storage.SetBlob(kCalibrationSettingKey, bytes.data(), bytes.size());
}

esp_err_t ResetCalibration(storage::StorageService& storage) {
    const auto error = storage.Erase(kCalibrationSettingKey);
    return error == ESP_ERR_NOT_FOUND ? ESP_OK : error;
}
} // namespace note4::display
