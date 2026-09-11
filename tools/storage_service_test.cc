#include "zectrix_storage_service.h"
#include "nvs.h"
#include "nvs_flash.h"

#include <cassert>
#include <csignal>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

using Value = std::variant<std::uint8_t, std::int32_t, std::uint32_t,
                           std::string, std::vector<std::uint8_t>>;
static std::unordered_map<std::string, Value> values;
static esp_err_t next_init_result = ESP_OK;
static int erase_count = 0;
static int commit_count = 0;
static esp_err_t deinit_result = ESP_OK;
static esp_err_t commit_result = ESP_OK;
static esp_err_t read_result = ESP_OK;
static esp_err_t open_result = ESP_OK;
static unsigned nvs_handles = 0;

esp_err_t nvs_flash_init() {
    const esp_err_t result = next_init_result;
    next_init_result = ESP_OK;
    return result;
}
esp_err_t nvs_flash_erase() { ++erase_count; values.clear(); return ESP_OK; }
esp_err_t nvs_flash_deinit() { return deinit_result; }
esp_err_t nvs_open(const char* name, nvs_open_mode_t, nvs_handle_t* handle) {
    if (std::string(name) != "zectrix") return ESP_FAIL;
    if (open_result != ESP_OK) return open_result;
    *handle = 1;
    ++nvs_handles;
    return ESP_OK;
}
void nvs_close(nvs_handle_t) { assert(nvs_handles); --nvs_handles; }
esp_err_t nvs_commit(nvs_handle_t) { ++commit_count; return commit_result; }

template <typename T>
esp_err_t Set(const char* key, T value) { values[key] = std::move(value); return ESP_OK; }
template <typename T>
esp_err_t Get(const char* key, T* value) {
    if (read_result != ESP_OK) return read_result;
    const auto it = values.find(key);
    if (it == values.end() || !std::holds_alternative<T>(it->second)) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    *value = std::get<T>(it->second);
    return ESP_OK;
}
esp_err_t nvs_set_u8(nvs_handle_t, const char* key, std::uint8_t value) { return Set(key, value); }
esp_err_t nvs_get_u8(nvs_handle_t, const char* key, std::uint8_t* value) { return Get(key, value); }
esp_err_t nvs_set_i32(nvs_handle_t, const char* key, std::int32_t value) { return Set(key, value); }
esp_err_t nvs_get_i32(nvs_handle_t, const char* key, std::int32_t* value) { return Get(key, value); }
esp_err_t nvs_set_u32(nvs_handle_t, const char* key, std::uint32_t value) { return Set(key, value); }
esp_err_t nvs_get_u32(nvs_handle_t, const char* key, std::uint32_t* value) { return Get(key, value); }
esp_err_t nvs_set_str(nvs_handle_t, const char* key, const char* value) { return Set(key, std::string(value)); }
esp_err_t nvs_get_str(nvs_handle_t, const char* key, char* value, std::size_t* length) {
    std::string stored;
    const esp_err_t err = Get(key, &stored);
    if (err != ESP_OK) return err;
    const std::size_t required = stored.size() + 1;
    if (value == nullptr) { *length = required; return ESP_OK; }
    if (*length < required) { *length = required; return ESP_ERR_NVS_INVALID_LENGTH; }
    std::memcpy(value, stored.c_str(), required); *length = required; return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t, const char* key, const void* value, std::size_t length) {
    const auto* bytes = static_cast<const std::uint8_t*>(value);
    return Set(key, std::vector<std::uint8_t>(bytes, bytes + length));
}
esp_err_t nvs_get_blob(nvs_handle_t, const char* key, void* value, std::size_t* length) {
    std::vector<std::uint8_t> stored;
    const esp_err_t err = Get(key, &stored);
    if (err != ESP_OK) return err;
    if (value == nullptr) { *length = stored.size(); return ESP_OK; }
    if (*length < stored.size()) { *length = stored.size(); return ESP_ERR_NVS_INVALID_LENGTH; }
    std::memcpy(value, stored.data(), stored.size()); *length = stored.size(); return ESP_OK;
}
esp_err_t nvs_erase_key(nvs_handle_t, const char* key) {
    return values.erase(key) == 1 ? ESP_OK : ESP_ERR_NVS_NOT_FOUND;
}

