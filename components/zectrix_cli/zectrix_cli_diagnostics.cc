#include "zectrix_cli_diagnostics.h"

#include <algorithm>
#include <cstdarg>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <iterator>

namespace zectrix::cli {
namespace {

constexpr CommandDescriptor Leaf(const char* name, const char* help,
                                  const char* usage, Handler handler,
                                  Execution execution = Execution::kOwnerRequest,
                                  Access access = Access::kReadOnly) {
    return {name, help, usage, access, execution, true, nullptr, 0, handler};
}

constexpr CommandDescriptor kSystem[] = {
    Leaf("info", "Firmware, chip and reset reason", "system info", Handler::kSystemInfo),
    Leaf("heap", "Internal and PSRAM heap in bytes", "system heap", Handler::kHeap),
    Leaf("tasks", "Task IDs, states, priorities and stack watermarks", "system tasks", Handler::kTasks),
    Leaf("uptime", "Monotonic time since boot", "system uptime", Handler::kUptime),
};
constexpr CommandDescriptor kDisplay[] = {
    Leaf("status", "Refresh state and framebuffer preview", "display status", Handler::kDisplayInspect),
    Leaf("telemetry", "Copy up to four frame observations as typed CSV rows",
         "display telemetry [after-sequence]", Handler::kDisplayTelemetry),
    Leaf("model", "Inspect fixed-point coefficients and calibration", "display model", Handler::kDisplayModel),
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
constexpr CommandDescriptor kPower[] = {Leaf("status", "Cached battery and charge state", "power status", Handler::kPower)};
constexpr CommandDescriptor kTime[] = {
    Leaf("get", "Local calendar, UTC and RTC state", "time get", Handler::kTime),
    Leaf("status", "Clock authority and persistence", "time status", Handler::kTime),
    Leaf("sync", "Inspect opportunistic sync, or confirm manual UTC and offset", "time sync [unix-ms offset-seconds]",
         Handler::kTimeSync, Execution::kOwnerRequest, Access::kConfirm),
};
constexpr CommandDescriptor kConnectivity[] = {
    Leaf("status", "Cached radio and authorized peer state", "connectivity status", Handler::kConnectivity),
};
constexpr CommandDescriptor kApps[] = {
    Leaf("list", "Compiled application catalog", "app list", Handler::kApps),
    Leaf("current", "Foreground lifecycle and generation", "app current", Handler::kCurrentApp),
};
constexpr CommandDescriptor kScene[] = {Leaf("dump", "Private scene stack, viewports and guest quotas", "scene dump", Handler::kScenes)};
constexpr CommandDescriptor kInput[] = {Leaf("watch", "Observe physical input without consuming it; Ctrl+C stops", "input watch", Handler::kInputWatch, Execution::kStream)};
constexpr CommandDescriptor kStorage[] = {Leaf("wipe", "Delete all books and micro-app files, then reboot; keep settings", "storage wipe", Handler::kStorageWipe, Execution::kOwnerRequest, Access::kConfirm)};
constexpr CommandDescriptor kFactory[] = {Leaf("reset", "Erase user files, settings and bonds, then reboot; keep firmware", "factory reset", Handler::kFactoryReset, Execution::kOwnerRequest, Access::kConfirm)};
#define GROUP(name, children) {name, "Use help for a subcommand", name, Access::kReadOnly, Execution::kImmediate, false, children, std::size(children)}
constexpr CommandDescriptor kCommands[] = {
    Leaf("help", "List commands or show command usage", "help [command]",
         Handler::kHelp, Execution::kImmediate),
    Leaf("version", "CLI protocol version", "version",
         Handler::kVersion, Execution::kImmediate),
    {"system", "System diagnostics", "system <info|heap|tasks|uptime>",
     Access::kReadOnly, Execution::kImmediate, false, kSystem, std::size(kSystem)},
    {"display", "Display diagnostics", "display <status|telemetry|model>", Access::kReadOnly,
     Execution::kImmediate, false, kDisplay, std::size(kDisplay)},
    {"log", "Log observation", "log <follow|stats>", Access::kReadOnly,
     Execution::kImmediate, false, kLog, std::size(kLog)},
    {"host", "USB book and settings session", "host start 1", Access::kReadOnly,
     Execution::kImmediate, false, kHost, std::size(kHost)},
    GROUP("power", kPower), GROUP("time", kTime), GROUP("connectivity", kConnectivity),
    GROUP("app", kApps), GROUP("scene", kScene), GROUP("input", kInput),
    GROUP("storage", kStorage), GROUP("factory", kFactory),
    Leaf("reboot", "Exit the foreground and restart firmware", "reboot", Handler::kReboot, Execution::kOwnerRequest, Access::kConfirm),
    Leaf("sleep", "Exit the foreground, show its sleep cover and power down", "sleep", Handler::kSleep, Execution::kOwnerRequest, Access::kConfirm),
    Leaf("confirm", "Confirm the exact pending USB operation within 15 seconds", "confirm <token>", Handler::kConfirm, Execution::kImmediate),
    Leaf("sysinfo", "Alias for system info", "sysinfo", Handler::kSystemInfo),
    Leaf("heap", "Alias for system heap", "heap", Handler::kHeap),
    Leaf("tasks", "Alias for system tasks", "tasks", Handler::kTasks),
    Leaf("uptime", "Alias for system uptime", "uptime", Handler::kUptime),
    Leaf("epd-inspect", "Alias for display status", "epd-inspect", Handler::kDisplayInspect),
    Leaf("log-stream", "Alias for log follow", "log-stream [error|warn|info|debug]",
         Handler::kLogFollow, Execution::kStream),
};
#undef GROUP

template <typename T> bool Number(const char* text, T* result) {
    if (!text || !*text) return false;
    const auto end = text + std::strlen(text);
    const auto parsed = std::from_chars(text, end, *result);
    return parsed.ec == std::errc{} && parsed.ptr == end;
}

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
        case ControlStatus::kDenied: return ExecuteStatus::kDenied;
        case ControlStatus::kUnknownOutcome: return ExecuteStatus::kUnknownOutcome;
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
        active_ = Handler::kHelp;
        cancellation_.Reset();
        result_ready_ = true;
        page_ = 0;
        return FormatResult(output);
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
    if (std::strcmp(invocation[0], "confirm") != 0) confirmation_token_ = 0;
    Resolution resolution;
    const auto resolved = Resolve(kCommands, std::size(kCommands), invocation, &resolution);
    if (resolved == ResolveStatus::kIncompleteCommand) return ExecuteStatus::kInvalidArguments;
    if (resolved != ResolveStatus::kOk) return ExecuteStatus::kUnknownCommand;
    const auto& command = *resolution.command;
    const std::size_t arguments = invocation.count - resolution.argument_index;
    if (command.handler == Handler::kConfirm) {
        uint64_t token = 0;
        const bool valid = arguments == 1 && Number(invocation[resolution.argument_index], &token) &&
            token != 0 && token == confirmation_token_ && clock_() < confirmation_deadline_;
        confirmation_token_ = 0;
        if (!valid) return ExecuteStatus::kDenied;
        confirmation_.confirmed = true;
        return Submit(confirmation_handler_, confirmation_);
    }
    if (command.handler == Handler::kHelp) return Help(invocation, output);
    if (command.handler == Handler::kHostStart) {
        if (arguments != 1 || std::strcmp(invocation[resolution.argument_index], "1") != 0)
            return ExecuteStatus::kInvalidArguments;
        return binary_ != nullptr && binary_->Start(output) ? ExecuteStatus::kBinary
                                                          : ExecuteStatus::kUnavailable;
    }
    if (command.handler != Handler::kLogFollow && command.handler != Handler::kTimeSync &&
        command.handler != Handler::kDisplayTelemetry && arguments != 0) {
        return ExecuteStatus::kInvalidArguments;
    }
    if (command.handler == Handler::kVersion) {
        output->Append("zectrix maintenance CLI D1.4");
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
    Handler handler = command.handler;
    switch (command.handler) {
        case Handler::kSystemInfo: request.operation = ControlOperation::kSystemInfo; break;
        case Handler::kHeap: request.operation = ControlOperation::kHeap; break;
        case Handler::kTasks: request.operation = ControlOperation::kTasks; break;
        case Handler::kUptime: request.operation = ControlOperation::kUptime; break;
        case Handler::kDisplayInspect: request.operation = ControlOperation::kDisplay; break;
        case Handler::kDisplayTelemetry:
            if (arguments > 1 || (arguments == 1 && !Number(invocation[resolution.argument_index], &request.cursor)))
                return ExecuteStatus::kInvalidArguments;
            request.operation = ControlOperation::kDisplayTelemetry;
            break;
        case Handler::kDisplayModel: request.operation = ControlOperation::kDisplayModel; break;
        case Handler::kPower: request.operation = ControlOperation::kPower; break;
        case Handler::kTime: request.operation = ControlOperation::kTime; break;
        case Handler::kConnectivity: request.operation = ControlOperation::kConnectivity; break;
        case Handler::kApps:
        case Handler::kCurrentApp: request.operation = ControlOperation::kApps; break;
        case Handler::kScenes: request.operation = ControlOperation::kScenes; break;
        case Handler::kInputWatch:
            request.operation = ControlOperation::kInput;
            input_cursor_ = next_input_poll_ms_ = 0;
            break;
        case Handler::kTimeSync:
            if (arguments == 0) { request.operation = ControlOperation::kTime; handler = Handler::kTime; break; }
            if (arguments != 2 || !Number(invocation[resolution.argument_index], &request.unix_ms) ||
                !Number(invocation[resolution.argument_index + 1], &request.offset_seconds) ||
                request.unix_ms < 946634400000LL || request.unix_ms > 4102495199999LL ||
                request.offset_seconds < -50400 || request.offset_seconds > 50400)
                return ExecuteStatus::kInvalidArguments;
            request.operation = ControlOperation::kTimeSync;
            break;
        case Handler::kReboot: request.operation = ControlOperation::kReboot; break;
        case Handler::kSleep: request.operation = ControlOperation::kSleep; break;
        case Handler::kStorageWipe: request.operation = ControlOperation::kStorageWipe; break;
        case Handler::kFactoryReset: request.operation = ControlOperation::kFactoryReset; break;
        default: return ExecuteStatus::kUnavailable;
    }
    if (IsMutation(request.operation)) {
        confirmation_ = request;
        confirmation_handler_ = handler;
        confirmation_token_ = ++next_token_;
        if (!confirmation_token_) confirmation_token_ = ++next_token_;
        confirmation_deadline_ = clock_() + 15000;
        if (handler == Handler::kTimeSync)
            Format(output, "Set UTC=%lld ms offset=%ld s. ", static_cast<long long>(request.unix_ms), static_cast<long>(request.offset_seconds));
        else Format(output, "%s. ", command.help);
        Format(output, "Type confirm %llu within 15 s; any other command or Ctrl+C cancels.",
               static_cast<unsigned long long>(confirmation_token_));
        return ExecuteStatus::kOk;
    }
    return Submit(handler, request);
}

ExecuteStatus DiagnosticExecutor::Submit(Handler handler, const ControlRequest& request) {
    const auto submitted = dispatcher_.Submit(request, &ticket_);
    if (submitted != ControlStatus::kOk) return MapStatus(submitted);
    active_ = handler;
    cancellation_.Reset();
    page_ = 0;
    result_ready_ = false;
    return ExecuteStatus::kPending;
}

ExecuteStatus DiagnosticExecutor::Poll(BoundedOutput* output) {
    if (output == nullptr) return ExecuteStatus::kInvalidArguments;
    if (cancellation_.IsCancelled() || active_ == Handler::kNone) return ExecuteStatus::kOk;
    if (active_ == Handler::kLogFollow) return PollLog(output);
    if (active_ == Handler::kInputWatch) return PollInput(output);
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
    CancelStatus();
}

ExecuteStatus DiagnosticExecutor::CancelStatus() {
    cancellation_.Cancel();
    const auto status = ticket_.id != 0 ? dispatcher_.Cancel(ticket_) : ControlStatus::kOk;
    confirmation_token_ = 0;
    ticket_ = {};
    active_ = Handler::kNone;
    result_ready_ = false;
    return status == ControlStatus::kUnknownOutcome ? ExecuteStatus::kUnknownOutcome : ExecuteStatus::kOk;
}

ExecuteStatus DiagnosticExecutor::FormatResult(BoundedOutput* output) {
    bool more = false;
    if (active_ == Handler::kHelp) {
        constexpr const char* pages[] = {
            "help [command], version\r\nsystem <info|heap|tasks|uptime>, display status\r\npower status, connectivity status\r\ntime <get|status|sync [unix-ms offset-seconds]>",
            "app <list|current>, scene dump, input watch\r\nlog follow [error|warn|info|debug], log stats\r\nreboot, sleep, storage wipe, factory reset, confirm <token>",
            "Aliases: sysinfo, heap, tasks, uptime, epd-inspect, log-stream\r\nCtrl+C cancels; host start 1 enters USB management.\r\nPairing and bond removal use the device's Connectivity screen."
        };
        output->Append(pages[page_]);
        more = page_ + 1 < std::size(pages);
    } else if (active_ == Handler::kSystemInfo) {
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
            Format(output, "baseline=%s partial_count=%lu dirty=%u rect=%d,%d %dx%d\r\n"
                   "partial_pixels=%lu high_contrast_pixels=%lu",
                   d.state.baseline == display::BaselineState::Valid1Bpp ? "valid-1bpp" : "unknown",
                   static_cast<unsigned long>(d.state.partial_refresh_count),
                   d.state.has_dirty_region, d.state.dirty_region.x, d.state.dirty_region.y,
                   d.state.dirty_region.width, d.state.dirty_region.height,
                   static_cast<unsigned long>(d.state.partial_changed_pixels),
                   static_cast<unsigned long>(display::StateModel::kHighContrastPixelLimit));
        } else if (page_ == 2) {
            Format(output, "model_revision=%lu reason=%u debt_q16: mean=%lu/%lu peak=%lu/%lu\r\n"
                   "Counts are observations; scheduling uses spatial debt. Use display telemetry and display model.",
                   static_cast<unsigned long>(d.model_revision), static_cast<unsigned>(d.last_reason),
                   static_cast<unsigned long>(d.debt_mean_q16), static_cast<unsigned long>(d.global_limit_q16),
                   static_cast<unsigned long>(d.debt_peak_q16), static_cast<unsigned long>(d.local_limit_q16));
        } else if (page_ == 3) {
            Format(output, "framebuffer: bpp=%u bytes=%lu valid=%u\r\n%s",
                   d.bits_per_pixel, static_cast<unsigned long>(d.framebuffer_bytes),
                   d.framebuffer_valid,
                   d.framebuffer_valid ? "First 64 bytes of the last successful frame:" : "Framebuffer preview unavailable.");
        } else {
            const std::size_t offset = (page_ - 4) * 16;
            Format(output, "%04zx:", offset);
            for (std::size_t index = offset; index < offset + 16; ++index) {
                Format(output, " %02x", d.preview[index]);
            }
        }
        more = page_ < 3 || (d.framebuffer_valid && page_ < 7);
    } else if (active_ == Handler::kDisplayTelemetry) {
        const auto& batch = result_.display_telemetry;
        const auto count = std::min<std::size_t>(batch.count, batch.frames.size());
        if (page_ == 0) {
            Format(output, "# epd next=%llu latest=%llu lost=%llu frames=%zu",
                   static_cast<unsigned long long>(batch.next), static_cast<unsigned long long>(batch.latest),
                   static_cast<unsigned long long>(batch.lost), count);
        } else if ((page_ - 1) / 3 < count) {
            const auto& f = batch.frames[(page_ - 1) / 3];
            const auto sequence = static_cast<unsigned long long>(f.sequence);
            if ((page_ - 1) % 3 == 0) {
                Format(output, "frame,%llu,%llu,%u,%u,%ld,%u,%d,%d,%d,%d,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%u",
                    sequence, static_cast<unsigned long long>(f.started_us), static_cast<unsigned>(f.kind),
                    static_cast<unsigned>(f.reason), static_cast<long>(f.error), f.flags,
                    f.window.x, f.window.y, f.window.width, f.window.height,
                    static_cast<unsigned long>(f.black_to_white), static_cast<unsigned long>(f.white_to_black),
                    static_cast<unsigned long>(f.duration_us), static_cast<unsigned long>(f.busy_us),
                    static_cast<unsigned long>(f.refresh_busy_us), static_cast<unsigned long>(f.spi_bytes),
                    static_cast<unsigned long>(f.ram_bytes), f.waveform_triggers);
            } else if ((page_ - 1) % 3 == 1) {
                Format(output, "env,%llu,%d,%lu,%u,%lu,%d,%u", sequence,
                    f.environment.temperature_centi_c, static_cast<unsigned long>(f.environment.temperature_age_ms),
                    f.environment.battery_mv, static_cast<unsigned long>(f.environment.battery_age_ms),
                    f.panel_temperature_centi_c, f.gain_q8);
            } else {
                Format(output, "debt,%llu,%lu,%lu,%lu,%lu,%lu,%lu", sequence,
                    static_cast<unsigned long>(f.model_revision), static_cast<unsigned long>(f.projected_mean_q16),
                    static_cast<unsigned long>(f.projected_peak_q16), static_cast<unsigned long>(f.committed_mean_q16),
                    static_cast<unsigned long>(f.committed_peak_q16), static_cast<unsigned long>(f.energy_uj));
            }
        }
        more = page_ < count * 3;
    } else if (active_ == Handler::kDisplayModel) {
        const auto& p = result_.display_model;
        if (page_ == 0) Format(output, "revision=%lu tiles=5x4 tile_pixels=6000 debt_one=65536\r\n"
            "weights_q8: window=%u flip=%u concentration=%u memory=%u",
            static_cast<unsigned long>(p.revision), p.window_weight_q8, p.flip_weight_q8,
            p.concentration_weight_q8, p.memory_weight_q8);
        else if (page_ == 1) Format(output, "limits_q16: global=%lu local=%lu\r\n"
            "memory_tau_ms=%lu debt_tau_ms=%lu sample_max_age_ms=%lu (tau=0 disables decay)",
            static_cast<unsigned long>(p.global_limit_q16), static_cast<unsigned long>(p.local_limit_q16),
            static_cast<unsigned long>(p.memory_tau_ms), static_cast<unsigned long>(p.debt_tau_ms),
            static_cast<unsigned long>(p.sample_max_age_ms));
        else if (page_ == 2) Format(output, "temperature_gain_q8 at C=-10,0,10,25,40: %u,%u,%u,%u,%u\r\n"
            "unknown_temperature_gain_q8=%u low_battery_mv=%u low_battery_gain_q8=%u",
            p.temperature_gain_q8[0], p.temperature_gain_q8[1], p.temperature_gain_q8[2],
            p.temperature_gain_q8[3], p.temperature_gain_q8[4], p.unknown_temperature_gain_q8,
            p.low_battery_mv, p.low_battery_gain_q8);
        else {
            const auto& e = p.energy[page_ - 2];
            Format(output, "energy mode=%zu calibrated=%u fixed_uj=%lu busy_power_uw=%lu\r\n"
                "spi_nj_per_byte=%u black_to_white_nj=%u white_to_black_nj=%u", page_ - 2, e.calibrated,
                static_cast<unsigned long>(e.fixed_uj), static_cast<unsigned long>(e.busy_power_uw),
                e.spi_nj_per_byte, e.black_to_white_nj, e.white_to_black_nj);
        }
        more = page_ < 5;
    }
    if (active_ == Handler::kPower) {
        const auto& p = result_.power;
        if (page_ == 0) Format(output, "battery_valid=%u mv=%u percent=%u age_ms=%lld\r\nexternal=%u charging=%u full=%u fault=%u absent=%u",
            p.valid, p.millivolts, p.percent, static_cast<long long>(p.age_ms), p.external, p.charging, p.full, p.fault, p.absent);
        else output->Append("Wi-Fi battery minimum=20%; external power permits Wi-Fi.\r\nStandby estimate unavailable; no measured discharge model.");
        more = page_ == 0;
    } else if (active_ == Handler::kTime || active_ == Handler::kTimeSync) {
        const auto& t = result_.time;
        if (page_ == 0) Format(output, "calendar_valid=%u local=%04d-%02d-%02d %02d:%02d:%02d\r\nunix_seconds=%lld offset_seconds=%ld offset_known=%u",
            t.calendar_valid, t.local[0], t.local[1], t.local[2], t.local[3], t.local[4], t.local[5],
            static_cast<long long>(t.unix_seconds), static_cast<long>(t.offset_seconds), t.offset_known);
        else if (page_ == 1) Format(output, "rtc_available=%u rtc_saved=%u save_pending=%u error=%ld\r\nsource=%s result=%s age_ms=%lld correction_ms=%lld rejected=%lu",
            t.rtc_available, t.persisted, t.pending, static_cast<long>(t.error), time::SyncSourceName(t.sync.source), time::SyncResultName(t.sync.result),
            static_cast<long long>(t.accepted_age_ms), static_cast<long long>(t.sync.correction_ms), static_cast<unsigned long>(t.sync.rejected));
        else output->Append("Automatic sync uses existing authorized Companion/verified HTTPS traffic.\r\nNo extra connection; schedulers use monotonic time.");
        more = page_ < 2;
    } else if (active_ == Handler::kConnectivity) {
        const auto& c = result_.connectivity;
        if (page_ == 0) Format(output, "radio=%.23s wifi=%.23s mode=%s resource_busy=%u book_transfer=%u\r\nssid=%.32s ip=%.15s",
            c.radio.data(), c.wifi.data(), c.mode == 2 ? "AP" : c.mode == 1 ? "STA" : "Off", c.busy, c.transfer,
            c.ssid.data(), c.address[0] ? c.address.data() : "unavailable");
        else if (page_ == 1) Format(output, "mac_valid=%u mac=%02x:%02x:%02x:%02x:%02x:%02x\r\nrssi_valid=%u rssi_dbm=%d (association sample)",
            c.mac_valid, c.mac[0], c.mac[1], c.mac[2], c.mac[3], c.mac[4], c.mac[5], c.rssi_valid, c.rssi);
        else Format(output, "ble=%.23s session=%lu pairing=%u\r\nencrypted=%u authenticated=%u bonded=%u negotiated=%u peer_authorized=%u",
            c.ble.data(), static_cast<unsigned long>(c.session), c.pairing, c.encrypted, c.authenticated, c.bonded, c.negotiated, c.authorized);
        more = page_ < 2;
    } else if (active_ == Handler::kApps || active_ == Handler::kCurrentApp) {
        const auto& a = result_.apps;
        if (active_ == Handler::kCurrentApp || page_ == 0) Format(output, "applications=%u foreground=%.31s generation=%lu lifecycle=%u error=%u",
            a.count, a.foreground.data(), static_cast<unsigned long>(a.generation), a.lifecycle, a.error);
        else if (page_ <= a.count && page_ <= a.entries.size())
            Format(output, "%.31s  %.31s", a.entries[page_ - 1].id.data(), a.entries[page_ - 1].label.data());
        more = active_ == Handler::kApps && page_ < a.count && page_ < a.entries.size();
    } else if (active_ == Handler::kScenes) {
        const auto& s = result_.scenes;
        const auto depth = std::min<std::size_t>(s.depth, s.scenes.size());
        if (page_ == 0) Format(output, "foreground=%.31s generation=%lu scene_depth=%zu transitioning=%u view_slots=%zu guest=%u",
            result_.apps.foreground.data(), static_cast<unsigned long>(result_.apps.generation), depth, s.transitioning, s.views.size(), s.guest);
        else if (page_ <= depth) Format(output, "scene[%zu] id=%u state=%lu%s", page_ - 1, s.scenes[page_ - 1].id,
            static_cast<unsigned long>(s.scenes[page_ - 1].state), page_ == depth ? " current" : "");
        else if (page_ <= depth + s.views.size()) {
            const auto index = page_ - depth - 1;
            const auto& v = s.views[index];
            Format(output, "view[%zu] configured=%u enabled=%u rect=%d,%d %dx%d dirty=%u quality=%u",
                index, v.configured, v.enabled, v.x, v.y, v.width, v.height, v.dirty, v.quality);
        } else Format(output, "guest=%.63s heap_live=%lu peak=%lu limit=%lu rejected=%lu instructions_per_callback=%lu",
            s.guest_name.data(), static_cast<unsigned long>(s.heap_live), static_cast<unsigned long>(s.heap_peak),
            static_cast<unsigned long>(s.heap_limit), static_cast<unsigned long>(s.heap_rejected), static_cast<unsigned long>(s.instruction_limit));
        more = page_ < depth + s.views.size() + (s.guest ? 1 : 0);
    } else if (active_ == Handler::kReboot || active_ == Handler::kSleep || active_ == Handler::kStorageWipe || active_ == Handler::kFactoryReset) {
        output->Append("Accepted; cannot cancel after owner handoff. Execution follows foreground Exit. USB may disconnect; do not retry automatically.");
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

ExecuteStatus DiagnosticExecutor::PollInput(BoundedOutput* output) {
    if (!ticket_.id && !result_ready_) {
        if (clock_() < next_input_poll_ms_) return ExecuteStatus::kPending;
        ControlRequest request;
        request.operation = ControlOperation::kInput;
        request.cursor = input_cursor_;
        const auto status = dispatcher_.Submit(request, &ticket_);
        if (status != ControlStatus::kOk) return MapStatus(status);
    }
    if (!result_ready_) {
        const auto status = dispatcher_.Take(ticket_, &result_);
        if (status == ControlStatus::kPending) return ExecuteStatus::kPending;
        ticket_ = {};
        if (status != ControlStatus::kOk) { active_ = Handler::kNone; return MapStatus(status); }
        result_ready_ = true;
        page_ = 0;
    }
    auto& batch = result_.input;
    if (!input_cursor_) {
        output->Append("Watching physical input; queued=1 means admitted, not necessarily delivered. Ctrl+C stops.");
    } else if (batch.lost) {
        Format(output, "input: lost=%llu (oldest trace records overwritten)", static_cast<unsigned long long>(batch.lost));
        batch.lost = 0;
        return ExecuteStatus::kPending;
    } else if (page_ < batch.count && page_ < batch.records.size()) {
        const auto& r = batch.records[page_++];
        Format(output, "input seq=%llu us=%lld button=%s action=%s queued=%u",
            static_cast<unsigned long long>(r.sequence), static_cast<long long>(r.timestamp_us),
            r.button == 0 ? "UP" : r.button == 1 ? "DOWN" : "OK", r.action == 0 ? "click" : "long", r.queued);
        if (page_ < batch.count && page_ < batch.records.size()) return ExecuteStatus::kPending;
    }
    input_cursor_ = batch.cursor;
    result_ready_ = false;
    next_input_poll_ms_ = clock_() + 50;
    return ExecuteStatus::kPending;
}

}  // namespace zectrix::cli
