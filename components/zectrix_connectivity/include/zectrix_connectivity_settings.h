#pragma once

#include "esp_err.h"
#include "zectrix_connectivity_policy.h"

namespace zectrix::storage { class StorageService; }
namespace zectrix::connectivity {
inline constexpr char kConnectivityPolicyKey[] = "conn_policy";
static_assert(sizeof(kConnectivityPolicyKey) <= 16);

// Routing preferences only: no credentials or trust material.
class StoredConnectivitySettings {
public:
    explicit StoredConnectivitySettings(storage::StorageService* storage) : storage_(storage) {}
    // Missing key selects Automatic. Invalid/unreadable storage selects Offline
    // and returns an error without overwriting the stored value.
    esp_err_t LoadPolicy(companion::UserConnectivityPolicy* policy) const;
    // An unchanged persisted value does not cause another flash commit.
    esp_err_t SavePolicy(companion::UserConnectivityPolicy policy);
private:
    storage::StorageService* storage_;
};
}  // namespace zectrix::connectivity
