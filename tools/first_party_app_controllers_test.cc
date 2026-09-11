#include "zectrix_first_party_app_controllers.h"
#include "zectrix_clock_editor.h"
#include "zectrix_launcher_controller.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

void TestLauncher() {
    using namespace zectrix::app;
    using namespace zectrix::sdk;
    class Factory final : public ApplicationFactory {
        Status Create(const ApplicationRegistry&, Application**) override {
            assert(false);
            return Status::Unsupported;
        }
    } factory;
    using Icon = ApplicationIcon;
    const InputEvent up{Button::Up, InputAction::Click};
    const InputEvent down{Button::Down, InputAction::Click};
    const InputEvent ok{Button::Ok, InputAction::Click};
    const InputEvent back{Button::Ok, InputAction::LongPress};
    const InputEvent off{Button::Down, InputAction::LongPress};

    for (bool reader : {false, true}) for (bool transfer : {false, true})
    for (bool connection : {false, true}) for (bool utilities : {false, true}) {
        ApplicationCatalog catalog;
        assert(catalog.Add("launcher", "Launcher", factory));
        std::vector<const char*> home, tools;
        const auto add = [&](const char* id, bool on_home, Icon icon = Icon::App) {
            assert(catalog.Add(id, id, factory, {icon, on_home}));
            (on_home ? home : tools).push_back(id);
        };
        if (reader) add("reader", true, Icon::Book);
        if (transfer) add("book-transfer", true, Icon::Transfer);
        if (utilities) add("utilities", true);
        add("clock", true, Icon::Clock);
        // Interleave groups so grouping cannot rely on contiguous indices.
        if (connection) add("connectivity", false);
        add("sleep-cover", true, Icon::Sleep);
        add("gallery", false);
        add("settings", true, Icon::Settings);
        add("diagnostics", false);
        add("about", false);
        LauncherController launcher(catalog);
        assert(launcher.Start() == Status::Ok);
        assert(launcher.Start() == Status::InvalidState);
        assert(launcher.Tick().decision == LauncherDecision::RenderQuality);
        assert(launcher.Tick().decision == LauncherDecision::None);
        const auto tools_index = home.size() + (reader ? 1 : 0);
        assert(launcher.count() == tools_index + 1);
        assert(launcher.has_reading_overview() == reader);
        if (reader) {
            assert(launcher.overview_selected() && launcher.tile_page() == 0);
            const auto result = launcher.Handle(ok);
            assert(result.decision == LauncherDecision::ContinueReading && std::strcmp(result.target, "reader") == 0);
            assert(launcher.Handle(down).decision == LauncherDecision::RenderFast);
        }
        for (const auto* id : home) {
            assert(!launcher.overview_selected() && launcher.tile_page() == 0);
            const auto result = launcher.Handle(ok);
            assert(result.decision == LauncherDecision::OpenSelected && std::strcmp(result.target, id) == 0);
            const auto previous_page = launcher.tile_page();
            const auto moved = launcher.Handle(down).decision;
            assert(moved == (previous_page == launcher.tile_page() ? LauncherDecision::RenderFast : LauncherDecision::RenderQuality));
        }
        assert(!launcher.EntryAt(launcher.selected()).id);
        assert(launcher.EntryAt(launcher.selected()).icon == Icon::Tools);
        assert(!launcher.EntryAt(launcher.count()).label);
        assert(launcher.Handle(ok).decision == LauncherDecision::RenderQuality);
        assert(launcher.scene() == LauncherScene::Tools && launcher.count() == tools.size());
        for (const auto* id : tools) {
            const auto result = launcher.Handle(ok);
            assert(result.decision == LauncherDecision::OpenSelected && std::strcmp(result.target, id) == 0);
            assert(launcher.Handle(down).decision == LauncherDecision::RenderFast);
        }
        launcher.Handle(up);
        const auto selected_tool = launcher.selected();
        assert(launcher.Handle(back).decision == LauncherDecision::RenderQuality);
        assert(launcher.scene() == LauncherScene::Home && launcher.selected() == tools_index);
        assert(launcher.Handle(back).decision == LauncherDecision::None);
        const auto previous_page = launcher.tile_page();
        const auto wrapped = launcher.Handle(down).decision;
        assert(wrapped == (previous_page == launcher.tile_page() ? LauncherDecision::RenderFast : LauncherDecision::RenderQuality));
        assert(launcher.selected() == 0);
        launcher.Handle(up);
        launcher.Handle(ok);
        assert(launcher.selected() == selected_tool);
        const auto saved = launcher.selection();
        launcher.Stop();
        assert(launcher.count() == 0 && launcher.Handle(ok).decision == LauncherDecision::None);
        LauncherController restored(catalog);
        assert(restored.Start(saved) == Status::Ok);
        restored.Tick();
        assert(restored.scene() == LauncherScene::Tools && restored.selected() == selected_tool);
        assert(restored.Handle(back).decision == LauncherDecision::RenderQuality);
        assert(restored.scene() == LauncherScene::Home && restored.selected() == tools_index);
        restored.Handle(ok);
        assert(restored.selected() == selected_tool);
        assert(restored.Handle(off).decision == LauncherDecision::Shutdown);
        restored.Presented(false);
        assert(restored.Tick().decision == LauncherDecision::RenderQuality);
        restored.Presented(true);
        assert(restored.Tick().decision == LauncherDecision::None);

        auto home_selection = saved;
        home_selection.scene = LauncherScene::Home;
        LauncherController home_return(catalog);
        assert(home_return.Start(home_selection) == Status::Ok);
        assert(home_return.scene() == LauncherScene::Home && home_return.selected() == tools_index);
        home_return.Handle(ok);
        assert(home_return.scene() == LauncherScene::Tools && home_return.selected() == selected_tool);
    }

    ApplicationCatalog empty;
    assert(empty.Add("launcher", "Launcher", factory));
    LauncherController no_apps(empty);
    assert(no_apps.Start({100, 100, LauncherScene::Tools}) == Status::Ok);
    no_apps.Tick();
    assert(no_apps.count() == 0 && no_apps.selected() == 0 && !no_apps.EntryAt(0).label);
    assert(no_apps.Handle(ok).decision == LauncherDecision::None);
    assert(no_apps.Handle(down).decision == LauncherDecision::None);
    assert(no_apps.Handle(off).decision == LauncherDecision::Shutdown);

    ApplicationCatalog single;
    assert(single.Add("launcher", "Launcher", factory));
    assert(single.Add("clock", "CLOCK", factory, {Icon::Clock, true}));
    LauncherController one_app(single);
    assert(one_app.Start({100, 100, LauncherScene::Tools}) == Status::Ok);
    one_app.Tick();
    assert(one_app.count() == 1 && one_app.selected() == 0);
    assert(one_app.Handle(down).decision == LauncherDecision::None);
    assert(one_app.Handle(up).decision == LauncherDecision::None);
    assert(one_app.Handle(back).decision == LauncherDecision::None);
    assert(one_app.Handle(off).decision == LauncherDecision::Shutdown);

    ApplicationCatalog many;
    std::array<std::array<char, 16>, ApplicationCatalog::kCapacity - 1> names{};
    assert(many.Add("launcher", "Launcher", factory));
    for (std::size_t i = 0; i < names.size(); ++i) {
        std::snprintf(names[i].data(), names[i].size(), "app%u", static_cast<unsigned>(i));
        assert(many.Add(names[i].data(), names[i].data(), factory, {Icon::App, true}));
    }
    assert(!many.Add("overflow", "overflow", factory));
    LauncherController pages(many);
    assert(pages.Start({5, 0}) == Status::Ok);
    pages.Tick();
    assert(pages.Handle(down).decision == LauncherDecision::RenderQuality && pages.selected() == 6);
    assert(pages.Handle(up).decision == LauncherDecision::RenderQuality && pages.selected() == 5);
    for (std::size_t i = 0; i < names.size(); ++i) {
        const auto result = pages.Handle(ok);
        assert(std::strcmp(result.target, names[pages.selected()].data()) == 0);
        pages.Handle(down);
    }
    assert(pages.selected() == 5);
    pages.Invalidate();
    assert(pages.Tick().decision == LauncherDecision::RenderFast);
    assert(pages.Tick().decision == LauncherDecision::None);

    ApplicationCatalog reader_pages;
    assert(reader_pages.Add("launcher", "Launcher", factory));
    assert(reader_pages.Add("reader", "Library", factory, {Icon::Book, true}));
    for (std::size_t i = 0; i < names.size() - 1; ++i)
        assert(reader_pages.Add(names[i].data(), names[i].data(), factory, {Icon::App, true}));
    LauncherController with_overview(reader_pages);
    assert(with_overview.Start() == Status::Ok && with_overview.count() == 16);
    with_overview.Tick();
    assert(with_overview.Handle(up).decision == LauncherDecision::RenderQuality);
    assert(with_overview.selected() == 15 && with_overview.tile_page() == 2);
    assert(with_overview.Handle(down).decision == LauncherDecision::RenderQuality);
    assert(with_overview.overview_selected() && with_overview.tile_page() == 0);
    for (unsigned i = 0; i < 6; ++i) with_overview.Handle(down);
    assert(with_overview.selected() == 6 && with_overview.tile_page() == 0);
    assert(with_overview.Handle(down).decision == LauncherDecision::RenderQuality);
    assert(with_overview.tile_page() == 1);

    ApplicationCatalog only_tools;
    assert(only_tools.Add("launcher", "Launcher", factory));
    assert(only_tools.Add("settings", "SETTINGS", factory));
    LauncherController tools_only(only_tools);
    assert(tools_only.Start() == Status::Ok);
    tools_only.Tick();
    assert(tools_only.count() == 1 && !tools_only.EntryAt(0).id);
    assert(tools_only.Handle(ok).decision == LauncherDecision::RenderQuality);
    assert(tools_only.scene() == LauncherScene::Tools && tools_only.count() == 1);
    assert(tools_only.Handle(down).decision == LauncherDecision::None);
    assert(tools_only.Handle(back).decision == LauncherDecision::RenderQuality);
}

