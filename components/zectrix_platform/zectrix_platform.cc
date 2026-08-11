#include "zectrix_platform.h"

#include "zectrix_display_service.h"
#include "zectrix_input_service.h"
#include "zectrix_power_service.h"
#include "zectrix_storage_service.h"
#include "zectrix_system_service.h"
#include "zectrix_time_service.h"

namespace zectrix {

Platform::~Platform() { ResetServices(); }

esp_err_t Platform::Initialize() {
    if (initialized_) return ESP_OK;
    if (initialization_attempted_) return ESP_ERR_INVALID_STATE;
    initialization_attempted_ = true;

    esp_err_t err = board_.Init();
    if (err == ESP_OK) err = input::InputService::Attach(board_, &input_);
    if (err == ESP_OK) err = power::PowerService::Attach(board_, &power_);
    if (err == ESP_OK) err = time::TimeService::Attach(board_, &time_);
    if (err == ESP_OK) err = storage::StorageService::Create(&storage_);
    if (err == ESP_OK) err = system::SystemService::Attach(board_, &system_);
    if (err == ESP_OK) err = display::DisplayService::Create(&display_);
    if (err != ESP_OK) {
        ResetServices();
        return err;
    }
    initialized_ = true;
    return ESP_OK;
}

display::DisplayService& Platform::Display() const { return *display_; }
input::InputService& Platform::Input() const { return *input_; }
power::PowerService& Platform::Power() const { return *power_; }
time::TimeService& Platform::Time() const { return *time_; }
storage::StorageService& Platform::Storage() const { return *storage_; }
system::SystemService& Platform::System() const { return *system_; }

void Platform::ResetServices() {
    // Destruction is the reverse of the initialization order.
    delete display_;
    display_ = nullptr;
    delete system_;
    system_ = nullptr;
    delete storage_;
    storage_ = nullptr;
    delete time_;
    time_ = nullptr;
    delete power_;
    power_ = nullptr;
    delete input_;
    input_ = nullptr;
    initialized_ = false;
}

}  // namespace zectrix
