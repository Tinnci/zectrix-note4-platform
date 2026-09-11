#include "zectrix_board.h"
#include "zectrix_board_config.h"
#include "zectrix_nfc.h"
#include "zectrix_nfc_service.h"
#include "zectrix_power_service.h"
#include "audio_codec.h"
#include "acoustic_selftest.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_codec_dev_defaults.h"
#include "esp_sleep.h"
#include "driver/rtc_io.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct BoardHostSemaphore {
    std::mutex mutex;
    std::condition_variable changed;
    std::recursive_timed_mutex recursive;
    bool ready = false;
};
struct BoardHostTask {
    std::string name;
    std::thread worker;
    std::mutex mutex;
    std::condition_variable changed;
    uint32_t notifications = 0;
    std::atomic<bool> deleted{false};
    std::atomic<unsigned> waits{0};
};
struct BoardHostQueue {
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<uint8_t> signals;
    UBaseType_t capacity = 0;
};
struct BoardHostI2cBus { unsigned devices = 0; };
struct BoardHostI2cDevice { BoardHostI2cBus* bus; uint16_t address; };
struct BoardHostI2sChannel { bool enabled = false; };
struct BoardHostAdc {};
struct BoardHostAdcCalibration {};
struct audio_codec_ctrl_if_t { i2c_master_dev_handle_t device; };
struct audio_codec_data_if_t { i2s_chan_handle_t rx, tx; };
struct audio_codec_gpio_if_t {};
struct audio_codec_if_t {};
struct BoardHostCodecDevice { const audio_codec_data_if_t* data; };

namespace {
using namespace std::chrono_literals;
struct TaskExit {};
struct SleepEntered {};
enum class Failure {
    None, Gpio, Queue, ButtonSemaphore, ButtonTask, I2cBus,
    NfcSemaphore, NfcTask, NfcIsr, NfcProbe, Codec, PlaybackSemaphore, PlaybackTask,
};
Failure failure = Failure::None;
thread_local TaskHandle_t current_task = nullptr;
std::vector<std::unique_ptr<BoardHostTask>> tasks;
std::array<std::atomic<int>, 49> levels{};
std::array<bool, 49> held{};
std::array<int, 49> modes{}, pullups{};
std::array<bool, 49> rtc_mode{}, rtc_pullup{}, rtc_pulldown{};
uint64_t wake_pins = 0;
bool fail_wake = false;
int release_after_polls = -1;
std::mutex isr_mutex;
void (*field_isr)(void*) = nullptr;
void* field_context = nullptr;
unsigned semaphores = 0, semaphore_calls = 0, queues = 0, i2c_buses = 0;
unsigned adc_units = 0, calibrations = 0, channels = 0, data_interfaces = 0;
unsigned control_interfaces = 0, gpio_interfaces = 0, codecs = 0, codec_devices = 0;
bool deep_sleep_hold = false, fail_bus_delete = false, interleave_stop = false;
std::atomic<bool> callback_entered{false}, callback_release{false};
bool delayed_playback = false;
std::atomic<bool> playback_entered{false}, playback_release{false};
std::atomic<bool> playback_timed_out{false}, playback_join_retry{false};
unsigned field_waits_before_stop = 0;
BoardHostTask* field_task = nullptr;
std::atomic<unsigned> button_polls{0}, queue_waits{0};
const auto epoch = std::chrono::steady_clock::now();
std::array<uint8_t, 16> rtc_registers{};
unsigned rtc_calendar_writes = 0, rtc_control_writes = 0;
bool rtc_fail_stop = false, rtc_fail_calendar = false, rtc_fail_resume = false;

template <typename Predicate>
void WaitFor(Predicate ready) {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!ready()) {
        assert(std::chrono::steady_clock::now() < deadline);
        std::this_thread::yield();
    }
}

void AssertReleased() {
    assert(semaphores == 0 && queues == 0 && i2c_buses == 0);
    assert(adc_units == 0 && calibrations == 0 && channels == 0);
    assert(data_interfaces == 0 && control_interfaces == 0 && gpio_interfaces == 0);
    assert(codecs == 0 && codec_devices == 0 && field_isr == nullptr);
}

void Reset() {
    for (auto& task : tasks) {
        task->worker.join();
        assert(task->deleted);
    }
    tasks.clear();
    AssertReleased();
    rtc_registers = {};
    rtc_registers[0x02] = 0x80;
    rtc_registers[0x0d] = 0x80;
    rtc_calendar_writes = rtc_control_writes = 0;
    rtc_fail_stop = rtc_fail_calendar = rtc_fail_resume = false;
    for (auto& level : levels) level = 1;
    held = {};
    modes = {};
    pullups = {};
    rtc_mode = rtc_pullup = rtc_pulldown = {};
    wake_pins = 0;
    fail_wake = false;
    release_after_polls = -1;
    failure = Failure::None;
    semaphore_calls = 0;
    deep_sleep_hold = fail_bus_delete = interleave_stop = false;
    callback_entered = callback_release = false;
    delayed_playback = false;
    playback_entered = playback_release = playback_timed_out = playback_join_retry = false;
    field_task = nullptr;
    button_polls = queue_waits = 0;
}