void TestFileWipe() {
    namespace fs = std::filesystem;
    using namespace zectrix::storage;
    char directory[] = "/tmp/zectrix-wipe-XXXXXX";
    assert(mkdtemp(directory));
    const fs::path root(directory);
    fs::create_directory(root / "books");
    std::ofstream(root / "outside.txt") << "outside";
    std::ofstream(root / "books/keep.txt") << "keep";
    std::ofstream(root / "books/.app-example.lua") << "return {}";
    BookStorage books((root / "books").c_str());
    BookFile reader;
    assert(books.Open("keep.txt", &reader) == ESP_OK);
    assert(books.Wipe() == ESP_ERR_INVALID_STATE && fs::exists(root / "books/keep.txt"));
    reader.Close();
    assert(books.BeginManagement() == ESP_OK);
    assert(books.Wipe() == ESP_ERR_INVALID_STATE);
    BookUpload upload;
    assert(books.BeginUpload("new.txt", 4, &upload) == BookWriteResult::Ok);
    assert(upload.Write("part", 4) == BookWriteResult::Ok);
    assert(books.Wipe() == ESP_ERR_INVALID_STATE && fs::exists(root / "books/.app-example.lua"));
    upload.Abort();
    assert(books.EndManagement() == ESP_OK);
    fs::create_symlink(root / "outside.txt", root / "books/link.txt");
    std::ofstream(root / "books/.upload.part") << "orphan";
    assert(books.Wipe() == ESP_OK && fs::is_empty(root / "books"));
    assert(fs::exists(root / "outside.txt"));
    assert(books.BeginManagement() == ESP_OK);
    assert(books.BeginUpload("after.txt", 0, &upload) == BookWriteResult::Ok);
    assert(upload.Commit() == BookWriteResult::Ok && books.EndManagement() == ESP_OK);
    assert(fs::exists(root / "books/after.txt"));
    fs::create_directory(root / "books/directory");
    std::ofstream(root / "books/directory/keep.txt") << "nested";
    assert(books.Wipe() == ESP_FAIL && fs::exists(root / "books/directory/keep.txt"));
    fs::remove_all(root);
}

void TestInterruptedUploads() {
    namespace fs = std::filesystem;
    using namespace zectrix::storage;
    char directory[] = "/tmp/zectrix-upload-faults-XXXXXX";
    assert(mkdtemp(directory));
    const fs::path root(directory);
    std::ofstream(root / "keep.txt") << "original";
    for (unsigned fault = 0; fault < 3; ++fault) {
        const auto pid = fork();
        assert(pid >= 0);
        if (pid == 0) {
            BookStorage books(directory);
            BookUpload upload;
            assert(books.BeginManagement() == ESP_OK);
            std::array<char, 8192> data{};
            if (fault == 0) {
                assert(books.BeginUpload("cut.txt", data.size() * 2, &upload) == BookWriteResult::Ok);
                assert(upload.Write(data.data(), data.size()) == BookWriteResult::Ok);
                // Exit without destructors or stdio flushing, like losing the owner.
                _exit(0);
            }
            rlimit saved{};
            assert(getrlimit(RLIMIT_FSIZE, &saved) == 0);
            auto limited = saved;
            limited.rlim_cur = fault == 1 ? 1024 : 0;
            std::signal(SIGXFSZ, SIG_IGN);
            assert(setrlimit(RLIMIT_FSIZE, &limited) == 0);
            const auto bytes = fault == 1 ? data.size() : 64;
            assert(books.BeginUpload("failed.txt", bytes, &upload) == BookWriteResult::Ok);
            if (fault == 1) {
                assert(upload.Write(data.data(), bytes) == BookWriteResult::IoError);
            } else {
                assert(upload.Write(data.data(), bytes) == BookWriteResult::Ok);
                assert(upload.Commit() == BookWriteResult::IoError);
            }
            assert(setrlimit(RLIMIT_FSIZE, &saved) == 0);
            // Restored space cannot make an uncertain upload publishable.
            assert(upload.Write(data.data(), bytes) == BookWriteResult::IoError);
            assert(upload.Commit() == BookWriteResult::IoError);
            assert(!fs::exists(root / "failed.txt"));
            upload.Abort();
            assert(books.EndManagement() == ESP_OK);
            _exit(0);
        }
        int status = 0;
        assert(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0);
        BookStorage rebooted(directory);
        BookEntry entries[4];
        std::size_t count = 0;
        bool more = false;
        assert(rebooted.List(entries, 4, &count, &more) == ESP_OK && count == 1 && !more);
        assert(std::strcmp(entries[0].name.data(), "keep.txt") == 0);
        if (fault == 0) assert(fs::file_size(root / ".upload.part") > 0);
        assert(rebooted.BeginManagement() == ESP_OK);
        assert(!fs::exists(root / ".upload.part"));
        BookUpload fresh;
        assert(rebooted.BeginUpload("recovered.txt", 4, &fresh) == BookWriteResult::Ok);
        assert(fresh.Write("good", 4) == BookWriteResult::Ok && fresh.Commit() == BookWriteResult::Ok);
        assert(rebooted.Remove("recovered.txt") == BookWriteResult::Ok);
        assert(rebooted.EndManagement() == ESP_OK);
        BookFile original;
        assert(rebooted.Open("keep.txt", &original) == ESP_OK && original.Size() == 8);
        char data[8];
        assert(original.Read(0, data, sizeof(data)) && std::memcmp(data, "original", 8) == 0);
    }
    fs::remove_all(root);
}

