#include "zectrix_platform.h"

#include <cassert>
#include <new>

#include "zectrix_board.h"
#include "zectrix_connectivity_service.h"
#include "zectrix_display_service.h"
#include "zectrix_nfc_service.h"
#include "zectrix_input_service.h"
#include "zectrix_power_service.h"
#include "zectrix_self_test.h"
#include "zectrix_storage_service.h"
#include "zectrix_system_service.h"
#include "zectrix_time_service.h"

namespace zectrix {

struct Platform::Impl {
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
};

Platform::~Platform() { ResetServices(); }

esp_err_t Platform::Initialize() {
    if (initialized_) return ESP_OK;
    if (initialization_attempted_) return ESP_ERR_INVALID_STATE;
    impl_ = new (std::nothrow) Impl;
    if (impl_ == nullptr) return ESP_ERR_NO_MEM;
    initialization_attempted_ = true;

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

void Platform::ResetServices() {
    if (impl_ == nullptr) return;
    // Destruction is the reverse of the initialization order.
    delete impl_->connectivity;
    delete impl_->nfc_service;
    delete impl_->diagnostics;
    delete impl_->display;
    delete impl_->system;
    delete impl_->storage;
    delete impl_->time;
    delete impl_->power;
    delete impl_->input;
    delete impl_;
    impl_ = nullptr;
    initialized_ = false;
}

}  // namespace zectrix