void TestLauncherDate() {
    using namespace zectrix::time;
    using zectrix::app::LauncherDateChanged;
    ClockSnapshot unset;
    auto tick = unset;
    tick.value.hour = 25;
    assert(!LauncherDateChanged(unset, tick));
    ClockSnapshot clock{{2026, 9, 10, 4, 23, 59, 0}, ClockSource::Rtc};
    assert(LauncherDateChanged(unset, clock));
    tick = clock;
    tick.value.second = 59;
    tick.value.minute = 58;
    tick.source = ClockSource::System;
    assert(!LauncherDateChanged(clock, tick));
    tick.value = {2026, 9, 11, 5, 0, 0, 0};
    assert(LauncherDateChanged(clock, tick));
    tick.value.month = 13;
    assert(LauncherDateChanged(clock, tick));
    assert(LauncherDateChanged(clock, unset));
}

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

void TestConnectivityNavigation() {
    using namespace zectrix::app;
    using namespace zectrix::sdk;
    const InputEvent up{Button::Up, InputAction::Click};
    const InputEvent down{Button::Down, InputAction::Click};
    const InputEvent ok{Button::Ok, InputAction::Click};
    const InputEvent back{Button::Ok, InputAction::LongPress};
    const InputEvent off{Button::Down, InputAction::LongPress};
    ConnectivityController connection;
    assert(connection.Handle(ok) == ConnectivityDecision::None);
    assert(connection.Start() == Status::Ok);
    assert(connection.Start() == Status::InvalidState);
    assert(connection.Tick() == ConnectivityDecision::RenderQuality);
    assert(connection.Tick() == ConnectivityDecision::None);
    assert(connection.Handle(ok) == ConnectivityDecision::StartPairing);
    assert(connection.Handle(down) == ConnectivityDecision::RenderFast && connection.selected() == 1);
    assert(connection.Handle(ok) == ConnectivityDecision::FetchResource);
    assert(connection.Handle(down) == ConnectivityDecision::RenderFast && connection.selected() == 2);
    assert(connection.Handle({Button::Up, InputAction::LongPress}) == ConnectivityDecision::None);
    assert(connection.Handle(ok) == ConnectivityDecision::RenderQuality);
    assert(connection.page() == ConnectivityPage::Forget && connection.selected() == 0);
    assert(connection.Handle(ok) == ConnectivityDecision::RenderQuality);
    assert(connection.page() == ConnectivityPage::Actions && connection.selected() == 2);
    connection.Handle(ok);
    connection.Handle(down);
    assert(connection.selected() == 1);
    assert(connection.Handle(back) == ConnectivityDecision::RenderQuality);
    assert(connection.page() == ConnectivityPage::Actions && connection.selected() == 2);
    connection.Handle(ok);
    assert(connection.selected() == 0);
    connection.Handle(up);
    assert(connection.selected() == 1);
    assert(connection.Handle(off) == ConnectivityDecision::Shutdown);
    assert(connection.page() == ConnectivityPage::Forget);
    assert(connection.Handle(ok) == ConnectivityDecision::ClearBonds);
    assert(connection.page() == ConnectivityPage::Actions && connection.selected() == 2);
    assert(connection.Handle(down) == ConnectivityDecision::RenderFast && connection.selected() == 0);
    connection.Presented(false);
    assert(connection.Tick() == ConnectivityDecision::RenderQuality);
    connection.Presented(true);
    assert(connection.Tick() == ConnectivityDecision::None);
    assert(connection.Handle(back) == ConnectivityDecision::Back);
    assert(connection.Handle(off) == ConnectivityDecision::Shutdown);
    connection.Stop();
    connection.Stop();
    connection.Presented(false);
    assert(connection.Handle(ok) == ConnectivityDecision::None);
    assert(connection.Tick() == ConnectivityDecision::None);
    assert(connection.Start() == Status::Ok && connection.page() == ConnectivityPage::Actions);
    assert(connection.Tick() == ConnectivityDecision::RenderQuality);
}

