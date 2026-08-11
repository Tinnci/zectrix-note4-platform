#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "zectrix_demo_ui.h"
#include "zectrix_display_service.h"
#include "zectrix_input_service.h"
#include "zectrix_power_service.h"
#include "zectrix_platform.h"
#include "zectrix_self_test.h"
#include "zectrix_storage_service.h"
#include "zectrix_system_service.h"
#include "zectrix_time_service.h"

extern "C" {
extern const uint8_t kLighthouse1bppStart[]
    asm("_binary_lighthouse_400x300_1bpp_bin_start");
extern const uint8_t kLighthouse1bppEnd[]
    asm("_binary_lighthouse_400x300_1bpp_bin_end");
extern const uint8_t kSnowPath1bppStart[]
    asm("_binary_snow_path_400x300_1bpp_bin_start");
extern const uint8_t kSnowPath1bppEnd[]
    asm("_binary_snow_path_400x300_1bpp_bin_end");
extern const uint8_t kFootprintAnimationStart[]
    asm("_binary_footprint_animation_bin_start");
extern const uint8_t kFootprintAnimationEnd[]
    asm("_binary_footprint_animation_bin_end");
extern const uint8_t kMountain4bppStart[]
    asm("_binary_mountain_400x300_4bpp_bin_start");
extern const uint8_t kMountain4bppEnd[]
    asm("_binary_mountain_400x300_4bpp_bin_end");
}

namespace {

constexpr char kTag[] = "epd_showcase";
constexpr uint8_t kFootprintMagic[] = {'Z', 'F', 'P', '1'};
constexpr size_t kAnimationHeaderSize = 8;
constexpr size_t kStepHeaderSize = 12;
constexpr TickType_t kHomeIdleTimeout = pdMS_TO_TICKS(15000);

enum class ControlResult {
    kContinue,
    kSelected,
    kBack,
    kShutdown,
};

struct SceneResult {
    esp_err_t error = ESP_OK;
    int64_t elapsed_ms = 0;
    ControlResult control = ControlResult::kContinue;
};

uint16_t ReadLe16(const uint8_t* data) {
    return static_cast<uint16_t>(data[0]) |
           static_cast<uint16_t>(data[1] << 8);
}

class DemoApp {
public:
    DemoApp() : ui_(nullptr) {
        test_states_.fill(ZectrixTestState::kWait);
    }

    void Run() {
        esp_err_t err = platform_.Initialize();
        if (err != ESP_OK) {
            ESP_LOGE(kTag, "platform initialization failed: %s",
                     esp_err_to_name(err));
            return;
        }
        display_ = &platform_.Display();
        input_ = &platform_.Input();
        power_ = &platform_.Power();
        time_ = &platform_.Time();
        storage_ = &platform_.Storage();
        system_ = &platform_.System();
        tests_ = &platform_.Diagnostics();
        ui_.SetDisplay(display_);
        ui_.SetTime(time_);
        ESP_ERROR_CHECK(ui_.ShowSplash());
        Wait(pdMS_TO_TICKS(1500), false);

        while (true) {
            const ControlResult result = RunHome();
            if (result == ControlResult::kShutdown) {
                Shutdown();
            }
        }
    }

private:
    ControlResult Wait(TickType_t duration, bool any_click_returns) {
        const TickType_t start = xTaskGetTickCount();
        while (xTaskGetTickCount() - start < duration) {
            zectrix::input::InputEvent event;
            const TickType_t elapsed = xTaskGetTickCount() - start;
            const TickType_t remaining = duration > elapsed ? duration - elapsed : 0;
            if (!input_->Wait(&event,
                              std::min(remaining, pdMS_TO_TICKS(100)))) {
                continue;
            }
            if (event.button == zectrix::input::Button::Down &&
                event.action == zectrix::input::Action::LongPress) {
                return ControlResult::kShutdown;
            }
            if (event.button == zectrix::input::Button::Ok &&
                event.action == zectrix::input::Action::LongPress) {
                return ControlResult::kBack;
            }
            if (any_click_returns &&
                event.action == zectrix::input::Action::Click) {
                return ControlResult::kBack;
            }
        }
        return ControlResult::kContinue;
    }

