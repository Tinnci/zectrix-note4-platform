#include "zectrix_board.h"

#include <algorithm>
#include <array>
#include <new>

#include "audio_codec.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "es8311_audio_codec.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "rtc_pcf8563.h"
#include "zectrix_board_config.h"
#include "zectrix_nfc.h"

namespace {

constexpr char kTag[] = "zectrix_board";
constexpr TickType_t kButtonPoll = pdMS_TO_TICKS(20);
constexpr TickType_t kButtonDebounce = pdMS_TO_TICKS(40);
constexpr TickType_t kOkLongPress = pdMS_TO_TICKS(1500);
constexpr TickType_t kDownLongPress = pdMS_TO_TICKS(3000);
constexpr adc_channel_t kBatteryAdcChannel = ADC_CHANNEL_3;

struct ButtonDefinition {
    gpio_num_t gpio;
    ZectrixButton button;
};

constexpr std::array<ButtonDefinition, 3> kButtons = {{
    {ZECTRIX_BUTTON_UP, ZectrixButton::kUp},
    {ZECTRIX_BUTTON_DOWN, ZectrixButton::kDown},
    {ZECTRIX_BUTTON_OK, ZectrixButton::kOk},
}};

TickType_t LongPressTicks(ZectrixButton button) {
    if (button == ZectrixButton::kDown) {
        return kDownLongPress;
    }
    if (button == ZectrixButton::kOk) {
        return kOkLongPress;
    }
    return portMAX_DELAY;
}

bool ClickOnPress(ZectrixButton button) {
    return button == ZectrixButton::kUp ||
           button == ZectrixButton::kDown;
}

}  // namespace

extern "C" void BoardI2cForcePowerOn() {
    gpio_hold_dis(ZECTRIX_AUDIO_POWER);
    gpio_set_level(ZECTRIX_AUDIO_POWER, 1);
    gpio_hold_en(ZECTRIX_AUDIO_POWER);
}

ZectrixBoard::ZectrixBoard() = default;

ZectrixBoard::~ZectrixBoard() {
    ShutdownPeripherals();
}

esp_err_t ZectrixBoard::ShutdownPeripherals() {
    if (button_task_done_ != nullptr) {
        button_task_stop_.store(true, std::memory_order_release);
        xSemaphoreTake(button_task_done_, portMAX_DELAY);
        vSemaphoreDelete(button_task_done_);
        button_task_done_ = nullptr;
    }
    audio_.reset();
    audio_started_ = false;
    nfc_.reset();
    rtc_.reset();
    esp_err_t result = ESP_OK;
    const auto record = [&result](esp_err_t error) {
        if (result == ESP_OK) result = error;
    };
    if (i2c_bus_ != nullptr) {
        const esp_err_t error = i2c_del_master_bus(i2c_bus_);
        record(error);
        if (error == ESP_OK) {
            i2c_bus_ = nullptr;
            // Do not feed unpowered devices through I2C or audio signal pins.
            gpio_config_t idle = {};
            idle.pin_bit_mask = (1ULL << ZECTRIX_I2C_SDA) |
                                (1ULL << ZECTRIX_I2C_SCL) |
                                (1ULL << ZECTRIX_AUDIO_MCLK) |
                                (1ULL << ZECTRIX_AUDIO_BCLK) |
                                (1ULL << ZECTRIX_AUDIO_WS) |
                                (1ULL << ZECTRIX_AUDIO_DOUT) |
                                (1ULL << ZECTRIX_AUDIO_DIN);
            idle.mode = GPIO_MODE_DISABLE;
            idle.pull_up_en = GPIO_PULLUP_DISABLE;
            idle.pull_down_en = GPIO_PULLDOWN_DISABLE;
            idle.intr_type = GPIO_INTR_DISABLE;
            record(gpio_config(&idle));
        }
    }
    if (adc_cali_ != nullptr) {
        const esp_err_t error = adc_cali_delete_scheme_curve_fitting(adc_cali_);
        record(error);
        if (error == ESP_OK) adc_cali_ = nullptr;
    }
    if (adc_handle_ != nullptr) {
        const esp_err_t error = adc_oneshot_del_unit(adc_handle_);
        record(error);
        if (error == ESP_OK) adc_handle_ = nullptr;
    }
    if (button_queue_ != nullptr) {
        vQueueDelete(button_queue_);
        button_queue_ = nullptr;
    }
    button_wait_wake_pending_.store(false);
    button_events_ = {};
    if (result != ESP_OK) {
        ESP_LOGW(kTag, "peripheral shutdown incomplete: %s", esp_err_to_name(result));
    }
    return result;
}