void TestButtonBuffer() {
    const ZectrixButtonEvent up{ZectrixButton::kUp, ZectrixButtonAction::kClick};
    const ZectrixButtonEvent down{ZectrixButton::kDown, ZectrixButtonAction::kClick};
    const ZectrixButtonEvent ok{ZectrixButton::kOk, ZectrixButtonAction::kClick};
    const ZectrixButtonEvent back{ZectrixButton::kOk, ZectrixButtonAction::kLongPress};
    const ZectrixButtonEvent shutdown{ZectrixButton::kDown, ZectrixButtonAction::kLongPress};
    ZectrixButtonEvent event;
    ZectrixButtonBuffer buffer;
    assert(!buffer.Pop(&event) && !buffer.Pop(nullptr));
    for (std::size_t i = 0; i < ZectrixButtonBuffer::kCapacity; ++i)
        assert(buffer.Push(i % 2 ? up : down));
    assert(!buffer.Push(down));
    assert(buffer.Push(ok) && buffer.Push(back));
    for (std::size_t i = 0; i < ZectrixButtonBuffer::kCapacity - 2; ++i) {
        assert(buffer.Pop(&event) && event.button == (i % 2 ? up.button : down.button));
        assert(event.action == ZectrixButtonAction::kClick);
    }
    assert(buffer.Pop(&event) && event.button == ok.button && event.action == ok.action);
    assert(buffer.Pop(&event) && event.button == back.button && event.action == back.action);
    assert(!buffer.Pop(&event));
    for (std::size_t i = 0; i < ZectrixButtonBuffer::kCapacity; ++i) assert(buffer.Push(ok));
    assert(!buffer.Push(ok) && buffer.Push(back));
    for (std::size_t i = 0; i < ZectrixButtonBuffer::kCapacity - 1; ++i)
        assert(buffer.Pop(&event) && event.action == ok.action);
    assert(buffer.Pop(&event) && event.action == back.action);
    for (std::size_t i = 0; i < ZectrixButtonBuffer::kCapacity; ++i) assert(buffer.Push(back));
    assert(!buffer.Push(back) && buffer.Push(shutdown));
    assert(!buffer.Push(up) && !buffer.Push(ok) && !buffer.Push(back));
    assert(buffer.Push(shutdown));
    assert(buffer.Pop(&event) && event.button == shutdown.button && event.action == shutdown.action);
    assert(!buffer.Pop(&event));
    assert(buffer.Push(up) && buffer.Pop(&event) && event.button == up.button);
}

void TestButtonProducerAndWake() {
    Reset();
    {
        ZectrixBoard board;
        assert(board.Init() == ESP_OK);
        WaitFor([] { return button_polls.load() > 0; });
        std::thread waiter([&] {
            ZectrixButtonEvent event;
            assert(board.WaitButton(&event, portMAX_DELAY));
            assert(event.action == ZectrixButtonAction::kWake);
        });
        WaitFor([] { return queue_waits.load() > 0; });
        for (unsigned i = 0; i < 1000; ++i) board.WakeButtonWait();
        waiter.join();
        board.DrainButtons();
        ZectrixButtonEvent event;
        assert(!board.WaitButton(&event, 0));
        const auto trace_cursor = board.ReadInputTrace(0).cursor;
        assert(board.ReadInputTrace(trace_cursor).count == 0);

        // Sampling proceeds while the foreground is busy and the one-slot
        // signal queue is full. Wake traffic must not consume physical input.
        std::atomic<bool> stop{false};
        std::thread waker([&] {
            while (!stop.load()) { board.WakeButtonWait(); std::this_thread::yield(); }
        });
        levels[ZECTRIX_BUTTON_UP] = 0;
        std::this_thread::sleep_for(100ms);
        stop = true;
        waker.join();
        const auto trace = board.ReadInputTrace(trace_cursor);
        assert(trace.count == 1 && trace.records[0].button == 0 && trace.records[0].action == 0 && trace.records[0].queued);
        assert(board.ReadInputTrace(trace_cursor).records[0].sequence == trace.records[0].sequence);
        assert(board.WaitButton(&event, 0));
        assert(event.button == ZectrixButton::kUp && event.action == ZectrixButtonAction::kClick);
        board.DrainButtons();
        const auto started = std::chrono::steady_clock::now();
        assert(!board.WaitButton(&event, pdMS_TO_TICKS(20)));
        assert(std::chrono::steady_clock::now() - started >= 20ms);
        assert(board.ShutdownPeripherals() == ESP_OK);
    }
    Reset();
}

void EmitField(bool present) {
    levels[ZECTRIX_NFC_FD] = present ? ZECTRIX_NFC_FD_ACTIVE_LEVEL
                                     : !ZECTRIX_NFC_FD_ACTIVE_LEVEL;
    std::lock_guard<std::mutex> lock(isr_mutex);
    if (field_isr != nullptr) field_isr(field_context);
}

void BlockField(bool present) {
    if (!present) return;
    callback_entered = true;
    WaitFor([] { return callback_release.load(); });
}

