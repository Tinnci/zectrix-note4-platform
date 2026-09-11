#include "zectrix_cli_diagnostics.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

using namespace zectrix::cli;

namespace {

std::atomic<uint64_t> now_ms{0};
uint64_t Clock() { return now_ms.load(); }

class Owner final : public ControlOwner {
public:
    bool IsCurrentTaskOwner() const override {
        return std::this_thread::get_id() == owner_thread;
    }
    void Wake() override { ++wakes; if (on_wake) on_wake(); }
    ControlStatus Inspect(const ControlRequest& request, ControlResult* output) override {
        assert(IsCurrentTaskOwner());
        ++calls;
        last_operation = request.operation;
        last_request = request;
        if (on_inspect) on_inspect();
        *output = sample;
        if (request.operation == ControlOperation::kInput && trace) output->input = trace->Read(request.cursor);
        if (request.operation == ControlOperation::kDisplayTelemetry && display_trace)
            output->display_telemetry = display_trace->Read(request.cursor);
        return status;
    }

    std::thread::id owner_thread = std::this_thread::get_id();
    std::atomic<unsigned> wakes{0};
    unsigned calls = 0;
    ControlOperation last_operation = ControlOperation::kSystemInfo;
    ControlRequest last_request{};
    zectrix::input::InputTrace* trace = nullptr;
    zectrix::display::TelemetryRecorder* display_trace = nullptr;
    ControlResult sample;
    ControlStatus status = ControlStatus::kOk;
    std::function<void()> on_inspect;
    std::function<void()> on_wake;
};

Invocation Parse(const char* text) {
    Invocation invocation;
    assert(ParseLine(text, std::strlen(text), &invocation) == ParseStatus::kOk);
    return invocation;
}

std::string Run(DiagnosticExecutor& executor, PlatformControlDispatcher& dispatcher,
                const char* command) {
    BoundedOutput chunk;
    auto status = executor.Execute(Parse(command), &chunk);
    assert(!chunk.truncated());
    std::string text = chunk.data();
    dispatcher.Dispatch();
    for (unsigned poll = 0; status == ExecuteStatus::kPending && poll < 50; ++poll) {
        chunk.Clear();
        status = executor.Poll(&chunk);
        assert(!chunk.truncated());
        if (chunk.size() != 0) text += std::string(chunk.data()) + '\n';
    }
    assert(status == ExecuteStatus::kOk);
    return text;
}

void TestDisplayTelemetryCommands() {
    Owner owner;
    zectrix::display::TelemetryRecorder recorder;
    owner.display_trace = &recorder;
    PlatformControlDispatcher dispatcher(owner, Clock);
    LogBuffer logs;
    DiagnosticExecutor executor(dispatcher, logs);
    assert(Run(executor, dispatcher, "display telemetry").find("frames=0") != std::string::npos);
    zectrix::display::FrameTelemetry frame;
    frame.started_us = UINT64_MAX;
    frame.duration_us = frame.busy_us = frame.refresh_busy_us = frame.spi_bytes = frame.ram_bytes = UINT32_MAX;
    frame.black_to_white = frame.white_to_black = UINT32_MAX;
    frame.window = {0, 0, 400, 300};
    frame.environment = {-4000, UINT16_MAX, UINT32_MAX, UINT32_MAX};
    frame.error = INT32_MIN;
    frame.flags = UINT16_MAX;
    frame.waveform_triggers = UINT8_MAX;
    frame.model_revision = UINT32_MAX;
    frame.projected_mean_q16 = frame.projected_peak_q16 = UINT32_MAX;
    frame.committed_mean_q16 = frame.committed_peak_q16 = frame.energy_uj = UINT32_MAX;
    for (unsigned index = 0; index < 20; ++index) recorder.Record(frame);
    const auto data = Run(executor, dispatcher, "display telemetry 0");
    assert(data.find("next=8 latest=20 lost=4 frames=4") != std::string::npos);
    assert(data.find("frame,5,18446744073709551615") != std::string::npos);
    assert(data.find("env,5,-4000,4294967295,65535,4294967295,0,256") != std::string::npos);
    assert(data.find("debt,8,4294967295,4294967295,4294967295,4294967295,4294967295,4294967295") != std::string::npos);
    assert(Run(executor, dispatcher, "display telemetry 8").find("frame,9,") != std::string::npos);
    assert(Run(executor, dispatcher, "display telemetry 20").find("frames=0") != std::string::npos);
    const auto model = Run(executor, dispatcher, "display model");
    assert(model.find("tiles=5x4") != std::string::npos && model.find("global=49152 local=262144") != std::string::npos);
    assert(model.find("640,512,384,256,256") != std::string::npos && model.find("energy mode=3 calibrated=0") != std::string::npos);
    BoundedOutput output;
    for (const char* command : {"display telemetry -1", "display telemetry 1 2", "display telemetry 18446744073709551616", "display model 1"})
        assert(executor.Execute(Parse(command), &output) == ExecuteStatus::kInvalidArguments);
    assert(executor.Execute(Parse("display telemetry"), &output) == ExecuteStatus::kPending);
    executor.Cancel();
    assert(!dispatcher.Dispatch());
    assert(executor.Execute(Parse("display telemetry"), &output) == ExecuteStatus::kPending);
    dispatcher.Dispatch();
    // Formatting is based on the copied batch, even after the ring overwrites it.
    for (unsigned index = 0; index < 32; ++index) recorder.Record({});
    assert(executor.Poll(&output) == ExecuteStatus::kPending);
    assert(std::string(output.data()).find("latest=20") != std::string::npos);
    executor.Cancel();
}

void TestCommands() {
    now_ms = 0;
    Owner owner;
    std::strcpy(owner.sample.system.firmware.project_name.data(), "note4");
    std::strcpy(owner.sample.system.firmware.version.data(), "1.2.3");
    std::strcpy(owner.sample.system.capabilities.chip_model.data(), "ESP32-S3");
    owner.sample.system.reset_reason = zectrix::system::ResetReason::Watchdog;
    owner.sample.system.wifi_mac = {2, 0x11, 0x22, 0x33, 0x44, 0x55};
    owner.sample.heap.internal = {10000, 8000, 6000, 4000};
    owner.sample.heap.psram = {8000000, 7000000, 5000000, 3000000};
    owner.sample.uptime_us = (86400ULL + 3600 + 120 + 3) * 1000000 + 456000;
    owner.sample.tasks.total = owner.sample.tasks.count = 2;
    owner.sample.tasks.tasks[0] = {1, 3, 1024, zectrix::system::TaskState::kRunning, true};
    owner.sample.tasks.tasks[1] = {2, 5, 2048, zectrix::system::TaskState::kBlocked, false};
    auto& display = owner.sample.display;
    display.bits_per_pixel = 1;
    display.framebuffer_bytes = 15000;
    display.framebuffer_valid = true;
    display.last_refresh = zectrix::display::RefreshKind::kPartial1Bpp;
    display.state.baseline = zectrix::display::BaselineState::Valid1Bpp;
    display.state.partial_refresh_count = 4;
    display.state.partial_changed_pixels = 48000;
    display.state.has_dirty_region = true;
    display.state.dirty_region = {8, 16, 24, 32};
    for (std::size_t index = 0; index < display.preview.size(); ++index) display.preview[index] = index;

    PlatformControlDispatcher dispatcher(owner, Clock);
    LogBuffer logs;
    DiagnosticExecutor executor(dispatcher, logs);
    BoundedOutput output;
    assert(executor.Execute(Parse("sysinfo"), &output) == ExecuteStatus::kPending);
    assert(output.size() == 0 && owner.calls == 0 && owner.wakes == 1);
    assert(executor.Poll(&output) == ExecuteStatus::kPending);
    assert(executor.Execute(Parse("heap"), &output) == ExecuteStatus::kBusy);
    std::thread wrong_owner([&] { assert(!dispatcher.Dispatch()); });
    wrong_owner.join();
    assert(owner.calls == 0);
    assert(dispatcher.Dispatch());
    owner.sample.system.firmware.version[0] = '9';
    assert(executor.Poll(&output) == ExecuteStatus::kPending);
    assert(std::string(output.data()).find("version=1.2.3") != std::string::npos);
    executor.Cancel();

    const auto info = Run(executor, dispatcher, "system info");
    assert(info.find("reset=watchdog") != std::string::npos);
    assert(info.find("02:11:22:33:44:55") != std::string::npos);
    for (const char* command : {"heap", "system heap"}) {
        const auto text = Run(executor, dispatcher, command);
        assert(text.find("minimum_free=6000") != std::string::npos);
        assert(text.find("psram heap bytes: total=8000000") != std::string::npos);
    }
    for (const char* command : {"tasks", "system tasks"}) {
        const auto text = Run(executor, dispatcher, command);
        assert(text.find("tasks=2") != std::string::npos);
        assert(text.find("MIN_STACK_BYTES") != std::string::npos);
        assert(text.find("application") != std::string::npos);
        assert(text.find("blocked") != std::string::npos);
    }
    for (const char* command : {"uptime", "system uptime"}) {
        assert(Run(executor, dispatcher, command).find("1 days 01:02:03.456") != std::string::npos);
    }
    for (const char* command : {"epd-inspect", "display status"}) {
        const auto text = Run(executor, dispatcher, command);
        assert(text.find("partial_count=4 ") != std::string::npos);
        assert(text.find("partial_pixels=48000 high_contrast_pixels=30000") != std::string::npos);
        assert(text.find("debt_q16:") != std::string::npos);
        assert(text.find("rect=8,16 24x32") != std::string::npos);
        assert(text.find("0000: 00 01 02") != std::string::npos);
        assert(text.find("0030: 30 31 32") != std::string::npos);
    }
    display.framebuffer_valid = false;
    const auto invalid_frame = Run(executor, dispatcher, "epd-inspect");
    assert(invalid_frame.find("preview unavailable") != std::string::npos);
    assert(invalid_frame.find("0000:") == std::string::npos);
    owner.sample.tasks = {};
    owner.sample.tasks.total = 40;
    owner.sample.tasks.capacity_exceeded = true;
    assert(Run(executor, dispatcher, "tasks").find("capacity exceeded") != std::string::npos);

    const auto calls = owner.calls;
    assert(Run(executor, dispatcher, "help").find("log-stream") != std::string::npos);
    assert(Run(executor, dispatcher, "help system").find("system <info|heap|tasks|uptime>") != std::string::npos);
    assert(Run(executor, dispatcher, "help log follow").find("[error|warn|info|debug]") != std::string::npos);
    assert(Run(executor, dispatcher, "version").find("D1.4") != std::string::npos);
    assert(Run(executor, dispatcher, "log stats").find("queued=0/32") != std::string::npos);
    assert(owner.calls == calls);

    for (const char* command : {"sysinfo extra", "system", "heap x", "tasks 1",
                                 "uptime now", "epd-inspect 0", "log-stream nope",
                                 "log follow info extra", "version extra"}) {
        output.Clear();
        assert(executor.Execute(Parse(command), &output) == ExecuteStatus::kInvalidArguments);
    }
    assert(executor.Execute(Parse("missing"), &output) == ExecuteStatus::kUnknownCommand);
    owner.status = ControlStatus::kUnavailable;
    assert(executor.Execute(Parse("heap"), &output) == ExecuteStatus::kPending);
    assert(dispatcher.Dispatch());
    assert(executor.Poll(&output) == ExecuteStatus::kUnavailable);
}

void TestDispatcherLifetime() {
    now_ms = 0;
    Owner owner;
    PlatformControlDispatcher dispatcher(owner, Clock);
    ControlTicket first, second;
    ControlResult result;
    assert(dispatcher.Submit({}, nullptr) == ControlStatus::kInvalidArgument);
    assert(dispatcher.Submit({}, &first) == ControlStatus::kOk);
    assert(dispatcher.Submit({}, &second) == ControlStatus::kQueueFull);
    dispatcher.Cancel(first);
    assert(!dispatcher.Dispatch() && owner.calls == 0);
    assert(dispatcher.Submit({}, &second) == ControlStatus::kOk);
    assert(second.id != first.id && second.generation != first.generation);
    auto stale_generation = second;
    --stale_generation.generation;
    assert(dispatcher.Take(stale_generation, &result) == ControlStatus::kUnavailable);
    dispatcher.Cancel(first);
    assert(dispatcher.Take(first, &result) == ControlStatus::kUnavailable);
    assert(dispatcher.Dispatch());
    assert(dispatcher.Take(second, &result) == ControlStatus::kOk);

    assert(dispatcher.Submit({}, &first) == ControlStatus::kOk);
    now_ms += kOwnerRequestTimeoutMs;
    assert(dispatcher.Take(first, &result) == ControlStatus::kTimeout);
    assert(!dispatcher.Dispatch());
    assert(dispatcher.Submit({}, &second) == ControlStatus::kOk);
    dispatcher.Shutdown();
    assert(dispatcher.Take(second, &result) == ControlStatus::kUnavailable);
    assert(dispatcher.Submit({}, &first) == ControlStatus::kUnavailable);
}

void TestNonblockingOwnerRetry() {
    Owner owner;
    PlatformControlDispatcher dispatcher(owner, Clock);
    std::mutex mutex;
    std::condition_variable changed;
    bool woke = false, release = false;
    // Model a sender preempted just after waking the owner, while it still
    // holds the slot lock. The owner must retain work without waiting on it.
    owner.on_wake = [&] {
        std::unique_lock<std::mutex> lock(mutex);
        woke = true;
        changed.notify_all();
        changed.wait(lock, [&] { return release; });
    };
    ControlTicket ticket;
    std::thread submitter([&] { assert(dispatcher.Submit({}, &ticket) == ControlStatus::kOk); });
    {
        std::unique_lock<std::mutex> lock(mutex);
        changed.wait(lock, [&] { return woke; });
    }
    assert(!dispatcher.Dispatch() && owner.calls == 0);
    {
        std::lock_guard<std::mutex> lock(mutex);
        release = true;
        changed.notify_all();
    }
    submitter.join();
    assert(dispatcher.Dispatch() && owner.calls == 1);
    ControlResult result;
    assert(dispatcher.Take(ticket, &result) == ControlStatus::kOk);
}

void TestExecutingCancellationAndShutdown(bool timeout, bool shutdown, bool mutation = false) {
    now_ms = 0;
    Owner owner;
    PlatformControlDispatcher dispatcher(owner, Clock);
    std::mutex mutex;
    std::condition_variable condition;
    bool entered = false;
    bool release = false;
    owner.on_inspect = [&] {
        std::unique_lock<std::mutex> lock(mutex);
        entered = true;
        condition.notify_all();
        condition.wait(lock, [&] { return release; });
    };
    ControlTicket ticket, next;
    ControlResult result;
    ControlRequest request;
    if (mutation) { request.operation = ControlOperation::kTimeSync; request.confirmed = true; }
    assert(dispatcher.Submit(request, &ticket) == ControlStatus::kOk);
    std::thread worker([&] {
        owner.owner_thread = std::this_thread::get_id();
        assert(dispatcher.Dispatch());
    });
    {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock, [&] { return entered; });
    }
    if (timeout) {
        now_ms += kOwnerRequestTimeoutMs;
        assert(dispatcher.Take(ticket, &result) == (mutation ? ControlStatus::kUnknownOutcome : ControlStatus::kTimeout));
    } else if (!shutdown) {
        assert(dispatcher.Cancel(ticket) == (mutation ? ControlStatus::kUnknownOutcome : ControlStatus::kCancelledBeforeStart));
    }
    assert(dispatcher.Submit({}, &next) == ControlStatus::kQueueFull);
    std::atomic<bool> stopped{false};
    std::thread stopper;
    if (shutdown) {
        stopper = std::thread([&] { dispatcher.Shutdown(); stopped = true; });
        // Observe closed admission before allowing owner execution to finish.
        // This proves shutdown waits, rather than merely racing a fast read.
        while (dispatcher.Submit({}, &next) == ControlStatus::kQueueFull) {
            std::this_thread::yield();
        }
        assert(dispatcher.Submit({}, &next) == ControlStatus::kUnavailable);
    }
    assert(!stopped.load());
    {
        std::lock_guard<std::mutex> lock(mutex);
        release = true;
        condition.notify_all();
    }
    worker.join();
    if (shutdown) {
        stopper.join();
        assert(stopped.load());
        assert(dispatcher.Take(ticket, &result) == (mutation ? ControlStatus::kUnknownOutcome : ControlStatus::kUnavailable));
    } else {
        assert(dispatcher.Take(ticket, &result) == ControlStatus::kUnavailable);
        owner.owner_thread = std::this_thread::get_id();
        assert(dispatcher.Submit({}, &next) == ControlStatus::kOk);
        assert(dispatcher.Dispatch());
        assert(dispatcher.Take(ticket, &result) == ControlStatus::kUnavailable);
        assert(dispatcher.Take(next, &result) == ControlStatus::kOk);
    }
}

