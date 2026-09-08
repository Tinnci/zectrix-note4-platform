#pragma once

#include <array>
#include <cstdint>

#include "esp_err.h"
#include "zectrix_system_snapshot.h"

class ZectrixBoard;

namespace zectrix::system {

class SystemService {
public:
    static esp_err_t Attach(ZectrixBoard& board, SystemService** out_service);
    ~SystemService();

    SystemService(const SystemService&) = delete;
    SystemService& operator=(const SystemService&) = delete;

    esp_err_t ReadSnapshot(SystemSnapshot* snapshot) const;
    esp_err_t ReadWifiMac(std::array<uint8_t, 6>* mac) const;
    esp_err_t ReadHeap(HeapSnapshot* snapshot) const;
    esp_err_t ReadTasks(TaskSnapshot* snapshot) const;

private:
    explicit SystemService(ZectrixBoard& board) : board_(&board) {}
    ZectrixBoard* board_;
};

}  // namespace zectrix::system