void TestPowerTransition() {
    for (unsigned audio_mode = 0; audio_mode < 4; ++audio_mode) {
        Reset();
        ZectrixBoard board;
        assert(board.Init() == ESP_OK);
        assert(board.HasRtc() && board.HasNfc());
        assert(rtc_registers[0x0d] == 0);
        zectrix::nfc::NfcService* nfc = nullptr;
        assert(zectrix::nfc::NfcService::Attach(*board.nfc(), &nfc) == ESP_OK);
        if (audio_mode != 0) {
            if (audio_mode == 3) failure = Failure::Codec;
            auto* audio = board.PrepareAudio();
            assert(audio != nullptr && audio->valid() == (audio_mode != 3));
            assert(channels == 2 && data_interfaces == 1);
            if (audio_mode == 2) {
                audio->EnableInput(false);
                audio->EnableOutput(false);
            }
        }
        delete nfc;
        zectrix::power::PowerService* power = nullptr;
        assert(zectrix::power::PowerService::Attach(board, &power) == ESP_OK);
        try { power->Shutdown(); } catch (const SleepEntered&) {}
        AssertReleased();
        assert(rtc_control_writes == 0 && rtc_calendar_writes == 0);
        assert(rtc_registers[0x02] == 0x80);
        assert(deep_sleep_hold);
        assert(wake_pins == (1ULL << ZECTRIX_BUTTON_DOWN));
        assert(rtc_mode[ZECTRIX_BUTTON_DOWN] && rtc_pullup[ZECTRIX_BUTTON_DOWN]);
        for (const int pin : {ZECTRIX_AUDIO_POWER, ZECTRIX_NFC_POWER, ZECTRIX_VBAT_LATCH}) {
            assert(levels[pin] == 0 && held[pin]);
        }
        assert(levels[ZECTRIX_POWER_LED] == 1 && held[ZECTRIX_POWER_LED]);
        if (audio_mode != 0) assert(levels[ZECTRIX_AUDIO_PA] == 0 && held[ZECTRIX_AUDIO_PA]);
        assert(modes[ZECTRIX_I2C_SDA] == GPIO_MODE_DISABLE && pullups[ZECTRIX_I2C_SDA] == 0);
        assert(modes[ZECTRIX_I2C_SCL] == GPIO_MODE_DISABLE && pullups[ZECTRIX_I2C_SCL] == 0);
        for (int pin : {ZECTRIX_AUDIO_MCLK, ZECTRIX_AUDIO_BCLK, ZECTRIX_AUDIO_WS,
                       ZECTRIX_AUDIO_DOUT, ZECTRIX_AUDIO_DIN}) {
            assert(modes[pin] == GPIO_MODE_DISABLE && pullups[pin] == 0);
        }
        assert(board.ShutdownPeripherals() == ESP_OK);
        assert(board.PrepareAudio() == nullptr && !board.HasRtc() && !board.HasNfc());
        delete power;
    }
    Reset();
}

void TestRtcCalendar() {
    Reset();
    {
        ZectrixBoard board;
        assert(board.Init() == ESP_OK);
        tm value{};
        assert(!board.ReadRtc(&value));
        value.tm_year = 124;
        value.tm_mon = 1;
        value.tm_mday = 29;
        value.tm_wday = 4;
        value.tm_hour = 23;
        value.tm_min = 59;
        value.tm_sec = 58;
        assert(board.WriteRtc(value));
        assert(rtc_control_writes == 2 && rtc_calendar_writes == 1);
        assert(rtc_registers[0] == 0 && rtc_registers[2] == 0x58 && rtc_registers[8] == 0x24);
        tm read{};
        assert(board.ReadRtc(&read));
        assert(read.tm_year == 124 && read.tm_mon == 1 && read.tm_mday == 29 && read.tm_sec == 58);
        const auto saved = rtc_registers;
        for (const uint8_t invalid : {0x20, 0x80, 0x08}) {
            rtc_registers[0] = invalid;
            assert(!board.ReadRtc(&read));
        }
        rtc_registers = saved;
        for (const unsigned reg : {2u, 7u}) {
            rtc_registers[reg] |= 0x80;
            assert(!board.ReadRtc(&read));
            rtc_registers = saved;
        }
        rtc_registers[3] = 0x6a;
        assert(!board.ReadRtc(&read));
        rtc_registers = saved;
        const unsigned writes = rtc_calendar_writes, control = rtc_control_writes;
        value.tm_mday = 30;
        assert(!board.WriteRtc(value));
        value.tm_mday = 29;
        value.tm_year = 126;
        assert(!board.WriteRtc(value));
        assert(rtc_calendar_writes == writes && rtc_control_writes == control);
        assert(board.ShutdownPeripherals() == ESP_OK);
        assert(rtc_registers == saved);
    }
    {
        // Peripheral teardown and a fresh board owner leave the RTC running.
        ZectrixBoard rebooted;
        assert(rebooted.Init() == ESP_OK);
        tm read{};
        assert(rebooted.ReadRtc(&read) && read.tm_year == 124 && read.tm_mday == 29);
        assert(rtc_control_writes == 2 && rtc_calendar_writes == 1);
    }
    Reset();
    for (unsigned stage = 0; stage < 3; ++stage) {
        {
            ZectrixBoard board;
            assert(board.Init() == ESP_OK);
            tm value{};
            value.tm_year = 126;
            value.tm_mon = 0;
            value.tm_mday = 1;
            value.tm_wday = 4;
            assert(board.WriteRtc(value));
            rtc_fail_stop = stage == 0;
            rtc_fail_calendar = stage == 1;
            rtc_fail_resume = stage == 2;
            value.tm_hour = 9;
            assert(!board.WriteRtc(value));
            assert((rtc_registers[0] & 0x20) == (stage == 0 ? 0 : 0x20));
        }
        {
            ZectrixBoard rebooted;
            assert(rebooted.Init() == ESP_OK);
            tm read{};
            assert(rebooted.ReadRtc(&read) == (stage == 0));
            rtc_fail_stop = rtc_fail_calendar = rtc_fail_resume = false;
            tm corrected{};
            corrected.tm_year = 126;
            corrected.tm_mon = 8;
            corrected.tm_mday = 10;
            corrected.tm_wday = 4;
            assert(rebooted.WriteRtc(corrected));
            assert(rebooted.ReadRtc(&read) && read.tm_mon == 8);
            assert(rtc_registers[0] == 0);
        }
        Reset();
    }
}