esp_err_t ZectrixBoard::InitPowerAndGpio() {
    gpio_deep_sleep_hold_dis();
    // EXT1 leaves its wake pin in RTC mode after deep sleep.
    rtc_gpio_hold_dis(ZECTRIX_BUTTON_DOWN);
    rtc_gpio_deinit(ZECTRIX_BUTTON_DOWN);
    gpio_hold_dis(ZECTRIX_VBAT_LATCH);
    gpio_set_level(ZECTRIX_VBAT_LATCH, 1);

    gpio_config_t outputs = {};
    outputs.pin_bit_mask = (1ULL << ZECTRIX_VBAT_LATCH) |
                           (1ULL << ZECTRIX_AUDIO_POWER) |
                           (1ULL << ZECTRIX_POWER_LED);
    outputs.mode = GPIO_MODE_OUTPUT;
    outputs.pull_up_en = GPIO_PULLUP_DISABLE;
    outputs.pull_down_en = GPIO_PULLDOWN_DISABLE;
    outputs.intr_type = GPIO_INTR_DISABLE;
    esp_err_t err = gpio_config(&outputs);
    if (err != ESP_OK) {
        return err;
    }

    gpio_set_level(ZECTRIX_VBAT_LATCH, 1);
    gpio_hold_en(ZECTRIX_VBAT_LATCH);
    SetAudioPower(true);
    SetPowerLed(false);

    gpio_config_t inputs = {};
    inputs.pin_bit_mask = (1ULL << ZECTRIX_BUTTON_UP) |
                          (1ULL << ZECTRIX_BUTTON_DOWN) |
                          (1ULL << ZECTRIX_BUTTON_OK);
    inputs.mode = GPIO_MODE_INPUT;
    inputs.pull_up_en = GPIO_PULLUP_ENABLE;
    inputs.pull_down_en = GPIO_PULLDOWN_DISABLE;
    inputs.intr_type = GPIO_INTR_DISABLE;
    return gpio_config(&inputs);
}

esp_err_t ZectrixBoard::InitI2c() {
    i2c_master_bus_config_t config = {};
    config.i2c_port = I2C_NUM_0;
    config.sda_io_num = ZECTRIX_I2C_SDA;
    config.scl_io_num = ZECTRIX_I2C_SCL;
    config.clk_source = I2C_CLK_SRC_DEFAULT;
    config.glitch_ignore_cnt = 7;
    config.intr_priority = 0;
    config.trans_queue_depth = 0;
    config.flags.enable_internal_pullup = 1;
    return i2c_new_master_bus(&config, &i2c_bus_);
}

void ZectrixBoard::InitBatteryAdc() {
    adc_oneshot_unit_init_cfg_t unit_config = {};
    unit_config.unit_id = ADC_UNIT_1;
    if (adc_oneshot_new_unit(&unit_config, &adc_handle_) != ESP_OK) {
        adc_handle_ = nullptr;
        ESP_LOGW(kTag, "battery ADC unit initialization failed");
        return;
    }

    adc_oneshot_chan_cfg_t channel_config = {};
    channel_config.atten = ADC_ATTEN_DB_12;
    channel_config.bitwidth = ADC_BITWIDTH_12;
    if (adc_oneshot_config_channel(adc_handle_, kBatteryAdcChannel,
                                   &channel_config) != ESP_OK) {
        ESP_LOGW(kTag, "battery ADC channel configuration failed");
        return;
    }

    adc_cali_curve_fitting_config_t calibration_config = {};
    calibration_config.unit_id = ADC_UNIT_1;
    calibration_config.atten = ADC_ATTEN_DB_12;
    calibration_config.bitwidth = ADC_BITWIDTH_12;
    if (adc_cali_create_scheme_curve_fitting(&calibration_config,
                                              &adc_cali_) != ESP_OK) {
        adc_cali_ = nullptr;
        ESP_LOGW(kTag, "battery ADC calibration is unavailable");
    }
}

