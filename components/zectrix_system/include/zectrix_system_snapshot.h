#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace zectrix::system {

enum class ResetReason : uint8_t {
    Unknown, PowerOn, Software, Panic, Watchdog, DeepSleep, Brownout, External,
};

// A diagnostic item may wait for physical input for up to 60 seconds.
inline constexpr uint32_t kForegroundWatchdogMs = 90000;
inline constexpr uint8_t kForegroundFailureLimit = 3;

struct HealthSnapshot {
    uint64_t last_progress_ms = 0;
    uint64_t maximum_gap_ms = 0;
    uint32_t heartbeats = 0;
    uint32_t failures = 0;
    uint32_t recoveries = 0;
    int32_t last_error = 0;  // SDK status from the foreground owner.
    int32_t storage_error = 0;  // Native NVS initialization result.
    uint8_t consecutive_failures = 0;
    bool watchdog_armed = false;
    bool watchdog_expired = false;
    bool recovery_boot = false;
    bool automatic_apps_suppressed = false;
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

struct HeapRegion {
    uint32_t total = 0;
    uint32_t free = 0;
    uint32_t minimum_free = 0;
    uint32_t largest_block = 0;
};

struct HeapSnapshot {
    HeapRegion internal;
    HeapRegion psram;
};

inline constexpr std::size_t kMaximumTasks = 32;
enum class TaskState : uint8_t {
    kRunning, kReady, kBlocked, kSuspended, kDeleted, kUnknown,
};

struct TaskInfo {
    uint32_t id = 0;
    uint32_t priority = 0;
    uint32_t minimum_stack_bytes = 0;
    TaskState state = TaskState::kUnknown;
    bool application_owner = false;
};

struct TaskSnapshot {
    std::array<TaskInfo, kMaximumTasks> tasks{};
    uint32_t total = 0;
    uint32_t count = 0;
    bool capacity_exceeded = false;
};

}  // namespace zectrix::system
