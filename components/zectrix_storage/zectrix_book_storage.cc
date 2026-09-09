#include "zectrix_book_storage.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstring>
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef ESP_PLATFORM
#include "esp_spiffs.h"
#endif

namespace zectrix::storage {
namespace {
bool BookName(const char* name) {
    if (!name || !*name) return false;
    const auto length = strnlen(name, BookEntry{}.name.size());
    if (length >= BookEntry{}.name.size()) return false;
    for (std::size_t i = 0; i < length; ++i) {
        const auto c = static_cast<uint8_t>(name[i]);
        if (c < 0x20 || c == 0x7f || c == '/' || c == '\\') return false;
        if (c < 0x80) continue;
        const unsigned extra = c >= 0xc2 && c <= 0xdf ? 1 : c >= 0xe0 && c <= 0xef ? 2 :
            c >= 0xf0 && c <= 0xf4 ? 3 : 0;
        if (!extra || extra >= length - i) return false;
        uint32_t cp = c & (extra == 1 ? 31 : extra == 2 ? 15 : 7);
        for (unsigned n = 0; n < extra; ++n) {
            const auto next = static_cast<uint8_t>(name[++i]);
            if ((next & 0xc0) != 0x80) return false;
            cp = (cp << 6) | (next & 63);
        }
        if (cp < (extra == 1 ? 0x80U : extra == 2 ? 0x800U : 0x10000U) ||
            cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return false;
    }
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
    if (owner_) owner_->ReaderClosed();
    owner_ = nullptr;
}

void BookStorage::ReaderClosed() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (readers_) --readers_;
}

bool BookStorage::ValidName(const char* name) { return BookName(name); }

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
                          std::size_t* count, bool* truncated, const char* after) {
    if (!entries || !capacity || !count || !truncated) return ESP_ERR_INVALID_ARG;
    std::lock_guard<std::mutex> lock(mutex_);
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
        if (after && std::strcmp(entry->d_name, after) <= 0) continue;
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
    return OpenImpl(name, file, false);
}

esp_err_t BookStorage::OpenManaged(const char* name, BookFile* file) {
    return OpenImpl(name, file, true);
}

esp_err_t BookStorage::OpenImpl(const char* name, BookFile* file, bool managed) {
    if (!file) return ESP_ERR_INVALID_ARG;
    file->Close();
    std::lock_guard<std::mutex> lock(mutex_);
    if (managing_ != managed || uploading_) return ESP_ERR_INVALID_STATE;
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
    file->file_ = opened;
    file->size_ = static_cast<uint32_t>(info.st_size);
    file->owner_ = this;
    ++readers_;
    return ESP_OK;
}

esp_err_t BookStorage::BeginManagement() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (managing_ || readers_) return ESP_ERR_INVALID_STATE;
    const auto result = Mount();
    if (result != ESP_OK) return result;
    char path[256];
    std::snprintf(path, sizeof(path), "%s/.upload.part", root_.data());
    // Only our reserved staging file is removed after an interrupted session.
    if (std::remove(path) != 0 && errno != ENOENT) return ESP_FAIL;
    managing_ = true;
    return ESP_OK;
}

esp_err_t BookStorage::EndManagement() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (readers_ || uploading_) return ESP_ERR_INVALID_STATE;
    managing_ = false;
    return ESP_OK;
}

esp_err_t BookStorage::ReadSpace(BookSpace* space) {
    const auto mounted = Mount();
    if (mounted != ESP_OK) return mounted;
    std::size_t total = 0x400000, used = 0;
#ifdef ESP_PLATFORM
    if (esp_spiffs_info("books", &total, &used) != ESP_OK) return ESP_FAIL;
#else
    DIR* directory = opendir(root_.data());
    if (!directory) return ESP_FAIL;
    while (const auto* entry = readdir(directory)) {
        char path[256];
        std::snprintf(path, sizeof(path), "%s/%s", root_.data(), entry->d_name);
        struct stat info{};
        if (FileInfo(path, &info) && S_ISREG(info.st_mode) && info.st_size > 0)
            used += static_cast<std::size_t>(info.st_size);
    }
    closedir(directory);
#endif
    // SPIFFS needs free pages for metadata and garbage collection.
    const auto limit = total * 3 / 4;
    *space = {static_cast<uint32_t>(total), static_cast<uint32_t>(std::min(used, total)),
              static_cast<uint32_t>(used < limit ? limit - used : 0)};
    return ESP_OK;
}

