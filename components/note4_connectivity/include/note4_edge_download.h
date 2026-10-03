#pragma once
#include <cstddef>
#include <cstdint>
#include "note4_edge_settings.h"
namespace note4::storage { class StorageService; }
namespace note4::connectivity {
// Internal owner-facing facade: no sockets, driver modes, or BLE capabilities.
class EdgePageDownload {
public:
    enum class Result { Pending, Success, Failed, Cancelled, StopFailed };
    explicit EdgePageDownload(storage::StorageService* storage);
    ~EdgePageDownload();
    EdgePageDownload(const EdgePageDownload&) = delete;
    EdgePageDownload& operator=(const EdgePageDownload&) = delete;
    bool Begin(const EdgeSettings& settings, uint32_t now_ms, const PageTelemetry& telemetry = {});
    Result Poll(uint32_t now_ms);
    void Cancel(uint32_t now_ms);
    uint8_t* Data();
    std::size_t Size() const;
private:
    struct Impl;
    Impl* impl_ = nullptr;
};
}