void TestDiagnosticsNavigation() {
    using namespace zectrix::app;
    using namespace zectrix::sdk;
    const InputEvent up{Button::Up, InputAction::Click};
    const InputEvent down{Button::Down, InputAction::Click};
    const InputEvent ok{Button::Ok, InputAction::Click};
    const InputEvent back{Button::Ok, InputAction::LongPress};
    const InputEvent off{Button::Down, InputAction::LongPress};
    DiagnosticsController diagnostics;
    assert(diagnostics.Handle(ok).decision == DiagnosticsDecision::None);
    assert(diagnostics.Start() == Status::Ok);
    assert(diagnostics.Start() == Status::InvalidState);
    assert(diagnostics.Tick().decision == DiagnosticsDecision::RenderQuality);
    assert(diagnostics.Tick().decision == DiagnosticsDecision::None);
    assert(diagnostics.Handle(down).selected == 1);
    assert(diagnostics.Handle(ok).page == DiagnosticsPage::Individual);
    assert(diagnostics.Handle(up).selected == DiagnosticsController::kTestCount - 1);
    for (bool cancelled : {false, true}) {
        const auto run = diagnostics.Handle(ok);
        assert(run.decision == DiagnosticsDecision::RunSelected && run.page == DiagnosticsPage::Running);
        assert(run.selected == DiagnosticsController::kTestCount - 1);
        assert(diagnostics.Handle(down).decision == DiagnosticsDecision::None);
        assert(diagnostics.Handle(off).decision == DiagnosticsDecision::Shutdown);
        const auto done = diagnostics.FinishRun(cancelled);
        assert(done.decision == DiagnosticsDecision::RenderQuality && done.page == DiagnosticsPage::Individual);
        assert(done.selected == run.selected);
        assert(diagnostics.FinishRun(cancelled).decision == DiagnosticsDecision::None);
    }
    assert(diagnostics.Handle(back).page == DiagnosticsPage::Mode && diagnostics.selected() == 1);
    diagnostics.Handle(ok);
    assert(diagnostics.page() == DiagnosticsPage::Individual);
    assert(diagnostics.selected() == DiagnosticsController::kTestCount - 1);
    diagnostics.Handle(back);
    diagnostics.Handle(up);
    assert(diagnostics.Handle(ok).decision == DiagnosticsDecision::RunAll);
    assert(diagnostics.FinishRun(true).page == DiagnosticsPage::Mode && diagnostics.selected() == 0);
    for (const auto exit : {ok, back}) {
        assert(diagnostics.Handle(ok).decision == DiagnosticsDecision::RunAll);
        const auto done = diagnostics.FinishRun(false);
        assert(done.page == DiagnosticsPage::Summary && done.decision == DiagnosticsDecision::RenderQuality);
        assert(diagnostics.Handle(up).decision == DiagnosticsDecision::None);
        assert(diagnostics.Handle(down).decision == DiagnosticsDecision::None);
        assert(diagnostics.Handle(off).decision == DiagnosticsDecision::Shutdown);
        diagnostics.Presented(false);
        assert(diagnostics.Tick().decision == DiagnosticsDecision::RenderQuality);
        const auto returned = diagnostics.Handle(exit);
        assert(returned.page == DiagnosticsPage::Mode && returned.decision == DiagnosticsDecision::RenderQuality);
    }
    assert(diagnostics.Handle(back).decision == DiagnosticsDecision::Back);
    diagnostics.Handle(ok);
    diagnostics.Stop();
    diagnostics.Stop();
    diagnostics.Presented(false);
    assert(diagnostics.Handle(ok).decision == DiagnosticsDecision::None);
    assert(diagnostics.Tick().decision == DiagnosticsDecision::None);
    assert(diagnostics.FinishRun(false).decision == DiagnosticsDecision::None);
    assert(diagnostics.Start() == Status::Ok && diagnostics.page() == DiagnosticsPage::Mode);
    assert(diagnostics.Tick().decision == DiagnosticsDecision::RenderQuality);
}