void TestPowerButtonWake() {
    for (unsigned scenario = 0; scenario < 4; ++scenario) {
        Reset();
        ZectrixBoard board;
        rtc_mode[ZECTRIX_BUTTON_DOWN] = held[ZECTRIX_BUTTON_DOWN] = true;
        assert(board.Init() == ESP_OK);
        assert(!rtc_mode[ZECTRIX_BUTTON_DOWN] && !held[ZECTRIX_BUTTON_DOWN]);
        assert(board.ShutdownPeripherals() == ESP_OK);
        if (scenario == 1) { levels[ZECTRIX_BUTTON_DOWN] = 0; release_after_polls = 4; }
        if (scenario == 2) levels[ZECTRIX_BUTTON_DOWN] = 0;
        if (scenario == 3) fail_wake = true;
        zectrix::power::PowerService* power = nullptr;
        assert(zectrix::power::PowerService::Attach(board, &power) == ESP_OK);
        try { power->Shutdown(); } catch (const SleepEntered&) {}
        assert(wake_pins == (scenario < 2 ? 1ULL << ZECTRIX_BUTTON_DOWN : 0));
        assert(deep_sleep_hold && levels[ZECTRIX_VBAT_LATCH] == 0 && held[ZECTRIX_VBAT_LATCH]);
        AssertReleased();
        delete power;
    }
    Reset();
}

void TestPartialInitialization() {
    for (const auto point : {Failure::Gpio, Failure::Queue, Failure::ButtonSemaphore,
             Failure::ButtonTask, Failure::I2cBus, Failure::NfcSemaphore,
             Failure::NfcTask, Failure::NfcIsr, Failure::NfcProbe}) {
        Reset();
        failure = point;
        {
            ZectrixBoard board;
            const auto result = board.Init();
            const bool optional = point >= Failure::NfcSemaphore;
            assert((result == ESP_OK) == optional);
            if (optional) assert(!board.HasNfc());
        }
        AssertReleased();
    }
    Reset();
    {
        ZectrixBoard board;
        assert(board.Init() == ESP_OK);
        fail_bus_delete = true;
        assert(board.ShutdownPeripherals() == ESP_FAIL);
        assert(i2c_buses == 1 && board.i2c_bus() != nullptr);
        fail_bus_delete = false;
        assert(board.ShutdownPeripherals() == ESP_OK);
        AssertReleased();
    }
    Reset();
}

void TestCallbackRemovalWaits() {
    Reset();
    {
        ZectrixBoard board;
        assert(board.Init() == ESP_OK);
        board.nfc()->SetFieldCallback(BlockField);
        EmitField(true);
        WaitFor([] { return callback_entered.load(); });
        std::promise<void> removing;
        auto detached = std::async(std::launch::async, [&] {
            removing.set_value();
            board.nfc()->SetFieldCallback(nullptr);
        });
        removing.get_future().wait();
        // A copied callback still owns its captured context until it returns.
        assert(detached.wait_for(30ms) == std::future_status::timeout);
        callback_release = true;
        detached.get();
        assert(board.ShutdownPeripherals() == ESP_OK);
    }
    Reset();
}

void TestServiceDetachAndFieldTaskExit() {
    Reset();
    {
        ZectrixBoard board;
        assert(board.Init() == ESP_OK);
        zectrix::nfc::NfcService* service = nullptr;
        assert(zectrix::nfc::NfcService::Attach(*board.nfc(), &service) == ESP_OK);
        service->SetEventCallback([] { BlockField(true); });
        EmitField(true);
        WaitFor([] { return callback_entered.load(); });
        auto detached = std::async(std::launch::async, [&] { delete service; });
        assert(detached.wait_for(30ms) == std::future_status::timeout);
        callback_release = true;
        detached.get();
        EmitField(false);
        assert(board.ShutdownPeripherals() == ESP_OK);
    }
    Reset();
    {
        ZectrixBoard board;
        assert(board.Init() == ESP_OK);
        board.nfc()->SetFieldCallback(BlockField);
        EmitField(true);
        WaitFor([] { return callback_entered.load(); });
        field_waits_before_stop = field_task->waits;
        interleave_stop = true;
        // Finish field processing while StopFieldTask is removing the ISR.
        // The task must remain alive until it receives the stop notification.
        assert(board.ShutdownPeripherals() == ESP_OK);
    }
    Reset();
}

