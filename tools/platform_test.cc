#include "zectrix_platform.h"
#include <cstring>
#include "sdkconfig.h"
#include "zectrix_boot_esp.h"
#include "zectrix_health_esp.h"
#include "zectrix_board.h"
#if CONFIG_ZECTRIX_ENABLE_USB_HOST
#include "zectrix_host_channel.h"
#endif

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <new>
#include <string>
#include <vector>

#include "zectrix_display_service.h"
#if CONFIG_ZECTRIX_ENABLE_USB_CLI
#include "zectrix_cli_usb.h"
#include "zectrix_cli_diagnostics.h"
#endif
namespace zectrix::cli { class CliUsbService; }
#include "freertos/task.h"
#if CONFIG_ZECTRIX_ENABLE_CONNECTIVITY
#include "zectrix_connectivity_service.h"
#include "zectrix_nfc_service.h"
#endif
#include "zectrix_input_service.h"
#include "zectrix_power_service.h"
#include "zectrix_storage_service.h"
#include "zectrix_system_service.h"
#include "zectrix_time_service.h"
#if CONFIG_ZECTRIX_ENABLE_UPDATE
#include "zectrix_update_esp.h"
#endif
#include "update_test_fixture.h"

class ZectrixNfc {};

namespace {
struct SleepEntered {};
std::vector<std::string> events;
std::string fail_at;
int nothrow_allocation_count = 0;
int fail_nothrow_allocation = 0;
#if CONFIG_ZECTRIX_ENABLE_USB_CLI
zectrix::cli::CliExecutor* cli_executor = nullptr;
#endif
unsigned inspections = 0;
unsigned time_polls = 0;
#if CONFIG_ZECTRIX_ENABLE_CONNECTIVITY
bool pending_clock_sample = false;
#endif
int64_t applied_clock_ms = 0;
bool boot_watchdog_armed = false;
uint32_t watchdog_timeout_ms = 0;
uint64_t health_now_ms = 0;
unsigned health_feeds = 0;
zectrix::system::ResetReason reset_reason = zectrix::system::ResetReason::PowerOn;
bool pending_boot = false;
bool boot_confirmed = false;
unsigned boot_probe_count = 0;
const zectrix::ServiceRegistry* inspected_registry = nullptr;

template <typename Interface>
void AssertWithdrawn() {
    if (inspected_registry) assert(!inspected_registry->Get<Interface>());
}

void AssertNoServices(const zectrix::Platform& platform) {
    const auto& registry = platform.Services();
    assert(!registry.Get<zectrix::display::DisplayService>());
    assert(!registry.Get<zectrix::input::InputService>());
    assert(!registry.Get<zectrix::power::PowerService>());
    assert(!registry.Get<zectrix::time::TimeService>());
    assert(!registry.Get<zectrix::storage::StorageService>());
    assert(!registry.Get<zectrix::system::SystemService>());
    assert(!registry.Get<zectrix::connectivity::ConnectivityService>());
    assert(!registry.Get<zectrix::update::UpdateService>());
    assert(!registry.Get<zectrix::update::BootGuard>());
    assert(!registry.Get<zectrix::cli::CliUsbService>());
    assert(!registry.Get<ZectrixSelfTest>());
}
esp_err_t Result(const char* name) {
    events.emplace_back(std::string("create:") + name);
    return fail_at == name ? ESP_FAIL : ESP_OK;
}
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    ++nothrow_allocation_count;
    if (nothrow_allocation_count == fail_nothrow_allocation) return nullptr;
    try { return ::operator new(size); } catch (const std::bad_alloc&) { return nullptr; }
}

void operator delete(void* pointer, const std::nothrow_t&) noexcept {
    ::operator delete(pointer);
}

const char* esp_err_to_name(esp_err_t) { return "host-error"; }

esp_err_t ZectrixBoard::Init() {
    assert(boot_probe_count != 0);
    if (inspected_registry) assert(inspected_registry->Get<zectrix::update::BootGuard>());
    events.emplace_back("init:board");
    return init_result;
}

