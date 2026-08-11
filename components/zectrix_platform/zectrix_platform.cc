#include "zectrix_platform.h"

#include "zectrix_board.h"
#include "zectrix_display_service.h"
#include "zectrix_input_service.h"
#include "zectrix_power_service.h"
#include "zectrix_self_test.h"
#include "zectrix_storage_service.h"
#include "zectrix_system_service.h"
#include "zectrix_time_service.h"

namespace zectrix {

struct Platform::Impl {
    ZectrixBoard board;
    input::InputService* input = nullptr;
    power::PowerService* power = nullptr;
    time::TimeService* time = nullptr;
    storage::StorageService* storage = nullptr;
    system::SystemService* system = nullptr;
    display::DisplayService* display = nullptr;
    ZectrixSelfTest* diagnostics = nullptr;
};

Platform::~Platform() { ResetServices(); }

esp_err_t Platform::Initialize() {
    if (initialized_) return ESP_OK;
    if (initialization_attempted_) return ESP_ERR_INVALID_STATE;
    initialization_attempted_ = true;
    impl_ = new Impl;

    esp_err_t err = impl_->board.Init();
    if (err == ESP_OK) err = input::InputService::Attach(impl_->board, &impl_->input);
    if (err == ESP_OK) err = power::PowerService::Attach(impl_->board, &impl_->power);
    if (err == ESP_OK) err = time::TimeService::Attach(impl_->board, &impl_->time);
    if (err == ESP_OK) err = storage::StorageService::Create(&impl_->storage);
    if (err == ESP_OK) err = system::SystemService::Attach(impl_->board, &impl_->system);
    if (err == ESP_OK) err = display::DisplayService::Create(&impl_->display);
    if (err == ESP_OK) {
        impl_->diagnostics = new ZectrixSelfTest(
            impl_->board, *impl_->input, *impl_->power, *impl_->time,
            *impl_->storage, *impl_->system);
    }
    if (err != ESP_OK) {
        ResetServices();
        return err;
    }
    initialized_ = true;
    return ESP_OK;
}

display::DisplayService& Platform::Display() const { return *impl_->display; }
input::InputService& Platform::Input() const { return *impl_->input; }
power::PowerService& Platform::Power() const { return *impl_->power; }
time::TimeService& Platform::Time() const { return *impl_->time; }
storage::StorageService& Platform::Storage() const { return *impl_->storage; }
system::SystemService& Platform::System() const { return *impl_->system; }
ZectrixSelfTest& Platform::Diagnostics() const { return *impl_->diagnostics; }

void Platform::ResetServices() {
    if (impl_ == nullptr) return;
    // Destruction is the reverse of the initialization order.
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
