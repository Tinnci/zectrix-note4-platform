#include "zectrix_language_setting.h"
#include "zectrix_storage_service.h"

namespace zectrix::i18n {
esp_err_t RestoreLanguage(storage::StorageService& storage) {
    SetLanguage(DefaultLanguage());
    uint32_t value = 0;
    const auto result = storage.GetUInt32(kLanguageSettingKey, &value);
    if (result != ESP_OK) return result;
    return SetLanguage(static_cast<Language>(value)) ? ESP_OK : ESP_ERR_NOT_SUPPORTED;
}

esp_err_t SaveLanguage(storage::StorageService& storage, Language language) {
    if (!SetLanguage(language)) return ESP_ERR_NOT_SUPPORTED;
    return storage.SetUInt32(kLanguageSettingKey, static_cast<uint32_t>(language));
}
}  // namespace zectrix::i18n
