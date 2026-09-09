#include "zectrix_first_party_app_controllers.h"
#include "zectrix_clock_editor.h"

#include <algorithm>
#include <array>
#include <cassert>

void TestClockDraft() {
    using zectrix::app::ClockEditor;
    ClockEditor editor;
    const zectrix::time::DateTime leap_day{2024, 2, 29, 4, 23, 59, 52};
    editor.Begin(leap_day, 5 * 3600 + 45 * 60);
    assert(editor.value().second == 0 && editor.offset_seconds() == 20700);
    editor.Adjust(1);
    assert(editor.value().year == 2025 && editor.value().day == 28);
    editor.Next();
    editor.Adjust(-1);
    assert(editor.value().month == 1);
    editor.Next();
    for (int i = 0; i < 4; ++i) editor.Adjust(1);
    assert(editor.value().day == 1);
    editor.Next();
    editor.Adjust(1);
    assert(editor.value().hour == 0);
    editor.Next();
    editor.Adjust(1);
    assert(editor.value().minute == 0);
    editor.Next();
    editor.Adjust(-1);
    assert(editor.offset_seconds() == 19800);
    editor.Next();
    assert(editor.field() == ClockEditor::Save && zectrix::time::IsValid(editor.value()));
    editor.Adjust(1);
    assert(editor.field() == ClockEditor::UtcOffset);
    editor.Next();
    editor.Adjust(-1);
    assert(editor.field() == ClockEditor::Year);
    // Cancelling is just dropping the draft; reopening starts from the sample.
    editor.Begin(leap_day, 50400);
    assert(editor.value().day == 29 && leap_day.second == 52);
    for (int i = 0; i < 5; ++i) editor.Next();
    editor.Adjust(1);
    assert(editor.offset_seconds() == -50400);
    editor.Adjust(-1);
    assert(editor.offset_seconds() == 50400);
    editor.Begin({0, 0, 0, 0, 26, 0, 0}, 100000);
    assert(editor.value().year == 2000 && editor.value().hour == 0 && editor.offset_seconds() == 0);
    editor.Adjust(-1);
    assert(editor.value().year == 2099);
    editor.Adjust(1);
    assert(editor.value().year == 2000);
}

int main() {
    TestClockDraft();
    using namespace zectrix::app;
    using Action = zectrix::sdk::InputAction;
    using zectrix::sdk::Button;
    using zectrix::sdk::InputEvent;

    for (const std::size_t count : {0u, 1u, 8u, 11u}) {
        LauncherController launcher(count);
        for (std::size_t selected = 0; selected < count; ++selected) {
            const auto opened = launcher.Handle({Button::Ok, Action::Click});
            assert(opened.decision == LauncherDecision::OpenSelected && opened.selected == selected);
            LauncherController restored(count, selected);
            assert(restored.selected() == selected);
            const auto moved = launcher.Handle({Button::Down, Action::Click});
            assert(moved.decision == LauncherDecision::RenderFast && moved.selected == (selected + 1) % count);
        }
        if (count) assert(launcher.Handle({Button::Up, Action::Click}).selected == count - 1);
        else assert(launcher.Handle({Button::Ok, Action::Click}).decision == LauncherDecision::None);
        assert(LauncherController(count, 100).selected() == 0);
        assert(launcher.Handle({Button::Down, Action::LongPress}).decision == LauncherDecision::Shutdown);
        assert(launcher.Handle({Button::Ok, Action::LongPress}).decision == LauncherDecision::None);
    }
    assert(kAutoShowcaseDefault == 0);
    assert(HandleClockInput({Button::Ok, Action::LongPress}) ==
           ClockDecision::Home);
    assert(HandleClockInput({Button::Down, Action::LongPress}) ==
           ClockDecision::Shutdown);
    assert(HandleClockInput({Button::Ok, Action::Click}) ==
           ClockDecision::None);
    assert(HandleClockInput({Button::Up, Action::LongPress}) ==
           ClockDecision::None);
    assert(HandleConnectivityInput({Button::Ok, Action::Click}) ==
           ConnectivityDecision::StartPairing);
    assert(HandleConnectivityInput({Button::Up, Action::LongPress}) ==
           ConnectivityDecision::ClearBonds);
    assert(HandleConnectivityInput({Button::Ok, Action::LongPress}) ==
           ConnectivityDecision::Home);
    assert(HandleConnectivityInput({Button::Down, Action::LongPress}) ==
           ConnectivityDecision::Shutdown);
    assert(HandleConnectivityInput({Button::Down, Action::Click}) ==
           ConnectivityDecision::None);
    assert(HandleConnectivityInput({Button::Up, Action::Click}) ==
           ConnectivityDecision::FetchResource);
    assert(!ClockDisplayChanged({2026, 8, 12, 10, 30},
                                {2026, 8, 12, 10, 30}));
    assert(ClockDisplayChanged({2026, 8, 12, 10, 30},
                               {2026, 8, 12, 10, 31}));
    assert(ClockDisplayChanged({2026, 8, 12, 23, 59},
                               {2026, 8, 13, 0, 0}));

    bool normalized = false;
    assert(NormalizeAutoShowcaseSetting(0, &normalized));
    assert(!normalized);
    assert(NormalizeAutoShowcaseSetting(1, &normalized));
    assert(normalized);
    assert(!NormalizeAutoShowcaseSetting(2, &normalized));
    assert(!NormalizeAutoShowcaseSetting(0, nullptr));

    SettingsController settings(true);
    SettingsResult setting = settings.Handle({Button::Down, Action::Click});
    assert(setting.decision == SettingsDecision::RenderFast);
    assert(!setting.auto_showcase);
    setting = settings.Handle({Button::Ok, Action::Click});
    assert(setting.decision == SettingsDecision::Save);
    assert(!setting.auto_showcase);
    assert(settings.Handle({Button::Ok, Action::LongPress}).decision ==
           SettingsDecision::Home);
    assert(settings.Handle({Button::Down, Action::LongPress}).decision ==
           SettingsDecision::Shutdown);

    DiagnosticsController diagnostics;
    DiagnosticsResult diagnostic =
        diagnostics.Handle({Button::Down, Action::Click});
    assert(diagnostic.decision == DiagnosticsDecision::RenderFast);
    assert(diagnostic.selected == 1);
    diagnostic = diagnostics.Handle({Button::Ok, Action::Click});
    assert(diagnostic.page == DiagnosticsPage::Individual);
    diagnostic = diagnostics.Handle({Button::Up, Action::Click});
    assert(diagnostic.selected == DiagnosticsController::kTestCount - 1);
    diagnostic = diagnostics.Handle({Button::Ok, Action::Click});
    assert(diagnostic.decision == DiagnosticsDecision::RunSelected);
    diagnostic = diagnostics.Handle({Button::Ok, Action::LongPress});
    assert(diagnostic.decision == DiagnosticsDecision::RenderFast);
    assert(diagnostic.page == DiagnosticsPage::Mode);
    diagnostic = diagnostics.Handle({Button::Ok, Action::Click});
    assert(diagnostic.decision == DiagnosticsDecision::RunAll);
    diagnostics.ShowSummary();
    diagnostic = diagnostics.Handle({Button::Up, Action::Click});
    assert(diagnostic.decision == DiagnosticsDecision::Home);
    assert(DiagnosticsController().Handle(
               {Button::Down, Action::LongPress}).decision ==
           DiagnosticsDecision::Shutdown);
}