    ControlResult RunMenu(const char* title, const char* const* items,
                          size_t count, const char* footer,
                          size_t* selected, TickType_t idle_timeout = portMAX_DELAY) {
        ESP_ERROR_CHECK(ui_.ShowMenu(title, items, count, *selected, footer, true));
        while (true) {
            zectrix::input::InputEvent event;
            if (!input_->Wait(&event, idle_timeout)) {
                return ControlResult::kContinue;
            }
            if (event.button == zectrix::input::Button::Down &&
                event.action == zectrix::input::Action::LongPress) {
                return ControlResult::kShutdown;
            }
            if (event.button == zectrix::input::Button::Ok &&
                event.action == zectrix::input::Action::LongPress) {
                return ControlResult::kBack;
            }
            if (event.action != zectrix::input::Action::Click) {
                continue;
            }
            if (event.button == zectrix::input::Button::Up) {
                *selected = (*selected + count - 1) % count;
                ESP_ERROR_CHECK(ui_.ShowMenu(title, items, count, *selected,
                                             footer, false));
            } else if (event.button == zectrix::input::Button::Down) {
                *selected = (*selected + 1) % count;
                ESP_ERROR_CHECK(ui_.ShowMenu(title, items, count, *selected,
                                             footer, false));
            } else if (event.button == zectrix::input::Button::Ok) {
                return ControlResult::kSelected;
            }
        }
    }

    ControlResult RunHome() {
        static constexpr const char* kItems[] = {
            "AUTO SHOWCASE", "DISPLAY GALLERY", "HARDWARE TESTS",
            "DEVICE INFO", "ABOUT & LICENSE"};
        size_t selected = 0;
        while (true) {
            const ControlResult menu = RunMenu(
                "ZECTRIX | HARDWARE SHOWCASE", kItems, std::size(kItems),
                "UP/DOWN Move  OK Select  Hold DOWN Off", &selected,
                kHomeIdleTimeout);
            if (menu == ControlResult::kShutdown) {
                return menu;
            }
            if (menu == ControlResult::kBack) {
                continue;
            }
            if (menu == ControlResult::kContinue) {
                selected = 0;
            }
            // A timeout and selecting AUTO SHOWCASE intentionally share the
            // same first action.
            if (selected == 0) {
                const ControlResult result = RunAutoShowcase();
                if (result == ControlResult::kShutdown) return result;
            } else if (selected == 1) {
                const ControlResult result = RunDisplayGallery();
                if (result == ControlResult::kShutdown) return result;
            } else if (selected == 2) {
                const ControlResult result = RunHardwareTests();
                if (result == ControlResult::kShutdown) return result;
            } else if (selected == 3) {
                const ControlResult result = RunDeviceInfo();
                if (result == ControlResult::kShutdown) return result;
            } else if (selected == 4) {
                ESP_ERROR_CHECK(ui_.ShowAbout());
                const ControlResult result = Wait(portMAX_DELAY, false);
                if (result == ControlResult::kShutdown) return result;
            }
        }
    }

    SceneResult RunLighthouse() {
        SceneResult result;
        const size_t size = static_cast<size_t>(kLighthouse1bppEnd -
                                                kLighthouse1bppStart);
        if (size != zectrix::display::DisplayService::kFrameBytes1Bpp) {
            result.error = ESP_ERR_INVALID_SIZE;
            return result;
        }
        const int64_t start = time_->MonotonicMicroseconds();
        result.error = display_->Present1Bpp(
            zectrix::display::DisplayIntent::Quality,
            kLighthouse1bppStart, size);
        result.elapsed_ms = (time_->MonotonicMicroseconds() - start) / 1000;
        if (result.error == ESP_OK) {
            result.control = Wait(pdMS_TO_TICKS(2500), true);
        }
        return result;
    }

