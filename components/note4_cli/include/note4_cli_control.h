#pragma once

#include <condition_variable>
#include <cstdint>
#include <mutex>

#include "note4_display_inspection.h"
#include "note4_system_snapshot.h"
#include "note4_cli_core.h"
#include "note4_cli_reflection.h"

namespace note4::cli {

enum class ControlOperation : uint8_t {
    kSystemInfo, kHeap, kTasks, kUptime, kDisplay, kPower, kTime, kConnectivity,
    kApps, kScenes, kInput, kDisplayTelemetry, kDisplayModel, kHealth,
    kDisplaySettings,
    kTimeSync, kConnectivityConfigure, kDisplayConfigure, kReboot, kSleep, kStorageWipe, kFactoryReset,
};
constexpr bool IsMutation(ControlOperation operation) { return operation >= ControlOperation::kTimeSync; }
enum class ControlStatus : uint8_t {
    kOk, kPending, kInvalidArgument, kDenied, kQueueFull, kTimeout,
    kCancelledBeforeStart, kBusy, kUnavailable, kUnknownOutcome,
};

struct ControlRequest {
    enum class Config : uint8_t { Policy, WifiSet, WifiClear, EdgeSource, EdgeSync, EdgeDisplay, EdgeToken, EdgeTelemetry };
    ControlOperation operation = ControlOperation::kSystemInfo;
    Origin origin = Origin::kUsbLocal;
    bool confirmed = false;
    int64_t unix_ms = 0;
    int32_t offset_seconds = 0;
    uint64_t cursor = 0;
    Config config = Config::Policy;
    std::array<char, 65> text1{}, text2{};
    std::array<uint32_t, 6> values{};
};

struct ControlTicket {
    uint64_t id = 0;
    uint32_t generation = 0;
};
inline void ClearRequestSecrets(ControlRequest* request) {
    for (auto* data : {request->text1.data(), request->text2.data()}) {
        volatile char* clear = data;
        for (std::size_t i = 0; i < request->text1.size(); ++i) clear[i] = 0;
    }
}

struct ControlResult {
    struct DisplaySettings {
        uint8_t active = 2, configured = 2;
        bool sleep_portrait = true;
    } display_settings;
    system::SystemSnapshot system;
    system::HealthSnapshot health;
    system::HeapSnapshot heap;
    system::TaskSnapshot tasks;
    display::DisplayInspection display;
    display::TelemetryBatch display_telemetry;
    display::PhysicsParameters display_model;
    uint64_t uptime_us = 0;
    PowerInspection power;
    TimeInspection time;
    ConnectivityInspection connectivity;
    AppInspection apps;
    SceneInspection scenes;
    input::TraceBatch input;
};

// The shell lends this adapter only while its runtime is alive. All calls run
// on the foreground owner; deferred actions execute after callbacks unwind.
class MaintenanceDelegate {
public:
    virtual ~MaintenanceDelegate() = default;
    virtual ControlStatus InspectApps(ControlResult*) = 0;
    virtual ControlStatus ScheduleMaintenance(ControlOperation operation) = 0;
    virtual ControlStatus HandleDisplaySettings(const ControlRequest&, ControlResult*) {
        return ControlStatus::kUnavailable;
    }
};

class ControlOwner {
public:
    virtual ~ControlOwner() = default;
    virtual bool IsCurrentTaskOwner() const = 0;
    // Wake only signals the owner; it must neither block nor dispatch inline.
    virtual void Wake() = 0;
    virtual ControlStatus Inspect(const ControlRequest&, ControlResult*) = 0;
};

using MonotonicMilliseconds = uint64_t (*)();
uint64_t SteadyMilliseconds();
inline constexpr uint64_t kOwnerRequestTimeoutMs = 30000;

class PlatformControlDispatcher final {
public:
    explicit PlatformControlDispatcher(
        ControlOwner& owner, MonotonicMilliseconds clock = SteadyMilliseconds)
        : owner_(owner), clock_(clock) {}
    ~PlatformControlDispatcher() { Shutdown(); }

    ControlStatus Submit(const ControlRequest& request, ControlTicket* ticket);
    ControlStatus Take(ControlTicket ticket, ControlResult* result);
    ControlStatus Cancel(ControlTicket ticket);
    // Only the application owner may execute one queued inspection.
    bool Dispatch();
    void Shutdown();

private:
    enum class State : uint8_t { kIdle, kQueued, kExecuting, kComplete };
    bool Matches(ControlTicket ticket) const;

    ControlOwner& owner_;
    MonotonicMilliseconds clock_;
    std::mutex mutex_;
    std::condition_variable idle_;
    ControlRequest request_;
    ControlTicket ticket_;
    // One slot matches the one-command session limit. Execution writes here
    // outside the mutex; consumers may copy it only after kComplete.
    ControlResult result_;
    ControlStatus status_ = ControlStatus::kUnavailable;
    State state_ = State::kIdle;
    uint64_t submitted_ms_ = 0;
    bool abandoned_ = false;
    bool closing_ = false;
};

}  // namespace note4::cli
