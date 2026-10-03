#ifndef NOTE4_SELF_TEST_H_
#define NOTE4_SELF_TEST_H_

#include <array>
#include <cstdint>
#include <functional>

class Note4Board;
namespace note4::input { class InputService; }
namespace note4::power { class PowerService; }
namespace note4::time { class TimeService; }
namespace note4::storage { class StorageService; }
namespace note4::system { class SystemService; }

enum class Note4TestId : uint8_t {
    kRf = 0,
    kAudio,
    kRtc,
    kCharge,
    kLed,
    kButtons,
    kNfc,
    kCount,
};

enum class Note4TestState : uint8_t {
    kWait = 0,
    kRunning,
    kPass,
    kFail,
    kSkipped,
};

enum class Note4TestResult : uint8_t {
    kPass = 0,
    kFail,
    kCancelled,
    kShutdown,
    kSkipped,
};

struct Note4TestUpdate {
    Note4TestId id = Note4TestId::kRf;
    Note4TestState state = Note4TestState::kWait;
    char title[32] = {};
    char hint[80] = {};
    std::array<std::array<char, 80>, 4> details = {};
};

class Note4SelfTest {
public:
    using UpdateCallback = std::function<void(const Note4TestUpdate&)>;

    Note4SelfTest(Note4Board& board,
                    note4::input::InputService& input,
                    note4::power::PowerService& power,
                    note4::time::TimeService& time,
                    note4::storage::StorageService& storage,
                    note4::system::SystemService& system)
        : board_(&board), input_(&input), power_(&power), time_(&time),
          storage_(&storage), system_(&system) {}

    static const char* Name(Note4TestId id);
    Note4TestResult Run(Note4TestId id, const UpdateCallback& callback);

private:
    Note4TestResult RunRf(const UpdateCallback& callback);
    Note4TestResult RunAudio(const UpdateCallback& callback);
    Note4TestResult RunRtc(const UpdateCallback& callback);
    Note4TestResult RunCharge(const UpdateCallback& callback);
    Note4TestResult RunLed(const UpdateCallback& callback);
    Note4TestResult RunButtons(const UpdateCallback& callback);
    Note4TestResult RunNfc(const UpdateCallback& callback);

    Note4Board* board_ = nullptr;
    note4::input::InputService* input_ = nullptr;
    note4::power::PowerService* power_ = nullptr;
    note4::time::TimeService* time_ = nullptr;
    note4::storage::StorageService* storage_ = nullptr;
    note4::system::SystemService* system_ = nullptr;
};

#endif  // NOTE4_SELF_TEST_H_
