#pragma once

#include "zectrix_book_storage.h"

namespace zectrix::storage {

// Apps share the books mount, staging file and exclusive management lease.
// Logical names never expose the reserved on-disk prefix to USB or the UI.
class AppStorage final {
public:
    static constexpr std::size_t kNameSize = 48;
    static constexpr uint32_t kSourceLimit = 32768;
    explicit AppStorage(BookStorage& storage) : storage_(storage) {}
    static bool ValidName(const char* name) { return BookStorage::ValidAppName(name); }
    esp_err_t List(BookEntry* entries, std::size_t capacity, std::size_t* count,
                   bool* more, const char* cursor = nullptr, bool previous = false) {
        return storage_.ListImpl(entries, capacity, count, more, cursor, true, previous);
    }
    esp_err_t Open(const char* name, BookFile* file) { return storage_.OpenImpl(name, file, false, true); }
    esp_err_t OpenManaged(const char* name, BookFile* file) { return storage_.OpenImpl(name, file, true, true); }
    BookWriteResult BeginUpload(const char* name, uint32_t size, BookUpload* upload) {
        return storage_.UploadImpl(name, size, upload, true);
    }
    BookWriteResult Remove(const char* name) { return storage_.RemoveImpl(name, true); }

private:
    BookStorage& storage_;
};

}  // namespace zectrix::storage