class DelayedCodec final : public AudioCodec {
public:
    DelayedCodec() { duplex_ = true; input_sample_rate_ = output_sample_rate_ = 16000; }
private:
    int Read(int16_t*, int samples) override {
        // Supply warmup and preroll, then fail capture while playback is active.
        return reads_++ < 31 ? samples : 0;
    }
    int Write(const int16_t*, int samples) override {
        playback_entered = true;
        WaitFor([] { return playback_release.load(); });
        return samples;
    }
    unsigned reads_ = 0;
};

void TestAudioPlaybackJoin() {
    for (const auto point : {Failure::None, Failure::PlaybackSemaphore, Failure::PlaybackTask}) {
        Reset();
        failure = point;
        DelayedCodec codec;
        AcousticSelftestSummary result;
        if (point == Failure::None) {
            delayed_playback = true;
            auto finished = std::async(std::launch::async, [&] { return AcousticSelftest().Run(&codec, {}); });
            WaitFor([] { return playback_join_retry.load(); });
            assert(finished.wait_for(30ms) == std::future_status::timeout);
            playback_release = true;
            result = finished.get();
        } else {
            result = AcousticSelftest().Run(&codec, {});
            assert(!playback_entered);
        }
        assert(!result.pass);
        AssertReleased();
    }
    Reset();
}
}  // namespace

SemaphoreHandle_t xSemaphoreCreateBinary() {
    ++semaphore_calls;
    if ((failure == Failure::ButtonSemaphore && semaphore_calls == 1) ||
        (failure == Failure::NfcSemaphore && semaphore_calls == 2) ||
        failure == Failure::PlaybackSemaphore) return nullptr;
    ++semaphores;
    return new BoardHostSemaphore;
}
SemaphoreHandle_t xSemaphoreCreateRecursiveMutex() {
    static BoardHostSemaphore bus_mutex;
    return &bus_mutex;
}
BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t ticks) {
    if (delayed_playback && ticks == pdMS_TO_TICKS(1500)) {
        WaitFor([] { return playback_entered.load(); });
        playback_timed_out = true;
        return pdFALSE;
    }
    if (delayed_playback && playback_timed_out && ticks == portMAX_DELAY) playback_join_retry = true;
    std::unique_lock<std::mutex> lock(semaphore->mutex);
    const auto ready = [&] { return semaphore->ready; };
    if (ticks == portMAX_DELAY) semaphore->changed.wait(lock, ready);
    else if (!semaphore->changed.wait_for(lock, std::chrono::milliseconds(ticks), ready)) return pdFALSE;
    semaphore->ready = false;
    return pdTRUE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore) {
    std::lock_guard<std::mutex> lock(semaphore->mutex);
    semaphore->ready = true;
    semaphore->changed.notify_one();
    return pdTRUE;
}
BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t semaphore, TickType_t ticks) {
    if (ticks == portMAX_DELAY) { semaphore->recursive.lock(); return pdTRUE; }
    return semaphore->recursive.try_lock_for(std::chrono::milliseconds(ticks)) ? pdTRUE : pdFALSE;
}
BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t semaphore) { semaphore->recursive.unlock(); return pdTRUE; }
void vSemaphoreDelete(SemaphoreHandle_t semaphore) { --semaphores; delete semaphore; }
BaseType_t xTaskCreate(TaskFunction_t function, const char* name, uint32_t, void* context,
                       UBaseType_t, TaskHandle_t* output) {
    const bool field = std::strcmp(name, "zectrix_nfc_fd") == 0;
    if (std::strcmp(name, "ft_audio_tx") == 0 && failure == Failure::PlaybackTask) return pdFALSE;
    if ((field && failure == Failure::NfcTask) || (!field && failure == Failure::ButtonTask)) return pdFALSE;
    auto task = std::make_unique<BoardHostTask>();
    task->name = name;
    const auto handle = task.get();
    if (output != nullptr) *output = handle;
    if (field) field_task = handle;
    task->worker = std::thread([=] {
        current_task = handle;
        try { function(context); } catch (const TaskExit&) {}
    });
    tasks.push_back(std::move(task));
    return pdPASS;
}
BaseType_t xTaskNotify(TaskHandle_t task, uint32_t bits, eNotifyAction action) {
    std::lock_guard<std::mutex> lock(task->mutex);
    assert(!task->deleted && action == eSetBits);
    task->notifications |= bits;
    task->changed.notify_all();
    return pdTRUE;
}
BaseType_t xTaskNotifyFromISR(TaskHandle_t task, uint32_t bits, eNotifyAction action, BaseType_t* woken) {
    *woken = pdFALSE;
    return xTaskNotify(task, bits, action);
}
BaseType_t xTaskNotifyWait(uint32_t clear_entry, uint32_t clear_exit, uint32_t* bits, TickType_t ticks) {
    std::unique_lock<std::mutex> lock(current_task->mutex);
    assert(ticks == portMAX_DELAY);
    current_task->notifications &= ~clear_entry;
    ++current_task->waits;
    current_task->changed.wait(lock, [] { return current_task->notifications != 0; });
    *bits = current_task->notifications;
    current_task->notifications &= ~clear_exit;
    return pdTRUE;
}
TickType_t xTaskGetTickCount() {
    return static_cast<TickType_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - epoch).count());
}
void vTaskDelay(TickType_t ticks) {
    if (current_task && current_task->name == "zectrix_buttons") ++button_polls;
    std::this_thread::sleep_for(std::chrono::milliseconds(std::min(ticks, 1u)));
}
void vTaskDelete(TaskHandle_t task) { assert(task == nullptr); current_task->deleted = true; throw TaskExit{}; }
QueueHandle_t xQueueCreate(UBaseType_t capacity, UBaseType_t size) {
    assert(capacity == 1 && size == sizeof(uint8_t));
    if (failure == Failure::Queue) return nullptr;
    ++queues;
    auto* queue = new BoardHostQueue;
    queue->capacity = capacity;
    return queue;
}
BaseType_t xQueueSend(QueueHandle_t queue, const void* signal, TickType_t ticks) {
    assert(ticks == 0);
    std::lock_guard<std::mutex> lock(queue->mutex);
    if (queue->signals.size() == queue->capacity) return pdFALSE;
    queue->signals.push_back(*static_cast<const uint8_t*>(signal));
    queue->changed.notify_one();
    return pdTRUE;
}
BaseType_t xQueueReceive(QueueHandle_t queue, void* signal, TickType_t ticks) {
    std::unique_lock<std::mutex> lock(queue->mutex);
    ++queue_waits;
    const auto ready = [&] { return !queue->signals.empty(); };
    if (ticks == portMAX_DELAY) queue->changed.wait(lock, ready);
    else if (!queue->changed.wait_for(lock, std::chrono::milliseconds(ticks), ready)) return pdFALSE;
    *static_cast<uint8_t*>(signal) = queue->signals.front();
    queue->signals.pop_front();
    return pdTRUE;
}
void vQueueDelete(QueueHandle_t queue) { --queues; delete queue; }

