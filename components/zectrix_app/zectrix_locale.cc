#include "zectrix_locale.h"
#include "sdkconfig.h"

namespace zectrix::i18n {
namespace {
constexpr const char* kEnglish[] = {"",
#define ZECTRIX_TEXT(id, english, chinese) english,
#include "zectrix_strings.inc"
#undef ZECTRIX_TEXT
};
#if CONFIG_ZECTRIX_ENABLE_UI_CHINESE
constexpr const char* kChinese[] = {"",
#define ZECTRIX_TEXT(id, english, chinese) chinese,
#include "zectrix_strings.inc"
#undef ZECTRIX_TEXT
};
#endif
#if CONFIG_ZECTRIX_ENABLE_UI_CHINESE && CONFIG_ZECTRIX_UI_DEFAULT_CHINESE
Language language = Language::Chinese;
#else
Language language = Language::English;
#endif
}  // namespace

Language DefaultLanguage() {
#if CONFIG_ZECTRIX_ENABLE_UI_CHINESE && CONFIG_ZECTRIX_UI_DEFAULT_CHINESE
    return Language::Chinese;
#else
    return Language::English;
#endif
}
Language CurrentLanguage() { return language; }
bool Supports(Language value) {
    return value == Language::English
#if CONFIG_ZECTRIX_ENABLE_UI_CHINESE
        || value == Language::Chinese
#endif
        ;
}
bool SetLanguage(Language value) {
    if (!Supports(value)) return false;
    language = value;
    return true;
}
std::size_t LanguageCount() { return Supports(Language::Chinese) ? 2 : 1; }
const char* LanguageName(Language value) {
    return Translate(value == Language::Chinese ? Text::Chinese : Text::English, value);
}
const char* Translate(Text text, Language value, const char* fallback) {
    const auto index = static_cast<std::size_t>(text);
    if (!index || index >= static_cast<std::size_t>(Text::Count)) return fallback ? fallback : "";
#if CONFIG_ZECTRIX_ENABLE_UI_CHINESE
    if (value == Language::Chinese && kChinese[index][0]) return kChinese[index];
#else
    (void)value;
#endif
    return kEnglish[index];
}
const char* Tr(Text text, const char* fallback) { return Translate(text, language, fallback); }

}  // namespace zectrix::i18n
