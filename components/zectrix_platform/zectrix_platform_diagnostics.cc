#include "zectrix_platform_diagnostics.h"

#include "zectrix_display_service.h"
#include "zectrix_input_service.h"
#include "zectrix_system_service.h"
#include "zectrix_time_service.h"

namespace zectrix {

PlatformDiagnostics::PlatformDiagnostics(system::SystemService& system,
                                         display::DisplayService& display,
                                         input::InputService& input,
                                         time::TimeService& time,
                                         cli::CliBinarySession* binary)
    : system_(system), display_(display), input_(input), time_(time),
      owner_task_(xTaskGetCurrentTaskHandle()), dispatcher_(*this),
      executor_(dispatcher_, cli::MaintenanceLogs(), binary) {
    input_.SetWaitHook(OnWait, this);
}

PlatformDiagnostics::~PlatformDiagnostics() {
    Shutdown();
    input_.SetWaitHook(nullptr, nullptr);
}

void PlatformDiagnostics::OnWait(void* context) {
    static_cast<PlatformDiagnostics*>(context)->Poll();
}

bool PlatformDiagnostics::IsCurrentTaskOwner() const {
    return xTaskGetCurrentTaskHandle() == owner_task_;
}

void PlatformDiagnostics::Wake() { input_.WakeWait(); }
void PlatformDiagnostics::Poll() { dispatcher_.Dispatch(); }
void PlatformDiagnostics::Shutdown() { dispatcher_.Shutdown(); }

cli::ControlStatus PlatformDiagnostics::Inspect(const cli::ControlRequest& request,
                                                cli::ControlResult* result) {
    if (!IsCurrentTaskOwner()) return cli::ControlStatus::kDenied;
    esp_err_t err = ESP_ERR_INVALID_ARG;
    switch (request.operation) {
        case cli::ControlOperation::kSystemInfo:
            err = system_.ReadSnapshot(&result->system);
            break;
        case cli::ControlOperation::kHeap:
            err = system_.ReadHeap(&result->heap);
            break;
        case cli::ControlOperation::kTasks:
            err = system_.ReadTasks(&result->tasks);
            break;
        case cli::ControlOperation::kUptime:
            result->uptime_us = time_.MonotonicMicroseconds();
            err = ESP_OK;
            break;
        case cli::ControlOperation::kDisplay:
            err = display_.ReadInspection(&result->display);
            break;
    }
    if (err == ESP_OK) return cli::ControlStatus::kOk;
    return err == ESP_ERR_INVALID_ARG ? cli::ControlStatus::kInvalidArgument
                                      : cli::ControlStatus::kUnavailable;
}

}  // namespace zectrix