esp_err_t gpio_config(const gpio_config_t* config) {
    if (failure == Failure::Gpio) return ESP_FAIL;
    for (unsigned pin = 0; pin < levels.size(); ++pin) {
        if ((config->pin_bit_mask & (1ULL << pin)) == 0) continue;
        modes[pin] = config->mode;
        pullups[pin] = config->pull_up_en;
    }
    return ESP_OK;
}
esp_err_t gpio_set_level(gpio_num_t pin, uint32_t level) {
    if (pin == ZECTRIX_AUDIO_POWER && level == 0) AssertReleased();
    levels[pin] = level;
    return ESP_OK;
}
int gpio_get_level(gpio_num_t pin) {
    if (pin == ZECTRIX_BUTTON_DOWN && release_after_polls >= 0) {
        if (release_after_polls-- == 0) levels[pin] = 1;
    }
    return levels[pin];
}
esp_err_t rtc_gpio_init(gpio_num_t pin) { rtc_mode[pin] = true; return ESP_OK; }
esp_err_t rtc_gpio_deinit(gpio_num_t pin) { rtc_mode[pin] = false; return ESP_OK; }
esp_err_t rtc_gpio_hold_dis(gpio_num_t pin) { held[pin] = false; return ESP_OK; }
esp_err_t rtc_gpio_set_direction(gpio_num_t pin, rtc_gpio_mode_t mode) {
    assert(rtc_mode[pin] && mode == RTC_GPIO_MODE_INPUT_ONLY); return ESP_OK;
}
esp_err_t rtc_gpio_pullup_en(gpio_num_t pin) { rtc_pullup[pin] = true; return ESP_OK; }
esp_err_t rtc_gpio_pulldown_dis(gpio_num_t pin) { rtc_pulldown[pin] = false; return ESP_OK; }
esp_err_t gpio_hold_dis(gpio_num_t pin) { held[pin] = false; return ESP_OK; }
esp_err_t gpio_hold_en(gpio_num_t pin) { held[pin] = true; return ESP_OK; }
void gpio_deep_sleep_hold_en() { deep_sleep_hold = true; }
void gpio_deep_sleep_hold_dis() { deep_sleep_hold = false; }
esp_err_t gpio_install_isr_service(int) { return ESP_OK; }
esp_err_t gpio_isr_handler_add(gpio_num_t, void (*handler)(void*), void* context) {
    if (failure == Failure::NfcIsr) return ESP_FAIL;
    std::lock_guard<std::mutex> lock(isr_mutex);
    field_isr = handler;
    field_context = context;
    return ESP_OK;
}
esp_err_t gpio_isr_handler_remove(gpio_num_t) {
    {
        std::lock_guard<std::mutex> lock(isr_mutex);
        field_isr = nullptr;
        field_context = nullptr;
    }
    if (interleave_stop) {
        callback_release = true;
        WaitFor([] { return field_task->deleted || field_task->waits > field_waits_before_stop; });
    }
    return ESP_OK;
}
esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t*, i2c_master_bus_handle_t* output) {
    if (failure == Failure::I2cBus) return ESP_FAIL;
    ++i2c_buses;
    *output = new BoardHostI2cBus;
    return ESP_OK;
}
esp_err_t i2c_del_master_bus(i2c_master_bus_handle_t bus) {
    assert(bus->devices == 0 && channels == 0 && field_isr == nullptr);
    if (fail_bus_delete) return ESP_FAIL;
    --i2c_buses;
    delete bus;
    return ESP_OK;
}
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus, const i2c_device_config_t* config,
                                    i2c_master_dev_handle_t* output) {
    assert(bus != nullptr);
    ++bus->devices;
    *output = new BoardHostI2cDevice{bus, config->device_address};
    return ESP_OK;
}
esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t device) {
    assert(device->bus->devices != 0);
    --device->bus->devices;
    delete device;
    return ESP_OK;
}
esp_err_t i2c_master_bus_reset(i2c_master_bus_handle_t bus) { assert(bus != nullptr); return ESP_OK; }
esp_err_t i2c_master_probe(i2c_master_bus_handle_t bus, uint16_t address, int) {
    assert(bus != nullptr && levels[ZECTRIX_AUDIO_POWER] == 1);
    return failure == Failure::NfcProbe && address == ZECTRIX_NFC_ADDR ? ESP_FAIL : ESP_OK;
}
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t device, const uint8_t* data, std::size_t size, int) {
    assert(device->bus->devices != 0 && levels[ZECTRIX_AUDIO_POWER] == 1);
    if (device->address == ZECTRIX_RTC_ADDR) {
        assert(size >= 2 && data[0] + size - 1 <= rtc_registers.size());
        if (data[0] == 0) {
            ++rtc_control_writes;
            if ((data[1] == 0x20 && rtc_fail_stop) || (data[1] == 0 && rtc_fail_resume)) return ESP_FAIL;
        }
        if (data[0] == 2) {
            assert(size == 8 && (rtc_registers[0] & 0x20) != 0);
            ++rtc_calendar_writes;
            if (rtc_fail_calendar) {
                std::memcpy(rtc_registers.data() + 2, data + 1, 3);
                return ESP_FAIL;
            }
        }
        std::memcpy(rtc_registers.data() + data[0], data + 1, size - 1);
    }
    return ESP_OK;
}
esp_err_t i2c_master_receive(i2c_master_dev_handle_t device, uint8_t* data, std::size_t size, int) {
    assert(device->bus->devices != 0 && levels[ZECTRIX_AUDIO_POWER] == 1);
    std::memset(data, 0, size);
    return ESP_OK;
}
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t device, const uint8_t* address, std::size_t address_size,
                                     uint8_t* data, std::size_t size, int timeout) {
    if (device->address == ZECTRIX_RTC_ADDR) {
        assert(address_size == 1 && address[0] + size <= rtc_registers.size());
        if (size > 1) assert(address[0] == 0 && size == 9);
        std::memcpy(data, rtc_registers.data() + address[0], size);
        return ESP_OK;
    }
    return i2c_master_receive(device, data, size, timeout);
}
esp_err_t adc_oneshot_new_unit(const adc_oneshot_unit_init_cfg_t*, adc_oneshot_unit_handle_t* output) {
    ++adc_units; *output = new BoardHostAdc; return ESP_OK;
}
esp_err_t adc_oneshot_config_channel(adc_oneshot_unit_handle_t, adc_channel_t, const adc_oneshot_chan_cfg_t*) { return ESP_OK; }
esp_err_t adc_oneshot_read(adc_oneshot_unit_handle_t, adc_channel_t, int* value) { *value = 1900; return ESP_OK; }
esp_err_t adc_oneshot_del_unit(adc_oneshot_unit_handle_t unit) { --adc_units; delete unit; return ESP_OK; }
esp_err_t adc_cali_create_scheme_curve_fitting(const adc_cali_curve_fitting_config_t*, adc_cali_handle_t* output) {
    ++calibrations; *output = new BoardHostAdcCalibration; return ESP_OK;
}
esp_err_t adc_cali_raw_to_voltage(adc_cali_handle_t, int raw, int* voltage) { *voltage = raw; return ESP_OK; }
esp_err_t adc_cali_delete_scheme_curve_fitting(adc_cali_handle_t calibration) { --calibrations; delete calibration; return ESP_OK; }

