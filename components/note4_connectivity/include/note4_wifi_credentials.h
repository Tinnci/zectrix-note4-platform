#pragma once

#include "esp_err.h"
#include "note4_wifi_backend.h"

namespace note4::storage { class StorageService; }

namespace note4::connectivity {

// One NVS blob commits the SSID and passphrase together through StorageService.
// Callers must supply their own local authorization before Save or Erase.
class StoredWifiCredentials final : public WifiCredentialSource {
public:
    explicit StoredWifiCredentials(storage::StorageService* storage)
        : storage_(storage) {}

    WifiCredentialResult Load(WifiCredentials* credentials) override;
    esp_err_t Save(const WifiCredentials& credentials);
    esp_err_t Erase();
    bool Available();

private:
    storage::StorageService* storage_;
};

}  // namespace note4::connectivity
