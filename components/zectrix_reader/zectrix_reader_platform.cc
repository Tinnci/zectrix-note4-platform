#include "zectrix_reader_platform.h"

#include <cstring>
#include <strings.h>

#include "zectrix_connectivity_service.h"

namespace zectrix::reader {
namespace {
constexpr char kBookmarksKey[] = "reader.marks";

Result StorageResult(esp_err_t result) {
    switch (result) {
        case ESP_OK: return Result::Ok;
        case ESP_ERR_NOT_FOUND: return Result::End;
        case ESP_ERR_NO_MEM: return Result::NoMemory;
        case ESP_ERR_INVALID_SIZE: return Result::TooLarge;
        case ESP_ERR_INVALID_ARG: return Result::Invalid;
        default: return Result::IoError;
    }
}

Result SyncResult(companion::SyncStatus result) {
    using Status = companion::SyncStatus;
    switch (result) {
        case Status::kOk:
        case Status::kDuplicate:
        case Status::kStaleRevision: return Result::Ok;
        case Status::kNotInitialized:
        case Status::kOutboxFull: return Result::Pending;
        case Status::kNotFound: return Result::End;
        case Status::kValueTooLarge: return Result::TooLarge;
        case Status::kStoreError: return Result::IoError;
        default: return Result::Invalid;
    }
}
}

Result StorageLibrary::Refresh() {
    Close();
    count_ = 0;
    const auto result = storage_.ListBooks(entries_.data(), entries_.size(), &count_, &truncated_);
    if (result != ESP_OK) count_ = 0;
    return StorageResult(result);
}

BookInfo StorageLibrary::Get(std::size_t index) const {
    if (index >= count_) return {};
    BookInfo book;
    book.id = entries_[index].name;
    book.bytes = entries_[index].size;
    const auto* suffix = std::strrchr(book.id.data(), '.');
    book.format = suffix && strcasecmp(suffix, ".epub") == 0 ? Format::Epub : Format::Text;
    return book;
}

Result StorageLibrary::Open(std::size_t index, Source** source) {
    if (!source || index >= count_) return Result::Invalid;
    *source = nullptr;
    Close();
    const auto result = storage_.OpenBook(entries_[index].name.data(), &source_.file);
    if (result != ESP_OK) return StorageResult(result);
    entries_[index].size = source_.Size();
    *source = &source_;
    return Result::Ok;
}

Result PlatformBookmarkStore::Load(uint8_t* output, std::size_t capacity, std::size_t* size) {
    if (!output || !size) return Result::Invalid;
    *size = 0;
    auto result = storage_.GetBlob(kBookmarksKey, nullptr, size);
    if (result != ESP_OK) return StorageResult(result);
    if (*size > capacity) return Result::TooLarge;
    result = storage_.GetBlob(kBookmarksKey, output, size);
    return StorageResult(result);
}

Result PlatformBookmarkStore::Save(const uint8_t* bytes, std::size_t size) {
    if (!bytes || !size || size > kBookmarkStoreBytes) return Result::Invalid;
    return StorageResult(storage_.SetBlob(kBookmarksKey, bytes, size));
}

Result PlatformBookmarkStore::Publish(uint32_t revision, const uint8_t* bytes, std::size_t size) {
    return SyncResult(connectivity_.PutDurableState(kProgressSyncKey, revision, bytes, size));
}

Result PlatformBookmarkStore::Receive(uint32_t* revision, uint8_t* output,
                                    std::size_t capacity, std::size_t* size) {
    if (!revision || !output || !size) return Result::Invalid;
    *size = capacity;
    return SyncResult(connectivity_.ReadDurableState(kProgressSyncKey, revision, output, size));
}

}  // namespace zectrix::reader
