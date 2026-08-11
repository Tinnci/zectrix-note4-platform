#pragma once

#include <array>
#include <cstdint>

#include "esp_err.h"

class ZectrixBoard;

namespace zectrix::system {

enum class ResetReason : uint8_t {
    Unknown,
    PowerOn,
    Software,
    Panic,
    Watchdog,
    DeepSleep,
    Brownout,
    External,
};

struct FirmwareIdentity {
    std::array<char, 32> project_name = {};
    std::array<char, 32> version = {};
    std::array<char, 32> idf_version = {};
    std::array<char, 16> build_date = {};
    std::array<char, 16> build_time = {};
    std::array<char, 65> app_elf_sha256 = {};
};

struct Capabilities {
    std::array<char, 24> chip_model = {};
    uint16_t chip_revision = 0;
    uint8_t core_count = 0;
    bool wifi = false;
    bool bluetooth_le = false;
    bool rtc = false;
    bool nfc = false;
    bool psram = false;
};

struct DiagnosticStatus {
    uint32_t flash_bytes = 0;
    uint32_t psram_bytes = 0;
    uint32_t free_internal_heap_bytes = 0;
    uint32_t minimum_free_internal_heap_bytes = 0;
    uint32_t largest_internal_heap_block_bytes = 0;
};

struct SystemSnapshot {
    FirmwareIdentity firmware;
    Capabilities capabilities;
    DiagnosticStatus diagnostics;
    ResetReason reset_reason = ResetReason::Unknown;
    std::array<uint8_t, 6> wifi_mac = {};
};

class SystemService {
public:
    static esp_err_t Attach(ZectrixBoard& board, SystemService** out_service);
    ~SystemService();

    SystemService(const SystemService&) = delete;
    SystemService& operator=(const SystemService&) = delete;

    esp_err_t ReadSnapshot(SystemSnapshot* snapshot) const;
    esp_err_t ReadWifiMac(std::array<uint8_t, 6>* mac) const;

private:
    explicit SystemService(ZectrixBoard& board) : board_(&board) {}
    ZectrixBoard* board_;
};

}  // namespace zectrix::system