namespace zectrix::update {
#if CONFIG_ZECTRIX_ENABLE_UPDATE
EspUpdateBackend::~EspUpdateBackend() { AbortImage(); }
#endif
Result EspBootBackend::ReadBootInfo(BootInfo* info) {
    ++boot_probe_count;
    if (fail_at == "boot") return Result::kIoError;
    *info = UpdateBootFixture(pending_boot ? PartitionKind::kOtaA : PartitionKind::kFactory);
    if (pending_boot && !boot_confirmed) info->image_state = ImageState::kPendingVerify;
    return Result::kOk;
}
uint64_t EspBootBackend::Milliseconds() const { return health_now_ms; }
Result EspBootBackend::ArmBootWatchdog(uint32_t timeout_ms) {
    assert(timeout_ms == kBootConfirmationTimeoutMs);
    boot_watchdog_armed = true;
    watchdog_timeout_ms = timeout_ms;
    return Result::kOk;
}
void EspBootBackend::DisarmBootWatchdog() { boot_watchdog_armed = false; }
Result EspBootBackend::ConfirmRunningImage(const Partition&) {
    if (fail_at == "confirm") return Result::kIoError;
    boot_confirmed = true;
    return Result::kOk;
}
#if CONFIG_ZECTRIX_ENABLE_UPDATE
Result EspUpdateBackend::BeginImage(const Partition&, uint32_t, const uint8_t*, std::size_t) {
    return Result::kInvalidState;
}
Result EspUpdateBackend::WriteImage(const uint8_t*, std::size_t) { return Result::kInvalidState; }
Result EspUpdateBackend::CommitImage(uint32_t) { return Result::kInvalidState; }
void EspUpdateBackend::AbortImage() {}
#endif
}

[[noreturn]] void esp_restart() { events.emplace_back("reboot"); throw SleepEntered{}; }

