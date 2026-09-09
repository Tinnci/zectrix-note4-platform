#pragma once

#include "zectrix_boot_guard.h"

namespace zectrix::update {

inline constexpr std::size_t kMaximumFirmwareChunkBytes = 4096;
// ESP image header, first segment header and application descriptor.
inline constexpr std::size_t kFirmwareHeaderBytes = 24 + 8 + 256;

// IEEE CRC-32, with a finalized previous value for incremental calls (initially 0).
uint32_t FirmwareCrc32(const uint8_t* data, std::size_t size, uint32_t previous = 0);

class UpdateBackend : public BootBackend {
public:
    // Validate the buffered header before erasing the inactive destination.
    virtual Result BeginImage(const Partition& target, uint32_t image_bytes,
                              const uint8_t* header, std::size_t header_bytes) = 0;
    virtual Result WriteImage(const uint8_t* data, std::size_t size) = 0;
    // Verify stored bytes and the native image, then select the next boot slot.
    // A complete commit attempt consumes the writer, including on failure.
    virtual Result CommitImage(uint32_t expected_crc32) = 0;
    virtual void AbortImage() = 0;
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
    explicit UpdateService(UpdateBackend& backend) : backend_(backend), boot_(backend) {}
    ~UpdateService();
    UpdateService(const UpdateService&) = delete;
    UpdateService& operator=(const UpdateService&) = delete;

    BootGuard& Boot() { return boot_; }
    Result BeginBoot() { return boot_.BeginBoot(); }
    Result ConfirmBoot() { return boot_.ConfirmBoot(); }
    Result SelectUpdateTarget(uint32_t image_bytes, Partition* target) {
        return boot_.SelectUpdateTarget(image_bytes, target);
    }
    BootStatus ReadBootStatus() const { return boot_.ReadBootStatus(); }

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
    UpdateBackend& backend_;
    BootGuard boot_;
    FirmwareStatus firmware_;
    std::array<uint8_t, kFirmwareHeaderBytes> header_{};
    std::size_t header_bytes_ = 0;
    uint32_t expected_crc32_ = 0;
    uint32_t received_crc32_ = 0;
    bool image_open_ = false;
};

}  // namespace zectrix::update