void TestNvsFailures() {
    using zectrix::storage::StorageService;
    values.clear();
    values["bookmark"] = std::uint32_t{42};
    const auto erases = erase_count;
    for (const auto error : {ESP_ERR_NVS_NO_FREE_PAGES, ESP_ERR_NVS_NEW_VERSION_FOUND,
                            ESP_ERR_NVS_CORRUPT_KEY_PART, ESP_FAIL}) {
        StorageService* service = nullptr;
        assert(StorageService::Create(&service) == ESP_OK);
        next_init_result = error;
        assert(service->Initialize() == error && !service->IsInitialized());
        assert(service->SetUInt32("bookmark", 0) == ESP_ERR_INVALID_STATE);
        zectrix::storage::BookFile file;
        // File calls still reach normal validation when settings are disabled.
        assert(service->OpenBook("../escape.txt", &file) == ESP_ERR_INVALID_ARG);
        assert(erase_count == erases && std::get<std::uint32_t>(values["bookmark"]) == 42);
        delete service;
        assert(nvs_handles == 0);
    }
    for (const auto error : {ESP_ERR_NO_MEM, ESP_ERR_NVS_NO_FREE_PAGES, ESP_FAIL}) {
        StorageService* service = nullptr;
        assert(StorageService::Create(&service) == ESP_OK);
        open_result = error;
        assert(service->Initialize() == error && !service->IsInitialized());
        assert(erase_count == erases && nvs_handles == 0);
        open_result = ESP_OK;
        assert(service->Initialize() == ESP_OK && nvs_handles == 1);
        delete service;
        assert(nvs_handles == 0 && std::get<std::uint32_t>(values["bookmark"]) == 42);
    }
    StorageService* service = nullptr;
    assert(StorageService::Create(&service) == ESP_OK && service->Initialize() == ESP_OK);
    uint32_t bookmark = 99;
    read_result = ESP_FAIL;
    assert(service->GetUInt32("bookmark", &bookmark) == ESP_FAIL && bookmark == 99);
    read_result = ESP_OK;
    commit_result = ESP_FAIL;
    assert(service->SetUInt32("bookmark", 43) == ESP_FAIL && erase_count == erases);
    commit_result = ESP_OK;
    assert(service->SetUInt32("bookmark", 44) == ESP_OK);
    delete service;
    assert(StorageService::Create(&service) == ESP_OK);
    next_init_result = ESP_ERR_NVS_NO_FREE_PAGES;
    assert(service->Initialize() == ESP_ERR_NVS_NO_FREE_PAGES);
    deinit_result = ESP_ERR_NVS_NOT_INITIALIZED;
    assert(service->ResetSettings() == ESP_OK && erase_count == erases + 1 && values.empty());
    deinit_result = ESP_OK;
    delete service;
}

int main() {
    using zectrix::storage::StorageService;
    StorageService* service = nullptr;
    assert(StorageService::Create(&service) == ESP_OK);
    bool flag = false;
    assert(service->GetBool("flag", &flag) == ESP_ERR_INVALID_STATE);
    next_init_result = ESP_ERR_NVS_NO_FREE_PAGES;
    assert(service->Initialize() == ESP_ERR_NVS_NO_FREE_PAGES && erase_count == 0);
    assert(service->Initialize() == ESP_OK && erase_count == 0);

    assert(service->SetBool("flag", true) == ESP_OK);
    assert(service->GetBool("flag", &flag) == ESP_OK && flag);
    assert(service->SetInt32("signed", -42) == ESP_OK);
    std::int32_t signed_value = 0;
    assert(service->GetInt32("signed", &signed_value) == ESP_OK && signed_value == -42);
    assert(service->SetUInt32("count", 42) == ESP_OK);
    std::uint32_t count = 0;
    assert(service->GetUInt32("count", &count) == ESP_OK && count == 42);
    assert(service->SetString("name", "note4") == ESP_OK);
    std::size_t string_length = 0;
    assert(service->GetString("name", nullptr, &string_length) == ESP_OK);
    char name[8] = {};
    assert(service->GetString("name", name, &string_length) == ESP_OK);
    assert(std::string(name) == "note4");
    const std::uint8_t blob[] = {1, 2, 3};
    assert(service->SetBlob("blob", blob, sizeof(blob)) == ESP_OK);
    std::uint8_t restored[3] = {};
    std::size_t blob_length = sizeof(restored);
    assert(service->GetBlob("blob", restored, &blob_length) == ESP_OK);
    assert(std::memcmp(blob, restored, sizeof(blob)) == 0);
    assert(service->Erase("flag") == ESP_OK);
    assert(service->GetBool("flag", &flag) == ESP_ERR_NOT_FOUND);
    assert(commit_count == 6);
    delete service;

    service = nullptr;
    assert(StorageService::Create(&service) == ESP_OK);
    assert(service->Initialize() == ESP_OK);
    count = 0;
    assert(service->GetUInt32("count", &count) == ESP_OK && count == 42);
    assert(service->ResetSettings() == ESP_OK && erase_count == 1 && values.empty());
    assert(!service->IsInitialized());
    assert(service->GetUInt32("count", &count) == ESP_ERR_INVALID_STATE);
    assert(service->Initialize() == ESP_OK);
    assert(service->SetUInt32("count", 7) == ESP_OK);
    deinit_result = ESP_FAIL;
    assert(service->ResetSettings() == ESP_FAIL && erase_count == 1 && values.size() == 1);
    delete service;
    TestFileWipe();
    TestNvsFailures();
    TestInterruptedUploads();
    assert(nvs_handles == 0);
}