namespace zectrix::input {
esp_err_t InputService::Attach(ZectrixBoard& board, InputService** output) {
    const esp_err_t result = Result("input");
    if (result == ESP_OK) *output = new InputService(board);
    return result;
}
InputService::~InputService() { AssertWithdrawn<InputService>(); events.emplace_back("delete:input"); }
void InputService::SetWaitHook(WaitHook hook, void* context) {
    wait_hook_ = hook;
    wait_context_ = context;
}
void InputService::WakeWait() { board_->WakeButtonWait(); }
TraceBatch InputService::ReadTrace(uint64_t cursor) const { TraceBatch result; result.cursor = cursor ? cursor : 1; return result; }
}
namespace zectrix::power {
esp_err_t PowerService::Attach(ZectrixBoard& board, PowerService** output) {
    const esp_err_t result = Result("power");
    if (result == ESP_OK) *output = new PowerService(board);
    return result;
}
PowerSnapshot PowerService::ReadSnapshot() const {
    cached_ = {true, 3888, 70, false, false, false, false, false};
    sampled_us_ = 1000000;
    return cached_;
}
PowerService::~PowerService() { AssertWithdrawn<PowerService>(); events.emplace_back("delete:power"); }
[[noreturn]] void PowerService::Shutdown(void (*ready)(void*), void* context) {
    AssertWithdrawn<PowerService>();
    assert(boot_watchdog_armed);
    if (ready != nullptr) ready(context);
    assert(!boot_watchdog_armed);
    events.emplace_back("shutdown:power");
    throw SleepEntered{};
}
}
namespace zectrix::time {
esp_err_t TimeService::Attach(ZectrixBoard& board, TimeService** output) {
    const esp_err_t result = Result("time");
    if (result == ESP_OK) *output = new TimeService(board);
    return result;
}
TimeService::~TimeService() { AssertWithdrawn<TimeService>(); events.emplace_back("delete:time"); }
esp_err_t TimeService::Initialize(storage::StorageService& storage) {
    if (inspected_registry) assert(inspected_registry->Get<storage::StorageService>() == &storage);
    events.emplace_back("init:time");
    return fail_at == "rtc-restore" ? ESP_FAIL : ESP_OK;
}
void TimeService::Poll() { ++time_polls; }
ClockSnapshot TimeService::Now() const { return {{2024, 2, 29, 4, 12, 0, 0}, ClockSource::System}; }
int64_t TimeService::UnixSeconds() const { return 1709179200; }
bool TimeService::RtcAvailable() const { return true; }
SyncResult TimeService::ApplySample(const TimeSample& sample) {
    assert(sample.source == SyncSource::Companion && sample.received_us == 1234000 && sample.has_offset);
    SetUnixTime(sample.unix_ms, sample.utc_offset_seconds);
    return SyncResult::Applied;
}
esp_err_t TimeService::SetUnixTime(int64_t milliseconds, int32_t offset) {
    assert(host_current_task == reinterpret_cast<void*>(1) && offset == 28800);
    applied_clock_ms = milliseconds;
    return ESP_OK;
}
int64_t TimeService::MonotonicMicroseconds() const { ++inspections; return 1234000; }
}
namespace zectrix::storage {
struct StorageService::Impl {};
esp_err_t StorageService::Create(StorageService** output) {
    const esp_err_t result = Result("storage");
    if (result == ESP_OK) *output = new StorageService(new Impl);
    return result;
}
esp_err_t StorageService::Initialize() { return Result("storage-init"); }
esp_err_t StorageService::WipeUserFiles() { events.emplace_back("wipe:files"); return fail_at == "wipe" ? ESP_FAIL : ESP_OK; }
esp_err_t StorageService::ResetSettings() { events.emplace_back("wipe:settings"); return ESP_OK; }
StorageService::~StorageService() {
    AssertWithdrawn<StorageService>();
    delete impl_;
    events.emplace_back("delete:storage");
}
}
namespace zectrix::system {
uint64_t EspHealthWatchdog::Milliseconds() const { return health_now_ms; }
esp_err_t EspHealthWatchdog::Arm(uint32_t timeout_ms) {
    assert(!boot_watchdog_armed && timeout_ms == kForegroundWatchdogMs);
    if (fail_at == "health") return ESP_FAIL;
    boot_watchdog_armed = true;
    watchdog_timeout_ms = timeout_ms;
    return ESP_OK;
}
void EspHealthWatchdog::Feed() { assert(boot_watchdog_armed); ++health_feeds; }
void EspHealthWatchdog::Disarm() { boot_watchdog_armed = false; }
esp_err_t SystemService::Attach(ZectrixBoard& board, SystemService** output) {
    const esp_err_t result = Result("system");
    if (result == ESP_OK) *output = new SystemService(board);
    return result;
}
SystemService::~SystemService() { AssertWithdrawn<SystemService>(); events.emplace_back("delete:system"); }
esp_err_t SystemService::ReadSnapshot(SystemSnapshot* result) const {
    ++inspections;
    *result = {};
    result->reset_reason = reset_reason;
    return ESP_OK;
}
esp_err_t SystemService::ReadHeap(HeapSnapshot* result) const {
    ++inspections;
    *result = {};
    return ESP_OK;
}
esp_err_t SystemService::ReadTasks(TaskSnapshot* result) const {
    ++inspections;
    *result = {};
    return ESP_OK;
}
}
namespace zectrix::display {
esp_err_t DisplayService::Create(DisplayService** output) {
    const esp_err_t result = Result("display");
    if (result == ESP_OK) *output = new DisplayService(nullptr);
    return result;
}
DisplayService::~DisplayService() { AssertWithdrawn<DisplayService>(); events.emplace_back("delete:display"); }
esp_err_t DisplayService::ReadInspection(DisplayInspection* result) const {
    ++inspections;
    *result = {};
    return ESP_OK;
}
}
#if CONFIG_ZECTRIX_ENABLE_CONNECTIVITY
namespace zectrix::nfc {
esp_err_t NfcService::Attach(ZectrixNfc& nfc, NfcService** output) {
    events.emplace_back("create:nfc");
    *output = new NfcService(nfc);
    return ESP_OK;
}
NfcService::~NfcService() { events.emplace_back("delete:nfc"); }
}