std::string Confirmation(DiagnosticExecutor& executor, const char* command) {
    BoundedOutput output;
    assert(executor.Execute(Parse(command), &output) == ExecuteStatus::kOk);
    assert(!output.truncated());
    const std::string text = output.data();
    const auto start = text.find("Type confirm ");
    assert(start != std::string::npos);
    const auto value = start + std::strlen("Type ");
    return text.substr(value, text.find(" within", value) - value);
}

void TestReflectionAndConfirmation() {
    now_ms = 0;
    Owner owner;
    PlatformControlDispatcher dispatcher(owner, Clock);
    LogBuffer logs;
    DiagnosticExecutor executor(dispatcher, logs, nullptr, Clock);
    owner.sample.power = {250, 3890, 71, true, false, false, false, false, false};
    owner.sample.time.local = {2024, 2, 29, 12, 0, 0};
    owner.sample.time.sync.source = zectrix::time::SyncSource::Companion;
    owner.sample.apps.count = 16;
    std::strcpy(owner.sample.apps.foreground.data(), "micro-apps");
    std::strcpy(owner.sample.apps.entries[15].id.data(), "last-native-app");
    owner.sample.scenes.depth = 8;
    owner.sample.scenes.scenes[7] = {42, 7};
    owner.sample.scenes.guest = true;
    owner.sample.scenes.heap_limit = 131072;
    owner.sample.scenes.instruction_limit = 10000;
    assert(Run(executor, dispatcher, "power status").find("mv=3890") != std::string::npos);
    assert(Run(executor, dispatcher, "time get").find("2024-02-29") != std::string::npos);
    assert(Run(executor, dispatcher, "time sync").find("source=companion") != std::string::npos);
    assert(Run(executor, dispatcher, "connectivity status").find("peer_authorized=0") != std::string::npos);
    assert(Run(executor, dispatcher, "app list").find("last-native-app") != std::string::npos);
    assert(Run(executor, dispatcher, "app current").find("foreground=micro-apps") != std::string::npos);
    const auto scenes = Run(executor, dispatcher, "scene dump");
    assert(scenes.find("scene[7] id=7 state=42 current") != std::string::npos);
    assert(scenes.find("view[3]") != std::string::npos && scenes.find("limit=131072") != std::string::npos);
    BoundedOutput output;
    for (const char* invalid : {"sleep now", "storage wipe all", "time sync 123", "time sync 1709179200000 50401",
             "time sync 1709179200000x 0", "time sync 99999999999999999999999999999 0", "time sync 0 0"}) {
        output.Clear();
        assert(executor.Execute(Parse(invalid), &output) == ExecuteStatus::kInvalidArguments);
        assert(!dispatcher.Dispatch());
    }
    ControlTicket ticket;
    ControlRequest request;
    request.operation = ControlOperation::kFactoryReset;
    assert(dispatcher.Submit(request, &ticket) == ControlStatus::kDenied);
    request.confirmed = true;
    request.origin = Origin::kAuthorizedCompanion;
    assert(dispatcher.Submit(request, &ticket) == ControlStatus::kDenied);

    auto confirmation = Confirmation(executor, "factory reset");
    assert(!dispatcher.Dispatch());
    output.Clear();
    assert(executor.Execute(Parse("confirm 99999"), &output) == ExecuteStatus::kDenied);
    assert(executor.Execute(Parse(confirmation.c_str()), &output) == ExecuteStatus::kDenied);
    confirmation = Confirmation(executor, "reboot");
    now_ms += 15000;
    assert(executor.Execute(Parse(confirmation.c_str()), &output) == ExecuteStatus::kDenied);
    confirmation = Confirmation(executor, "sleep");
    Run(executor, dispatcher, "version");
    assert(executor.Execute(Parse(confirmation.c_str()), &output) == ExecuteStatus::kDenied);
    confirmation = Confirmation(executor, "storage wipe");
    executor.Cancel();
    assert(executor.Execute(Parse(confirmation.c_str()), &output) == ExecuteStatus::kDenied);
    confirmation = Confirmation(executor, "time sync 1709179200123 28800");
    assert(executor.Execute(Parse(confirmation.c_str()), &output) == ExecuteStatus::kPending);
    assert(executor.CancelStatus() == ExecuteStatus::kOk && !dispatcher.Dispatch());

    confirmation = Confirmation(executor, "time sync 1709179200123 28800");
    const auto calls = owner.calls;
    Run(executor, dispatcher, confirmation.c_str());
    assert(owner.calls == calls + 1 && owner.last_operation == ControlOperation::kTimeSync);
    assert(owner.last_request.unix_ms == 1709179200123 && owner.last_request.offset_seconds == 28800);
    assert(owner.last_request.confirmed && owner.last_request.origin == Origin::kUsbLocal);
    assert(executor.Execute(Parse(confirmation.c_str()), &output) == ExecuteStatus::kDenied);
    for (const auto* command : {"reboot", "sleep", "storage wipe", "factory reset"}) {
        confirmation = Confirmation(executor, command);
        assert(Run(executor, dispatcher, confirmation.c_str()).find("Accepted") != std::string::npos);
    }
    confirmation = Confirmation(executor, "reboot");
    assert(executor.Execute(Parse(confirmation.c_str()), &output) == ExecuteStatus::kPending);
    assert(dispatcher.Dispatch());
    assert(executor.CancelStatus() == ExecuteStatus::kUnknownOutcome);
    assert(!dispatcher.Dispatch());
}

