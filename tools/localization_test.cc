#include "zectrix_canvas.h"
#include "zectrix_status_bar.h"
#include "zectrix_first_party_app_controllers.h"
#include "zectrix_language_setting.h"
#include "zectrix_storage_service.h"
#include "zectrix_utf8.h"
#include "sdkconfig.h"

#include <cassert>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

namespace {
std::size_t allocations = 0;
bool present = false, fail_read = false, fail_commit = false;
uint32_t staged = 0, committed = 0;
unsigned writes = 0;
}

void* operator new(std::size_t size) {
    ++allocations;
    if (void* value = std::malloc(size ? size : 1)) return value;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }

namespace zectrix::storage {
esp_err_t StorageService::Create(StorageService** output) { *output = new StorageService(nullptr); return ESP_OK; }
StorageService::~StorageService() = default;
esp_err_t StorageService::GetUInt32(const char* key, uint32_t* value) const {
    assert(std::strcmp(key, i18n::kLanguageSettingKey) == 0);
    if (fail_read) return ESP_FAIL;
    if (!present) return ESP_ERR_NOT_FOUND;
    *value = staged;
    return ESP_OK;
}
esp_err_t StorageService::SetUInt32(const char* key, uint32_t value) {
    assert(std::strcmp(key, i18n::kLanguageSettingKey) == 0);
    ++writes;
    present = true;
    staged = value;
    // NVS can expose a staged value through the same handle after commit fails.
    if (fail_commit) return ESP_FAIL;
    committed = staged;
    return ESP_OK;
}
}  // namespace zectrix::storage

using namespace zectrix::i18n;

std::vector<std::string> FormatArguments(const char* text) {
    std::vector<std::string> result;
    while ((text = std::strchr(text, '%'))) {
        ++text;
        if (*text == '%') { ++text; continue; }
        while (*text && std::strchr("-+ #0.*123456789", *text)) ++text;
        const char* start = text;
        while (*text && std::strchr("hljztL", *text)) ++text;
        if (*text && std::strchr("diouxXfFeEgGaAcsp", *text)) result.emplace_back(start, ++text);
    }
    return result;
}

