#include "zectrix/sdk/application.h"

namespace zectrix::sdk {
inline namespace v1 {

Status ApplicationRegistry::Validate(const ApplicationId& launcher_id) const {
    if (descriptors_ == nullptr || descriptor_count_ == 0 ||
        launcher_id.empty()) {
        return Status::InvalidArgument;
    }
    for (std::size_t index = 0; index < descriptor_count_; ++index) {
        const auto& descriptor = descriptors_[index];
        ApplicationId id;
        if (!ApplicationId::Copy(descriptor.id, &id) ||
            descriptor.display_name == nullptr ||
            descriptor.display_name[0] == '\0' || descriptor.factory == nullptr) {
            return Status::InvalidArgument;
        }
        for (std::size_t other = 0; other < index; ++other) {
            ApplicationId other_id;
            if (ApplicationId::Copy(descriptors_[other].id, &other_id) &&
                id == other_id) {
                return Status::InvalidState;
            }
        }
    }
    return Find(launcher_id) == nullptr ? Status::NotFound : Status::Ok;
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
    const char* launcher_id, RuntimeDelegate& delegate)
    : registry_(descriptors, descriptor_count),
      delegate_(&delegate),
      context_(*this) {
    ApplicationId::Copy(launcher_id, &launcher_id_);
}

ApplicationRuntime::~ApplicationRuntime() { ExitAndDestroyForeground(); }

Status ApplicationRuntime::Start() {
    if (state_ == LifecycleState::Active) return Status::Ok;
    if (state_ != LifecycleState::Absent) return Status::InvalidState;
    const Status validation = registry_.Validate(launcher_id_);
    if (!IsOk(validation)) {
        last_error_ = validation;
        return validation;
    }
    return SwitchTo(launcher_id_, false);
}

Status ApplicationRuntime::SwitchTo(const ApplicationId& id,
                                    bool allow_fallback) {
    const ApplicationDescriptor* descriptor = registry_.Find(id);
    if (descriptor == nullptr) return Status::NotFound;

    state_ = LifecycleState::Creating;
    Application* raw_candidate = nullptr;
    const Status create_result =
        descriptor->factory->Create(registry_, &raw_candidate);
    std::unique_ptr<Application> candidate(raw_candidate);
    if (!IsOk(create_result) || candidate == nullptr) {
        const Status failure =
            IsOk(create_result) ? Status::NoMemory : create_result;
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
    const Status enter_result = InvokeCallback(
        [this] { return foreground_->Enter(context_); });
    if (IsOk(enter_result)) {
        state_ = LifecycleState::Active;
        return Status::Ok;
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

Status ApplicationRuntime::EnterLauncherFallback(Status reason) {
    const Status result = SwitchTo(launcher_id_, false);
    if (!IsOk(result) && state_ != LifecycleState::Failsafe) {
        EnterFailsafe(reason);
    }
    return result;
}

void ApplicationRuntime::EnterFailsafe(Status reason) {
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
    const Status exit_result = InvokeCallback(
        [this] { return foreground_->Exit(); });
    if (!IsOk(exit_result)) last_error_ = exit_result;
    foreground_.reset();
    foreground_id_ = {};
    state_ = LifecycleState::Absent;
}

Status ApplicationRuntime::Step(const InputEvent* event) {
    if (state_ != LifecycleState::Active || !foreground_) {
        return Status::InvalidState;
    }
    Status result = Status::Ok;
    if (event != nullptr) {
        result = InvokeCallback(
            [this, event] { return foreground_->HandleEvent(*event, context_); });
        if (!IsOk(result)) last_error_ = result;
    }
    const Status command_result = ProcessCommand();
    if (!IsOk(command_result)) result = command_result;
    if (state_ == LifecycleState::Active) {
        const Status render_result = ProcessRender();
        if (!IsOk(render_result)) result = render_result;
    }
    return result;
}

Status ApplicationRuntime::Idle() {
    if (state_ != LifecycleState::Active || !foreground_) {
        return Status::InvalidState;
    }
    Status result = InvokeCallback(
        [this] { return foreground_->HandleIdle(context_); });
    if (!IsOk(result)) last_error_ = result;
    const Status command_result = ProcessCommand();
    if (!IsOk(command_result)) result = command_result;
    if (state_ == LifecycleState::Active) {
        const Status render_result = ProcessRender();
        if (!IsOk(render_result)) result = render_result;
    }
    return result;
}

Status ApplicationRuntime::ProcessCommand() {
    AppCommand command;
    if (!commands_.Take(&command)) return Status::Ok;
    switch (command.kind) {
        case CommandKind::Open:
            return SwitchTo(command.target, true);
        case CommandKind::Back:
        case CommandKind::Home:
            return SwitchTo(launcher_id_, false);
        case CommandKind::Shutdown:
            return Stop();
        case CommandKind::None:
            return Status::Ok;
    }
    return Status::InvalidArgument;
}

Status ApplicationRuntime::ProcessRender() {
    RenderRequest request;
    if (!renders_.TakeFor(generation_, &request)) return Status::Ok;
    const Status result = InvokeCallback(
        [this, &request] { return foreground_->Render(request); });
    if (!IsOk(result)) last_error_ = result;
    return result;
}

Status ApplicationRuntime::Stop() {
    if (state_ == LifecycleState::Stopped) return Status::Ok;
    ExitAndDestroyForeground();
    renders_.Discard();
    commands_ = {};
    const Status result = delegate_->Shutdown();
    if (!IsOk(result)) last_error_ = result;
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

std::uint32_t ApplicationContext::foreground_generation() const {
    return runtime_->foreground_generation();
}

}  // namespace v1
}  // namespace zectrix::sdk