int main() {
    TestClockDraft();
    TestLauncher();
    TestLauncherDate();
    TestConnectivityNavigation();
    TestDiagnosticsNavigation();
    using namespace zectrix::app;
    using Action = zectrix::sdk::InputAction;
    using zectrix::sdk::Button;
    using zectrix::sdk::InputEvent;

    assert(kAutoShowcaseDefault == 0);
    assert(MapNavigation({Button::Ok, Action::LongPress}) == Navigation::Back);
    assert(MapNavigation({Button::Down, Action::LongPress}) == Navigation::Shutdown);
    assert(MapNavigation({Button::Ok, Action::Click}) == Navigation::Confirm);
    assert(MapNavigation({Button::Up, Action::Click}) == Navigation::Previous);
    assert(MapNavigation({Button::Down, Action::Click}) == Navigation::Next);
    assert(MapNavigation({Button::Up, Action::LongPress}) == Navigation::None);
    assert(MapNavigation({static_cast<Button>(99), Action::Click}) == Navigation::None);
    assert(MapNavigation({Button::Down, static_cast<Action>(99)}) == Navigation::None);
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
    assert(settings.Handle({Button::Ok, Action::Click}).decision == SettingsDecision::None);
    assert(settings.Start() == zectrix::sdk::Status::Ok);
    assert(settings.Start() == zectrix::sdk::Status::InvalidState);
    assert(settings.Tick().decision == SettingsDecision::RenderQuality);
    SettingsResult setting = settings.Handle({Button::Down, Action::Click});
    assert(setting.decision == SettingsDecision::RenderFast);
    assert(setting.auto_showcase);
    setting = settings.Handle({Button::Ok, Action::Click});
    assert(setting.decision == SettingsDecision::Save);
    assert(!setting.auto_showcase);
    settings.SaveCompleted(false);
    setting = settings.Handle({Button::Ok, Action::Click});
    assert(setting.decision == SettingsDecision::Save && !setting.auto_showcase);
    settings.SaveCompleted(true);
    setting = settings.Handle({Button::Ok, Action::Click});
    assert(setting.decision == SettingsDecision::Save && setting.auto_showcase);
    assert(settings.Handle({Button::Ok, Action::LongPress}).decision ==
           SettingsDecision::Back);
    assert(settings.Handle({Button::Down, Action::LongPress}).decision ==
           SettingsDecision::Shutdown);
    settings.Presented(false);
    assert(settings.Tick().decision == SettingsDecision::RenderQuality);
    settings.Presented(true);
    assert(settings.Tick().decision == SettingsDecision::None);
    settings.Stop();
    assert(settings.Handle({Button::Ok, Action::Click}).decision == SettingsDecision::None);

}