void TestCatalogAndGlyphs() {
    const char* names[] = {"None",
#define ZECTRIX_TEXT(id, english, chinese) #id,
#include "zectrix_strings.inc"
#undef ZECTRIX_TEXT
    };
    ZectrixCanvas canvas;
    for (std::size_t i = 1; i < static_cast<std::size_t>(Text::Count); ++i) {
        const auto id = static_cast<Text>(i);
        const char* english = Translate(id, Language::English);
        const char* chinese = Translate(id, Language::Chinese);
        assert(*english && *chinese);
        assert(FormatArguments(english) == FormatArguments(chinese));
        for (auto language : {Language::English, Language::Chinese}) {
            if (!Supports(language)) continue;
            const char* text = Translate(id, language);
            if (std::strncmp(names[i], "Nav", 3) == 0 && canvas.TextWidth(text) > 384) {
                std::fprintf(stderr, "Footer exceeds 384px: %s (%dpx)\n", names[i], canvas.TextWidth(text));
                assert(false);
            }
            while (*text) {
                const char* first = text;
                const auto cp = zectrix::ui::NextUtf8(text);
                assert(cp != 0xfffd);
                if (cp < 128) continue;
                char glyph[5]{};
                std::memcpy(glyph, first, static_cast<std::size_t>(text - first));
                assert(canvas.TextWidth(glyph) == 16);
            }
        }
    }
    assert(std::strcmp(Tr(Text::None, "App label"), "App label") == 0);
    assert(std::strcmp(Tr(static_cast<Text>(65535), "fallback"), "fallback") == 0);
    assert(std::strcmp(Tr(static_cast<Text>(65535), nullptr), "") == 0);
    assert(std::strcmp(LanguageName(Language::English), "English") == 0);
    if (Supports(Language::Chinese)) assert(std::strcmp(LanguageName(Language::Chinese), "简体中文") == 0);

    for (const char* bad : {"\xe4", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xc0"}) {
        const char* cursor = bad;
        assert(zectrix::ui::NextUtf8(cursor) == 0xfffd && !*cursor);
        assert(zectrix::ui::NextUtf8(cursor) == 0);
    }
    const char* mixed = "\xe4" "A";
    assert(zectrix::ui::NextUtf8(mixed) == 0xfffd);
    assert(zectrix::ui::NextUtf8(mixed) == 'A');

    const auto before = allocations;
    for (int i = 0; i < 20; ++i) {
        canvas.Clear();
        canvas.Text(8, 8, Tr(Text::Language));
        canvas.TextCentered(40, Tr(Text::WakeHint), 1, true);
        canvas.TextFitted(8, 80, "中文阅读 ABCDEFG", 72, i % 2);
        assert(canvas.TextWidth(Tr(Text::Settings)) > 0);
    }
    assert(allocations == before);

    canvas.Clear();
    const int fitted_width = canvas.TextWidth("中文...");
    canvas.TextFitted(10, 20, "中文阅读 ABCDEFG", fitted_width, true);
    for (int y = 0; y < 300; ++y) for (int x = 0; x < 400; ++x) {
        if (y >= 20 && y < 36 && x >= 10 && x < 10 + fitted_width) continue;
        assert(canvas.data()[y * 50 + x / 8] & (0x80 >> (x % 8)));
    }
    ZectrixCanvas expected;
    expected.Clear();
    expected.Text(10, 20, "中文...", 1, true);
    assert(std::memcmp(canvas.data(), expected.data(), canvas.size()) == 0);
    canvas.Clear();
    canvas.TextFitted(10, 20, "中文 ABC", 5);
    for (std::size_t i = 0; i < canvas.size(); ++i) assert(canvas.data()[i] == 0xff);
}

void TestLanguagePersistence() {
    zectrix::storage::StorageService* storage = nullptr;
    assert(zectrix::storage::StorageService::Create(&storage) == ESP_OK);
    assert(RestoreLanguage(*storage) == ESP_ERR_NOT_FOUND);
    assert(CurrentLanguage() == DefaultLanguage() && writes == 0);
    present = true;
    staged = 99;
    assert(RestoreLanguage(*storage) == ESP_ERR_NOT_SUPPORTED);
    assert(CurrentLanguage() == DefaultLanguage() && staged == 99 && writes == 0);
    fail_read = true;
    assert(RestoreLanguage(*storage) == ESP_FAIL && writes == 0);
    fail_read = false;
    assert(SaveLanguage(*storage, Language::English) == ESP_OK);
    SetLanguage(DefaultLanguage());
    staged = committed;
    assert(RestoreLanguage(*storage) == ESP_OK && CurrentLanguage() == Language::English);
    const auto previous_writes = writes;
    assert(!SetLanguage(static_cast<Language>(99)) && CurrentLanguage() == Language::English);
    assert(SaveLanguage(*storage, static_cast<Language>(99)) == ESP_ERR_NOT_SUPPORTED && writes == previous_writes);
    if (Supports(Language::Chinese)) {
        assert(SaveLanguage(*storage, Language::Chinese) == ESP_OK);
        fail_commit = true;
        assert(SaveLanguage(*storage, Language::English) == ESP_FAIL);
        assert(CurrentLanguage() == Language::English && committed == 1 && staged == 0);
        const auto failed_writes = writes;
        fail_commit = false;
        assert(SaveLanguage(*storage, Language::English) == ESP_OK);
        assert(writes == failed_writes + 1 && committed == 0);
    } else {
        staged = 1;
        assert(RestoreLanguage(*storage) == ESP_ERR_NOT_SUPPORTED && CurrentLanguage() == Language::English);
        assert(SaveLanguage(*storage, Language::Chinese) == ESP_ERR_NOT_SUPPORTED);
    }
    delete storage;
}

bool Ink(const ZectrixCanvas& canvas, int x, int y) {
    return !(canvas.data()[y * ZectrixCanvas::kStride + x / 8] & (0x80 >> (x & 7)));
}

void TestStatusIcons() {
    using namespace zectrix::ui;
    StatusBarState state;
    state.time_valid = state.battery_valid = true;
    state.hour = 12;
    state.minute = 34;
    ZectrixCanvas normal, inverse, clipped;
    const auto before = allocations;
    unsigned previous_fill = 0;
    for (const uint8_t percent : {0, 5, 20, 50, 80, 100}) {
        state.battery_percent = percent;
        normal.Clear(false);
        DrawStatusBar(normal, state);
        inverse.Clear(false);
        DrawStatusBar(inverse, state, true);
        for (int i = 0; i < ZectrixCanvas::kStride * kStatusBarHeight; ++i)
            assert(normal.data()[i] == static_cast<uint8_t>(~inverse.data()[i]));
        for (std::size_t i = ZectrixCanvas::kStride * kStatusBarHeight; i < normal.size(); ++i)
            assert(normal.data()[i] == 0 && inverse.data()[i] == 0);
        unsigned fill = 0;
        for (int y = 9; y < 15; ++y) for (int x = 334; x < 348; ++x) fill += Ink(normal, x, y);
        assert(fill >= previous_fill);
        if (percent == 0) assert(fill == 0);
        else assert(fill > 0);
        if (percent == 100) assert(fill == 60);
        previous_fill = fill;

        // Power marks must never erase charge cells or the measured percentage.
        for (unsigned flags = 0; flags < 16; ++flags) {
            auto power = state;
            power.charging = flags & 1;
            power.charge_full = flags & 2;
            power.external_power = flags & 4;
            power.charge_fault = flags & 8;
            DrawStatusBar(inverse, power);
            for (int y = 0; y < kStatusBarHeight; ++y)
                for (int x = 332; x < 400; ++x) assert(Ink(normal, x, y) == Ink(inverse, x, y));
        }
    }
    // Every radio mode must remain identifiable in either fixed slot.
    constexpr RadioIndicator radios[] = {RadioIndicator::Off, RadioIndicator::Ready,
        RadioIndicator::Connected, RadioIndicator::Active, RadioIndicator::Fault};
    for (const auto radio : radios) for (const auto other : radios) {
        state.ble = state.wifi = radio;
        DrawStatusBar(normal, state);
        auto changed = state;
        changed.ble = changed.wifi = other;
        DrawStatusBar(inverse, changed);
        for (int left : {256, 288}) {
            unsigned different = 0;
            for (int y = 0; y < 23; ++y) for (int x = left; x < left + 24; ++x)
                different += Ink(normal, x, y) != Ink(inverse, x, y);
            assert((different == 0) == (radio == other));
        }
    }
    const auto same_visible = [&](const StatusBarState& left, const StatusBarState& right) {
        assert(left == right);
        DrawStatusBar(normal, left);
        DrawStatusBar(inverse, right);
        assert(std::memcmp(normal.data(), inverse.data(), normal.size()) == 0);
    };
    auto hidden = state;
    hidden.battery_percent = 255;
    same_visible(state, hidden);
    state.charge_fault = true;
    hidden = state;
    hidden.charging = hidden.charge_full = hidden.external_power = true;
    same_visible(state, hidden);
    state = hidden;
    state.charge_fault = hidden.charge_fault = false;
    hidden.charging = hidden.external_power = false;
    same_visible(state, hidden);
    state.time_valid = state.battery_valid = false;
    hidden = state;
    hidden.hour = hidden.minute = hidden.battery_percent = 0;
    same_visible(state, hidden);
    state.battery_absent = true;
    hidden = state;
    hidden.battery_valid = true;
    hidden.charging = hidden.charge_full = false;
    same_visible(state, hidden);
    hidden.battery_absent = false;
    assert(!(state == hidden));
    DrawStatusBar(inverse, hidden);
    assert(std::memcmp(normal.data(), inverse.data(), 24 * 50) != 0);

    for (bool inverted : {false, true}) {
        normal.Clear(false);
        DrawStatusBar(normal, state, inverted);
        clipped.ResetClip();
        clipped.Clear(false);
        clipped.SetClip({300, 8, 44, 8});
        DrawStatusBar(clipped, state, inverted);
        assert(clipped.clip().x == 300 && clipped.clip().width == 44);
        for (int y = 0; y < 300; ++y) for (int x = 0; x < 400; ++x)
            assert(Ink(clipped, x, y) == (x >= 300 && x < 344 && y >= 8 && y < 16 ? Ink(normal, x, y) : true));
    }
    const auto language = CurrentLanguage();
    SetLanguage(Language::English);
    DrawStatusBar(normal, state);
    SetLanguage(Language::Chinese);
    DrawStatusBar(inverse, state);
    assert(std::memcmp(normal.data(), inverse.data(), normal.size()) == 0);
    SetLanguage(language);
    assert(allocations == before);
}

void TestTypography() {
    using zectrix::sdk::TextStyle;
    ZectrixCanvas canvas, clipped, expected;
    const auto before = allocations;
    for (int scale : {1, 2, 3}) for (unsigned flags = 0; flags < 32; ++flags) {
        const auto style = static_cast<TextStyle>(flags);
        for (bool inverted : {false, true}) {
            canvas.Clear();
            canvas.Text(8, 20, "AV中jg文", scale, inverted, style);
            const auto width = canvas.TextWidth("AV中jg文", scale, style);
            const auto height = canvas.TextHeight("AV中jg文", scale, style);
            clipped.ResetClip();
            clipped.Clear();
            const ZectrixCanvas::Clip clip{13, 23, 43, 17};
            clipped.SetClip(clip);
            clipped.Text(8, 20, "AV中jg文", scale, inverted, style);
            for (int y = 0; y < 300; ++y) for (int x = 0; x < 400; ++x) {
                if (x < 8 || x >= 8 + width || y < 20 || y >= 20 + height)
                    assert(!Ink(canvas, x, y));
                const bool inside = x >= clip.x && x < clip.x + clip.width &&
                    y >= clip.y && y < clip.y + clip.height;
                assert(Ink(clipped, x, y) == (inside && Ink(canvas, x, y)));
            }
            assert(clipped.clip().x == clip.x && clipped.clip().width == clip.width);
        }
    }
    for (unsigned flags = 0; flags < 32; ++flags) {
        const auto style = static_cast<TextStyle>(flags);
        const auto width = canvas.TextWidth("A中...", 1, style);
        canvas.Clear(); expected.Clear();
        canvas.TextFitted(8, 20, "A中文 and more words", width, true, style);
        expected.Text(8, 20, "A中...", 1, true, style);
        assert(std::memcmp(canvas.data(), expected.data(), canvas.size()) == 0);
        for (int limit = 0; limit < width; ++limit) {
            canvas.Clear();
            canvas.TextFitted(8, 20, "A中文 and more words", limit, true, style);
            for (int y = 0; y < 70; ++y) for (int x = 8 + limit; x < 150; ++x)
                assert(!Ink(canvas, x, y));
        }
    }

    // Bold must be an exact horizontal dilation, not a shifted or clipped glyph.
    for (int scale : {1, 2}) {
        canvas.Clear(); expected.Clear();
        canvas.Text(8, 20, "F", scale, false, TextStyle::Bold);
        expected.Text(8, 20, "F", scale);
        const int regular = canvas.TextWidth("F", scale);
        assert(canvas.TextWidth("F", scale, TextStyle::Bold) == regular + scale);
        for (int y = 20; y < 20 + 16 * scale; ++y) for (int x = 8; x < 8 + regular + scale; ++x) {
            bool dilated = false;
            for (int delta = 0; delta <= scale; ++delta) dilated |= Ink(expected, x - delta, y);
            assert(Ink(canvas, x, y) == dilated);
        }
    }
    for (auto style : {TextStyle::Bold, TextStyle::Italic, TextStyle::Bold | TextStyle::Italic}) {
        canvas.Clear(); expected.Clear();
        canvas.Text(8, 20, "中", 1, false, style);
        expected.Text(8, 20, "中", 1, false, TextStyle::Underline);
        assert(std::memcmp(canvas.data(), expected.data(), canvas.size()) == 0);
        assert(canvas.TextHeight("中", 1, style) == 18);
    }
    canvas.Clear(); expected.Clear();
    canvas.Text(8, 20, "Dim text 中文阅读", 1, false, TextStyle::Dim);
    expected.Text(8, 20, "Dim text 中文阅读");
    unsigned regular_ink = 0, dim_ink = 0;
    for (int y = 20; y < 36; ++y) for (int x = 8; x < 300; ++x) {
        assert(!Ink(canvas, x, y) || Ink(expected, x, y));
        regular_ink += Ink(expected, x, y); dim_ink += Ink(canvas, x, y);
    }
    assert(dim_ink * 2 > regular_ink && dim_ink < regular_ink);
    canvas.Clear();
    canvas.Text(8, 20, "OK", 1, false, TextStyle::Keycap);
    const int box_width = canvas.TextWidth("OK", 1, TextStyle::Keycap);
    assert(box_width == canvas.TextWidth("OK") + 6);
    assert(canvas.TextHeight("OK", 1, TextStyle::Keycap) == 20);
    for (int x = 8; x < 8 + box_width; ++x) assert(Ink(canvas, x, 20) && Ink(canvas, x, 39));
    for (int y = 20; y < 40; ++y) assert(Ink(canvas, 8, y) && Ink(canvas, 7 + box_width, y));
    canvas.Clear();
    for (int edge : {INT_MIN, INT_MAX}) {
        canvas.Text(edge, edge, "OK", 1, true, TextStyle::Keycap | TextStyle::Italic);
        canvas.TextCentered(edge, "OK", INT_MAX, true, TextStyle::Bold);
        assert(canvas.TextWidth("OK", edge) == 0);
    }
    assert(canvas.TextHeight(nullptr, 1, TextStyle::Keycap) == 0);
    assert(canvas.TextWidth("", 1, TextStyle::Keycap) == 0);
    for (std::size_t i = 0; i < canvas.size(); ++i) assert(canvas.data()[i] == 0xff);
    assert(allocations == before);
}

void TestLanguageScenes() {
    using namespace zectrix::app;
    using namespace zectrix::sdk;
    const InputEvent ok{Button::Ok, InputAction::Click}, down{Button::Down, InputAction::Click},
        back{Button::Ok, InputAction::LongPress}, off{Button::Down, InputAction::LongPress};
    SetLanguage(Language::English);
    SettingsController settings(false);
    assert(settings.Start() == Status::Ok);
    settings.Tick();
    if (LanguageCount() > 1) {
        assert(settings.Handle(ok).decision == SettingsDecision::RenderQuality);
        assert(settings.page() == SettingsPage::Language && settings.selected() == 0);
        assert(settings.Handle(down).decision == SettingsDecision::RenderFast);
        assert(CurrentLanguage() == Language::English);
        assert(settings.Handle(back).decision == SettingsDecision::RenderQuality);
        assert(settings.page() == SettingsPage::Options && settings.selected() == 0);
        settings.Handle(ok);
        assert(settings.selected() == 0);
        settings.Handle(down);
        const auto apply = settings.Handle(ok);
        assert(apply.decision == SettingsDecision::SaveLanguage && apply.language == Language::Chinese);
        assert(SetLanguage(apply.language));
        settings.Presented(false);
        assert(settings.Tick().decision == SettingsDecision::RenderQuality);
        settings.Presented(true);
        assert(settings.Handle(off).decision == SettingsDecision::Shutdown);
        assert(settings.Handle(back).decision == SettingsDecision::RenderQuality);
        settings.Handle(ok);
        assert(settings.selected() == 1);
        settings.Handle(down);
        assert(settings.Handle(ok).language == Language::English);
        assert(SetLanguage(Language::English));
        settings.Handle(back);
    } else {
        assert(settings.option_count() == 1);
        assert(settings.Handle(ok).decision == SettingsDecision::Save);
    }
    assert(settings.Handle(back).decision == SettingsDecision::Back);
    settings.Stop();
    assert(settings.Tick().decision == SettingsDecision::None);
    assert(settings.Handle(ok).decision == SettingsDecision::None);
}

int main() {
    TestCatalogAndGlyphs();
    TestTypography();
    TestStatusIcons();
    TestLanguagePersistence();
    TestLanguageScenes();
    std::printf("PASS: localization, UTF-8 bounds, zero-allocation drawing, persistence and scenes (reader=%d, Chinese=%d).\n",
        CONFIG_ZECTRIX_ENABLE_READER, CONFIG_ZECTRIX_ENABLE_UI_CHINESE);
}
