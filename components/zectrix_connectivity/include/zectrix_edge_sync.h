#pragma once
#include "zectrix_wifi_backend.h"
#include "zectrix_edge_page.h"

namespace zectrix::connectivity {
// Dedicated, preconfigured driver supplied by the unattended foreground owner.
// This is not a Companion ResourceRequest and cannot route through a phone.
class EdgeSync {
public:
    enum class Result { Pending, Success, Failed, Cancelled, StopFailed };
    EdgeSync(WifiCredentialSource& credentials, WifiBackendDriver& driver)
        : credentials_(credentials), driver_(driver) {}
    bool Begin(uint8_t* buffer, std::size_t capacity, uint32_t budget_ms, uint32_t now_ms);
    Result Poll(uint32_t now_ms);
    void Cancel(uint32_t now_ms);
    std::size_t Size() const { return size_; }
private:
    enum class State { Idle, Credentials, Start, Association, Ip, Dns, Tls, Fetch, Stop, Done };
    void Stop(Result result, uint32_t now_ms);
    WifiCredentialSource& credentials_;
    WifiBackendDriver& driver_;
    WifiCredentials credentials_buffer_{};
    uint8_t* buffer_ = nullptr;
    std::size_t size_ = 0;
    uint32_t deadline_ = 0, stop_deadline_ = 0;
    State state_ = State::Idle;
    Result result_ = Result::Pending;
    bool started_ = false;
};
}  // namespace zectrix::connectivity
