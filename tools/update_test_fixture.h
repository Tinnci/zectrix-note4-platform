#pragma once

#include "zectrix_update_service.h"

inline zectrix::update::BootInfo UpdateBootFixture(
    zectrix::update::PartitionKind running = zectrix::update::PartitionKind::kFactory) {
    using namespace zectrix::update;
    BootInfo info;
    info.flash_bytes = 16 * 1024 * 1024;
    info.partition_table_address = 0x8000;
    info.partition_count = 6;
    info.partitions[0] = {PartitionKind::kOther, 0x9000, 0x6000};
    info.partitions[1] = {PartitionKind::kOther, 0xf000, 0x1000};
    info.partitions[2] = {PartitionKind::kFactory, 0x10000, 0x300000};
    info.partitions[3] = {PartitionKind::kOtaA, 0x310000, 0x300000};
    info.partitions[4] = {PartitionKind::kOtaB, 0x610000, 0x300000};
    info.partitions[5] = {PartitionKind::kOtaData, 0x910000, 0x2000};
    info.running = info.partitions[running == PartitionKind::kFactory ? 2 :
                                   running == PartitionKind::kOtaA ? 3 : 4];
    info.boot = info.running;
    info.next_update = info.partitions[running == PartitionKind::kOtaA ? 4 : 3];
    info.image_state = running == PartitionKind::kFactory ? ImageState::kFactory : ImageState::kValid;
    info.rollback_available = true;
    return info;
}