    SceneResult RunFootprints() {
        SceneResult result;
        const size_t background_size = static_cast<size_t>(
            kSnowPath1bppEnd - kSnowPath1bppStart);
        const size_t animation_size = static_cast<size_t>(
            kFootprintAnimationEnd - kFootprintAnimationStart);
        if (background_size != zectrix::display::DisplayService::kFrameBytes1Bpp ||
            animation_size < kAnimationHeaderSize ||
            std::memcmp(kFootprintAnimationStart, kFootprintMagic,
                        sizeof(kFootprintMagic)) != 0) {
            result.error = ESP_ERR_INVALID_SIZE;
            return result;
        }

        const int64_t start = time_->MonotonicMicroseconds();
        result.error = display_->BeginBatch();
        if (result.error == ESP_OK) {
            result.error = display_->Present1Bpp(
                zectrix::display::DisplayIntent::FullClean,
                kSnowPath1bppStart, background_size);
        }
        if (result.error == ESP_OK) {
            result.control = Wait(pdMS_TO_TICKS(900), true);
        }

        const uint16_t count = ReadLe16(kFootprintAnimationStart + 4);
        const uint8_t* cursor = kFootprintAnimationStart + kAnimationHeaderSize;
        for (uint16_t index = 0;
             result.error == ESP_OK &&
             result.control == ControlResult::kContinue && index < count;
             ++index) {
            if (static_cast<size_t>(kFootprintAnimationEnd - cursor) <
                kStepHeaderSize) {
                result.error = ESP_ERR_INVALID_SIZE;
                break;
            }
            const uint16_t x = ReadLe16(cursor);
            const uint16_t y = ReadLe16(cursor + 2);
            const uint16_t width = ReadLe16(cursor + 4);
            const uint16_t height = ReadLe16(cursor + 6);
            const uint16_t data_size = ReadLe16(cursor + 10);
            cursor += kStepHeaderSize;
            const size_t expected = static_cast<size_t>((width + 7) / 8) * height;
            if (data_size != expected ||
                data_size > static_cast<size_t>(kFootprintAnimationEnd - cursor)) {
                result.error = ESP_ERR_INVALID_SIZE;
                break;
            }
            const zectrix::display::Rect rect = {
                static_cast<int>(x), static_cast<int>(y),
                static_cast<int>(width), static_cast<int>(height)};
            result.error = display_->Present1Bpp(
                zectrix::display::DisplayIntent::Fast,
                kSnowPath1bppStart, background_size,
                rect, cursor, data_size);
            cursor += data_size;
            if (result.error == ESP_OK) {
                result.control = Wait(pdMS_TO_TICKS(400), true);
            }
        }
        const esp_err_t end_batch = display_->EndBatch();
        if (result.error == ESP_OK) result.error = end_batch;
        result.elapsed_ms = (time_->MonotonicMicroseconds() - start) / 1000;
        if (result.error == ESP_OK &&
            result.control == ControlResult::kContinue) {
            result.control = Wait(pdMS_TO_TICKS(2200), true);
        }
        return result;
    }

    SceneResult RunMountain() {
        SceneResult result;
        const size_t size = static_cast<size_t>(kMountain4bppEnd -
                                                kMountain4bppStart);
        if (size != zectrix::display::DisplayService::kFrameBytes4Bpp) {
            result.error = ESP_ERR_INVALID_SIZE;
            return result;
        }
        const int64_t start = time_->MonotonicMicroseconds();
        // A true white 1bpp full refresh is deliberately performed before
        // every gray refresh to minimize visible history on this panel.
        result.error = ui_.ClearDisplay();
        if (result.error == ESP_OK) {
            result.error = display_->Present4Bpp(
                zectrix::display::DisplayIntent::Quality,
                kMountain4bppStart, size);
        }
        result.elapsed_ms = (time_->MonotonicMicroseconds() - start) / 1000;
        if (result.error == ESP_OK) {
            result.control = Wait(pdMS_TO_TICKS(5000), true);
        }
        return result;
    }

    SceneResult RunScene(size_t scene) {
        ESP_LOGI(kTag, "running display scene %u", static_cast<unsigned>(scene));
        if (scene == 0) return RunLighthouse();
        if (scene == 1) return RunFootprints();
        return RunMountain();
    }

