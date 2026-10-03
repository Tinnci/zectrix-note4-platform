#include "note4_edge_sync.h"
namespace note4::connectivity {
namespace {
bool Reached(uint32_t now, uint32_t deadline) { return static_cast<int32_t>(now - deadline) >= 0; }
constexpr auto kDriverRoute = companion::ResourceCapability::kPublicTestDocumentV1;
}
bool EdgeSync::Begin(uint8_t* buffer, std::size_t capacity, uint32_t budget, uint32_t now) {
    if (state_ != State::Idle || !buffer || capacity != storage::edge::kFileSize || budget < 1000 || budget > 60000) return false;
    buffer_ = buffer; deadline_ = now + budget; state_ = State::Credentials;
    return true;
}
void EdgeSync::Stop(Result result, uint32_t now) {
    ClearWifiCredentials(&credentials_buffer_);
    result_ = result; stop_deadline_ = now + WifiBackend::kStopTimeoutMs;
    state_ = started_ ? State::Stop : State::Done;
}
void EdgeSync::Cancel(uint32_t now) {
    if (state_ != State::Idle && state_ != State::Done && state_ != State::Stop) Stop(Result::Cancelled, now);
}
EdgeSync::Result EdgeSync::Poll(uint32_t now) {
    if (state_ == State::Done) return result_;
    if (state_ == State::Idle) return Result::Failed;
    if (state_ != State::Stop && Reached(now, deadline_)) Stop(Result::Failed, now);
    WifiDriverResult step = WifiDriverResult::kPending;
    State next = state_;
    switch (state_) {
        case State::Credentials:
            if (credentials_.Load(&credentials_buffer_) != WifiCredentialResult::kAvailable ||
                !ValidateWifiCredentials(credentials_buffer_)) Stop(Result::Failed, now);
            else state_ = State::Start;
            return state_ == State::Done ? result_ : Result::Pending;
        case State::Start:
            step = driver_.StartStation(credentials_buffer_);
            ClearWifiCredentials(&credentials_buffer_);
            if (step == WifiDriverResult::kReady || step == WifiDriverResult::kPending) {
                started_ = true; state_ = State::Association;
            } else Stop(Result::Failed, now);
            return state_ == State::Done ? result_ : Result::Pending;
        case State::Association: step = driver_.PollAssociation(); next = State::Ip; break;
        case State::Ip: step = driver_.PollIp(); next = State::Dns; break;
        case State::Dns: step = driver_.Resolve(kDriverRoute); next = State::Tls; break;
        case State::Tls: step = driver_.OpenTls(kDriverRoute); next = State::Fetch; break;
        case State::Fetch:
            step = driver_.Fetch(kDriverRoute, buffer_, storage::edge::kFileSize, &size_);
            if (step == WifiDriverResult::kReady) {
                storage::edge::Page page;
                Stop(storage::edge::Decode(buffer_, size_, &page) ? Result::Success : Result::Failed, now);
            } else if (step != WifiDriverResult::kPending) Stop(Result::Failed, now);
            return state_ == State::Done ? result_ : Result::Pending;
        case State::Stop:
            step = driver_.StopStation();
            if (step == WifiDriverResult::kReady) { started_ = false; state_ = State::Done; }
            else if (Reached(now, stop_deadline_)) { result_ = Result::StopFailed; state_ = State::Done; }
            return state_ == State::Done ? result_ : Result::Pending;
        case State::Idle: case State::Done: break;
    }
    if (next != state_) {
        if (step == WifiDriverResult::kReady) state_ = next;
        else if (step != WifiDriverResult::kPending) Stop(Result::Failed, now);
    }
    return state_ == State::Done ? result_ : Result::Pending;
}
}  // namespace note4::connectivity
