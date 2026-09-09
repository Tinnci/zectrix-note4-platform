#include "zectrix_boot_guard.h"

namespace zectrix::update {
namespace {

bool IsApplication(PartitionKind kind) {
    return kind == PartitionKind::kFactory || kind == PartitionKind::kOtaA ||
           kind == PartitionKind::kOtaB;
}

bool KnownGood(const BootInfo& info) {
    return (info.running.kind == PartitionKind::kFactory &&
            info.image_state == ImageState::kFactory) ||
           ((info.running.kind == PartitionKind::kOtaA ||
             info.running.kind == PartitionKind::kOtaB) &&
            info.image_state == ImageState::kValid);
}

}  // namespace

const char* ResultName(Result result) {
    switch (result) {
        case Result::kOk: return "ok";
        case Result::kInvalidArgument: return "invalid argument";
        case Result::kInvalidLayout: return "invalid A/B partition layout";
        case Result::kBootMismatch: return "running/selected boot partition mismatch";
        case Result::kInvalidState: return "invalid update state";
        case Result::kConfirmationRequired: return "boot confirmation required";
        case Result::kImageTooLarge: return "image exceeds inactive slot";
        case Result::kTimeout: return "boot confirmation timed out";
        case Result::kIoError: return "OTA I/O failure";
        case Result::kChecksumMismatch: return "firmware CRC-32 mismatch";
        case Result::kInvalidImage: return "invalid firmware image";
        case Result::kIncompleteImage: return "incomplete firmware image";
        case Result::kUnexpectedOffset: return "unexpected firmware chunk offset";
    }
    return "unknown update result";
}

bool SamePartition(const Partition& left, const Partition& right) {
    return left.kind == right.kind && left.address == right.address &&
           left.size == right.size && left.readonly == right.readonly;
}

Result VerifyPartitions(const BootInfo& info, Partition* target) {
    if (target == nullptr) return Result::kInvalidArgument;
    *target = {};
    if (info.partition_count == 0 || info.partition_count > info.partitions.size() ||
        info.partition_table_address < 0x1000 ||
        info.partition_table_address % 0x1000 != 0) return Result::kInvalidLayout;
    const uint64_t reserved_end = static_cast<uint64_t>(info.partition_table_address) + 0x1000;
    const Partition* a = nullptr;
    const Partition* b = nullptr;
    const Partition* data = nullptr;
    const Partition* factory = nullptr;
    bool running_found = false;
    for (std::size_t index = 0; index < info.partition_count; ++index) {
        const auto& partition = info.partitions[index];
        const uint64_t end = static_cast<uint64_t>(partition.address) + partition.size;
        if (partition.size == 0 || partition.address < reserved_end ||
            partition.address % 0x1000 != 0 || partition.size % 0x1000 != 0 ||
            end > info.flash_bytes || partition.kind == PartitionKind::kOtherOta ||
            (IsApplication(partition.kind) && partition.address % 0x10000 != 0)) {
            return Result::kInvalidLayout;
        }
        for (std::size_t previous = 0; previous < index; ++previous) {
            const auto& other = info.partitions[previous];
            if (partition.address < static_cast<uint64_t>(other.address) + other.size &&
                other.address < end) return Result::kInvalidLayout;
        }
        const Partition** slot = nullptr;
        if (partition.kind == PartitionKind::kOtaA) slot = &a;
        if (partition.kind == PartitionKind::kOtaB) slot = &b;
        if (partition.kind == PartitionKind::kOtaData) slot = &data;
        if (partition.kind == PartitionKind::kFactory) slot = &factory;
        if (slot != nullptr) {
            if (*slot != nullptr) return Result::kInvalidLayout;
            *slot = &partition;
        }
        if (SamePartition(partition, info.running)) running_found = true;
    }
    if (a == nullptr || b == nullptr || data == nullptr || data->size < 0x2000 ||
        data->readonly || a->readonly || b->readonly || !running_found ||
        !IsApplication(info.running.kind)) return Result::kInvalidLayout;
    // Selected boot can differ after a scheduled update or bootloader fallback.
    // Neither case authorizes overwriting or confirming that selected image.
    if (!SamePartition(info.running, info.boot)) return Result::kBootMismatch;
    const Partition& inactive = info.running.kind == PartitionKind::kOtaA ? *b : *a;
    if (!SamePartition(inactive, info.next_update) || SamePartition(inactive, info.running)) {
        return Result::kInvalidLayout;
    }
    *target = inactive;
    return Result::kOk;
}

Result BootGuard::BeginBoot() {
    if (phase_ != Phase::kInitial) {
        return phase_ == Phase::kFailed ? Result::kInvalidState : Result::kOk;
    }
    phase_ = Phase::kFailed;
    started_ms_ = backend_.Milliseconds();
    const auto armed = backend_.ArmBootWatchdog(kBootConfirmationTimeoutMs);
    if (armed != Result::kOk) return armed;
    BootInfo info;
    const auto read = backend_.ReadBootInfo(&info);
    if (read != Result::kOk) return read;
    Partition target;
    status_ = {info.running, info.boot, info.next_update, info.image_state,
               VerifyPartitions(info, &target), false, info.rollback_available};
    if (KnownGood(info)) {
        // A recovery/factory image may still run when OTA is unavailable.
        // Target selection remains disabled until its layout is valid.
        backend_.DisarmBootWatchdog();
        phase_ = Phase::kReady;
        return Result::kOk;
    }
    if (info.image_state != ImageState::kPendingVerify ||
        (info.running.kind != PartitionKind::kOtaA &&
         info.running.kind != PartitionKind::kOtaB)) return Result::kInvalidState;
    if (status_.layout_result != Result::kOk) return status_.layout_result;
    status_.confirmation_pending = true;
    phase_ = Phase::kPending;
    return Result::kOk;
}

Result BootGuard::ConfirmBoot() {
    if (phase_ == Phase::kReady) return Result::kOk;
    if (phase_ != Phase::kPending) return Result::kInvalidState;
    if (backend_.Milliseconds() - started_ms_ >= kBootConfirmationTimeoutMs) {
        phase_ = Phase::kFailed;
        return Result::kTimeout;
    }
    BootInfo info;
    auto result = backend_.ReadBootInfo(&info);
    Partition target;
    if (result == Result::kOk) result = VerifyPartitions(info, &target);
    if (result == Result::kOk && (!SamePartition(info.running, status_.running) ||
        info.image_state != ImageState::kPendingVerify)) result = Result::kInvalidState;
    if (result == Result::kOk &&
        backend_.Milliseconds() - started_ms_ >= kBootConfirmationTimeoutMs) result = Result::kTimeout;
    if (result == Result::kOk) result = backend_.ConfirmRunningImage(status_.running);
    if (result != Result::kOk) {
        phase_ = Phase::kFailed;
        return result;
    }
    // Commit VALID before removing reset protection. Failed writes keep it armed.
    backend_.DisarmBootWatchdog();
    phase_ = Phase::kReady;
    status_.image_state = ImageState::kValid;
    status_.confirmation_pending = false;
    return Result::kOk;
}

Result BootGuard::SelectUpdateTarget(uint32_t image_bytes, Partition* target) {
    if (target == nullptr) return Result::kInvalidArgument;
    *target = {};
    if (image_bytes == 0) return Result::kInvalidArgument;
    if (phase_ == Phase::kPending) return Result::kConfirmationRequired;
    if (phase_ != Phase::kReady) return Result::kInvalidState;
    BootInfo info;
    auto result = backend_.ReadBootInfo(&info);
    if (result != Result::kOk) return result;
    if (!KnownGood(info) || !SamePartition(info.running, status_.running)) {
        return Result::kInvalidState;
    }
    Partition inactive;
    result = VerifyPartitions(info, &inactive);
    if (result != Result::kOk) return result;
    if (image_bytes > inactive.size) return Result::kImageTooLarge;
    *target = inactive;
    return Result::kOk;
}

}  // namespace zectrix::update
