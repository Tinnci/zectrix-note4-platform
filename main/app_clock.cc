#include "terminal_internal.h"

#include <cstdio>

#include "esp_log.h"
#include "zectrix_clock_editor.h"
#include "zectrix_first_party_app_controllers.h"
#include "zectrix_scene_manager.h"

namespace zectrix::terminal {

class TerminalApp::ClockApplication final : public sdk::Application {
public:
    explicit ClockApplication(TerminalApp& owner)
        : owner_(&owner), scenes_(handlers_, std::size(handlers_), this) {}

    sdk::Status Enter(sdk::ApplicationContext& context) override {
        ReadTime();
        const auto started = scenes_.Start(kClock);
        if (!sdk::IsOk(started)) return started;
        dirty_ = false;
        return context.RequestRender({0, 0, 400, 300}, sdk::RenderIntent::Quality)
                   ? sdk::Status::Ok : sdk::Status::InternalError;
    }
    sdk::Status HandleEvent(const sdk::InputEvent& event,
                            sdk::ApplicationContext& context) override {
        const auto key = app::MapNavigation(event);
        if (key == app::Navigation::Shutdown) {
            context.RequestCommand(sdk::AppCommand::Shutdown());
            return sdk::Status::Ok;
        }
        const auto previous_scene = scenes_.current();
        const bool retry = dirty_;
        const auto type = key == app::Navigation::Back ? app::SceneEvent::Type::Back
                                                     : app::SceneEvent::Type::Input;
        if (!scenes_.Dispatch({type, event}) && type == app::SceneEvent::Type::Back)
            return owner_->RequestBack(context);
        if (dirty_) {
            context.RequestRender({0, 24, 400, 276}, retry || scenes_.current() != previous_scene
                ? sdk::RenderIntent::Quality : sdk::RenderIntent::Fast);
            dirty_ = false;
        }
        return sdk::Status::Ok;
    }
    sdk::Status HandleIdle(sdk::ApplicationContext& context) override {
        if (dirty_) {
            dirty_ = false;
            return context.RequestRender({0, 24, 400, 276}, sdk::RenderIntent::Quality)
                ? sdk::Status::Ok : sdk::Status::InternalError;
        }
        if (scenes_.current() != kClock || owner_->time_->MonotonicMicroseconds() < next_read_us_)
            return sdk::Status::Ok;
        const app::ClockMinute previous{clock_.value.year, clock_.value.month, clock_.value.day,
                                        clock_.value.hour, clock_.value.minute};
        const auto previous_source = clock_.source;
        const auto previous_status = status_;
        ReadTime();
        const app::ClockMinute next{clock_.value.year, clock_.value.month, clock_.value.day,
                                    clock_.value.hour, clock_.value.minute};
        if (app::ClockDisplayChanged(previous, next) || previous_source != clock_.source ||
            previous_status.rtc_persisted != status_.rtc_persisted ||
            previous_status.persistence_pending != status_.persistence_pending ||
            previous_status.utc_offset_known != status_.utc_offset_known) {
            context.RequestRender({0, 24, 400, 276}, sdk::RenderIntent::Fast);
        }
        return sdk::Status::Ok;
    }
    sdk::Status Render(const sdk::RenderRequest& request) override {
        const bool quality = request.intent == sdk::RenderIntent::Quality;
        sdk::Status result;
        if (scenes_.current() == kEdit) {
            result = RenderEditor(quality);
        } else {
            const char* source = clock_.source == time::ClockSource::Uptime ? "UPTIME - TIME NOT SET" :
                status_.persistence_pending ? "SYSTEM TIME - SAVE PENDING" :
                !status_.utc_offset_known ? (clock_.source == time::ClockSource::Rtc ?
                    "LOCAL TIME - SET UTC OFFSET" : "SYSTEM UTC - SET CLOCK") :
                status_.rtc_persisted ? "RTC SAVED" : "SYSTEM TIME";
            result = ToSdkStatus(owner_->ui_.ShowClock(clock_.value, quality, source,
                clock_.source != time::ClockSource::Uptime));
        }
        dirty_ = !sdk::IsOk(result);
        return result;
    }
    sdk::Status Exit() override { scenes_.Stop(); return sdk::Status::Ok; }

private:
    static constexpr app::SceneId kClock = 0, kEdit = 1;
    static void EnterScene(void* context, app::SceneId scene) {
        auto& self = *static_cast<ClockApplication*>(context);
        if (scene == kClock) self.ReadTime();
        self.dirty_ = true;
    }
    static bool OnSceneEvent(void* context, const app::SceneEvent& event) {
        auto& self = *static_cast<ClockApplication*>(context);
        if (event.type != app::SceneEvent::Type::Input) return false;
        const auto key = app::MapNavigation(event.input);
        if (self.scenes_.current() == kClock) {
            if (key != app::Navigation::Confirm) return false;
            self.editor_.Begin(self.owner_->time_->Now().value, self.owner_->time_->Status().utc_offset_seconds);
            self.save_failed_ = false;
            self.scenes_.Push(kEdit);
        } else if (key == app::Navigation::Confirm) {
            if (self.editor_.field() == app::ClockEditor::Save) {
                const auto saved = self.owner_->time_->SetLocalTime(self.editor_.value(), self.editor_.offset_seconds());
                if (saved == ESP_OK) {
                    self.scenes_.Pop();
                } else {
                    self.save_failed_ = true;
                }
                if (saved != ESP_OK) ESP_LOGW(kTag, "clock save incomplete: %s", esp_err_to_name(saved));
                else if (self.owner_->time_->Status().persistence_pending)
                    ESP_LOGW(kTag, "clock set; RTC persistence will retry");
            } else {
                self.editor_.Next();
            }
        } else if (key == app::Navigation::Previous || key == app::Navigation::Next) {
            self.editor_.Adjust(key == app::Navigation::Previous ? 1 : -1);
            self.save_failed_ = false;
        } else return false;
        self.dirty_ = true;
        return true;
    }
    static constexpr app::SceneHandler handlers_[] = {
        {EnterScene, OnSceneEvent, nullptr}, {EnterScene, OnSceneEvent, nullptr},
    };