esp_err_t BookStorage::Space(BookSpace* space) {
    if (!space) return ESP_ERR_INVALID_ARG;
    std::lock_guard<std::mutex> lock(mutex_);
    return ReadSpace(space);
}

BookWriteResult BookStorage::BeginUpload(const char* name, uint32_t size, BookUpload* upload) {
    if (!upload || upload->owner_) return BookWriteResult::Busy;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!managing_ || uploading_ || readers_) return BookWriteResult::Busy;
    if (!Path(name, upload->target_.data(), upload->target_.size())) return BookWriteResult::Invalid;
    struct stat info{};
    if (FileInfo(upload->target_.data(), &info)) return BookWriteResult::Exists;
    if (errno != ENOENT) return BookWriteResult::IoError;
    BookSpace space;
    if (ReadSpace(&space) != ESP_OK) return BookWriteResult::IoError;
    if (size > space.available) return BookWriteResult::NoSpace;
    std::snprintf(upload->temporary_.data(), upload->temporary_.size(), "%s/.upload.part", root_.data());
    upload->file_ = std::fopen(upload->temporary_.data(), "wb");
    if (!upload->file_) return errno == ENOSPC ? BookWriteResult::NoSpace : BookWriteResult::IoError;
    upload->expected_ = size;
    upload->received_ = 0;
    upload->owner_ = this;
    uploading_ = true;
    return BookWriteResult::Ok;
}

BookWriteResult BookStorage::Remove(const char* name) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!managing_ || uploading_ || readers_) return BookWriteResult::Busy;
    char path[256];
    if (!Path(name, path, sizeof(path))) return BookWriteResult::Invalid;
    struct stat info{};
    if (!FileInfo(path, &info)) return errno == ENOENT ? BookWriteResult::NotFound : BookWriteResult::IoError;
    if (!S_ISREG(info.st_mode)) return BookWriteResult::Invalid;
    return std::remove(path) == 0 ? BookWriteResult::Ok : BookWriteResult::IoError;
}

BookWriteResult BookUpload::Write(const void* data, std::size_t size) {
    if (!file_ || (size && !data) || size > expected_ - received_) return BookWriteResult::Invalid;
    if (size && std::fwrite(data, 1, size, file_) != size)
        return errno == ENOSPC ? BookWriteResult::NoSpace : BookWriteResult::IoError;
    received_ += size;
    return BookWriteResult::Ok;
}

BookWriteResult BookUpload::Commit() {
    if (!file_ || received_ != expected_) return BookWriteResult::Invalid;
    if (std::fflush(file_) != 0 || fsync(fileno(file_)) != 0)
        return errno == ENOSPC ? BookWriteResult::NoSpace : BookWriteResult::IoError;
    const int closed = std::fclose(file_);
    file_ = nullptr;
    if (closed != 0) return BookWriteResult::IoError;
    std::lock_guard<std::mutex> lock(owner_->mutex_);
    struct stat info{};
    if (FileInfo(target_.data(), &info)) return BookWriteResult::Exists;
    if (errno != ENOENT || std::rename(temporary_.data(), target_.data()) != 0) return BookWriteResult::IoError;
    owner_->uploading_ = false;
    owner_ = nullptr;
    return BookWriteResult::Ok;
}

void BookUpload::Abort() {
    if (file_) std::fclose(file_);
    file_ = nullptr;
    if (owner_) {
        std::lock_guard<std::mutex> lock(owner_->mutex_);
        std::remove(temporary_.data());
        owner_->uploading_ = false;
        owner_ = nullptr;
    }
}

}  // namespace zectrix::storage