void TestInputStream() {
    now_ms = 0;
    Owner owner;
    zectrix::input::InputTrace trace;
    owner.trace = &trace;
    PlatformControlDispatcher dispatcher(owner, Clock);
    LogBuffer logs;
    DiagnosticExecutor executor(dispatcher, logs, nullptr, Clock);
    BoundedOutput output;
    assert(executor.Execute(Parse("input watch"), &output) == ExecuteStatus::kPending);
    assert(dispatcher.Dispatch());
    assert(executor.Poll(&output) == ExecuteStatus::kPending && executor.streaming());
    for (unsigned i = 0; i < 40; ++i) trace.Push(i * 1000, i % 3, i % 2, true);
    std::string text;
    for (int i = 0; i < 40; ++i) {
        now_ms += 50;
        output.Clear();
        assert(executor.Poll(&output) == ExecuteStatus::kPending);
        assert(!output.truncated());
        text += output.data();
        dispatcher.Dispatch();
    }
    assert(text.find("lost=24") != std::string::npos);
    assert(text.find("seq=25") != std::string::npos && text.find("seq=40") != std::string::npos);
    assert(trace.Read(1).lost == 24 && trace.Read(1).records[0].sequence == 25);
    executor.Cancel();
    assert(!executor.streaming() && !dispatcher.Dispatch());
}