    ControlResult RunAutoShowcase() {
        while (true) {
            for (size_t scene = 0; scene < 3; ++scene) {
                const SceneResult result = RunScene(scene);
                if (result.error != ESP_OK) {
                    ESP_LOGE(kTag, "scene failed: %s",
                             esp_err_to_name(result.error));
                    return ControlResult::kBack;
                }
                if (result.control != ControlResult::kContinue) {
                    return result.control;
                }
            }
        }
    }

    ControlResult RunDisplayGallery() {
        static constexpr const char* kItems[] = {
            "FULL REFRESH / 1BPP", "PARTIAL / FOOTPRINTS",
            "16-GRAY / 4BPP", "RUN ALL SCENES"};
        static constexpr const char* kTitles[] = {
            "LIGHTHOUSE", "FOOTPRINTS", "MOUNTAIN LANDSCAPE"};
        static constexpr const char* kModes[] = {
            "FULL", "PARTIAL", "FULL + PRE-CLEAR"};
        static constexpr const char* kFormats[] = {
            "1BPP B/W", "1BPP B/W", "4BPP / 16 GRAY"};
        size_t selected = 0;
        while (true) {
            const ControlResult menu = RunMenu(
                "DISPLAY GALLERY", kItems, std::size(kItems),
                "UP/DOWN Move  OK Run  Hold OK Back", &selected);
            if (menu == ControlResult::kShutdown) return menu;
            if (menu == ControlResult::kBack) {
                return ControlResult::kBack;
            }
            if (selected == 3) {
                for (size_t scene = 0; scene < 3; ++scene) {
                    const SceneResult result = RunScene(scene);
                    if (result.control == ControlResult::kShutdown) return result.control;
                    if (result.control == ControlResult::kBack) break;
                }
                continue;
            }
            const SceneResult result = RunScene(selected);
            if (result.control == ControlResult::kShutdown) return result.control;
            ESP_ERROR_CHECK(ui_.ShowSceneInfo(
                kTitles[selected], kModes[selected], kFormats[selected],
                selected == 2
                    ? zectrix::display::DisplayService::kFrameBytes4Bpp
                    : zectrix::display::DisplayService::kFrameBytes1Bpp,
                result.elapsed_ms, result.error));
            while (true) {
                zectrix::input::InputEvent event;
                if (!input_->Wait(&event, portMAX_DELAY)) continue;
                if (event.button == zectrix::input::Button::Down &&
                    event.action == zectrix::input::Action::LongPress) {
                    return ControlResult::kShutdown;
                }
                if (event.button == zectrix::input::Button::Ok &&
                    event.action == zectrix::input::Action::Click) {
                    break;
                }
                if (event.button == zectrix::input::Button::Ok &&
                    event.action == zectrix::input::Action::LongPress) {
                    break;
                }
            }
        }
    }

    ControlResult RunHardwareTests() {
        static constexpr const char* kItems[] = {
            "RUN ALL TESTS", "SELECT INDIVIDUAL TEST"};
        size_t selected = 0;
        const ControlResult menu = RunMenu(
            "HARDWARE TESTS", kItems, std::size(kItems),
            "UP/DOWN Move  OK Select  Hold OK Back", &selected);
        if (menu == ControlResult::kShutdown) return menu;
        if (menu == ControlResult::kBack) return menu;
        return selected == 0 ? RunAllTests() : RunIndividualTests();
    }

    ZectrixTestResult ExecuteTest(ZectrixTestId id) {
        test_states_[static_cast<size_t>(id)] = ZectrixTestState::kRunning;
        return tests_->Run(id, [this](const ZectrixTestUpdate& update) {
            test_states_[static_cast<size_t>(update.id)] = update.state;
            const esp_err_t err = ui_.ShowTestUpdate(update, test_states_);
            if (err != ESP_OK) {
                ESP_LOGE(kTag, "test UI refresh failed: %s", esp_err_to_name(err));
            }
        });
    }

