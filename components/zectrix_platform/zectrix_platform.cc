#include "zectrix_platform.h"
#include "zectrix_platform_diagnostics.h"

#include <cassert>
#include <new>
#include <utility>

#include "esp_log.h"
#include "zectrix_board.h"
#include "zectrix_cli_usb.h"
#include "zectrix_connectivity_service.h"
#include "zectrix_display_service.h"
#include "zectrix_nfc_service.h"
#include "zectrix_input_service.h"
#include "zectrix_power_service.h"
#include "zectrix_self_test.h"
#include "zectrix_storage_service.h"
#include "zectrix_system_service.h"
#include "zectrix_time_service.h"
#include "zectrix_update_esp.h"

namespace zectrix {

struct Platform::Impl {
    update::EspUpdateBackend update_backend;
    update::UpdateService update{update_backend};
    ZectrixBoard board;
    nfc::NfcService* nfc_service = nullptr;
    input::InputService* input = nullptr;
    power::PowerService* power = nullptr;
    time::TimeService* time = nullptr;
    storage::StorageService* storage = nullptr;
    system::SystemService* system = nullptr;
    display::DisplayService* display = nullptr;
    ZectrixSelfTest* diagnostics = nullptr;
    connectivity::ConnectivityService* connectivity = nullptr;
    cli::CliUsbService* cli_usb = nullptr;
    PlatformDiagnostics* maintenance = nullptr;
};

Platform::~Platform() { ResetServices(); }

esp_err_t Platform::Initialize() {
    if (initialized_) return ESP_OK;
    if (initialization_attempted_) return ESP_ERR_INVALID_STATE;
    impl_ = new (std::nothrow) Impl;
    if (impl_ == nullptr) return ESP_ERR_NO_MEM;
    initialization_attempted_ = true;

    const auto boot_result = impl_->update.BeginBoot();
    if (boot_result != update::Result::kOk) {
        ESP_LOGE("update", "boot protection failed: %s", update::ResultName(boot_result));
        ResetServices();
        return ESP_FAIL;
    }
    const auto boot = impl_->update.ReadBootStatus();
    ESP_LOGI("update", "running=0x%08lx selected=0x%08lx update=0x%08lx state=%u layout=%s",
             static_cast<unsigned long>(boot.running.address),
             static_cast<unsigned long>(boot.boot.address),
             static_cast<unsigned long>(boot.next_update.address),
             static_cast<unsigned>(boot.image_state), update::ResultName(boot.layout_result));
    if (boot.confirmation_pending && !boot.rollback_available) {
        ESP_LOGW("update", "trial boot has no verified fallback image");
    }
    esp_err_t err = impl_->board.Init();
    if (err == ESP_OK && impl_->board.HasNfc() &&
        impl_->board.nfc() != nullptr) {
        err = nfc::NfcService::Attach(*impl_->board.nfc(),
                                      &impl_->nfc_service);
    }
    if (err == ESP_OK) err = input::InputService::Attach(impl_->board, &impl_->input);
    if (err == ESP_OK) err = power::PowerService::Attach(impl_->board, &impl_->power);
    if (err == ESP_OK) err = time::TimeService::Attach(impl_->board, &impl_->time);
    if (err == ESP_OK) err = storage::StorageService::Create(&impl_->storage);
    if (err == ESP_OK) err = impl_->storage->Initialize();
    if (err == ESP_OK) err = system::SystemService::Attach(impl_->board, &impl_->system);
    if (err == ESP_OK) err = display::DisplayService::Create(&impl_->display);
    if (err == ESP_OK) {
        impl_->diagnostics = new (std::nothrow) ZectrixSelfTest(
            impl_->board, *impl_->input, *impl_->power, *impl_->time,
            *impl_->storage, *impl_->system);
        if (impl_->diagnostics == nullptr) err = ESP_ERR_NO_MEM;
    }
    if (err == ESP_OK) {
        const auto result = connectivity::ConnectivityService::Create(
            &impl_->connectivity);
        if (result != connectivity::ConnectivityResult::kOk) err = ESP_ERR_NO_MEM;
    }
    if (err == ESP_OK) {
        impl_->connectivity->SetNfcService(impl_->nfc_service);
        impl_->connectivity->SetStorageService(impl_->storage);
    }
    if (err == ESP_OK &&
        impl_->connectivity->Initialize() !=
            connectivity::ConnectivityResult::kOk) {
        err = ESP_FAIL;
    }
    if (err == ESP_OK) {
        impl_->maintenance = new (std::nothrow) PlatformDiagnostics(
            *impl_->system, *impl_->display, *impl_->input, *impl_->time);
        if (impl_->maintenance == nullptr) err = ESP_ERR_NO_MEM;
    }
    if (err == ESP_OK) {
        impl_->cli_usb = new (std::nothrow) cli::CliUsbService;
        if (impl_->cli_usb == nullptr) err = ESP_ERR_NO_MEM;
    }
    if (err == ESP_OK) err = impl_->cli_usb->Start(&impl_->maintenance->executor());
    if (err != ESP_OK) {
        ResetServices();
        return err;
    }
    initialized_ = true;
    return ESP_OK;
}

#define ZECTRIX_PLATFORM_ACCESSOR(Type, Name, Member) \
    Type& Platform::Name() const {                    \
        assert(initialized_ && impl_ != nullptr && impl_->Member != nullptr); \
        return *impl_->Member;                        \
    }

ZECTRIX_PLATFORM_ACCESSOR(display::DisplayService, Display, display)
ZECTRIX_PLATFORM_ACCESSOR(input::InputService, Input, input)
ZECTRIX_PLATFORM_ACCESSOR(power::PowerService, Power, power)
ZECTRIX_PLATFORM_ACCESSOR(time::TimeService, Time, time)
ZECTRIX_PLATFORM_ACCESSOR(storage::StorageService, Storage, storage)
ZECTRIX_PLATFORM_ACCESSOR(system::SystemService, System, system)
ZECTRIX_PLATFORM_ACCESSOR(connectivity::ConnectivityService, Connectivity,
                          connectivity)
ZECTRIX_PLATFORM_ACCESSOR(ZectrixSelfTest, Diagnostics, diagnostics)

#undef ZECTRIX_PLATFORM_ACCESSOR

update::UpdateService& Platform::Update() const {
    assert(initialized_ && impl_ != nullptr);
    return impl_->update;
}

void Platform::PollMaintenance() {
    if (impl_ != nullptr && impl_->maintenance != nullptr) impl_->maintenance->Poll();
}

void Platform::StopMaintenance() {
    if (impl_ == nullptr) return;
    if (impl_->maintenance != nullptr) impl_->maintenance->Shutdown();
    if (impl_->cli_usb != nullptr) impl_->cli_usb->Stop();
}

[[noreturn]] void Platform::Shutdown() {
    assert(initialized_ && impl_ != nullptr && impl_->power != nullptr);
    ReleaseServices();
    initialized_ = false;
    impl_->power->Shutdown();
}

void Platform::ReleaseServices() {
    if (impl_ == nullptr) return;
    // Stop producers before releasing the services and board devices they use.
    StopMaintenance();
    delete std::exchange(impl_->cli_usb, nullptr);
    delete std::exchange(impl_->maintenance, nullptr);
    delete std::exchange(impl_->connectivity, nullptr);
    delete std::exchange(impl_->nfc_service, nullptr);
    delete std::exchange(impl_->diagnostics, nullptr);
    delete std::exchange(impl_->display, nullptr);
    delete std::exchange(impl_->system, nullptr);
    delete std::exchange(impl_->storage, nullptr);
    delete std::exchange(impl_->time, nullptr);
}

void Platform::ResetServices() {
    if (impl_ == nullptr) return;
    ReleaseServices();
    delete impl_->power;
    delete impl_->input;
    delete impl_;
    impl_ = nullptr;
    initialized_ = false;
}

}  // namespace zectrix
