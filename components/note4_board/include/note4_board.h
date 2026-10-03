#ifndef NOTE4_BOARD_H_
#define NOTE4_BOARD_H_

#include <array>
#include <atomic>
#include <cstdint>
#include <ctime>
#include <memory>

#include "charge_status.h"
#include "driver/i2c_master.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "note4_button_buffer.h"
#include "note4_input_trace.h"
#include "sdkconfig.h"

class AudioCodec;
class RtcPcf8563;
class Note4Nfc;

struct Note4PowerSnapshot {
    bool battery_valid = false;
    uint16_t battery_mv = 0;
    uint8_t battery_percent = 0;
    ChargeStatus::Snapshot charge = {};
};

class Note4Board {
public:
    Note4Board();
    ~Note4Board();

    esp_err_t Init();
    // The lifecycle owner must first stop services using board devices.
    // Safe after partial initialization and on repeated cleanup attempts.
    esp_err_t ShutdownPeripherals();
    bool WaitButton(Note4ButtonEvent* event, TickType_t timeout);
    void WakeButtonWait();
    void DrainButtons();
    note4::input::TraceBatch ReadInputTrace(uint64_t cursor);

    bool HasRtc() const;
    bool ReadRtc(tm* value);
    bool StopRtcClock();
    bool WriteRtc(const tm& value);
    bool StartRtcCountdown(uint8_t seconds);
    bool StopRtcCountdown();
    bool ClearRtcTimerFlag();
    esp_err_t ReadRtcTimerFlag(bool* fired);
    bool IsRtcInterruptActive() const;
    bool HasNfc() const;
    Note4Nfc* nfc() const { return nfc_.get(); }
    AudioCodec* PrepareAudio();
    i2c_master_bus_handle_t i2c_bus() const { return i2c_bus_; }

    Note4PowerSnapshot ReadPowerSnapshot();
    void SetPowerLed(bool on);
    void SetAudioPower(bool on);
    void CutBatteryPower();
    // Arm the released DOWN button for the USB-powered deep-sleep fallback.
    esp_err_t PreparePowerButtonWake();

private:
    struct ButtonState {
        int stable_level = 1;
        int sampled_level = 1;
        TickType_t sampled_at = 0;
        TickType_t pressed_at = 0;
        bool armed = false;
        bool long_sent = false;
    };

    static void ButtonTaskEntry(void* arg);
    void ButtonTask();
    void QueueButtonEvent(const Note4ButtonEvent& event);
    esp_err_t InitPowerAndGpio();
    esp_err_t InitI2c();
    void InitBatteryAdc();
    bool ReadBattery(uint16_t* voltage_mv, uint8_t* percent);

    i2c_master_bus_handle_t i2c_bus_ = nullptr;
    adc_oneshot_unit_handle_t adc_handle_ = nullptr;
    adc_cali_handle_t adc_cali_ = nullptr;
    QueueHandle_t button_queue_ = nullptr;
    portMUX_TYPE button_lock_ = portMUX_INITIALIZER_UNLOCKED;
    Note4ButtonBuffer button_events_;
#if CONFIG_NOTE4_ENABLE_USB_CLI
    note4::input::InputTrace input_trace_;
#endif
    std::atomic<bool> button_wait_wake_pending_{false};
    SemaphoreHandle_t button_task_done_ = nullptr;
    std::atomic<bool> button_task_stop_{false};
    std::unique_ptr<RtcPcf8563> rtc_;
    std::unique_ptr<Note4Nfc> nfc_;
    std::unique_ptr<AudioCodec> audio_;
    ChargeStatus charge_status_;
    bool audio_started_ = false;
};

#endif  // NOTE4_BOARD_H_
