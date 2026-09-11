#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "zectrix_cli_diagnostics.h"

namespace zectrix {
class ServiceRegistry;
namespace display { class DisplayService; }
namespace input { class InputService; }
namespace system { class SystemService; }
namespace time { class TimeService; }

// This adapter belongs to Platform, not to the CLI task or public SDK.
class PlatformDiagnostics final : public cli::ControlOwner {
public:
    PlatformDiagnostics(const ServiceRegistry& services, system::SystemService& system,
                        display::DisplayService& display,
                        input::InputService& input, time::TimeService& time,
                        cli::CliBinarySession* binary = nullptr);
    ~PlatformDiagnostics();
    void Poll();
    void Shutdown();
    cli::CliExecutor& executor() { return executor_; }
    void SetDelegate(cli::MaintenanceDelegate* delegate) { delegate_ = delegate; }

    bool IsCurrentTaskOwner() const override;
    void Wake() override;
    cli::ControlStatus Inspect(const cli::ControlRequest&, cli::ControlResult*) override;

private:
    static void OnWait(void* context);
    void ReadTime(cli::TimeInspection* result) const;
    const ServiceRegistry& services_;
    cli::MaintenanceDelegate* delegate_ = nullptr;
    system::SystemService& system_;
    display::DisplayService& display_;
    input::InputService& input_;
    time::TimeService& time_;
    TaskHandle_t owner_task_;
    cli::PlatformControlDispatcher dispatcher_;
    cli::DiagnosticExecutor executor_;
};

}  // namespace zectrix