void TestLogs() {
    Owner owner;
    PlatformControlDispatcher dispatcher(owner, Clock);
    LogBuffer logs;
    DiagnosticExecutor executor(dispatcher, logs);
    for (unsigned index = 0; index < 40; ++index) {
        logs.Push(LogLevel::kInfo, ("I log " + std::to_string(index)).c_str());
    }
    auto stats = logs.Stats();
    assert(stats.queued == 32 && stats.dropped == 8);
    LogRecord record;
    assert(logs.Pop(&record) && std::string(record.text.data()) == "I log 8");
    while (logs.Pop(&record)) {}
    logs.Push(LogLevel::kInfo, std::string(400, 'x').c_str());
    assert(logs.Pop(&record));
    assert(std::string(record.text.data()).find("[truncated]") != std::string::npos);
    assert(logs.Stats().truncated == 1);
    assert(DetectLogLevel("\x1b[0;31mE (42) test") == LogLevel::kError);
    assert(DetectLogLevel("W (42) test") == LogLevel::kWarn);
    assert(DetectLogLevel("\x1b[") == LogLevel::kInfo);

    BoundedOutput output;
    assert(executor.Execute(Parse("log-stream warn"), &output) == ExecuteStatus::kPending);
    assert(owner.calls == 0);
    output.Clear();
    assert(executor.Poll(&output) == ExecuteStatus::kPending);
    assert(std::string(output.data()).find("dropped=8") != std::string::npos);
    logs.Push(LogLevel::kDebug, "debug hidden");
    logs.Push(LogLevel::kInfo, "info hidden");
    logs.Push(LogLevel::kWarn, "warning visible\n");
    output.Clear();
    assert(executor.Poll(&output) == ExecuteStatus::kPending);
    assert(std::string(output.data()) == "warning visible");
    executor.Cancel();
    output.Clear();
    assert(executor.Poll(&output) == ExecuteStatus::kOk && output.size() == 0);
    assert(executor.Execute(Parse("log follow error"), &output) == ExecuteStatus::kPending);
    executor.Cancel();

    LogBuffer concurrent;
    std::thread producer1([&] { for (int i = 0; i < 1000; ++i) concurrent.Push(LogLevel::kInfo, "one"); });
    std::thread producer2([&] { for (int i = 0; i < 1000; ++i) concurrent.Push(LogLevel::kError, "two"); });
    producer1.join();
    producer2.join();
    stats = concurrent.Stats();
    assert(stats.queued <= kLogRecords && stats.queued + stats.dropped == 2000);
}

