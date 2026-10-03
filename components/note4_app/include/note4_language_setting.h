#pragma once

#include "esp_err.h"
#include "note4_locale.h"

namespace note4::storage { class StorageService; }

namespace note4::i18n {
inline constexpr char kLanguageSettingKey[] = "ui.language";
// Invalid or unavailable preferences use the compiled default without a write.
esp_err_t RestoreLanguage(storage::StorageService& storage);
// The selection takes effect for this boot even when persistence fails.
esp_err_t SaveLanguage(storage::StorageService& storage, Language language);
}  // namespace note4::i18n
