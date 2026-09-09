#include "zectrix_platform.h"
#include "zectrix_board.h"

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <new>
#include <string>
#include <vector>

#include "zectrix_display_service.h"
#include "zectrix_cli_usb.h"
#include "zectrix_cli_diagnostics.h"
#include "freertos/task.h"
#include "zectrix_connectivity_service.h"
#include "zectrix_nfc_service.h"
#include "zectrix_input_service.h"
#include "zectrix_power_service.h"
#include "zectrix_storage_service.h"
#include "zectrix_system_service.h"
#include "zectrix_time_service.h"
#include "zectrix_update_esp.h"
#include "update_test_fixture.h"

class ZectrixNfc {};

namespace {
struct SleepEntered {};
std::vector<std::string> events;
std::string fail_at;
int nothrow_allocation_count = 0;
int fail_nothrow_allocation = 0;
zectrix::cli::CliExecutor* cli_executor = nullptr;
unsigned inspections = 0;
bool boot_watchdog_armed = false;
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

esp_err_t ZectrixBoard::Init() {
    assert(boot_probe_count != 0);
    if (inspected_registry) assert(inspected_registry->Get<zectrix::update::UpdateService>());
    events.emplace_back("init:board");
    return init_result;
}

namespace zectrix::update {
EspUpdateBackend::~EspUpdateBackend() { AbortImage(); }
Result EspUpdateBackend::ReadBootInfo(BootInfo* info) {
    ++boot_probe_count;
    if (fail_at == "boot") return Result::kIoError;
    *info = UpdateBootFixture(pending_boot ? PartitionKind::kOtaA : PartitionKind::kFactory);
    if (pending_boot && !boot_confirmed) info->image_state = ImageState::kPendingVerify;
    return Result::kOk;
}
uint64_t EspUpdateBackend::Milliseconds() const { return 0; }
Result EspUpdateBackend::ArmBootWatchdog(uint32_t timeout_ms) {
    assert(timeout_ms == kBootConfirmationTimeoutMs);
    boot_watchdog_armed = true;
    return Result::kOk;
}
void EspUpdateBackend::DisarmBootWatchdog() { boot_watchdog_armed = false; }
Result EspUpdateBackend::ConfirmRunningImage(const Partition&) {
    boot_confirmed = true;
    return Result::kOk;
}
Result EspUpdateBackend::BeginImage(const Partition&, uint32_t, const uint8_t*, std::size_t) {
    return Result::kInvalidState;
}
Result EspUpdateBackend::WriteImage(const uint8_t*, std::size_t) { return Result::kInvalidState; }
Result EspUpdateBackend::CommitImage(uint32_t) { return Result::kInvalidState; }
void EspUpdateBackend::AbortImage() {}
}

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
}
namespace zectrix::power {
esp_err_t PowerService::Attach(ZectrixBoard& board, PowerService** output) {
    const esp_err_t result = Result("power");
    if (result == ESP_OK) *output = new PowerService(board);
    return result;
}
PowerService::~PowerService() { AssertWithdrawn<PowerService>(); events.emplace_back("delete:power"); }
[[noreturn]] void PowerService::Shutdown() {
    AssertWithdrawn<PowerService>();
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
StorageService::~StorageService() {
    AssertWithdrawn<StorageService>();
    delete impl_;
    events.emplace_back("delete:storage");
}
}
namespace zectrix::system {
esp_err_t SystemService::Attach(ZectrixBoard& board, SystemService** output) {
    const esp_err_t result = Result("system");
    if (result == ESP_OK) *output = new SystemService(board);
    return result;
}
SystemService::~SystemService() { AssertWithdrawn<SystemService>(); events.emplace_back("delete:system"); }
esp_err_t SystemService::ReadSnapshot(SystemSnapshot* result) const {
    ++inspections;
    *result = {};
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
    events.emplace_back("create:connectivity-init");
    return fail_at == "connectivity-init"
               ? ConnectivityResult::kTransportError
               : ConnectivityResult::kOk;
}
ConnectivityService::~ConnectivityService() {
    AssertWithdrawn<ConnectivityService>();
    delete impl_;
    events.emplace_back("delete:connectivity");
}
}

namespace zectrix::cli {
CliUsbService::CliUsbService() = default;
CliUsbService::~CliUsbService() { AssertWithdrawn<CliUsbService>(); events.emplace_back("delete:cli"); }
esp_err_t CliUsbService::Start(CliExecutor* executor) {
    assert(executor != nullptr);
    cli_executor = executor;
    return Result("cli");
}
void CliUsbService::Stop() {}
bool CliUsbService::running() const { return true; }
LogBuffer& MaintenanceLogs() { static LogBuffer logs; return logs; }
}

int main() {
    host_current_task = reinterpret_cast<void*>(1);
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
        assert(&registry == inspected_registry && registry.size() == 10);
        // These lookups compile in another translation unit than registration.
        assert(registry.Get<zectrix::display::DisplayService>() == &platform.Display());
        assert(registry.Get<zectrix::input::InputService>() == &platform.Input());
        assert(registry.Get<zectrix::power::PowerService>() == &platform.Power());
        assert(registry.Get<zectrix::time::TimeService>() == &platform.Time());
        assert(registry.Get<zectrix::storage::StorageService>() == &platform.Storage());
        assert(registry.Get<zectrix::system::SystemService>() == &platform.System());
        assert(registry.Get<zectrix::connectivity::ConnectivityService>() == &platform.Connectivity());
        assert(registry.Get<zectrix::update::UpdateService>() == &platform.Update());
        assert(registry.Get<ZectrixSelfTest>() == &platform.Diagnostics());
        assert(registry.Get<zectrix::cli::CliUsbService>());
        assert(!registry.Get<zectrix::nfc::NfcService>());
        assert(!boot_watchdog_armed);
        assert((events == std::vector<std::string>{
            "init:board", "create:input", "create:power", "create:time",
            "create:storage", "create:storage-init", "create:system",
            "create:display", "create:connectivity",
            "create:connectivity-init", "create:cli"}));
        zectrix::cli::Invocation invocation;
        zectrix::cli::BoundedOutput output;
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
        platform.StopMaintenance();
        output.Clear();
        assert(cli_executor->Execute(invocation, &output) == zectrix::cli::ExecuteStatus::kUnavailable);
    }
    inspected_registry = nullptr;
    assert((events == std::vector<std::string>{
        "init:board", "create:input", "create:power", "create:time",
        "create:storage", "create:storage-init", "create:system",
        "create:display", "create:connectivity", "create:connectivity-init",
        "create:cli", "delete:cli", "delete:connectivity", "delete:display",
        "delete:system", "delete:storage", "delete:time",
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
            "delete:system", "delete:storage", "delete:time", "shutdown:power"}));
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
        assert(trial.Update().ConfirmBoot() == zectrix::update::Result::kOk);
        assert(boot_confirmed);
        assert(!boot_watchdog_armed);
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
        "init:board", "create:input", "create:power", "create:time",
        "create:storage", "create:storage-init", "create:system",
        "delete:storage", "delete:time",
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
        "init:board", "create:input", "create:power", "create:time",
        "create:storage", "create:storage-init", "create:system",
        "create:display",
        "delete:display", "delete:system", "delete:storage", "delete:time",
        "delete:power", "delete:input"}));

    events.clear();
    fail_at = "storage-init";
    zectrix::Platform failed_storage_init;
    assert(failed_storage_init.Initialize() == ESP_FAIL);
    assert(!failed_storage_init.IsInitialized());
    AssertNoServices(failed_storage_init);
    assert((events == std::vector<std::string>{
        "init:board", "create:input", "create:power", "create:time",
        "create:storage", "create:storage-init", "delete:storage",
        "delete:time", "delete:power", "delete:input"}));

    events.clear();
    fail_at = "cli";
    zectrix::Platform failed_cli;
    assert(failed_cli.Initialize() == ESP_FAIL);
    assert(!failed_cli.IsInitialized());
    AssertNoServices(failed_cli);
    assert((events == std::vector<std::string>{
        "init:board", "create:input", "create:power", "create:time",
        "create:storage", "create:storage-init", "create:system",
        "create:display", "create:connectivity", "create:connectivity-init",
        "create:cli", "delete:cli", "delete:connectivity",
        "delete:display", "delete:system", "delete:storage", "delete:time",
        "delete:power", "delete:input"}));

    // Exercise every adapter failure with NFC already attached by the board.
    ZectrixNfc nfc;
    ZectrixBoard::nfc_device = &nfc;
    for (const char* failure : {"input", "power", "time", "storage", "storage-init",
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
