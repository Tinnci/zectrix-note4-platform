#include "zectrix_book_storage.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstring>
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>

#ifdef ESP_PLATFORM
#include "esp_spiffs.h"
#endif

namespace zectrix::storage {
namespace {
bool BookName(const char* name) {
    if (!name || !*name) return false;
    const auto length = strnlen(name, BookEntry{}.name.size());
    if (length >= BookEntry{}.name.size()) return false;
    for (std::size_t i = 0; i < length; ++i)
        if (static_cast<unsigned char>(name[i]) < 0x20 || name[i] == '/' || name[i] == '\\') return false;
    const char* suffix = std::strrchr(name, '.');
    if (!suffix || suffix == name) return false;
    return strcasecmp(suffix, ".txt") == 0 || strcasecmp(suffix, ".epub") == 0;
}

bool FileInfo(const char* path, struct stat* info) {
#ifdef ESP_PLATFORM
    // SPIFFS has no links; its VFS implements stat, not lstat.
    return stat(path, info) == 0;
#else
    return lstat(path, info) == 0;
#endif
}
}

void BookFile::Close() {
    if (file_) std::fclose(file_);
    file_ = nullptr;
    size_ = offset_ = 0;
}

bool BookFile::Read(uint32_t offset, void* output, std::size_t size) {
    if (!file_ || offset > size_ || size > size_ - offset || (size && !output)) return false;
    if (offset != offset_ && std::fseek(file_, static_cast<long>(offset), SEEK_SET) != 0) return false;
    offset_ = offset;
    if (!size) return true;
    const auto read = std::fread(output, 1, size, file_);
    offset_ += read;
    return read == size;
}

BookStorage::BookStorage(const char* root) {
    if (root && *root && std::strlen(root) < root_.size())
        std::strcpy(root_.data(), root);
}

BookStorage::~BookStorage() {
#ifdef ESP_PLATFORM
    if (mounted_) esp_vfs_spiffs_unregister("books");
#endif
}

esp_err_t BookStorage::Mount() {
    if (mounted_) return ESP_OK;
    if (!root_[0]) return ESP_ERR_INVALID_ARG;
#ifdef ESP_PLATFORM
    esp_vfs_spiffs_conf_t config{};
    config.base_path = root_.data();
    config.partition_label = "books";
    config.max_files = 2;
    config.format_if_mount_failed = false;
    const auto result = esp_vfs_spiffs_register(&config);
    if (result != ESP_OK) return result;
#else
    DIR* directory = opendir(root_.data());
    if (!directory) return errno == ENOENT ? ESP_ERR_NOT_FOUND : ESP_FAIL;
    closedir(directory);
#endif
    mounted_ = true;
    return ESP_OK;
}

bool BookStorage::Path(const char* name, char* output, std::size_t capacity) const {
    if (!BookName(name)) return false;
    const int size = std::snprintf(output, capacity, "%s/%s", root_.data(), name);
    return size > 0 && static_cast<std::size_t>(size) < capacity;
}

esp_err_t BookStorage::List(BookEntry* entries, std::size_t capacity,
                          std::size_t* count, bool* truncated) {
    if (!entries || !capacity || !count || !truncated) return ESP_ERR_INVALID_ARG;
    *count = 0;
    *truncated = false;
    const auto mounted = Mount();
    if (mounted != ESP_OK) return mounted;
    DIR* directory = opendir(root_.data());
    if (!directory) return ESP_FAIL;
    esp_err_t result = ESP_OK;
    for (;;) {
        errno = 0;
        const auto* entry = readdir(directory);
        if (!entry) { if (errno) result = ESP_FAIL; break; }
        char path[256];
        struct stat info{};
        if (!Path(entry->d_name, path, sizeof(path)) || !FileInfo(path, &info) ||
            !S_ISREG(info.st_mode) || info.st_size < 0 ||
            static_cast<uint64_t>(info.st_size) > LONG_MAX ||
            static_cast<uint64_t>(info.st_size) > UINT32_MAX) continue;
        BookEntry book;
        std::strcpy(book.name.data(), entry->d_name);
        book.size = static_cast<uint32_t>(info.st_size);
        // Keep a deterministic bounded prefix even if directory order changes.
        std::size_t index = 0;
        while (index < *count && std::strcmp(entries[index].name.data(), book.name.data()) < 0) ++index;
        if (*count == capacity) *truncated = true;
        if (index == capacity) continue;
        if (*count < capacity) ++*count;
        for (std::size_t i = *count - 1; i > index; --i) entries[i] = entries[i - 1];
        entries[index] = book;
    }
    closedir(directory);
    return result;
}

esp_err_t BookStorage::Open(const char* name, BookFile* file) {
    if (!file) return ESP_ERR_INVALID_ARG;
    char path[256];
    if (!Path(name, path, sizeof(path))) return ESP_ERR_INVALID_ARG;
    const auto mounted = Mount();
    if (mounted != ESP_OK) return mounted;
    struct stat info{};
    if (!FileInfo(path, &info)) return errno == ENOENT ? ESP_ERR_NOT_FOUND : ESP_FAIL;
    if (!S_ISREG(info.st_mode) || info.st_size < 0 ||
        static_cast<uint64_t>(info.st_size) > LONG_MAX ||
        static_cast<uint64_t>(info.st_size) > UINT32_MAX) return ESP_ERR_INVALID_SIZE;
    auto* opened = std::fopen(path, "rb");
    if (!opened) return ESP_FAIL;
    file->Close();
    file->file_ = opened;
    file->size_ = static_cast<uint32_t>(info.st_size);
    return ESP_OK;
}

}  // namespace zectrix::storage
