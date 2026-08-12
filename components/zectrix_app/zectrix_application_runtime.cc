#include "zectrix_application_runtime.h"

#include <cstring>

namespace zectrix::app {

esp_err_t ApplicationRegistry::Validate(
    const ApplicationId& launcher_id) const {
    if (descriptors_ == nullptr || descriptor_count_ == 0 ||
        launcher_id.empty()) {
        return ESP_ERR_INVALID_ARG;
    }
    for (std::size_t index = 0; index < descriptor_count_; ++index) {
        const auto& descriptor = descriptors_[index];
        ApplicationId id;
        if (!ApplicationId::Copy(descriptor.id, &id) ||
            descriptor.display_name == nullptr ||
            descriptor.display_name[0] == '\0' || descriptor.factory == nullptr) {
            return ESP_ERR_INVALID_ARG;
        }
        for (std::size_t other = 0; other < index; ++other) {
            ApplicationId other_id;
            if (ApplicationId::Copy(descriptors_[other].id, &other_id) &&
                id == other_id) {
                return ESP_ERR_INVALID_STATE;
            }
        }
    }
    return Find(launcher_id) == nullptr ? ESP_ERR_NOT_FOUND : ESP_OK;
}

const ApplicationDescriptor* ApplicationRegistry::Find(
    const ApplicationId& id) const {
    if (id.empty()) return nullptr;
    for (std::size_t index = 0; index < descriptor_count_; ++index) {
        ApplicationId descriptor_id;
        if (ApplicationId::Copy(descriptors_[index].id, &descriptor_id) &&
            descriptor_id == id) {
            return &descriptors_[index];
        }
    }
    return nullptr;
}

const ApplicationDescriptor* ApplicationRegistry::At(std::size_t index) const {
    return descriptors_ != nullptr && index < descriptor_count_
               ? &descriptors_[index]
               : nullptr;
}

ApplicationRuntime::ApplicationRuntime(
    const ApplicationDescriptor* descriptors, std::size_t descriptor_count,
    const char* launcher_id, Platform& platform, RuntimeDelegate& delegate)
    : registry_(descriptors, descriptor_count),
      platform_(&platform),
      delegate_(&delegate),
      context_(*this) {
    ApplicationId::Copy(launcher_id, &launcher_id_);
}

ApplicationRuntime::~ApplicationRuntime() { ExitAndDestroyForeground(); }

esp_err_t ApplicationRuntime::Start() {
    if (state_ == LifecycleState::Active) return ESP_OK;
    if (state_ != LifecycleState::Absent) return ESP_ERR_INVALID_STATE;
    const esp_err_t validation = registry_.Validate(launcher_id_);
    if (validation != ESP_OK) {
        last_error_ = validation;
        return validation;
    }
    return SwitchTo(launcher_id_, false);
}

esp_err_t ApplicationRuntime::SwitchTo(const ApplicationId& id,
                                       bool allow_fallback) {
    const ApplicationDescriptor* descriptor = registry_.Find(id);
    if (descriptor == nullptr) return ESP_ERR_NOT_FOUND;

    state_ = LifecycleState::Creating;
    Application* raw_candidate = nullptr;
    const esp_err_t create_result =
        descriptor->factory(*platform_, registry_, &raw_candidate);
    std::unique_ptr<Application> candidate(raw_candidate);
    if (create_result != ESP_OK || candidate == nullptr) {
        const esp_err_t failure =
            create_result == ESP_OK ? ESP_ERR_NO_MEM : create_result;
        last_error_ = failure;
        state_ = foreground_ ? LifecycleState::Active : LifecycleState::Absent;
        if (!foreground_ && allow_fallback) return EnterLauncherFallback(failure);
        if (!foreground_ && id == launcher_id_) EnterFailsafe(failure);
        return failure;
    }

    ExitAndDestroyForeground();
    ++generation_;
    renders_.Discard();
    commands_ = {};
    foreground_id_ = id;
    foreground_ = std::move(candidate);
    state_ = LifecycleState::Entering;
    const esp_err_t enter_result = InvokeCallback(
        [this] { return foreground_->Enter(context_); });
    if (enter_result == ESP_OK) {
        state_ = LifecycleState::Active;
        return ESP_OK;
    }

    last_error_ = enter_result;
    ExitAndDestroyForeground();
    renders_.Discard();
    commands_ = {};
    if (allow_fallback && id != launcher_id_) {
        return EnterLauncherFallback(enter_result);
    }
    EnterFailsafe(enter_result);
    return enter_result;
}

esp_err_t ApplicationRuntime::EnterLauncherFallback(esp_err_t reason) {
    const esp_err_t result = SwitchTo(launcher_id_, false);
    if (result != ESP_OK && state_ != LifecycleState::Failsafe) {
        EnterFailsafe(reason);
    }
    return result;
}

void ApplicationRuntime::EnterFailsafe(esp_err_t reason) {
    ExitAndDestroyForeground();
    renders_.Discard();
    commands_ = {};
    state_ = LifecycleState::Failsafe;
    last_error_ = reason;
    delegate_->EnterFailsafe(reason);
}

void ApplicationRuntime::ExitAndDestroyForeground() {
    if (!foreground_) {
        foreground_id_ = {};
        return;
    }
    state_ = LifecycleState::Exiting;
    const esp_err_t exit_result = InvokeCallback(
        [this] { return foreground_->Exit(); });
    if (exit_result != ESP_OK) last_error_ = exit_result;
    foreground_.reset();
    foreground_id_ = {};
    state_ = LifecycleState::Absent;
}

esp_err_t ApplicationRuntime::Step(const input::InputEvent* event) {
    if (state_ != LifecycleState::Active || !foreground_) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t result = ESP_OK;
    if (event != nullptr) {
        result = InvokeCallback(
            [this, event] { return foreground_->HandleEvent(*event, context_); });
        if (result != ESP_OK) last_error_ = result;
    }
    const esp_err_t command_result = ProcessCommand();
    if (command_result != ESP_OK) result = command_result;
    if (state_ == LifecycleState::Active) {
        const esp_err_t render_result = ProcessRender();
        if (render_result != ESP_OK) result = render_result;
    }
    return result;
}

esp_err_t ApplicationRuntime::ProcessCommand() {
    AppCommand command;
    if (!commands_.Take(&command)) return ESP_OK;
    switch (command.kind) {
        case CommandKind::Open:
            return SwitchTo(command.target, true);
        case CommandKind::Back:
        case CommandKind::Home:
            return SwitchTo(launcher_id_, false);
        case CommandKind::Shutdown:
            return Stop();
        case CommandKind::None:
            return ESP_OK;
    }
    return ESP_ERR_INVALID_ARG;
}

esp_err_t ApplicationRuntime::ProcessRender() {
    RenderRequest request;
    if (!renders_.TakeFor(generation_, &request)) return ESP_OK;
    const esp_err_t result = InvokeCallback(
        [this, &request] { return foreground_->Render(request); });
    if (result != ESP_OK) last_error_ = result;
    return result;
}

esp_err_t ApplicationRuntime::Stop() {
    if (state_ == LifecycleState::Stopped) return ESP_OK;
    ExitAndDestroyForeground();
    renders_.Discard();
    commands_ = {};
    const esp_err_t result = delegate_->Shutdown();
    if (result != ESP_OK) last_error_ = result;
    state_ = LifecycleState::Stopped;
    return result;
}

SubmitResult ApplicationRuntime::SubmitCommand(const AppCommand& command) {
    if (!callback_active_ || state_ == LifecycleState::Exiting) {
        return SubmitResult::Rejected;
    }
    return commands_.Submit(command);
}

bool ApplicationRuntime::SubmitRender(const DirtyRegion& dirty,
                                      RenderIntent intent) {
    if (!callback_active_ || dirty.IsEmpty() ||
        state_ == LifecycleState::Exiting) {
        return false;
    }
    renders_.Submit({generation_, dirty, intent});
    return true;
}

SubmitResult ApplicationContext::RequestCommand(const AppCommand& command) {
    return runtime_->SubmitCommand(command);
}

bool ApplicationContext::RequestRender(const DirtyRegion& dirty,
                                       RenderIntent intent) {
    return runtime_->SubmitRender(dirty, intent);
}

uint32_t ApplicationContext::foreground_generation() const {
    return runtime_->foreground_generation();
}

}  // namespace zectrix::app
