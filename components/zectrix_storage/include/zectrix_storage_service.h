#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"

namespace zectrix::storage {

class StorageService {
public:
    static esp_err_t Create(StorageService** out_service);
    ~StorageService();

    StorageService(const StorageService&) = delete;
    StorageService& operator=(const StorageService&) = delete;

    esp_err_t Initialize();
    bool IsInitialized() const;

    esp_err_t SetBool(const char* key, bool value);
    esp_err_t GetBool(const char* key, bool* value) const;
    esp_err_t SetInt32(const char* key, int32_t value);
    esp_err_t GetInt32(const char* key, int32_t* value) const;
    esp_err_t SetUInt32(const char* key, uint32_t value);
    esp_err_t GetUInt32(const char* key, uint32_t* value) const;
    esp_err_t SetString(const char* key, const char* value);
    esp_err_t GetString(const char* key, char* value,
                        std::size_t* length) const;
    esp_err_t SetBlob(const char* key, const void* value, std::size_t length);
    esp_err_t GetBlob(const char* key, void* value, std::size_t* length) const;
    esp_err_t Erase(const char* key);

private:
    struct Impl;
    explicit StorageService(Impl* impl) : impl_(impl) {}
    esp_err_t Commit(esp_err_t operation_result);
    Impl* impl_;
};

}  // namespace zectrix::storage