namespace zectrix::connectivity {
struct ConnectivityService::Impl {};
void ConnectivityService::SetNfcService(nfc::NfcService*) {}
void ConnectivityService::SetStorageService(storage::StorageService*) {}
ConnectivityResult ConnectivityService::Create(ConnectivityService** output) {
    events.emplace_back("create:connectivity");
    if (fail_at == "connectivity") return ConnectivityResult::kUnavailable;
    *output = new ConnectivityService(new Impl);
    return ConnectivityResult::kOk;
}
ConnectivityResult ConnectivityService::Initialize() {
    assert(std::find(events.begin(), events.end(), "init:time") != events.end());
    events.emplace_back("create:connectivity-init");
    return fail_at == "connectivity-init"
               ? ConnectivityResult::kTransportError
               : ConnectivityResult::kOk;
}
bool ConnectivityService::TakeClockSample(companion::ClockSample* sample) {
    if (!pending_clock_sample) return false;
    pending_clock_sample = false;
    *sample = {1709179200123, 28800};
    return true;
}
ConnectivityResult ConnectivityService::Stop() {
    events.emplace_back("stop:connectivity");
    return fail_at == "stop-connectivity" ? ConnectivityResult::kBusy : ConnectivityResult::kOk;
}
bool ConnectivityService::TakeNetworkClockSample(time::TimeSample*) { return false; }
bool ConnectivityService::TrySnapshot(ConnectivitySnapshot* snapshot) const {
    *snapshot = {};
    std::strcpy(snapshot->wifi.ssid.data(), "bad\x1bssid");
    return fail_at != "snapshot-busy";
}
ConnectivityService::~ConnectivityService() {
    AssertWithdrawn<ConnectivityService>();
    delete impl_;
    events.emplace_back("delete:connectivity");
}
}

#endif

#if CONFIG_ZECTRIX_ENABLE_USB_CLI
namespace zectrix::cli {
CliUsbService::CliUsbService() = default;
CliUsbService::~CliUsbService() { AssertWithdrawn<CliUsbService>(); events.emplace_back("delete:cli"); }
esp_err_t CliUsbService::Start(CliExecutor* executor) {
    assert(executor != nullptr);
    assert((executor->BinarySession() != nullptr) == (CONFIG_ZECTRIX_ENABLE_USB_HOST != 0));
    cli_executor = executor;
    return Result("cli");
}
void CliUsbService::Stop() {}
bool CliUsbService::running() const { return true; }
LogBuffer& MaintenanceLogs() { static LogBuffer logs; return logs; }
}

#endif

void TestMaintenanceReset() {
    for (const char* failure : {"", "wipe", "stop-connectivity"}) {
        fail_at.clear();
        zectrix::Platform platform;
        assert(platform.Initialize() == ESP_OK);
        events.clear();
        fail_at = failure;
        const auto result = platform.ResetUserData(true);
#if CONFIG_ZECTRIX_ENABLE_CONNECTIVITY
        assert(events[0] == "stop:connectivity");
        if (fail_at == "stop-connectivity") {
            assert(result == ESP_ERR_INVALID_STATE && events.size() == 1);
        } else
#endif
        {
            assert(std::find(events.begin(), events.end(), "wipe:files") != events.end());
            assert((std::find(events.begin(), events.end(), "wipe:settings") != events.end()) == (fail_at != "wipe"));
            assert(result == (fail_at == "wipe" ? ESP_FAIL : ESP_OK));
        }
        fail_at.clear();
        try { platform.Reboot(); } catch (const SleepEntered&) {}
        assert(events.back() == "reboot" && !platform.IsInitialized());
        AssertNoServices(platform);
    }
    events.clear();
}

void TestUnsetClockDoesNotBlockStartup() {
    fail_at = "rtc-restore";
    {
        zectrix::Platform platform;
        assert(platform.Initialize() == ESP_OK);
        assert(platform.Services().Get<zectrix::time::TimeService>());
        platform.Poll();
    }
    fail_at.clear();
    events.clear();
    time_polls = 0;
}