class Transport final : public CliTransport {
public:
    bool IsConnected() const override { return connected; }
    std::size_t Read(uint8_t* bytes, std::size_t capacity) override {
        const std::size_t count = std::min(capacity, input.size());
        for (std::size_t index = 0; index < count; ++index) {
            bytes[index] = input.front();
            input.pop_front();
        }
        return count;
    }
    bool Write(const char* bytes, std::size_t size) override {
        if (fail_write) return false;
        output.append(bytes, size);
        return true;
    }
    void Send(const char* text) { while (*text != '\0') input.push_back(*text++); }
    void DiscardInput() override { input.clear(); }
    bool connected = true;
    bool fail_write = false;
    std::deque<uint8_t> input;
    std::string output;
};

void TestAsyncSession() {
    now_ms = 0;
    Owner owner;
    PlatformControlDispatcher dispatcher(owner, Clock);
    LogBuffer logs;
    DiagnosticExecutor executor(dispatcher, logs);
    Transport transport;
    CliSession session(transport, executor);
    session.Poll();
    transport.Send("heap\r");
    session.Poll();
    assert(session.command_active() && owner.calls == 0);
    transport.Send("uptime\r");
    session.Poll();
    assert(owner.wakes == 1);
    transport.Send("\x1b[\x03");
    session.Poll();
    assert(!session.command_active() && !dispatcher.Dispatch());
    assert(transport.output.find("^C\r\nzectrix> ") != std::string::npos);

    transport.Send("uptime\r");
    session.Poll();
    now_ms += kOwnerRequestTimeoutMs;
    session.Poll();
    assert(!session.command_active());
    assert(transport.output.find("owner request timed out\r\nzectrix> ") != std::string::npos);

    transport.Send("log-stream\r");
    session.Poll();
    logs.Push(LogLevel::kInfo, "I streamed\n");
    session.Poll();
    assert(transport.output.find("I streamed\r\n") != std::string::npos);
    transport.connected = false;
    session.Poll();
    assert(!session.command_active());
    transport.connected = true;
    session.Poll();
    assert(transport.output.rfind("zectrix> ") == transport.output.size() - 9);

    transport.Send("heap\r");
    session.Poll();
    transport.connected = false;
    session.Poll();
    assert(!dispatcher.Dispatch());
    transport.connected = true;
    session.Poll();
    transport.Send("heap\r");
    session.Poll();
    assert(dispatcher.Dispatch());
    transport.fail_write = true;
    session.Poll();
    assert(!session.connected() && !session.command_active());
    transport.fail_write = false;
    session.Poll();
    transport.Send("version\r");
    session.Poll();
    assert(transport.output.find("D1.4\r\nzectrix> ") != std::string::npos);
    transport.Send("reboot\r");
    session.Poll();
    assert(transport.output.find("Type confirm 1") != std::string::npos && !dispatcher.Dispatch());
    transport.connected = false;
    session.Poll();
    transport.connected = true;
    session.Poll();
    transport.Send("confirm 1\r");
    session.Poll();
    assert(transport.output.find("confirmation denied") != std::string::npos && !dispatcher.Dispatch());
    transport.Send("storage wipe\r");
    session.Poll();
    assert(transport.output.find("Type confirm 2") != std::string::npos);
    transport.Send("\x03");
    session.Poll();
    transport.Send("confirm 2\r");
    session.Poll();
    assert(!dispatcher.Dispatch());
    session.Reset();
}

}  // namespace

int main() {
    TestCommands();
    TestDisplayTelemetryCommands();
    TestDispatcherLifetime();
    TestNonblockingOwnerRetry();
    TestExecutingCancellationAndShutdown(false, false);
    TestExecutingCancellationAndShutdown(true, false);
    TestExecutingCancellationAndShutdown(false, true);
    TestExecutingCancellationAndShutdown(false, false, true);
    TestExecutingCancellationAndShutdown(true, false, true);
    TestExecutingCancellationAndShutdown(false, true, true);
    TestReflectionAndConfirmation();
    TestInputStream();
    TestLogs();
    TestAsyncSession();
}
