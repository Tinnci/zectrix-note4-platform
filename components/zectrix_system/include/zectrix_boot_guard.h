#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace zectrix::update {

inline constexpr std::size_t kMaximumPartitions = 32;
inline constexpr uint32_t kBootConfirmationTimeoutMs = 60000;

enum class Result : uint8_t {
    kOk, kInvalidArgument, kInvalidLayout, kBootMismatch, kInvalidState,
    kConfirmationRequired, kImageTooLarge, kTimeout, kIoError,
    kChecksumMismatch, kInvalidImage, kIncompleteImage, kUnexpectedOffset,
};
const char* ResultName(Result result);

enum class PartitionKind : uint8_t {
    kOther, kFactory, kOtaA, kOtaB, kOtaData, kOtherOta,
};
struct Partition {
    PartitionKind kind = PartitionKind::kOther;
    uint32_t address = 0;
    uint32_t size = 0;
    bool readonly = false;
};
bool SamePartition(const Partition& left, const Partition& right);

enum class ImageState : uint8_t {
    kUnknown, kFactory, kNew, kPendingVerify, kValid, kInvalid, kAborted, kUndefined,
};
struct BootInfo {
    uint32_t flash_bytes = 0;
    uint32_t partition_table_address = 0;
    std::array<Partition, kMaximumPartitions> partitions{};
    std::size_t partition_count = 0;
    Partition running;
    Partition boot;
    Partition next_update;
    ImageState image_state = ImageState::kUnknown;
    bool rollback_available = false;
};

// Validate copied metadata before exposing an inactive update destination.
Result VerifyPartitions(const BootInfo& info, Partition* target);

class BootBackend {
public:
    virtual ~BootBackend() = default;
    virtual Result ReadBootInfo(BootInfo* info) = 0;
    virtual uint64_t Milliseconds() const = 0;
    virtual Result ArmBootWatchdog(uint32_t timeout_ms) = 0;
    virtual void DisarmBootWatchdog() = 0;
    virtual Result ConfirmRunningImage(const Partition& expected) = 0;
};

struct BootStatus {
    Partition running;
    Partition boot;
    Partition next_update;
    ImageState image_state = ImageState::kUnknown;
    Result layout_result = Result::kInvalidState;
    bool confirmation_pending = false;
    bool rollback_available = false;
};

// Boot protection is mandatory even when firmware ingestion is compiled out.
// All operations run on the platform owner; destruction leaves watchdogs armed.
class BootGuard final {
public:
    explicit BootGuard(BootBackend& backend) : backend_(backend) {}
    BootGuard(const BootGuard&) = delete;
    BootGuard& operator=(const BootGuard&) = delete;

    Result BeginBoot();
    Result ConfirmBoot();
    Result SelectUpdateTarget(uint32_t image_bytes, Partition* target);
    BootStatus ReadBootStatus() const { return status_; }

private:
    friend class UpdateService;
    enum class Phase : uint8_t { kInitial, kPending, kReady, kFailed };
    BootBackend& backend_;
    BootStatus status_;
    uint64_t started_ms_ = 0;
    Phase phase_ = Phase::kInitial;
};

}  // namespace zectrix::update