esp_err_t i2s_new_channel(const i2s_chan_config_t*, i2s_chan_handle_t* tx, i2s_chan_handle_t* rx) {
    *tx = new BoardHostI2sChannel;
    *rx = new BoardHostI2sChannel;
    channels += 2;
    return ESP_OK;
}
esp_err_t i2s_channel_init_std_mode(i2s_chan_handle_t, const i2s_std_config_t*) { return ESP_OK; }
esp_err_t i2s_channel_enable(i2s_chan_handle_t channel) { assert(!channel->enabled); channel->enabled = true; return ESP_OK; }
esp_err_t i2s_channel_disable(i2s_chan_handle_t channel) {
    if (!channel->enabled) return ESP_ERR_INVALID_STATE;
    channel->enabled = false;
    return ESP_OK;
}
esp_err_t i2s_del_channel(i2s_chan_handle_t channel) {
    assert(!channel->enabled && data_interfaces == 0);
    --channels; delete channel; return ESP_OK;
}
const audio_codec_data_if_t* audio_codec_new_i2s_data(const audio_codec_i2s_cfg_t* config) {
    ++data_interfaces;
    return new audio_codec_data_if_t{config->rx_handle, config->tx_handle};
}
const audio_codec_ctrl_if_t* audio_codec_new_i2c_ctrl(const audio_codec_i2c_cfg_t* config) {
    auto* control = new audio_codec_ctrl_if_t;
    i2c_device_config_t device{};
    device.device_address = config->addr >> 1;
    assert(i2c_master_bus_add_device(static_cast<i2c_master_bus_handle_t>(config->bus_handle), &device, &control->device) == ESP_OK);
    ++control_interfaces;
    return control;
}
const audio_codec_gpio_if_t* audio_codec_new_gpio() { ++gpio_interfaces; return new audio_codec_gpio_if_t; }
const audio_codec_if_t* es8311_codec_new(const es8311_codec_cfg_t*) {
    if (failure == Failure::Codec) return nullptr;
    ++codecs; return new audio_codec_if_t;
}
int audio_codec_delete_codec_if(const audio_codec_if_t* codec) { if (codec != nullptr) { --codecs; delete codec; } return ESP_OK; }
int audio_codec_delete_ctrl_if(const audio_codec_ctrl_if_t* control) {
    assert(codec_devices == 0);
    i2c_master_bus_rm_device(control->device);
    --control_interfaces; delete control; return ESP_OK;
}
int audio_codec_delete_gpio_if(const audio_codec_gpio_if_t* gpio) { --gpio_interfaces; delete gpio; return ESP_OK; }
int audio_codec_delete_data_if(const audio_codec_data_if_t* data) {
    assert(codec_devices == 0);
    --data_interfaces; delete data; return ESP_OK;
}
esp_codec_dev_handle_t esp_codec_dev_new(const esp_codec_dev_cfg_t* config) {
    ++codec_devices; return new BoardHostCodecDevice{config->data_if};
}
esp_err_t esp_codec_dev_open(esp_codec_dev_handle_t, const esp_codec_dev_sample_info_t*) { return ESP_OK; }
esp_err_t esp_codec_dev_close(esp_codec_dev_handle_t device) {
    i2s_channel_disable(device->data->rx);
    i2s_channel_disable(device->data->tx);
    return ESP_OK;
}
void esp_codec_dev_delete(esp_codec_dev_handle_t device) {
    if (device == nullptr) return;
    esp_codec_dev_close(device);
    --codec_devices; delete device;
}
esp_err_t esp_codec_dev_set_in_gain(esp_codec_dev_handle_t, float) { return ESP_OK; }
esp_err_t esp_codec_dev_set_out_vol(esp_codec_dev_handle_t, int) { return ESP_OK; }
esp_err_t esp_codec_dev_read(esp_codec_dev_handle_t, void*, int) { return ESP_OK; }
esp_err_t esp_codec_dev_write(esp_codec_dev_handle_t, void*, int) { return ESP_OK; }
const char* esp_err_to_name(esp_err_t error) { return error == ESP_OK ? "ESP_OK" : "ESP_FAIL"; }
int64_t esp_timer_get_time() { return static_cast<int64_t>(xTaskGetTickCount()) * 1000; }
void esp_rom_delay_us(uint32_t) {}
esp_sleep_wakeup_cause_t esp_sleep_get_wakeup_cause() { return ESP_SLEEP_WAKEUP_UNDEFINED; }
esp_err_t esp_sleep_enable_ext1_wakeup_io(uint64_t mask, esp_sleep_ext1_wakeup_mode_t mode) {
    assert(mode == ESP_EXT1_WAKEUP_ANY_LOW && mask == (1ULL << ZECTRIX_BUTTON_DOWN));
    assert(rtc_mode[ZECTRIX_BUTTON_DOWN] && rtc_pullup[ZECTRIX_BUTTON_DOWN] && !rtc_pulldown[ZECTRIX_BUTTON_DOWN]);
    if (fail_wake) return ESP_FAIL;
    wake_pins = mask; return ESP_OK;
}
[[noreturn]] void esp_deep_sleep_start() { AssertReleased(); throw SleepEntered{}; }

int main() {
    TestButtonBuffer();
    TestButtonProducerAndWake();
    TestRtcCalendar();
    TestPowerTransition();
    TestPowerButtonWake();
    TestPartialInitialization();
    TestCallbackRemovalWaits();
    TestServiceDetachAndFieldTaskExit();
    TestAudioPlaybackJoin();
}
