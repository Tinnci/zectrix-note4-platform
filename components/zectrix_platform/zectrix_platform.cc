#include "zectrix_platform.h"

#include <cassert>
#include <new>
#include <utility>

#include "esp_log.h"
#include "sdkconfig.h"
#include "zectrix_board.h"
#include "zectrix_boot_esp.h"
#if CONFIG_ZECTRIX_ENABLE_USB_HOST
#include "zectrix_host_protocol.h"
#endif
#if CONFIG_ZECTRIX_ENABLE_USB_CLI
#include "zectrix_platform_diagnostics.h"
#include "zectrix_cli_usb.h"
#endif
#if CONFIG_ZECTRIX_ENABLE_CONNECTIVITY
#include "zectrix_connectivity_service.h"
#include "zectrix_nfc_service.h"
#endif
#include "zectrix_display_service.h"
#include "zectrix_input_service.h"
#include "zectrix_power_service.h"
#include "zectrix_self_test.h"
#include "zectrix_storage_service.h"
#include "zectrix_system_service.h"
#include "zectrix_time_service.h"
#if CONFIG_ZECTRIX_ENABLE_UPDATE
#include "zectrix_update_esp.h"
#endif

namespace zectrix {

namespace {
// Keep legacy factories and hardware ownership in the composition root. The
// bindings are members of Impl, so lifecycle adaptation adds no allocations.
template <typename Interface, typename Owner>
class ServiceBinding final : public ServiceProvider<Interface> {
public:
    using Operation = esp_err_t (*)(Owner&);
    ServiceBinding(Owner& owner, Interface*& instance, Operation init,
                   Operation start = nullptr, Operation stop = nullptr)
        : owner_(owner), instance_(instance), init_(init), start_(start), stop_(stop) {}

    esp_err_t Init() override { return init_ ? init_(owner_) : ESP_OK; }
    esp_err_t Start() override { return start_ ? start_(owner_) : ESP_OK; }
    esp_err_t Stop() override {
        if (stop_) return stop_(owner_);
        delete std::exchange(instance_, nullptr);
        return ESP_OK;
    }
    Interface* GetInterface() override { return instance_; }

private:
    Owner& owner_;
    Interface*& instance_;
    Operation init_, start_, stop_;
};
}  // namespace

struct Platform::Impl {
#if CONFIG_ZECTRIX_ENABLE_UPDATE
    update::EspUpdateBackend update_backend;
    update::UpdateService update{update_backend};
    update::BootGuard* boot_facade = &update.Boot();
#else
    update::EspBootBackend boot_backend;
    update::BootGuard boot{boot_backend};
    update::BootGuard* boot_facade = &boot;
#endif
    ZectrixBoard board;
#if CONFIG_ZECTRIX_ENABLE_CONNECTIVITY
    nfc::NfcService* nfc_service = nullptr;
    connectivity::ConnectivityService* connectivity = nullptr;
#endif
    input::InputService* input = nullptr;
    power::PowerService* power = nullptr;
    time::TimeService* time = nullptr;
    storage::StorageService* storage = nullptr;
    system::SystemService* system = nullptr;
    display::DisplayService* display = nullptr;
    ZectrixSelfTest* diagnostics = nullptr;
#if CONFIG_ZECTRIX_ENABLE_USB_HOST
    host::Channel host_channel;
    host::Protocol host_protocol{host_channel};
    host::Channel* host_facade = &host_channel;
    ServiceBinding<host::Channel, Impl> host_binding{
        *this, host_facade, nullptr, nullptr, [](Impl& self) {
            self.host_channel.Disable();
            return ESP_OK;
        }};
#endif
#if CONFIG_ZECTRIX_ENABLE_USB_CLI
    cli::CliUsbService* cli_usb = nullptr;
    PlatformDiagnostics* maintenance = nullptr;
#endif

    void StopMaintenance() {
#if CONFIG_ZECTRIX_ENABLE_USB_HOST
        host_channel.Disable();
#endif
#if CONFIG_ZECTRIX_ENABLE_USB_CLI
        if (maintenance != nullptr) maintenance->Shutdown();
        if (cli_usb != nullptr) cli_usb->Stop();
#endif
    }