    sdk::Status RenderEditor(bool quality) {
        const auto& value = editor_.value();
        std::array<std::array<char, 40>, app::ClockEditor::Count> rows{};
        std::snprintf(rows[0].data(), rows[0].size(), "YEAR         %04d", value.year);
        std::snprintf(rows[1].data(), rows[1].size(), "MONTH        %02d", value.month);
        std::snprintf(rows[2].data(), rows[2].size(), "DAY          %02d", value.day);
        std::snprintf(rows[3].data(), rows[3].size(), "HOUR         %02d", value.hour);
        std::snprintf(rows[4].data(), rows[4].size(), "MINUTE       %02d", value.minute);
        const int offset = editor_.offset_seconds();
        const int magnitude = offset < 0 ? -offset : offset;
        std::snprintf(rows[5].data(), rows[5].size(), "UTC OFFSET   %c%02d:%02d",
                      offset < 0 ? '-' : '+', magnitude / 3600, magnitude / 60 % 60);
        std::snprintf(rows[6].data(), rows[6].size(), "%s", save_failed_ ? "SAVE FAILED - OK RETRY" : "SAVE DATE AND TIME");
        std::array<const char*, app::ClockEditor::Count> labels{};
        for (std::size_t i = 0; i < labels.size(); ++i) labels[i] = rows[i].data();
        const char* footer = editor_.field() == app::ClockEditor::Save ?
            "UP Back  DOWN Start  OK Save  Hold OK Cancel" : "UP +  DOWN -  OK Next  Hold OK Cancel";
        return ToSdkStatus(owner_->ui_.ShowMenu("SET CLOCK", labels.data(), labels.size(), editor_.field(), footer, quality));
    }

    void ReadTime() {
        clock_ = owner_->time_->Now();
        status_ = owner_->time_->Status();
        next_read_us_ = owner_->time_->MonotonicMicroseconds() + 1000000;
    }

    TerminalApp* owner_;
    app::SceneManager scenes_;
    app::ClockEditor editor_;
    time::ClockSnapshot clock_{};
    time::ClockStatus status_{};
    bool dirty_ = false, save_failed_ = false;
    int64_t next_read_us_ = 0;
};

sdk::Status TerminalApp::CreateClock(TerminalApp& owner, sdk::Application** output) {
    return CreateApplication<ClockApplication>(owner, output);
}

}  // namespace zectrix::terminal
