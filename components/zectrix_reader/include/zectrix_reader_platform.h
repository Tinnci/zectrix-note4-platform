#pragma once

#include "zectrix_reader_bookmarks.h"
#include "zectrix_reader_library.h"
#include "zectrix_storage_service.h"

namespace zectrix::connectivity { class ConnectivityService; }

namespace zectrix::reader {

class StorageLibrary final : public Library {
public:
    static constexpr std::size_t kCapacity = 32;
    explicit StorageLibrary(storage::StorageService& storage) : storage_(storage) {}
    Result Refresh() override;
    std::size_t count() const override { return count_; }
    BookInfo Get(std::size_t index) const override;
    bool truncated() const override { return truncated_; }
    Result Open(std::size_t index, Source** source) override;
    void Close() override { source_.file.Close(); }

private:
    class FileSource final : public Source {
    public:
        uint32_t Size() const override { return file.Size(); }
        bool Read(uint32_t offset, void* output, std::size_t size) override {
            return file.Read(offset, output, size);
        }
        storage::BookFile file;
    } source_;
    storage::StorageService& storage_;
    std::array<storage::BookEntry, kCapacity> entries_{};
    std::size_t count_ = 0;
    bool truncated_ = false;
};

class PlatformBookmarkStore final : public BookmarkStore {
public:
    PlatformBookmarkStore(storage::StorageService& storage,
                          connectivity::ConnectivityService& connectivity)
        : storage_(storage), connectivity_(connectivity) {}
    Result Load(uint8_t* output, std::size_t capacity, std::size_t* size) override;
    Result Save(const uint8_t* bytes, std::size_t size) override;
    Result Publish(uint32_t revision, const uint8_t* bytes, std::size_t size) override;
    Result Receive(uint32_t* revision, uint8_t* output, std::size_t capacity, std::size_t* size) override;

private:
    storage::StorageService& storage_;
    connectivity::ConnectivityService& connectivity_;
};

}  // namespace zectrix::reader
