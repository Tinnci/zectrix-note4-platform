#pragma once

#include "note4_log.h"

namespace note4::cli {
// Compatibility names for CLI consumers; storage/transport belong to note4_log.
using log::DetectLogLevel;
using log::LogBuffer;
using log::LogLevel;
using log::LogRecord;
using log::LogStats;
inline constexpr auto kLogRecords = log::kLogRecords;
LogBuffer& MaintenanceLogs();
void StartMaintenanceLogCapture();
void StopMaintenanceLogCapture();
}  // namespace note4::cli
