#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <mutex>

#include "esp_err.h"
#include "zectrix_app_package.h"

namespace zectrix::storage {

struct BookEntry {
    std::array<char, 64> name{};
    uint32_t size = 0;
};

struct BookSpace {
    uint32_t total = 0;
    uint32_t used = 0;
    uint32_t available = 0;
};

enum class BookWriteResult : uint8_t { Ok, Invalid, Busy, Exists, NotFound, NoSpace, IoError };
class BookStorage;
class AppStorage;

class BookUpload {
public:
    BookUpload() = default;
    ~BookUpload() { Abort(); }
    BookUpload(const BookUpload&) = delete;
    BookUpload& operator=(const BookUpload&) = delete;
    BookWriteResult Write(const void* data, std::size_t size);
    BookWriteResult Commit();
    void Abort();

private:
    friend class BookStorage;
    BookStorage* owner_ = nullptr;
    std::FILE* file_ = nullptr;
    std::array<char, 256> target_{}, temporary_{};
    uint32_t expected_ = 0, received_ = 0;
    BookWriteResult error_ = BookWriteResult::Ok;
    package::Validator package_;
    bool packaged_ = false;
};

class BookFile {
public:
    BookFile() = default;
    ~BookFile() { Close(); }
    BookFile(const BookFile&) = delete;
    BookFile& operator=(const BookFile&) = delete;
    bool Read(uint32_t offset, void* output, std::size_t size);
    uint32_t Size() const { return size_; }
    void Close();

private:
    friend class BookStorage;
    std::FILE* file_ = nullptr;
    BookStorage* owner_ = nullptr;
    uint32_t size_ = 0;
    uint32_t offset_ = 0;
};

// Storage owns the mount. Readers close their files before Storage is released.
// Mount failure never formats or replaces the user's books.
class BookStorage {
public:
    explicit BookStorage(const char* root = "/books");
    ~BookStorage();
    BookStorage(const BookStorage&) = delete;
    BookStorage& operator=(const BookStorage&) = delete;
    esp_err_t List(BookEntry* entries, std::size_t capacity,
                   std::size_t* count, bool* truncated, const char* after = nullptr);
    esp_err_t Open(const char* name, BookFile* file);
    static bool ValidName(const char* name);
    // Management excludes reader handles until its USB or HTTP owner releases it.
    esp_err_t BeginManagement();
    esp_err_t EndManagement();
    esp_err_t Space(BookSpace* space);
    esp_err_t OpenManaged(const char* name, BookFile* file);
    BookWriteResult BeginUpload(const char* name, uint32_t size, BookUpload* upload);
    BookWriteResult Remove(const char* name);
    // Explicit maintenance only. Readers, uploads and management must exit first.
    esp_err_t Wipe();

private:
    friend class BookFile;
    friend class BookUpload;
    friend class AppStorage;
    void ReaderClosed();
    static bool ValidAppName(const char* name);
    esp_err_t ListImpl(BookEntry* entries, std::size_t capacity, std::size_t* count,
                       bool* truncated, const char* after, bool application, bool reverse = false);
    esp_err_t OpenImpl(const char* name, BookFile* file, bool managed, bool application = false);
    BookWriteResult UploadImpl(const char* name, uint32_t size, BookUpload* upload, bool application);
    BookWriteResult RemoveImpl(const char* name, bool application);
    esp_err_t ReadSpace(BookSpace* space);
    esp_err_t Mount();
    bool Path(const char* name, char* output, std::size_t capacity, bool application = false) const;
    std::array<char, 192> root_{};
    bool mounted_ = false;
    bool managing_ = false, uploading_ = false;
    unsigned readers_ = 0;
    std::mutex mutex_;
};

}  // namespace zectrix::storage
