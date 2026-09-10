#include "zectrix_host_channel.h"

namespace zectrix::host {

void Channel::Retire() {
    session_ = 0;
    if (state_ != State::Executing) state_ = State::Idle;
}
void Channel::Enable() {
    std::lock_guard<std::mutex> lock(mutex_);
    enabled_ = true;
}
void Channel::Disable() {
    std::lock_guard<std::mutex> lock(mutex_);
    enabled_ = false;
    Retire();
}
uint32_t Channel::Connect() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!enabled_ || session_ != 0 || state_ != State::Idle) return 0;
    if (++next_session_ == 0) ++next_session_;
    return session_ = next_session_;
}
void Channel::Disconnect(uint32_t session) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (session != 0 && session == session_) Retire();
}
uint32_t Channel::Session() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return session_;
}
Status Channel::Submit(const Frame& request) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!enabled_ || request.session == 0 || request.session != session_) return Status::Cancelled;
    if (state_ != State::Idle) return Status::Busy;
    slot_ = request;
    state_ = State::Queued;
    return Status::Ok;
}
bool Channel::TakeRequest(Frame* request) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!request || state_ != State::Queued) return false;
    *request = slot_;
    state_ = State::Executing;
    return true;
}
void Channel::Complete(const Frame& reply) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != State::Executing || slot_.id != reply.id || slot_.session != reply.session) return;
    if (reply.session != session_) state_ = State::Idle;
    else {
        slot_ = reply;
        state_ = State::Replied;
    }
}
bool Channel::TakeReply(uint32_t session, Frame* reply) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!reply || state_ != State::Replied || session == 0 || session != session_) return false;
    *reply = slot_;
    state_ = State::Idle;
    return true;
}

}  // namespace zectrix::host
