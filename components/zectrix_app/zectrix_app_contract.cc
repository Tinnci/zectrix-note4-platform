#include "zectrix_app_contract.h"

#include <algorithm>
#include <cstring>

namespace zectrix::app {
namespace {

int Priority(CommandKind kind) {
    switch (kind) {
        case CommandKind::Shutdown: return 3;
        case CommandKind::Home: return 2;
        case CommandKind::Open:
        case CommandKind::Back: return 1;
        case CommandKind::None: return 0;
    }
    return 0;
}

}  // namespace

bool ApplicationId::Copy(const char* source, ApplicationId* output) {
    if (source == nullptr || output == nullptr) return false;
    std::size_t length = 0;
    while (length <= kCapacity && source[length] != '\0') ++length;
    if (length == 0 || length > kCapacity) return false;
    for (std::size_t index = 0; index < length; ++index) {
        const char value = source[index];
        const bool valid = (value >= 'a' && value <= 'z') ||
                           (value >= '0' && value <= '9') || value == '-' ||
                           value == '_' || value == '.';
        if (!valid) return false;
    }
    ApplicationId candidate;
    std::memcpy(candidate.value_.data(), source, length);
    candidate.value_[length] = '\0';
    *output = candidate;
    return true;
}

bool ApplicationId::operator==(const ApplicationId& other) const {
    return value_ == other.value_;
}

AppCommand AppCommand::Back() { return {CommandKind::Back, {}}; }
AppCommand AppCommand::Home() { return {CommandKind::Home, {}}; }
AppCommand AppCommand::Shutdown() { return {CommandKind::Shutdown, {}}; }

bool AppCommand::Open(const char* target, AppCommand* output) {
    if (output == nullptr) return false;
    ApplicationId id;
    if (!ApplicationId::Copy(target, &id)) return false;
    *output = {CommandKind::Open, id};
    return true;
}

SubmitResult CommandArbiter::Submit(const AppCommand& command) {
    if (command.kind == CommandKind::None ||
        (command.kind == CommandKind::Open && command.target.empty())) {
        return SubmitResult::Conflict;
    }
    if (!has_pending()) {
        pending_ = command;
        return SubmitResult::Accepted;
    }
    if (Priority(command.kind) > Priority(pending_.kind)) {
        pending_ = command;
        return SubmitResult::Superseded;
    }
    return SubmitResult::Conflict;
}

bool CommandArbiter::Take(AppCommand* output) {
    if (output == nullptr || !has_pending()) return false;
    *output = pending_;
    pending_ = {};
    return true;
}

DirtyRegion DirtyRegion::Union(const DirtyRegion& first,
                               const DirtyRegion& second) {
    if (first.IsEmpty()) return second;
    if (second.IsEmpty()) return first;
    const int32_t left = std::min<int32_t>(first.x, second.x);
    const int32_t top = std::min<int32_t>(first.y, second.y);
    const int32_t right = std::max<int32_t>(first.x + first.width,
                                            second.x + second.width);
    const int32_t bottom = std::max<int32_t>(first.y + first.height,
                                             second.y + second.height);
    return {static_cast<int16_t>(left), static_cast<int16_t>(top),
            static_cast<int16_t>(right - left),
            static_cast<int16_t>(bottom - top)};
}

void RenderAccumulator::Submit(const RenderRequest& request) {
    if (!pending_ || request.owner_generation != request_.owner_generation) {
        request_ = request;
        pending_ = true;
        return;
    }
    request_.dirty = DirtyRegion::Union(request_.dirty, request.dirty);
    if (request.intent == RenderIntent::Quality) {
        request_.intent = RenderIntent::Quality;
    }
}

bool RenderAccumulator::TakeFor(uint32_t current_generation,
                                RenderRequest* output) {
    if (output == nullptr || !pending_) return false;
    if (request_.owner_generation != current_generation) {
        pending_ = false;
        return false;
    }
    *output = request_;
    pending_ = false;
    return true;
}

}  // namespace zectrix::app
