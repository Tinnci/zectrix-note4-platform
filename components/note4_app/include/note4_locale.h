#pragma once

#include <cstddef>
#include <cstdint>

namespace note4::i18n {

enum class Language : uint32_t { English = 0, Chinese = 1 };
enum class Text : uint16_t {
    None,
#define NOTE4_TEXT(id, english, chinese) id,
#include "note4_strings.inc"
#undef NOTE4_TEXT
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

}  // namespace note4::i18n
