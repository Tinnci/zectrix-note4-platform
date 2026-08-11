#ifndef ZECTRIX_SELF_TEST_H_
#define ZECTRIX_SELF_TEST_H_

#include <array>
#include <cstdint>
#include <functional>

class ZectrixBoard;
namespace zectrix::input { class InputService; }
namespace zectrix::power { class PowerService; }
namespace zectrix::time { class TimeService; }
namespace zectrix::storage { class StorageService; }
namespace zectrix::system { class SystemService; }

enum class ZectrixTestId : uint8_t {
    kRf = 0,
    kAudio,
    kRtc,
    kCharge,
    kLed,
    kButtons,
    kNfc,
    kCount,
};

enum class ZectrixTestState : uint8_t {
    kWait = 0,
    kRunning,
    kPass,
    kFail,
};

enum class ZectrixTestResult : uint8_t {
    kPass = 0,
    kFail,
    kCancelled,
    kShutdown,
};

struct ZectrixTestUpdate {
    ZectrixTestId id = ZectrixTestId::kRf;
    ZectrixTestState state = ZectrixTestState::kWait;
    char title[32] = {};
    char hint[80] = {};
    std::array<std::array<char, 80>, 4> details = {};
};

class ZectrixSelfTest {
public:
    using UpdateCallback = std::function<void(const ZectrixTestUpdate&)>;

    ZectrixSelfTest(ZectrixBoard& board,
                    zectrix::input::InputService& input,
                    zectrix::power::PowerService& power,
                    zectrix::time::TimeService& time,
                    zectrix::storage::StorageService& storage,
                    zectrix::system::SystemService& system)
        : board_(&board), input_(&input), power_(&power), time_(&time),
          storage_(&storage), system_(&system) {}

    static const char* Name(ZectrixTestId id);
    ZectrixTestResult Run(ZectrixTestId id, const UpdateCallback& callback);

private:
    ZectrixTestResult RunRf(const UpdateCallback& callback);
    ZectrixTestResult RunAudio(const UpdateCallback& callback);
    ZectrixTestResult RunRtc(const UpdateCallback& callback);
    ZectrixTestResult RunCharge(const UpdateCallback& callback);
    ZectrixTestResult RunLed(const UpdateCallback& callback);
    ZectrixTestResult RunButtons(const UpdateCallback& callback);
    ZectrixTestResult RunNfc(const UpdateCallback& callback);

    ZectrixBoard* board_ = nullptr;
    zectrix::input::InputService* input_ = nullptr;
    zectrix::power::PowerService* power_ = nullptr;
    zectrix::time::TimeService* time_ = nullptr;
    zectrix::storage::StorageService* storage_ = nullptr;
    zectrix::system::SystemService* system_ = nullptr;
};

#endif  // ZECTRIX_SELF_TEST_H_
