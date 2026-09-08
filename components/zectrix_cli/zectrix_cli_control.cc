#include "zectrix_cli_control.h"

#include <chrono>
#include <type_traits>

namespace zectrix::cli {

static_assert(std::is_trivially_copyable_v<ControlRequest>);
static_assert(std::is_trivially_copyable_v<ControlResult>);

uint64_t SteadyMilliseconds() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool PlatformControlDispatcher::Matches(ControlTicket ticket) const {
    return ticket.id != 0 && ticket.id == ticket_.id &&
           ticket.generation == ticket_.generation && state_ != State::kIdle;
}

ControlStatus PlatformControlDispatcher::Submit(const ControlRequest& request,
                                               ControlTicket* ticket) {
    if (ticket == nullptr || request.operation > ControlOperation::kDisplay) {
        return ControlStatus::kInvalidArgument;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (closing_) return ControlStatus::kUnavailable;
    if (state_ != State::kIdle) return ControlStatus::kQueueFull;
    ++ticket_.id;
    if (ticket_.id == 0) ++ticket_.id;
    ++ticket_.generation;
    request_ = request;
    submitted_ms_ = clock_();
    state_ = State::kQueued;
    abandoned_ = false;
    *ticket = ticket_;
    // Serialize wake with shutdown so it cannot outlive the owner adapter.
    owner_.Wake();
    return ControlStatus::kOk;
}

ControlStatus PlatformControlDispatcher::Take(ControlTicket ticket,
                                             ControlResult* result) {
    if (result == nullptr) return ControlStatus::kInvalidArgument;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!Matches(ticket) || abandoned_) return ControlStatus::kUnavailable;
    if (!closing_ && clock_() - submitted_ms_ >= kOwnerRequestTimeoutMs) {
        if (state_ == State::kExecuting) abandoned_ = true;
        else state_ = State::kIdle;
        return ControlStatus::kTimeout;
    }
    if (state_ != State::kComplete) return ControlStatus::kPending;
    if (status_ == ControlStatus::kOk) *result = result_;
    state_ = State::kIdle;
    return status_;
}

void PlatformControlDispatcher::Cancel(ControlTicket ticket) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!Matches(ticket)) return;
    if (state_ == State::kExecuting) abandoned_ = true;
    else state_ = State::kIdle;
}

bool PlatformControlDispatcher::Dispatch() {
    if (!owner_.IsCurrentTaskOwner()) return false;
    std::unique_lock<std::mutex> lock(mutex_);
    if (closing_ || state_ != State::kQueued) return false;
    if (clock_() - submitted_ms_ >= kOwnerRequestTimeoutMs) {
        status_ = ControlStatus::kTimeout;
        state_ = State::kComplete;
        return true;
    }
    state_ = State::kExecuting;
    const ControlRequest request = request_;
    lock.unlock();
    result_ = {};
    const ControlStatus status = owner_.Inspect(request, &result_);
    lock.lock();
    status_ = closing_ ? ControlStatus::kUnavailable : status;
    state_ = abandoned_ ? State::kIdle : State::kComplete;
    idle_.notify_all();
    return true;
}

void PlatformControlDispatcher::Shutdown() {
    std::unique_lock<std::mutex> lock(mutex_);
    closing_ = true;
    if (state_ == State::kQueued || state_ == State::kComplete) {
        status_ = ControlStatus::kUnavailable;
        state_ = State::kComplete;
    }
    idle_.wait(lock, [this] { return state_ != State::kExecuting; });
}

}  // namespace zectrix::cli
