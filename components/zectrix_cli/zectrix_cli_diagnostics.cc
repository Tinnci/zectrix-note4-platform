#include "zectrix_cli_diagnostics.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <iterator>

namespace zectrix::cli {
namespace {

constexpr CommandDescriptor Leaf(const char* name, const char* help,
                                  const char* usage, Handler handler,
                                  Execution execution = Execution::kOwnerRequest) {
    return {name, help, usage, Access::kReadOnly, execution, true, nullptr, 0, handler};
}

constexpr CommandDescriptor kSystem[] = {
    Leaf("info", "Firmware, chip and reset reason", "system info", Handler::kSystemInfo),
    Leaf("heap", "Internal and PSRAM heap in bytes", "system heap", Handler::kHeap),
    Leaf("tasks", "Task IDs, states, priorities and stack watermarks", "system tasks", Handler::kTasks),
    Leaf("uptime", "Monotonic time since boot", "system uptime", Handler::kUptime),
};
constexpr CommandDescriptor kDisplay[] = {
    Leaf("status", "Refresh state and framebuffer preview", "display status", Handler::kDisplayInspect),
};
constexpr CommandDescriptor kLog[] = {
    Leaf("follow", "Observe logs until Ctrl+C", "log follow [error|warn|info|debug]",
         Handler::kLogFollow, Execution::kStream),
    Leaf("stats", "Log queue, drop and truncation counts", "log stats",
         Handler::kLogStats, Execution::kImmediate),
};
constexpr CommandDescriptor kHost[] = {
    Leaf("start", "Enter USB management; open USB MANAGER on the device first",
         "host start 1", Handler::kHostStart, Execution::kImmediate),
};
constexpr CommandDescriptor kCommands[] = {
    Leaf("help", "List commands or show command usage", "help [command]",
         Handler::kHelp, Execution::kImmediate),
    Leaf("version", "CLI protocol version", "version",
         Handler::kVersion, Execution::kImmediate),
    {"system", "System diagnostics", "system <info|heap|tasks|uptime>",
     Access::kReadOnly, Execution::kImmediate, false, kSystem, std::size(kSystem)},
    {"display", "Display diagnostics", "display status", Access::kReadOnly,
     Execution::kImmediate, false, kDisplay, std::size(kDisplay)},
    {"log", "Log observation", "log <follow|stats>", Access::kReadOnly,
     Execution::kImmediate, false, kLog, std::size(kLog)},
    {"host", "USB book and settings session", "host start 1", Access::kReadOnly,
     Execution::kImmediate, false, kHost, std::size(kHost)},
    Leaf("sysinfo", "Alias for system info", "sysinfo", Handler::kSystemInfo),
    Leaf("heap", "Alias for system heap", "heap", Handler::kHeap),
    Leaf("tasks", "Alias for system tasks", "tasks", Handler::kTasks),
    Leaf("uptime", "Alias for system uptime", "uptime", Handler::kUptime),
    Leaf("epd-inspect", "Alias for display status", "epd-inspect", Handler::kDisplayInspect),
    Leaf("log-stream", "Alias for log follow", "log-stream [error|warn|info|debug]",
         Handler::kLogFollow, Execution::kStream),
};

void Format(BoundedOutput* output, const char* format, ...) {
    char text[kMaximumOutputSize + 1]{};
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    output->Append(text);
}

ExecuteStatus MapStatus(ControlStatus status) {
    switch (status) {
        case ControlStatus::kOk: return ExecuteStatus::kOk;
        case ControlStatus::kPending: return ExecuteStatus::kPending;
        case ControlStatus::kInvalidArgument: return ExecuteStatus::kInvalidArguments;
        case ControlStatus::kQueueFull:
        case ControlStatus::kBusy: return ExecuteStatus::kBusy;
        case ControlStatus::kTimeout: return ExecuteStatus::kTimeout;
        default: return ExecuteStatus::kUnavailable;
    }
}

const char* ResetName(system::ResetReason reason) {
    switch (reason) {
        case system::ResetReason::PowerOn: return "power-on";
        case system::ResetReason::Software: return "software";
        case system::ResetReason::Panic: return "panic";
        case system::ResetReason::Watchdog: return "watchdog";
        case system::ResetReason::DeepSleep: return "deep-sleep";
        case system::ResetReason::Brownout: return "brownout";
        case system::ResetReason::External: return "external";
        default: return "unknown";
    }
}

const char* TaskStateName(system::TaskState state) {
    switch (state) {
        case system::TaskState::kRunning: return "running";
        case system::TaskState::kReady: return "ready";
        case system::TaskState::kBlocked: return "blocked";
        case system::TaskState::kSuspended: return "suspended";
        case system::TaskState::kDeleted: return "deleted";
        default: return "unknown";
    }
}

const char* RefreshName(display::RefreshKind kind) {
    switch (kind) {
        case display::RefreshKind::kFull1Bpp: return "full-1bpp";
        case display::RefreshKind::kPartial1Bpp: return "partial-1bpp";
        case display::RefreshKind::kFull4Bpp: return "full-4bpp";
        default: return "none";
    }
}

}  // namespace

const CommandDescriptor* DiagnosticCommands(std::size_t* count) {
    if (count != nullptr) *count = std::size(kCommands);
    return kCommands;
}

ExecuteStatus DiagnosticExecutor::Help(const Invocation& invocation,
                                       BoundedOutput* output) {
    if (invocation.count == 1) {
        output->Append("help [command], version\r\n"
                       "system <info|heap|tasks|uptime>, display status\r\n"
                       "log follow [error|warn|info|debug], log stats\r\n"
                       "Aliases: sysinfo, heap, tasks, uptime, epd-inspect, log-stream\r\n"
                       "Ctrl+C cancels; host start 1 enters USB management.");
        return ExecuteStatus::kOk;
    }
    const CommandDescriptor* commands = kCommands;
    std::size_t count = std::size(kCommands);
    const CommandDescriptor* found = nullptr;
    for (std::size_t argument = 1; argument < invocation.count; ++argument) {
        if (argument > kMaximumCommandDepth) return ExecuteStatus::kInvalidArguments;
        found = nullptr;
        for (std::size_t index = 0; index < count; ++index) {
            if (std::strcmp(commands[index].name, invocation[argument]) == 0) {
                found = &commands[index];
                break;
            }
        }
        if (found == nullptr) return ExecuteStatus::kUnknownCommand;
        commands = found->children;
        count = found->child_count;
    }
    Format(output, "%s\r\n%s", found->usage, found->help);
    return ExecuteStatus::kOk;
}

ExecuteStatus DiagnosticExecutor::Execute(const Invocation& invocation,
                                          BoundedOutput* output) {
    if (output == nullptr || invocation.count == 0) return ExecuteStatus::kInvalidArguments;
    if (active_ != Handler::kNone) return ExecuteStatus::kBusy;
    Resolution resolution;
    const auto resolved = Resolve(kCommands, std::size(kCommands), invocation, &resolution);
    if (resolved == ResolveStatus::kIncompleteCommand) return ExecuteStatus::kInvalidArguments;
    if (resolved != ResolveStatus::kOk) return ExecuteStatus::kUnknownCommand;
    const auto& command = *resolution.command;
    const std::size_t arguments = invocation.count - resolution.argument_index;
    if (command.handler == Handler::kHelp) return Help(invocation, output);
    if (command.handler == Handler::kHostStart) {
        if (arguments != 1 || std::strcmp(invocation[resolution.argument_index], "1") != 0)
            return ExecuteStatus::kInvalidArguments;
        return binary_ != nullptr && binary_->Start(output) ? ExecuteStatus::kBinary
                                                          : ExecuteStatus::kUnavailable;
    }
    if (command.handler != Handler::kLogFollow && arguments != 0) {
        return ExecuteStatus::kInvalidArguments;
    }
    if (command.handler == Handler::kVersion) {
        output->Append("zectrix maintenance CLI D1.2");
        return ExecuteStatus::kOk;
    }
    if (command.handler == Handler::kLogStats) {
        const auto stats = logs_.Stats();
        Format(output, "logs queued=%lu/%zu dropped=%lu truncated=%lu",
               static_cast<unsigned long>(stats.queued), kLogRecords,
               static_cast<unsigned long>(stats.dropped),
               static_cast<unsigned long>(stats.truncated));
        return ExecuteStatus::kOk;
    }
    if (command.handler == Handler::kLogFollow) {
        if (arguments > 1) return ExecuteStatus::kInvalidArguments;
        log_level_ = LogLevel::kInfo;
        if (arguments == 1) {
            const char* level = invocation[resolution.argument_index];
            if (std::strcmp(level, "error") == 0) log_level_ = LogLevel::kError;
            else if (std::strcmp(level, "warn") == 0) log_level_ = LogLevel::kWarn;
            else if (std::strcmp(level, "info") == 0) log_level_ = LogLevel::kInfo;
            else if (std::strcmp(level, "debug") == 0) log_level_ = LogLevel::kDebug;
            else return ExecuteStatus::kInvalidArguments;
        }
        active_ = Handler::kLogFollow;
        cancellation_.Reset();
        reported_drops_ = 0;
        output->Append("Following captured logs; Ctrl+C to stop.");
        return ExecuteStatus::kPending;
    }

    ControlRequest request;
    switch (command.handler) {
        case Handler::kSystemInfo: request.operation = ControlOperation::kSystemInfo; break;
        case Handler::kHeap: request.operation = ControlOperation::kHeap; break;
        case Handler::kTasks: request.operation = ControlOperation::kTasks; break;
        case Handler::kUptime: request.operation = ControlOperation::kUptime; break;
        case Handler::kDisplayInspect: request.operation = ControlOperation::kDisplay; break;
        default: return ExecuteStatus::kUnavailable;
    }
    const auto submitted = dispatcher_.Submit(request, &ticket_);
    if (submitted != ControlStatus::kOk) return MapStatus(submitted);
    active_ = command.handler;
    cancellation_.Reset();
    page_ = 0;
    result_ready_ = false;
    return ExecuteStatus::kPending;
}

ExecuteStatus DiagnosticExecutor::Poll(BoundedOutput* output) {
    if (output == nullptr) return ExecuteStatus::kInvalidArguments;
    if (cancellation_.IsCancelled() || active_ == Handler::kNone) return ExecuteStatus::kOk;
    if (active_ == Handler::kLogFollow) return PollLog(output);
    if (!result_ready_) {
        const auto status = dispatcher_.Take(ticket_, &result_);
        if (status == ControlStatus::kPending) return ExecuteStatus::kPending;
        ticket_ = {};
        if (status != ControlStatus::kOk) {
            active_ = Handler::kNone;
            return MapStatus(status);
        }
        result_ready_ = true;
    }
    return FormatResult(output);
}

void DiagnosticExecutor::Cancel() {
    cancellation_.Cancel();
    if (ticket_.id != 0) dispatcher_.Cancel(ticket_);
    ticket_ = {};
    active_ = Handler::kNone;
    result_ready_ = false;
}

ExecuteStatus DiagnosticExecutor::FormatResult(BoundedOutput* output) {
    bool more = false;
    if (active_ == Handler::kSystemInfo) {
        const auto& s = result_.system;
        if (page_ == 0) {
            Format(output, "project=%.31s version=%.31s\r\nIDF=%.31s build=%.15s %.15s",
                   s.firmware.project_name.data(), s.firmware.version.data(),
                   s.firmware.idf_version.data(), s.firmware.build_date.data(),
                   s.firmware.build_time.data());
        } else if (page_ == 1) {
            Format(output, "chip=%.23s revision=%u cores=%u reset=%s\r\n"
                   "wifi=%u ble=%u rtc=%u nfc=%u psram=%u",
                   s.capabilities.chip_model.data(), s.capabilities.chip_revision,
                   s.capabilities.core_count, ResetName(s.reset_reason),
                   s.capabilities.wifi, s.capabilities.bluetooth_le,
                   s.capabilities.rtc, s.capabilities.nfc, s.capabilities.psram);
        } else {
            Format(output, "flash_bytes=%lu psram_bytes=%lu\r\nwifi_mac=%02x:%02x:%02x:%02x:%02x:%02x",
                   static_cast<unsigned long>(s.diagnostics.flash_bytes),
                   static_cast<unsigned long>(s.diagnostics.psram_bytes),
                   s.wifi_mac[0], s.wifi_mac[1], s.wifi_mac[2],
                   s.wifi_mac[3], s.wifi_mac[4], s.wifi_mac[5]);
        }
        more = page_ < 2;
    } else if (active_ == Handler::kHeap) {
        const auto& region = page_ == 0 ? result_.heap.internal : result_.heap.psram;
        Format(output, "%s heap bytes: total=%lu free=%lu minimum_free=%lu largest_block=%lu",
               page_ == 0 ? "internal" : "psram",
               static_cast<unsigned long>(region.total), static_cast<unsigned long>(region.free),
               static_cast<unsigned long>(region.minimum_free), static_cast<unsigned long>(region.largest_block));
        more = page_ == 0;
    } else if (active_ == Handler::kUptime) {
        const uint64_t seconds = result_.uptime_us / 1000000;
        Format(output, "uptime=%llu days %02u:%02u:%02u.%03u (%llu ms)",
               static_cast<unsigned long long>(seconds / 86400),
               static_cast<unsigned>((seconds / 3600) % 24),
               static_cast<unsigned>((seconds / 60) % 60),
               static_cast<unsigned>(seconds % 60),
               static_cast<unsigned>((result_.uptime_us / 1000) % 1000),
               static_cast<unsigned long long>(result_.uptime_us / 1000));
    } else if (active_ == Handler::kTasks) {
        const auto& tasks = result_.tasks;
        if (page_ == 0) {
            Format(output, "tasks=%lu capacity=%zu%s\r\nID  STATE      PRIORITY  MIN_STACK_BYTES  OWNER",
                   static_cast<unsigned long>(tasks.total), system::kMaximumTasks,
                   tasks.capacity_exceeded ? " (snapshot capacity exceeded)" : "");
        } else if (page_ <= tasks.count && page_ <= tasks.tasks.size()) {
            const auto& task = tasks.tasks[page_ - 1];
            Format(output, "%lu  %-9s  %lu  %lu  %s",
                   static_cast<unsigned long>(task.id), TaskStateName(task.state),
                   static_cast<unsigned long>(task.priority),
                   static_cast<unsigned long>(task.minimum_stack_bytes),
                   task.application_owner ? "application" : "-");
        }
        more = page_ < tasks.count && page_ < tasks.tasks.size();
    } else if (active_ == Handler::kDisplayInspect) {
        const auto& d = result_.display;
        if (page_ == 0) {
            Format(output, "panel=400x300 powered=%u batch=%u\r\n"
                   "last_refresh=%s attempts=%lu failures=%lu error=%ld duration_us=%llu",
                   d.powered, d.batch_active, RefreshName(d.last_refresh),
                   static_cast<unsigned long>(d.refresh_count),
                   static_cast<unsigned long>(d.failed_refresh_count),
                   static_cast<long>(d.last_error), static_cast<unsigned long long>(d.last_duration_us));
        } else if (page_ == 1) {
            Format(output, "baseline=%s partial_count=%lu/%lu dirty=%u rect=%d,%d %dx%d\r\n"
                   "partial_pixels=%lu/%lu high_contrast_pixels=%lu",
                   d.state.baseline == display::BaselineState::Valid1Bpp ? "valid-1bpp" : "unknown",
                   static_cast<unsigned long>(d.state.partial_refresh_count),
                   static_cast<unsigned long>(display::StateModel::kPartialRefreshLimit),
                   d.state.has_dirty_region, d.state.dirty_region.x, d.state.dirty_region.y,
                   d.state.dirty_region.width, d.state.dirty_region.height,
                   static_cast<unsigned long>(d.state.partial_changed_pixels),
                   static_cast<unsigned long>(display::StateModel::kPartialPixelLimit),
                   static_cast<unsigned long>(display::StateModel::kHighContrastPixelLimit));
        } else if (page_ == 2) {
            Format(output, "framebuffer: bpp=%u bytes=%lu valid=%u\r\n%s",
                   d.bits_per_pixel, static_cast<unsigned long>(d.framebuffer_bytes),
                   d.framebuffer_valid,
                   d.framebuffer_valid ? "First 64 bytes of the last successful frame:" : "Framebuffer preview unavailable.");
        } else {
            const std::size_t offset = (page_ - 3) * 16;
            Format(output, "%04zx:", offset);
            for (std::size_t index = offset; index < offset + 16; ++index) {
                Format(output, " %02x", d.preview[index]);
            }
        }
        more = page_ < 2 || (d.framebuffer_valid && page_ < 6);
    }
    ++page_;
    if (!more) active_ = Handler::kNone;
    return more ? ExecuteStatus::kPending : ExecuteStatus::kOk;
}

ExecuteStatus DiagnosticExecutor::PollLog(BoundedOutput* output) {
    const auto stats = logs_.Stats();
    if (stats.dropped != reported_drops_) {
        Format(output, "log: dropped=%lu (+%lu) truncated=%lu",
               static_cast<unsigned long>(stats.dropped),
               static_cast<unsigned long>(stats.dropped - reported_drops_),
               static_cast<unsigned long>(stats.truncated));
        reported_drops_ = stats.dropped;
        return ExecuteStatus::kPending;
    }
    // Limit filtering work as well as output to keep Ctrl+C responsive.
    LogRecord record;
    for (std::size_t count = 0; count < 4 && logs_.Pop(&record); ++count) {
        if (record.level > log_level_) continue;
        std::size_t size = std::strlen(record.text.data());
        while (size != 0 && (record.text[size - 1] == '\r' || record.text[size - 1] == '\n')) --size;
        record.text[size] = '\0';
        output->Append(record.text.data());
        break;
    }
    return ExecuteStatus::kPending;
}

}  // namespace zectrix::cli
