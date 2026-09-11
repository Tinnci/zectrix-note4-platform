#pragma once

#include <condition_variable>
#include <cstdint>
#include <mutex>

#include "zectrix_display_inspection.h"
#include "zectrix_system_snapshot.h"
#include "zectrix_cli_core.h"
#include "zectrix_cli_reflection.h"

namespace zectrix::cli {

enum class ControlOperation : uint8_t {
    kSystemInfo, kHeap, kTasks, kUptime, kDisplay, kPower, kTime, kConnectivity,
    kApps, kScenes, kInput, kTimeSync, kReboot, kSleep, kStorageWipe, kFactoryReset,
};
constexpr bool IsMutation(ControlOperation operation) { return operation >= ControlOperation::kTimeSync; }
enum class ControlStatus : uint8_t {
    kOk, kPending, kInvalidArgument, kDenied, kQueueFull, kTimeout,
    kCancelledBeforeStart, kBusy, kUnavailable, kUnknownOutcome,
};

struct ControlRequest {
    ControlOperation operation = ControlOperation::kSystemInfo;
    Origin origin = Origin::kUsbLocal;
    bool confirmed = false;
    int64_t unix_ms = 0;
    int32_t offset_seconds = 0;
    uint64_t cursor = 0;
};

struct ControlTicket {
    uint64_t id = 0;
    uint32_t generation = 0;
};

struct ControlResult {
    system::SystemSnapshot system;
    system::HeapSnapshot heap;
    system::TaskSnapshot tasks;
    display::DisplayInspection display;
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

}  // namespace zectrix::cli