    ControlResult RunAllTests() {
        test_states_.fill(ZectrixTestState::kWait);
        for (size_t i = 0; i < static_cast<size_t>(ZectrixTestId::kCount); ++i) {
            const ZectrixTestId id = static_cast<ZectrixTestId>(i);
            ESP_ERROR_CHECK(ui_.ShowTestMenu(i, test_states_, true));
            const ZectrixTestResult result = ExecuteTest(id);
            if (result == ZectrixTestResult::kShutdown) {
                return ControlResult::kShutdown;
            }
            if (result == ZectrixTestResult::kCancelled) {
                break;
            }
            test_states_[i] = result == ZectrixTestResult::kPass
                                  ? ZectrixTestState::kPass
                                  : ZectrixTestState::kFail;
            const ControlResult pause = Wait(pdMS_TO_TICKS(800), false);
            if (pause == ControlResult::kShutdown) return pause;
        }
        ESP_ERROR_CHECK(ui_.ShowTestSummary(test_states_));
        return Wait(portMAX_DELAY, true);
    }

    ControlResult RunIndividualTests() {
        size_t selected = 0;
        ESP_ERROR_CHECK(ui_.ShowTestMenu(selected, test_states_, true));
        while (true) {
            zectrix::input::InputEvent event;
            if (!input_->Wait(&event, portMAX_DELAY)) continue;
            if (event.button == zectrix::input::Button::Down &&
                event.action == zectrix::input::Action::LongPress) {
                return ControlResult::kShutdown;
            }
            if (event.button == zectrix::input::Button::Ok &&
                event.action == zectrix::input::Action::LongPress) {
                return ControlResult::kBack;
            }
            if (event.action != zectrix::input::Action::Click) continue;
            if (event.button == zectrix::input::Button::Up) {
                selected = (selected + 6) % 7;
                ESP_ERROR_CHECK(ui_.ShowTestMenu(selected, test_states_, false));
            } else if (event.button == zectrix::input::Button::Down) {
                selected = (selected + 1) % 7;
                ESP_ERROR_CHECK(ui_.ShowTestMenu(selected, test_states_, false));
            } else if (event.button == zectrix::input::Button::Ok) {
                const ZectrixTestId id = static_cast<ZectrixTestId>(selected);
                const ZectrixTestResult result = ExecuteTest(id);
                if (result == ZectrixTestResult::kShutdown) {
                    return ControlResult::kShutdown;
                }
                test_states_[selected] = result == ZectrixTestResult::kPass
                                             ? ZectrixTestState::kPass
                                             : (result == ZectrixTestResult::kFail
                                                    ? ZectrixTestState::kFail
                                                    : test_states_[selected]);
                const ControlResult pause = Wait(pdMS_TO_TICKS(1200), true);
                if (pause == ControlResult::kShutdown) {
                    return pause;
                }
                ESP_ERROR_CHECK(ui_.ShowTestMenu(selected, test_states_, true));
            }
        }
    }

    ControlResult RunDeviceInfo() {
        zectrix::system::SystemSnapshot system;
        ESP_ERROR_CHECK(system_->ReadSnapshot(&system));
        ESP_ERROR_CHECK(ui_.ShowDeviceInfo(power_->ReadSnapshot(), system));
        return Wait(portMAX_DELAY, false);
    }

    [[noreturn]] void Shutdown() {
        ESP_LOGI(kTag, "clearing display before shutdown");
        const esp_err_t clear = ui_.ClearDisplay();
        if (clear != ESP_OK) {
            ESP_LOGW(kTag, "display clear failed: %s", esp_err_to_name(clear));
        }
        ESP_LOGI(kTag, "entering power service shutdown");
        power_->Shutdown();
    }

    zectrix::Platform platform_;
    zectrix::input::InputService* input_ = nullptr;
    zectrix::power::PowerService* power_ = nullptr;
    zectrix::display::DisplayService* display_ = nullptr;
    zectrix::time::TimeService* time_ = nullptr;
    zectrix::storage::StorageService* storage_ = nullptr;
    zectrix::system::SystemService* system_ = nullptr;
    ZectrixDemoUi ui_;
    ZectrixSelfTest* tests_ = nullptr;
    std::array<ZectrixTestState,
               static_cast<size_t>(ZectrixTestId::kCount)> test_states_;
};

}  // namespace

extern "C" void app_main(void) {
    static DemoApp app;
    app.Run();
}
