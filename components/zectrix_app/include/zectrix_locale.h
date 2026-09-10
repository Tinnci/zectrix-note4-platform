#pragma once

#include <cstddef>
#include <cstdint>

namespace zectrix::i18n {

enum class Language : uint32_t { English = 0, Chinese = 1 };
enum class Text : uint16_t {
    None,
#define ZECTRIX_TEXT(id, english, chinese) id,
#include "zectrix_strings.inc"
#undef ZECTRIX_TEXT
    Count,
};

// Only the foreground application owner changes language. Returned strings
// have static lifetime; lookup never opens storage or allocates memory.
Language DefaultLanguage();
Language CurrentLanguage();
bool Supports(Language language);
bool SetLanguage(Language language);
std::size_t LanguageCount();
const char* LanguageName(Language language);
const char* Translate(Text text, Language language, const char* fallback = "");
const char* Tr(Text text, const char* fallback = "");

}  // namespace zectrix::i18n
