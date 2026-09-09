#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "esp_err.h"

namespace zectrix::storage {

struct BookEntry {
    std::array<char, 64> name{};
    uint32_t size = 0;
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
                   std::size_t* count, bool* truncated);
    esp_err_t Open(const char* name, BookFile* file);

private:
    esp_err_t Mount();
    bool Path(const char* name, char* output, std::size_t capacity) const;
    std::array<char, 192> root_{};
    bool mounted_ = false;
};

}  // namespace zectrix::storage
