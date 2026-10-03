#pragma once

#include <array>
#include <cstdint>

#include "esp_err.h"
#include "note4_system_snapshot.h"

class Note4Board;

namespace note4::system {

class SystemService {
public:
    static esp_err_t Attach(Note4Board& board, SystemService** out_service);
    ~SystemService();

    SystemService(const SystemService&) = delete;
    SystemService& operator=(const SystemService&) = delete;

    esp_err_t ReadSnapshot(SystemSnapshot* snapshot) const;
    esp_err_t ReadWifiMac(std::array<uint8_t, 6>* mac) const;
    esp_err_t ReadHeap(HeapSnapshot* snapshot) const;
    esp_err_t ReadTasks(TaskSnapshot* snapshot) const;

private:
    explicit SystemService(Note4Board& board) : board_(&board) {}
    Note4Board* board_;
};

}  // namespace note4::system