esp_err_t ZectrixBoard::Init() {
    esp_err_t err = InitPowerAndGpio();
    if (err != ESP_OK) {
        return err;
    }

    button_queue_ = xQueueCreate(1, sizeof(uint8_t));
    if (button_queue_ == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    button_task_done_ = xSemaphoreCreateBinary();
    if (button_task_done_ == nullptr) return ESP_ERR_NO_MEM;
    button_task_stop_.store(false);
    if (xTaskCreate(ButtonTaskEntry, "zectrix_buttons", 3072, this, 5,
                    nullptr) != pdPASS) {
        vSemaphoreDelete(button_task_done_);
        button_task_done_ = nullptr;
        return ESP_ERR_NO_MEM;
    }

    InitBatteryAdc();
    const int64_t now_ms = esp_timer_get_time() / 1000;
    charge_status_.Init(CHARGE_DETECT_GPIO, CHARGE_FULL_GPIO, now_ms);
    ReadPowerSnapshot();

    err = InitI2c();
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "I2C initialization failed: %s", esp_err_to_name(err));
        return err;
    }

    rtc_.reset(new (std::nothrow) RtcPcf8563(i2c_bus_, ZECTRIX_RTC_ADDR));
    if (rtc_ == nullptr || !rtc_->Init(ZECTRIX_RTC_INT) ||
        i2c_master_probe(i2c_bus_, ZECTRIX_RTC_ADDR, 200) != ESP_OK) {
        ESP_LOGW(kTag, "RTC is unavailable");
        rtc_.reset();
    }

    nfc_.reset(new (std::nothrow) ZectrixNfc(
        i2c_bus_, ZECTRIX_NFC_ADDR, ZECTRIX_NFC_POWER,
        ZECTRIX_NFC_FD, ZECTRIX_NFC_FD_ACTIVE_LEVEL));
    if (nfc_ == nullptr || !nfc_->Init()) {
        ESP_LOGW(kTag, "NFC is unavailable");
        nfc_.reset();
    }

    ESP_LOGI(kTag, "board initialized rtc=%d nfc=%d",
             rtc_ != nullptr, nfc_ != nullptr);
    return ESP_OK;
}

void ZectrixBoard::ButtonTaskEntry(void* arg) {
    static_cast<ZectrixBoard*>(arg)->ButtonTask();
}

void ZectrixBoard::ButtonTask() {
    std::array<ButtonState, kButtons.size()> states = {};
    const TickType_t start = xTaskGetTickCount();
    for (size_t i = 0; i < kButtons.size(); ++i) {
        const int level = gpio_get_level(kButtons[i].gpio);
        states[i].stable_level = level;
        states[i].sampled_level = level;
        states[i].sampled_at = start;
        states[i].armed = level != 0;
    }

    while (!button_task_stop_.load(std::memory_order_acquire)) {
        const TickType_t now = xTaskGetTickCount();
        for (size_t i = 0; i < kButtons.size(); ++i) {
            ButtonState& state = states[i];
            const ButtonDefinition& definition = kButtons[i];
            const int sampled = gpio_get_level(definition.gpio);
            if (sampled != state.sampled_level) {
                state.sampled_level = sampled;
                state.sampled_at = now;
            }

            if (sampled != state.stable_level &&
                now - state.sampled_at >= kButtonDebounce) {
                state.stable_level = sampled;
                if (sampled == 0) {
                    if (state.armed) {
                        state.pressed_at = now;
                        state.long_sent = false;
                        if (ClickOnPress(definition.button)) {
                            const ZectrixButtonEvent event = {
                                definition.button,
                                ZectrixButtonAction::kClick};
                            QueueButtonEvent(event);
                        }
                    }
                } else if (!state.armed) {
                    state.armed = true;
                } else if (!state.long_sent &&
                           !ClickOnPress(definition.button)) {
                    const ZectrixButtonEvent event = {
                        definition.button, ZectrixButtonAction::kClick};
                    QueueButtonEvent(event);
                }
            }

            if (state.armed && state.stable_level == 0 &&
                !state.long_sent) {
                const TickType_t threshold = LongPressTicks(definition.button);
                if (threshold != portMAX_DELAY &&
                    now - state.pressed_at >= threshold) {
                    state.long_sent = true;
                    const ZectrixButtonEvent event = {
                        definition.button, ZectrixButtonAction::kLongPress};
                    QueueButtonEvent(event);
                }
            }
        }
        vTaskDelay(kButtonPoll);
    }
    xSemaphoreGive(button_task_done_);
    vTaskDelete(nullptr);
}