void TestDegradedStorageAndHealth() {
    fail_at = "storage-init";
    reset_reason = zectrix::system::ResetReason::Watchdog;
    {
        zectrix::Platform platform;
        assert(platform.Initialize() == ESP_OK);
        const auto health = platform.Health().Snapshot();
        assert(health.storage_error == ESP_FAIL && health.recovery_boot && health.watchdog_armed);
        assert(!platform.Health().AutomaticAppsAllowed());
        assert(platform.Services().Get<zectrix::storage::StorageService>());
        assert(std::count(events.begin(), events.end(), "create:connectivity-init") == 0);
        assert((platform.Services().Get<zectrix::connectivity::ConnectivityService>() != nullptr) ==
               (CONFIG_ZECTRIX_ENABLE_CONNECTIVITY != 0));
        const auto feeds = health_feeds;
        platform.Poll();
        platform.PollMaintenance();
        assert(health_feeds == feeds);
        assert(platform.Health().CompleteForeground(0, 1) == false && health_feeds == feeds + 1);
        assert(platform.ConfirmBoot() == zectrix::update::Result::kOk);
        assert(boot_watchdog_armed && watchdog_timeout_ms == zectrix::system::kForegroundWatchdogMs);
        try { platform.Shutdown(); } catch (const SleepEntered&) {}
        assert(!boot_watchdog_armed);
    }
    fail_at.clear();
    for (const auto reason : {zectrix::system::ResetReason::Panic, zectrix::system::ResetReason::Watchdog}) {
        events.clear();
        reset_reason = reason;
        zectrix::Platform platform;
        assert(platform.Initialize() == ESP_OK);
        assert(platform.Health().Snapshot().storage_error == ESP_OK);
        assert(platform.Health().Snapshot().recovery_boot && !platform.Health().AutomaticAppsAllowed());
        assert(std::count(events.begin(), events.end(), "create:connectivity-init") == 0);
    }
    fail_at = "health";
    {
        zectrix::Platform platform;
        assert(platform.Initialize() == ESP_FAIL);
        AssertNoServices(platform);
    }
    fail_at = "confirm";
    pending_boot = true;
    {
        zectrix::Platform platform;
        assert(platform.Initialize() == ESP_OK);
        assert(!platform.Health().Snapshot().watchdog_armed);
        platform.Health().Progress();
        assert(platform.ConfirmBoot() == zectrix::update::Result::kIoError);
        assert(!boot_confirmed && boot_watchdog_armed && watchdog_timeout_ms == 60000);
    }
    assert(boot_watchdog_armed);
    pending_boot = false;
    reset_reason = zectrix::system::ResetReason::PowerOn;
    fail_at.clear();
    events.clear();
    inspections = time_polls = 0;
}