    static esp_err_t KeepForPowerTransition(Impl&) { return ESP_OK; }
    ServiceBinding<update::BootGuard, Impl> boot_binding{
        *this, boot_facade, [](Impl& self) {
            const auto result = self.boot_facade->BeginBoot();
            if (result != update::Result::kOk) {
                ESP_LOGE("update", "boot protection failed: %s", update::ResultName(result));
                return ESP_FAIL;
            }
            const auto boot = self.boot_facade->ReadBootStatus();
            ESP_LOGI("update", "running=0x%08lx selected=0x%08lx update=0x%08lx state=%u layout=%s",
                     static_cast<unsigned long>(boot.running.address),
                     static_cast<unsigned long>(boot.boot.address),
                     static_cast<unsigned long>(boot.next_update.address),
                     static_cast<unsigned>(boot.image_state), update::ResultName(boot.layout_result));
            if (boot.confirmation_pending && !boot.rollback_available)
                ESP_LOGW("update", "trial boot has no verified fallback image");
            return ESP_OK;
        }, nullptr, KeepForPowerTransition};
#if CONFIG_ZECTRIX_ENABLE_UPDATE
    update::UpdateService* update_facade = &update;
    ServiceBinding<update::UpdateService, Impl> update_binding{
        *this, update_facade, nullptr, nullptr, [](Impl& self) {
            // Stopping a provider must never confirm a trial image or disarm its watchdog.
            self.update.AbortFirmware();
            return ESP_OK;
        }};
#endif
    ServiceBinding<input::InputService, Impl> input_binding{
        *this, input, [](Impl& self) {
            esp_err_t err = self.board.Init();
#if CONFIG_ZECTRIX_ENABLE_CONNECTIVITY
            if (err == ESP_OK && self.board.HasNfc() && self.board.nfc() != nullptr)
                err = nfc::NfcService::Attach(*self.board.nfc(), &self.nfc_service);
#endif
            if (err == ESP_OK) err = input::InputService::Attach(self.board, &self.input);
            return err;
        }, nullptr, KeepForPowerTransition};
    ServiceBinding<power::PowerService, Impl> power_binding{
        *this, power, [](Impl& self) { return power::PowerService::Attach(self.board, &self.power); },
        nullptr, KeepForPowerTransition};
    ServiceBinding<time::TimeService, Impl> time_binding{
        *this, time, [](Impl& self) { return time::TimeService::Attach(self.board, &self.time); },
        [](Impl& self) {
            const esp_err_t restored = self.time->Initialize(*self.storage);
            if (restored == ESP_OK) ESP_LOGI("time", "wall clock restored from RTC");
            else ESP_LOGW("time", "RTC restoration unavailable: %s; clock setup remains available",
                          esp_err_to_name(restored));
            return ESP_OK;
        }};
    ServiceBinding<storage::StorageService, Impl> storage_binding{
        *this, storage, [](Impl& self) { return storage::StorageService::Create(&self.storage); },
        [](Impl& self) { return self.storage->Initialize(); }};
    ServiceBinding<system::SystemService, Impl> system_binding{
        *this, system, [](Impl& self) { return system::SystemService::Attach(self.board, &self.system); }};
    ServiceBinding<display::DisplayService, Impl> display_binding{
        *this, display, [](Impl& self) { return display::DisplayService::Create(&self.display); }};
    ServiceBinding<ZectrixSelfTest, Impl> diagnostics_binding{
        *this, diagnostics, [](Impl& self) {
            self.diagnostics = new (std::nothrow) ZectrixSelfTest(
                self.board, *self.input, *self.power, *self.time, *self.storage, *self.system);
            return self.diagnostics ? ESP_OK : ESP_ERR_NO_MEM;
        }};
#if CONFIG_ZECTRIX_ENABLE_CONNECTIVITY
    ServiceBinding<connectivity::ConnectivityService, Impl> connectivity_binding{
        *this, connectivity, [](Impl& self) {
            const auto result = connectivity::ConnectivityService::Create(&self.connectivity);
            if (result != connectivity::ConnectivityResult::kOk) return ESP_ERR_NO_MEM;
            self.connectivity->SetNfcService(self.nfc_service);
            self.connectivity->SetStorageService(self.storage);
            return ESP_OK;
        }, [](Impl& self) {
            return self.connectivity->Initialize() == connectivity::ConnectivityResult::kOk ? ESP_OK : ESP_FAIL;
        }, [](Impl& self) {
            delete std::exchange(self.connectivity, nullptr);
            delete std::exchange(self.nfc_service, nullptr);
            return ESP_OK;
        }};
#endif
#if CONFIG_ZECTRIX_ENABLE_USB_CLI
    ServiceBinding<cli::CliUsbService, Impl> maintenance_binding{
        *this, cli_usb, [](Impl& self) {
            self.maintenance = new (std::nothrow) PlatformDiagnostics(
                *self.system, *self.display, *self.input, *self.time
#if CONFIG_ZECTRIX_ENABLE_USB_HOST
                , &self.host_protocol
#endif
            );
            if (self.maintenance == nullptr) return ESP_ERR_NO_MEM;
            self.cli_usb = new (std::nothrow) cli::CliUsbService;
            return self.cli_usb ? ESP_OK : ESP_ERR_NO_MEM;
        }, [](Impl& self) { return self.cli_usb->Start(&self.maintenance->executor()); },
        [](Impl& self) {
            self.StopMaintenance();
            delete std::exchange(self.cli_usb, nullptr);
            delete std::exchange(self.maintenance, nullptr);
            return ESP_OK;
        }};
#endif