void ZectrixBoard::QueueButtonEvent(const ZectrixButtonEvent& event) {
#if CONFIG_ZECTRIX_ENABLE_USB_CLI
    const auto timestamp = esp_timer_get_time();
#endif
    portENTER_CRITICAL(&button_lock_);
    const bool accepted = button_events_.Push(event);
#if CONFIG_ZECTRIX_ENABLE_USB_CLI
    if (event.action != ZectrixButtonAction::kWake)
        input_trace_.Push(timestamp, static_cast<uint8_t>(event.button), static_cast<uint8_t>(event.action), accepted);
#endif
    portEXIT_CRITICAL(&button_lock_);
    if (accepted) {
        const uint8_t signal = 1;
        xQueueSend(button_queue_, &signal, 0);
    }
}

zectrix::input::TraceBatch ZectrixBoard::ReadInputTrace(uint64_t cursor) {
#if CONFIG_ZECTRIX_ENABLE_USB_CLI
    portENTER_CRITICAL(&button_lock_);
    const auto batch = input_trace_.Read(cursor);
    portEXIT_CRITICAL(&button_lock_);
    return batch;
#else
    (void)cursor;
    return {};
#endif
}

bool ZectrixBoard::WaitButton(ZectrixButtonEvent* event, TickType_t timeout) {
    if (event == nullptr || button_queue_ == nullptr) return false;
    const TickType_t started = xTaskGetTickCount();
    for (;;) {
        portENTER_CRITICAL(&button_lock_);
        const bool received = button_events_.Pop(event);
        portEXIT_CRITICAL(&button_lock_);
        if (received) return true;
        if (button_wait_wake_pending_.exchange(false)) {
            *event = {ZectrixButton::kOk, ZectrixButtonAction::kWake};
            return true;
        }
        // A stale or coalesced signal must not extend the caller's deadline.
        const TickType_t elapsed = xTaskGetTickCount() - started;
        const TickType_t remaining = timeout == portMAX_DELAY ? portMAX_DELAY :
            (elapsed < timeout ? timeout - elapsed : 0);
        uint8_t signal;
        if (xQueueReceive(button_queue_, &signal, remaining) != pdTRUE) return false;
    }
}

void ZectrixBoard::WakeButtonWait() {
    if (button_queue_ == nullptr || button_wait_wake_pending_.exchange(true)) return;
    const uint8_t signal = 1;
    // A full signal queue already makes the owner's next wait runnable.
    xQueueSend(button_queue_, &signal, 0);
}

void ZectrixBoard::DrainButtons() {
    if (button_queue_ == nullptr) {
        return;
    }
    ZectrixButtonEvent event;
    while (WaitButton(&event, 0)) {}
}

AudioCodec* ZectrixBoard::PrepareAudio() {
    if (i2c_bus_ == nullptr) return nullptr;
    if (audio_ == nullptr) {
        // esp_codec_dev expects the 8 bit shifted address and right shifts it
        // itself, so the library constant (0x30) must be used rather than the
        // 7 bit address 0x18 that the other devices on this bus use.
        audio_.reset(new (std::nothrow) Es8311AudioCodec(
            i2c_bus_, I2C_NUM_0, ZECTRIX_AUDIO_SAMPLE_RATE,
            ZECTRIX_AUDIO_SAMPLE_RATE, ZECTRIX_AUDIO_MCLK,
            ZECTRIX_AUDIO_BCLK, ZECTRIX_AUDIO_WS, ZECTRIX_AUDIO_DOUT,
            ZECTRIX_AUDIO_DIN, ZECTRIX_AUDIO_PA, ES8311_CODEC_DEFAULT_ADDR));
        if (audio_ == nullptr) return nullptr;
    }
    if (!audio_started_) {
        audio_->Start();
        audio_->SetOutputVolume(80);
        audio_started_ = true;
    }
    return audio_.get();
}

bool ZectrixBoard::ReadBattery(uint16_t* voltage_mv, uint8_t* percent) {
    if (voltage_mv == nullptr || percent == nullptr || adc_handle_ == nullptr ||
        adc_cali_ == nullptr) {
        return false;
    }

    int voltage_sum = 0;
    for (int i = 0; i < 10; ++i) {
        int raw = 0;
        int pin_mv = 0;
        if (adc_oneshot_read(adc_handle_, kBatteryAdcChannel, &raw) != ESP_OK ||
            adc_cali_raw_to_voltage(adc_cali_, raw, &pin_mv) != ESP_OK) {
            return false;
        }
        voltage_sum += pin_mv * 2;
    }

    const int average_mv = voltage_sum / 10;
    const int calculated =
        (-average_mv * average_mv + 9016 * average_mv - 19189000) / 10000;
    *voltage_mv = static_cast<uint16_t>(std::max(0, average_mv));
    *percent = static_cast<uint8_t>(std::clamp(calculated, 0, 100));
    return average_mv > 0;
}

