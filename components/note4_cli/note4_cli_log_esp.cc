#include "note4_cli_log.h"

namespace note4::cli {
LogBuffer& MaintenanceLogs() { return log::SystemLogs(); }
void StartMaintenanceLogCapture() { log::StartCapture(); }
void StopMaintenanceLogCapture() { log::StopCapture(); }
}  // namespace note4::cli
