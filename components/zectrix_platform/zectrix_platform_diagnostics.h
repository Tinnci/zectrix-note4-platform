#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "zectrix_cli_diagnostics.h"

namespace zectrix {
namespace display { class DisplayService; }
namespace input { class InputService; }
namespace system { class SystemService; }
namespace time { class TimeService; }

// This adapter belongs to Platform, not to the CLI task or public SDK.
class PlatformDiagnostics final : public cli::ControlOwner {
public:
    PlatformDiagnostics(system::SystemService& system,
                        display::DisplayService& display,
                        input::InputService& input, time::TimeService& time);
    ~PlatformDiagnostics();
    void Poll();
    void Shutdown();
    cli::CliExecutor& executor() { return executor_; }

    bool IsCurrentTaskOwner() const override;
    void Wake() override;
    cli::ControlStatus Inspect(const cli::ControlRequest&, cli::ControlResult*) override;

private:
    static void OnWait(void* context);
    system::SystemService& system_;
    display::DisplayService& display_;
    input::InputService& input_;
    time::TimeService& time_;
    TaskHandle_t owner_task_;
    cli::PlatformControlDispatcher dispatcher_;
    cli::DiagnosticExecutor executor_;
};

}  // namespace zectrix