ZectrixPowerSnapshot ZectrixBoard::ReadPowerSnapshot() {
    ZectrixPowerSnapshot result;
    result.battery_valid = ReadBattery(&result.battery_mv,
                                       &result.battery_percent);
    charge_status_.Tick(esp_timer_get_time() / 1000, result.battery_mv,
                        result.battery_valid);
    result.charge = charge_status_.Get();
    return result;
}

bool ZectrixBoard::HasRtc() const { return rtc_ != nullptr; }

bool ZectrixBoard::ReadRtc(tm* value) {
    return rtc_ != nullptr && value != nullptr && rtc_->GetTime(*value);
}

bool ZectrixBoard::WriteRtc(const tm& value) {
    return rtc_ != nullptr && rtc_->SetTime(value);
}

bool ZectrixBoard::StopRtcClock() {
    return rtc_ != nullptr && rtc_->StopClock();
}

bool ZectrixBoard::StartRtcCountdown(uint8_t seconds) {
    return rtc_ != nullptr && rtc_->StartCountdownTimer(seconds);
}

bool ZectrixBoard::StopRtcCountdown() {
    return rtc_ != nullptr && rtc_->StopCountdownTimer();
}

bool ZectrixBoard::ClearRtcTimerFlag() {
    return rtc_ != nullptr && rtc_->ClearTimerFlag();
}

esp_err_t ZectrixBoard::ReadRtcTimerFlag(bool* fired) {
    if (fired == nullptr) return ESP_ERR_INVALID_ARG;
    if (rtc_ == nullptr) return ESP_ERR_NOT_FOUND;
    return rtc_->ReadTimerFlag(fired);
}

bool ZectrixBoard::IsRtcInterruptActive() const {
    return rtc_ != nullptr && gpio_get_level(ZECTRIX_RTC_INT) == 0;
}

bool ZectrixBoard::HasNfc() const { return nfc_ != nullptr; }

void ZectrixBoard::SetPowerLed(bool on) {
    gpio_hold_dis(ZECTRIX_POWER_LED);
    gpio_set_level(ZECTRIX_POWER_LED, on ? 0 : 1);
    gpio_hold_en(ZECTRIX_POWER_LED);
}

void ZectrixBoard::SetAudioPower(bool on) {
    gpio_hold_dis(ZECTRIX_AUDIO_POWER);
    gpio_set_level(ZECTRIX_AUDIO_POWER, on ? 1 : 0);
    gpio_hold_en(ZECTRIX_AUDIO_POWER);
}

void ZectrixBoard::CutBatteryPower() {
    gpio_hold_dis(ZECTRIX_VBAT_LATCH);
    gpio_set_level(ZECTRIX_VBAT_LATCH, 0);
    gpio_hold_en(ZECTRIX_VBAT_LATCH);
    // USB can keep the MCU powered after the battery latch is released.
    gpio_deep_sleep_hold_en();
}

esp_err_t ZectrixBoard::PreparePowerButtonWake() {
    // A held shutdown button must not trigger an immediate reboot. Give the
    // user up to five seconds to release it, then keep rail-off as the fallback.
    unsigned released = 0;
    for (unsigned poll = 0; poll < 250; ++poll) {
        released = gpio_get_level(ZECTRIX_BUTTON_DOWN) ? released + 1 : 0;
        if (released == 3) break;
        vTaskDelay(kButtonPoll);
    }
    if (released < 3) return ESP_ERR_TIMEOUT;
    esp_err_t result = rtc_gpio_init(ZECTRIX_BUTTON_DOWN);
    if (result == ESP_OK) result = rtc_gpio_set_direction(ZECTRIX_BUTTON_DOWN, RTC_GPIO_MODE_INPUT_ONLY);
    if (result == ESP_OK) result = rtc_gpio_pullup_en(ZECTRIX_BUTTON_DOWN);
    if (result == ESP_OK) result = rtc_gpio_pulldown_dis(ZECTRIX_BUTTON_DOWN);
    if (result == ESP_OK) result = esp_sleep_enable_ext1_wakeup_io(1ULL << ZECTRIX_BUTTON_DOWN, ESP_EXT1_WAKEUP_ANY_LOW);
    return result;
}