    esp_err_t RegisterServices(ServiceRegistry& registry) {
        esp_err_t err = registry.Register(boot_binding);
#if CONFIG_ZECTRIX_ENABLE_UPDATE
        if (err == ESP_OK) err = registry.Register(update_binding);
#endif
        if (err == ESP_OK) err = registry.Register(input_binding);
        if (err == ESP_OK) err = registry.Register(power_binding);
        if (err == ESP_OK) err = registry.Register(storage_binding);
        if (err == ESP_OK) err = registry.Register(time_binding);
        if (err == ESP_OK) err = registry.Register(system_binding);
        if (err == ESP_OK) err = registry.Register(display_binding);
        if (err == ESP_OK) err = registry.Register(diagnostics_binding);
#if CONFIG_ZECTRIX_ENABLE_CONNECTIVITY
        if (err == ESP_OK) err = registry.Register(connectivity_binding);
#endif
#if CONFIG_ZECTRIX_ENABLE_USB_CLI
#if CONFIG_ZECTRIX_ENABLE_USB_HOST
        if (err == ESP_OK) err = registry.Register(host_binding);
#endif
        if (err == ESP_OK) err = registry.Register(maintenance_binding);
#endif
        return err;
    }
};

Platform::~Platform() { ResetServices(); }

esp_err_t Platform::Initialize() {
    if (initialized_) return ESP_OK;
    if (initialization_attempted_) return ESP_ERR_INVALID_STATE;
    impl_ = new (std::nothrow) Impl;
    if (impl_ == nullptr) return ESP_ERR_NO_MEM;
    initialization_attempted_ = true;

    esp_err_t err = impl_->RegisterServices(services_);
    if (err == ESP_OK) err = services_.StartAll();
    if (err != ESP_OK) {
        ResetServices();
        return err;
    }
    initialized_ = true;
    return ESP_OK;
}

#define ZECTRIX_PLATFORM_ACCESSOR(Type, Name)         \
    Type& Platform::Name() const {                    \
        auto* service = services_.Get<Type>();         \
        assert(initialized_ && service != nullptr);   \
        return *service;                             \
    }

ZECTRIX_PLATFORM_ACCESSOR(display::DisplayService, Display)
ZECTRIX_PLATFORM_ACCESSOR(input::InputService, Input)
ZECTRIX_PLATFORM_ACCESSOR(power::PowerService, Power)
ZECTRIX_PLATFORM_ACCESSOR(time::TimeService, Time)
ZECTRIX_PLATFORM_ACCESSOR(storage::StorageService, Storage)
ZECTRIX_PLATFORM_ACCESSOR(system::SystemService, System)
ZECTRIX_PLATFORM_ACCESSOR(update::BootGuard, Boot)
ZECTRIX_PLATFORM_ACCESSOR(connectivity::ConnectivityService, Connectivity)
ZECTRIX_PLATFORM_ACCESSOR(update::UpdateService, Update)
ZECTRIX_PLATFORM_ACCESSOR(ZectrixSelfTest, Diagnostics)

#undef ZECTRIX_PLATFORM_ACCESSOR

void Platform::Poll() {
    if (!initialized_) return;
#if CONFIG_ZECTRIX_ENABLE_CONNECTIVITY
    companion::ClockSample sample;
    if (impl_->connectivity && impl_->connectivity->TakeClockSample(&sample)) {
        const esp_err_t calibrated = impl_->time->SetUnixTime(sample.unix_milliseconds, sample.utc_offset_seconds);
        if (calibrated != ESP_OK) ESP_LOGW("time", "companion clock rejected: %s", esp_err_to_name(calibrated));
        else ESP_LOGI("time", "companion clock applied; RTC saved=%d", impl_->time->Status().rtc_persisted);
    }
#endif
    impl_->time->Poll();
    PollMaintenance();
}

void Platform::PollMaintenance() {
#if CONFIG_ZECTRIX_ENABLE_USB_CLI
    if (impl_ != nullptr && impl_->maintenance != nullptr) impl_->maintenance->Poll();
#endif
}

void Platform::StopMaintenance() {
    if (impl_ != nullptr) impl_->StopMaintenance();
}

[[noreturn]] void Platform::Shutdown() {
    assert(initialized_ && impl_ != nullptr && impl_->power != nullptr);
    ReleaseServices();
    initialized_ = false;
    // Lookup has been withdrawn, but this owner retains the final power handle.
    impl_->power->Shutdown();
}

void Platform::ReleaseServices() {
    if (impl_ == nullptr) return;
    const auto stopped = services_.StopAll();
    if (stopped != ESP_OK) ESP_LOGW("platform", "service stop failed: %d", stopped);
#if CONFIG_ZECTRIX_ENABLE_CONNECTIVITY
    // Board initialization can attach NFC before the connectivity provider runs.
    delete std::exchange(impl_->nfc_service, nullptr);
#endif
}

void Platform::ResetServices() {
    if (impl_ == nullptr) return;
    ReleaseServices();
    services_.Clear();
    delete impl_->power;
    delete impl_->input;
    delete impl_;
    impl_ = nullptr;
    initialized_ = false;
}

}  // namespace zectrix
