#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace zectrix::update {

inline constexpr std::size_t kMaximumPartitions = 32;
inline constexpr uint32_t kBootConfirmationTimeoutMs = 60000;
inline constexpr std::size_t kMaximumFirmwareChunkBytes = 4096;
// ESP image header, first segment header and application descriptor.
inline constexpr std::size_t kFirmwareHeaderBytes = 24 + 8 + 256;

enum class Result : uint8_t {
    kOk, kInvalidArgument, kInvalidLayout, kBootMismatch, kInvalidState,
    kConfirmationRequired, kImageTooLarge, kTimeout, kIoError,
    kChecksumMismatch, kInvalidImage, kIncompleteImage, kUnexpectedOffset,
};
const char* ResultName(Result result);

// IEEE CRC-32, with a finalized previous value for incremental calls (initially 0).
uint32_t FirmwareCrc32(const uint8_t* data, std::size_t size, uint32_t previous = 0);

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

class UpdateBackend {
public:
    virtual ~UpdateBackend() = default;
    virtual Result ReadBootInfo(BootInfo* info) = 0;
    virtual uint64_t Milliseconds() const = 0;
    virtual Result ArmBootWatchdog(uint32_t timeout_ms) = 0;
    virtual void DisarmBootWatchdog() = 0;
    virtual Result ConfirmRunningImage(const Partition& expected) = 0;
    // Validate the buffered header before erasing the inactive destination.
    virtual Result BeginImage(const Partition& target, uint32_t image_bytes,
                              const uint8_t* header, std::size_t header_bytes) = 0;
    virtual Result WriteImage(const uint8_t* data, std::size_t size) = 0;
    // Verify stored bytes and the native image, then select the next boot slot.
    // A complete commit attempt consumes the writer, including on failure.
    virtual Result CommitImage(uint32_t expected_crc32) = 0;
    virtual void AbortImage() = 0;
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

enum class FirmwarePhase : uint8_t { kIdle, kReceiving, kFailed, kCommitted };
struct FirmwareStatus {
    Partition target;
    uint32_t image_bytes = 0;
    uint32_t received_bytes = 0;
    FirmwarePhase phase = FirmwarePhase::kIdle;
};

// All calls belong to the platform owner task. Destruction aborts a transfer;
// it never confirms an image or disarms an unconfirmed boot watchdog.
class UpdateService final {
public:
    explicit UpdateService(UpdateBackend& backend) : backend_(backend) {}
    ~UpdateService();
    UpdateService(const UpdateService&) = delete;
    UpdateService& operator=(const UpdateService&) = delete;

    Result BeginBoot();
    Result ConfirmBoot();
    Result SelectUpdateTarget(uint32_t image_bytes, Partition* target);
    BootStatus ReadBootStatus() const { return status_; }

    Result BeginFirmware(uint32_t image_bytes, uint32_t image_crc32);
    // Rejected arguments, offsets and chunk CRCs leave progress unchanged.
    // The caller keeps data stable until this synchronous call returns.
    Result WriteFirmwareChunk(uint32_t offset, const uint8_t* data,
                              std::size_t size, uint32_t chunk_crc32);
    // Success schedules the trial image for the next reboot; it does not reset.
    Result CommitFirmware();
    void AbortFirmware();
    FirmwareStatus ReadFirmwareStatus() const { return firmware_; }

private:
    Result FailFirmware(Result result);
    enum class Phase : uint8_t { kInitial, kPending, kReady, kFailed };
    UpdateBackend& backend_;
    BootStatus status_;
    uint64_t started_ms_ = 0;
    Phase phase_ = Phase::kInitial;
    FirmwareStatus firmware_;
    std::array<uint8_t, kFirmwareHeaderBytes> header_{};
    std::size_t header_bytes_ = 0;
    uint32_t expected_crc32_ = 0;
    uint32_t received_crc32_ = 0;
    bool image_open_ = false;
};

}  // namespace zectrix::update
