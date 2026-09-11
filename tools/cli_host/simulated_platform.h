#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "zectrix_cli_control.h"
#include "zectrix_cli_log.h"

namespace zectrix::cli::host {

struct SimulationOptions {
    uint32_t owner_delay_ms = 0;
    uint32_t log_interval_ms = 1000;
    uint32_t log_burst = 0;
};

class SimulatedPlatform final : public ControlOwner {
public:
    SimulatedPlatform(LogBuffer& logs, SimulationOptions options);
    ~SimulatedPlatform() { Stop(); }
    void Start(PlatformControlDispatcher& dispatcher);
    void Stop();

    bool IsCurrentTaskOwner() const override;
    void Wake() override;
    ControlStatus Inspect(const ControlRequest&, ControlResult*) override;

private:
    using Clock = std::chrono::steady_clock;
    void Run(PlatformControlDispatcher& dispatcher);
    void Refresh();
    void EmitLog();

    LogBuffer& logs_;
    SimulationOptions options_;
    ControlResult snapshot_;
    input::InputTrace input_trace_;
    int64_t unix_base_ms_ = 1709179200000;
    display::StateModel display_state_;
    Clock::time_point started_ = Clock::now();
    uint32_t log_sequence_ = 0;
    std::mutex mutex_;
    std::condition_variable changed_;
    std::thread thread_;
    bool wake_pending_ = false;
    bool stopping_ = false;
};

}  // namespace zectrix::cli::host