#if CONFIG_ZECTRIX_ENABLE_CONNECTIVITY && CONFIG_ZECTRIX_ENABLE_USB_CLI && CONFIG_ZECTRIX_ENABLE_UPDATE
int main() {
    host_current_task = reinterpret_cast<void*>(1);
    TestUnsetClockDoesNotBlockStartup();
    TestMaintenanceReset();
    TestDegradedStorageAndHealth();
    {
        zectrix::Platform platform;
        inspected_registry = &platform.Services();
        assert(!platform.IsInitialized());
        AssertNoServices(platform);
        assert(platform.Initialize() == ESP_OK);
        assert(platform.IsInitialized());
        assert(platform.Initialize() == ESP_OK);
        (void)platform.Display();
        (void)platform.Input();
        (void)platform.Power();
        (void)platform.Time();
        (void)platform.Storage();
        (void)platform.System();
        (void)platform.Connectivity();
        (void)platform.Update();
        const auto& registry = platform.Services();
        assert(&registry == inspected_registry && registry.size() == 11 + CONFIG_ZECTRIX_ENABLE_USB_HOST);
        // These lookups compile in another translation unit than registration.
        assert(registry.Get<zectrix::display::DisplayService>() == &platform.Display());
        assert(registry.Get<zectrix::input::InputService>() == &platform.Input());
        assert(registry.Get<zectrix::power::PowerService>() == &platform.Power());
        assert(registry.Get<zectrix::time::TimeService>() == &platform.Time());
        assert(registry.Get<zectrix::storage::StorageService>() == &platform.Storage());
        assert(registry.Get<zectrix::system::SystemService>() == &platform.System());
        assert(registry.Get<zectrix::connectivity::ConnectivityService>() == &platform.Connectivity());
        assert(registry.Get<zectrix::update::UpdateService>() == &platform.Update());
        assert(registry.Get<zectrix::update::BootGuard>() == &platform.Boot());
        assert(&platform.Boot() == &platform.Update().Boot());
        assert(registry.Get<ZectrixSelfTest>() == &platform.Diagnostics());
        assert(registry.Get<zectrix::cli::CliUsbService>());
        assert(!registry.Get<zectrix::nfc::NfcService>());
        assert(boot_watchdog_armed && platform.Health().Snapshot().watchdog_armed);
        assert((events == std::vector<std::string>{
            "init:board", "create:input", "create:power",
            "create:storage", "create:storage-init", "create:time", "init:time", "create:system",
            "create:display", "create:connectivity",
            "create:connectivity-init", "create:cli"}));
        zectrix::cli::Invocation invocation;
        zectrix::cli::BoundedOutput output;
        inspections = 0;
        assert(zectrix::cli::ParseLine("uptime", 6, &invocation) == zectrix::cli::ParseStatus::kOk);
        assert(cli_executor->Execute(invocation, &output) == zectrix::cli::ExecuteStatus::kPending);
        assert(inspections == 0);
        host_current_task = reinterpret_cast<void*>(2);
        platform.PollMaintenance();
        assert(inspections == 0);
        host_current_task = reinterpret_cast<void*>(1);
        platform.PollMaintenance();
        assert(cli_executor->Poll(&output) == zectrix::cli::ExecuteStatus::kOk);
        assert(inspections == 1);
        assert(std::string(output.data()).find("1234 ms") != std::string::npos);
        const auto query = [&](const char* command) {
            output.Clear();
            assert(zectrix::cli::ParseLine(command, std::strlen(command), &invocation) == zectrix::cli::ParseStatus::kOk);
            assert(cli_executor->Execute(invocation, &output) == zectrix::cli::ExecuteStatus::kPending);
            platform.PollMaintenance();
            std::string text;
            auto state = zectrix::cli::ExecuteStatus::kPending;
            for (unsigned i = 0; state == zectrix::cli::ExecuteStatus::kPending && i < 20; ++i) {
                output.Clear();
                state = cli_executor->Poll(&output);
                text += output.data();
            }
            assert(state == zectrix::cli::ExecuteStatus::kOk);
            return text;
        };
        platform.Power().ReadSnapshot();
        assert(query("system health").find("watchdog_armed=1") != std::string::npos);
        assert(query("power status").find("mv=3888") != std::string::npos);
        assert(query("display telemetry").find("frames=0") != std::string::npos);
        assert(query("display model").find("weights_q8") != std::string::npos);
        assert(query("time status").find("unix_seconds=1709179200") != std::string::npos);
        assert(query("connectivity status").find("ssid=bad?ssid") != std::string::npos);
        fail_at = "snapshot-busy";
        assert(zectrix::cli::ParseLine("connectivity status", 19, &invocation) == zectrix::cli::ParseStatus::kOk);
        assert(cli_executor->Execute(invocation, &output) == zectrix::cli::ExecuteStatus::kPending);
        platform.PollMaintenance();
        assert(cli_executor->Poll(&output) == zectrix::cli::ExecuteStatus::kBusy);
        fail_at.clear();
        pending_clock_sample = true;
        platform.Poll();
        assert(time_polls == 1 && applied_clock_ms == 1709179200123 && !pending_clock_sample);
#if CONFIG_ZECTRIX_ENABLE_USB_HOST
        auto* host = registry.Get<zectrix::host::Channel>();
        assert(host && !host->Connect());
        host->Enable();
        assert(host->Connect());
#endif
        platform.StopMaintenance();
#if CONFIG_ZECTRIX_ENABLE_USB_HOST
        assert(!host->Session() && !host->Connect());
#endif
        output.Clear();
        assert(cli_executor->Execute(invocation, &output) == zectrix::cli::ExecuteStatus::kUnavailable);
    }
    inspected_registry = nullptr;
    assert((events == std::vector<std::string>{
        "init:board", "create:input", "create:power",
        "create:storage", "create:storage-init", "create:time", "init:time", "create:system",
        "create:display", "create:connectivity", "create:connectivity-init",
        "create:cli", "delete:cli", "delete:connectivity", "delete:display",
        "delete:system", "delete:time", "delete:storage",
        "delete:power", "delete:input"}));

    events.clear();
    {
        ZectrixNfc nfc;
        ZectrixBoard::nfc_device = &nfc;
        zectrix::Platform platform;
        inspected_registry = &platform.Services();
        assert(platform.Initialize() == ESP_OK);
        events.clear();
        try {
            platform.Shutdown();
        } catch (const SleepEntered&) {}
        assert(!platform.IsInitialized());
        AssertNoServices(platform);
        assert((events == std::vector<std::string>{
            "delete:cli", "delete:connectivity", "delete:nfc", "delete:display",
            "delete:system", "delete:time", "delete:storage", "shutdown:power"}));
        assert(platform.Initialize() == ESP_ERR_INVALID_STATE);
        ZectrixBoard::nfc_device = nullptr;
    }
    inspected_registry = nullptr;
    assert(events[events.size() - 2] == "delete:power" && events.back() == "delete:input");

    events.clear();
    fail_at = "boot";
    {
        zectrix::Platform failed_boot;
        assert(failed_boot.Initialize() == ESP_FAIL);
        assert(events.empty());
        assert(boot_watchdog_armed);
    }
    assert(boot_watchdog_armed);

    fail_at = "system";
    pending_boot = true;
    {
        zectrix::Platform failed_trial;
        assert(failed_trial.Initialize() == ESP_FAIL);
        assert(!boot_confirmed);
    }
    assert(boot_watchdog_armed);
    fail_at.clear();
    {
        zectrix::Platform trial;
        assert(trial.Initialize() == ESP_OK);
        assert(boot_watchdog_armed);
        assert(!boot_confirmed);
        assert(trial.ConfirmBoot() == zectrix::update::Result::kOk);
        assert(boot_confirmed);
        assert(boot_watchdog_armed && trial.Health().Snapshot().watchdog_armed);
    }
    pending_boot = boot_confirmed = false;

    events.clear();
    fail_at = "system";
    zectrix::Platform failed;
    assert(failed.Initialize() == ESP_FAIL);
    assert(!failed.IsInitialized());
    AssertNoServices(failed);
    assert(failed.Services().size() == 0);
    assert(failed.Initialize() == ESP_ERR_INVALID_STATE);
    assert((events == std::vector<std::string>{
        "init:board", "create:input", "create:power",
        "create:storage", "create:storage-init", "create:time", "init:time", "create:system",
        "delete:time", "delete:storage",
        "delete:power", "delete:input"}));

    events.clear();
    fail_at.clear();
    nothrow_allocation_count = 0;
    fail_nothrow_allocation = 1;
    zectrix::Platform no_impl_memory;
    assert(no_impl_memory.Initialize() == ESP_ERR_NO_MEM);
    assert(!no_impl_memory.IsInitialized());
    AssertNoServices(no_impl_memory);
    assert(events.empty());
    fail_nothrow_allocation = 0;
    assert(no_impl_memory.Initialize() == ESP_OK);

    events.clear();
    nothrow_allocation_count = 0;
    fail_nothrow_allocation = 2;
    zectrix::Platform no_diagnostics_memory;
    assert(no_diagnostics_memory.Initialize() == ESP_ERR_NO_MEM);
    assert(!no_diagnostics_memory.IsInitialized());
    AssertNoServices(no_diagnostics_memory);
    assert((events == std::vector<std::string>{
        "init:board", "create:input", "create:power",
        "create:storage", "create:storage-init", "create:time", "init:time", "create:system",
        "create:display",
        "delete:display", "delete:system", "delete:time", "delete:storage",
        "delete:power", "delete:input"}));

    events.clear();
    fail_at = "cli";
    zectrix::Platform failed_cli;
    assert(failed_cli.Initialize() == ESP_FAIL);
    assert(!failed_cli.IsInitialized());
    AssertNoServices(failed_cli);
    assert((events == std::vector<std::string>{
        "init:board", "create:input", "create:power",
        "create:storage", "create:storage-init", "create:time", "init:time", "create:system",
        "create:display", "create:connectivity", "create:connectivity-init",
        "create:cli", "delete:cli", "delete:connectivity",
        "delete:display", "delete:system", "delete:time", "delete:storage",
        "delete:power", "delete:input"}));

    // Exercise every adapter failure with NFC already attached by the board.
    ZectrixNfc nfc;
    ZectrixBoard::nfc_device = &nfc;
    for (const char* failure : {"input", "power", "time", "storage",
                               "system", "display", "connectivity", "connectivity-init", "cli"}) {
        events.clear();
        fail_at = failure;
        zectrix::Platform partial;
        inspected_registry = &partial.Services();
        assert(partial.Initialize() != ESP_OK);
        AssertNoServices(partial);
        assert(partial.Services().size() == 0);
        assert(partial.Initialize() == ESP_ERR_INVALID_STATE);
        assert(std::count(events.begin(), events.end(), "create:nfc") == 1);
        assert(std::count(events.begin(), events.end(), "delete:nfc") == 1);
    }
    fail_at.clear();
    for (int allocation : {2, 3, 4}) {
        events.clear();
        nothrow_allocation_count = 0;
        fail_nothrow_allocation = allocation;
        zectrix::Platform partial;
        inspected_registry = &partial.Services();
        assert(partial.Initialize() == ESP_ERR_NO_MEM);
        AssertNoServices(partial);
        assert(std::count(events.begin(), events.end(), "delete:nfc") == 1);
        fail_nothrow_allocation = 0;
    }
    inspected_registry = nullptr;
    ZectrixBoard::nfc_device = nullptr;
}
#else
int main() {
    host_current_task = reinterpret_cast<void*>(1);
    TestUnsetClockDoesNotBlockStartup();
    TestMaintenanceReset();
    TestDegradedStorageAndHealth();
    ZectrixNfc nfc;
    ZectrixBoard::nfc_device = &nfc;
    {
        zectrix::Platform platform;
        inspected_registry = &platform.Services();
        AssertNoServices(platform);
        assert(platform.Initialize() == ESP_OK);
        const auto& registry = platform.Services();
        assert(registry.Get<zectrix::update::BootGuard>() == &platform.Boot());
        assert(registry.Get<zectrix::time::TimeService>() == &platform.Time());
        assert(registry.Get<zectrix::display::DisplayService>() == &platform.Display());
        assert((registry.Get<zectrix::connectivity::ConnectivityService>() != nullptr) ==
               (CONFIG_ZECTRIX_ENABLE_CONNECTIVITY != 0));
        assert((registry.Get<zectrix::cli::CliUsbService>() != nullptr) ==
               (CONFIG_ZECTRIX_ENABLE_USB_CLI != 0));
        assert((registry.Get<zectrix::update::UpdateService>() != nullptr) ==
               (CONFIG_ZECTRIX_ENABLE_UPDATE != 0));
        assert(boot_watchdog_armed && platform.Health().Snapshot().watchdog_armed);
        assert(std::count(events.begin(), events.end(), "create:nfc") == CONFIG_ZECTRIX_ENABLE_CONNECTIVITY);
        platform.Poll();
        platform.StopMaintenance();
        events.clear();
        try { platform.Shutdown(); } catch (const SleepEntered&) {}
        assert(!platform.IsInitialized());
        AssertNoServices(platform);
        assert(events.back() == "shutdown:power");
        assert(std::count(events.begin(), events.end(), "delete:cli") == CONFIG_ZECTRIX_ENABLE_USB_CLI);
        assert(std::count(events.begin(), events.end(), "delete:connectivity") == CONFIG_ZECTRIX_ENABLE_CONNECTIVITY);
        assert(std::count(events.begin(), events.end(), "delete:nfc") == CONFIG_ZECTRIX_ENABLE_CONNECTIVITY);
        assert(platform.Initialize() == ESP_ERR_INVALID_STATE);
    }
    inspected_registry = nullptr;
    assert(events[events.size() - 2] == "delete:power" && events.back() == "delete:input");
    ZectrixBoard::nfc_device = nullptr;

    // Removing the update writer must not remove trial-boot protection.
    pending_boot = true;
    {
        zectrix::Platform unconfirmed;
        assert(unconfirmed.Initialize() == ESP_OK);
        assert(boot_watchdog_armed && !boot_confirmed);
    }
    assert(boot_watchdog_armed && !boot_confirmed);
    events.clear();
    fail_at = "system";
    {
        zectrix::Platform failed;
        assert(failed.Initialize() == ESP_FAIL);
        AssertNoServices(failed);
        assert(!boot_confirmed && boot_watchdog_armed);
    }
    fail_at.clear();
    {
        zectrix::Platform trial;
        assert(trial.Initialize() == ESP_OK);
        assert(boot_watchdog_armed && !boot_confirmed);
        assert(trial.ConfirmBoot() == zectrix::update::Result::kOk);
        assert(boot_confirmed && boot_watchdog_armed && trial.Health().Snapshot().watchdog_armed);
    }
    events.clear();
    fail_at = "boot";
    {
        zectrix::Platform failed;
        assert(failed.Initialize() == ESP_FAIL);
        assert(events.empty() && boot_watchdog_armed);
        AssertNoServices(failed);
    }
}
#endif
