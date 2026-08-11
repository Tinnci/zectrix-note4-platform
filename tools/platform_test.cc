#include "zectrix_platform.h"

#include <cassert>
#include <string>
#include <vector>

#include "zectrix_display_service.h"
#include "zectrix_input_service.h"
#include "zectrix_power_service.h"
#include "zectrix_storage_service.h"
#include "zectrix_system_service.h"
#include "zectrix_time_service.h"

namespace {
std::vector<std::string> events;
std::string fail_at;
esp_err_t Result(const char* name) {
    events.emplace_back(std::string("create:") + name);
    return fail_at == name ? ESP_FAIL : ESP_OK;
}
}

esp_err_t ZectrixBoard::Init() {
    events.emplace_back("init:board");
    return init_result;
}

namespace zectrix::input {
esp_err_t InputService::Attach(ZectrixBoard& board, InputService** output) {
    const esp_err_t result = Result("input");
    if (result == ESP_OK) *output = new InputService(board);
    return result;
}
InputService::~InputService() { events.emplace_back("delete:input"); }
}
namespace zectrix::power {
esp_err_t PowerService::Attach(ZectrixBoard& board, PowerService** output) {
    const esp_err_t result = Result("power");
    if (result == ESP_OK) *output = new PowerService(board);
    return result;
}
PowerService::~PowerService() { events.emplace_back("delete:power"); }
}
namespace zectrix::time {
esp_err_t TimeService::Attach(ZectrixBoard& board, TimeService** output) {
    const esp_err_t result = Result("time");
    if (result == ESP_OK) *output = new TimeService(board);
    return result;
}
TimeService::~TimeService() { events.emplace_back("delete:time"); }
}
namespace zectrix::storage {
struct StorageService::Impl {};
esp_err_t StorageService::Create(StorageService** output) {
    const esp_err_t result = Result("storage");
    if (result == ESP_OK) *output = new StorageService(new Impl);
    return result;
}
StorageService::~StorageService() {
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
SystemService::~SystemService() { events.emplace_back("delete:system"); }
}
namespace zectrix::display {
esp_err_t DisplayService::Create(DisplayService** output) {
    const esp_err_t result = Result("display");
    if (result == ESP_OK) *output = new DisplayService(nullptr);
    return result;
}
DisplayService::~DisplayService() { events.emplace_back("delete:display"); }
}

int main() {
    {
        zectrix::Platform platform;
        assert(!platform.IsInitialized());
        assert(platform.Initialize() == ESP_OK);
        assert(platform.IsInitialized());
        assert(platform.Initialize() == ESP_OK);
        (void)platform.Display();
        (void)platform.Input();
        (void)platform.Power();
        (void)platform.Time();
        (void)platform.Storage();
        (void)platform.System();
        assert((events == std::vector<std::string>{
            "init:board", "create:input", "create:power", "create:time",
            "create:storage", "create:system", "create:display"}));
    }
    assert((events == std::vector<std::string>{
        "init:board", "create:input", "create:power", "create:time",
        "create:storage", "create:system", "create:display",
        "delete:display", "delete:system", "delete:storage", "delete:time",
        "delete:power", "delete:input"}));

    events.clear();
    fail_at = "system";
    zectrix::Platform failed;
    assert(failed.Initialize() == ESP_FAIL);
    assert(!failed.IsInitialized());
    assert(failed.Initialize() == ESP_ERR_INVALID_STATE);
    assert((events == std::vector<std::string>{
        "init:board", "create:input", "create:power", "create:time",
        "create:storage", "create:system", "delete:storage", "delete:time",
        "delete:power", "delete:input"}));
}
