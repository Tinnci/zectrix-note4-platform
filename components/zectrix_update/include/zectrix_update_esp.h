#pragma once

#include "zectrix_update_service.h"

namespace zectrix::update {

// ESP-IDF OTA metadata and the inherited RTC watchdog stay below the service.
class EspUpdateBackend final : public UpdateBackend {
public:
    EspUpdateBackend() = default;
    ~EspUpdateBackend() override;
    EspUpdateBackend(const EspUpdateBackend&) = delete;
    EspUpdateBackend& operator=(const EspUpdateBackend&) = delete;

    Result ReadBootInfo(BootInfo* info) override;
    uint64_t Milliseconds() const override;
    Result ArmBootWatchdog(uint32_t timeout_ms) override;
    void DisarmBootWatchdog() override;
    Result ConfirmRunningImage(const Partition& expected) override;
    Result BeginImage(const Partition& target, uint32_t image_bytes,
                      const uint8_t* header, std::size_t header_bytes) override;
    Result WriteImage(const uint8_t* data, std::size_t size) override;
    Result CommitImage(uint32_t expected_crc32) override;
    void AbortImage() override;

private:
    Result CheckImageTarget() const;
    Partition image_target_;
    Partition image_running_;
    uint32_t image_bytes_ = 0;
    uint32_t written_bytes_ = 0;
    uint32_t ota_handle_ = 0;
    bool ota_open_ = false;
};

}  // namespace zectrix::update
