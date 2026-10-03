#include "note4_edge_download.h"
#include <memory>
#include <new>
#include "note4_edge_sync.h"
#include "note4_wifi_credentials.h"
#include "note4_wifi_esp_driver.h"
namespace note4::connectivity {
struct EdgePageDownload::Impl {
    explicit Impl(storage::StorageService* storage) : credentials(storage), sync(credentials, driver) {}
    storage::StorageService* storage = nullptr;
    StoredWifiCredentials credentials;
    EspWifiBackendDriver driver;
    EdgeSync sync;
    std::unique_ptr<uint8_t[]> data;
};
EdgePageDownload::EdgePageDownload(storage::StorageService* storage) : impl_(new (std::nothrow) Impl(storage)) { if (impl_) impl_->storage = storage; }
EdgePageDownload::~EdgePageDownload() { delete impl_; }
bool EdgePageDownload::Begin(const EdgeSettings& s, uint32_t now, const PageTelemetry& telemetry) {
    if (!impl_ || !ValidateEdgeSettings(s) || !s.enabled) return false;
    std::array<char, 65> token{};
    const auto read = StoredEdgeSettings(impl_->storage).LoadToken(&token);
    const bool configured = read == ESP_OK && impl_->driver.ConfigurePageSource(s.host.data(), s.path.data(), token.data(), telemetry);
    volatile char* clear = token.data();
    for (std::size_t i = 0; i < token.size(); ++i) clear[i] = 0;
    if (!configured) return false;
    impl_->data.reset(new (std::nothrow) uint8_t[storage::edge::kFileSize]);
    return impl_->data && impl_->sync.Begin(impl_->data.get(), storage::edge::kFileSize, s.budget_ms, now);
}
EdgePageDownload::Result EdgePageDownload::Poll(uint32_t now) {
    if (!impl_) return Result::Failed;
    switch (impl_->sync.Poll(now)) {
        case EdgeSync::Result::Pending: return Result::Pending;
        case EdgeSync::Result::Success: return Result::Success;
        case EdgeSync::Result::Failed: return Result::Failed;
        case EdgeSync::Result::Cancelled: return Result::Cancelled;
        case EdgeSync::Result::StopFailed: return Result::StopFailed;
    }
    return Result::Failed;
}
void EdgePageDownload::Cancel(uint32_t now) { if (impl_) impl_->sync.Cancel(now); }
uint8_t* EdgePageDownload::Data() { return impl_ ? impl_->data.get() : nullptr; }
std::size_t EdgePageDownload::Size() const { return impl_ ? impl_->sync.Size() : 0; }
}
