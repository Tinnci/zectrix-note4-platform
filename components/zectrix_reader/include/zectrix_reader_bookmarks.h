#pragma once

#include "zectrix_reader.h"

namespace zectrix::reader {

constexpr uint16_t kProgressSyncKey = 0x0101;
constexpr std::size_t kBookmarkBytes = 78;
constexpr std::size_t kBookmarkStoreBytes = 800;

struct Bookmark {
    std::array<char, 64> book_id{};
    uint32_t source_bytes = 0;
    Position position{};
    FontSize font = FontSize::Small;
    uint16_t progress_per_mille = 0;
    bool operator==(const Bookmark& other) const;
};

bool EncodeBookmark(const Bookmark& bookmark, uint8_t* output, std::size_t capacity, std::size_t* size);
bool DecodeBookmark(const uint8_t* input, std::size_t size, Bookmark* bookmark);

class BookmarkStore {
public:
    virtual ~BookmarkStore() = default;
    virtual Result Load(uint8_t* output, std::size_t capacity, std::size_t* size) = 0;
    virtual Result Save(const uint8_t* bytes, std::size_t size) = 0;
    // Publish maps an already queued or acknowledged revision to Ok.
    virtual Result Publish(uint32_t revision, const uint8_t* bytes, std::size_t size) = 0;
    virtual Result Receive(uint32_t* revision, uint8_t* output, std::size_t capacity, std::size_t* size) = 0;
};

class Bookmarks {
public:
    static constexpr std::size_t kCapacity = 8;
    explicit Bookmarks(BookmarkStore& store) : store_(store) {}
    Result Load();
    const Bookmark* Find(const char* book_id, uint32_t source_bytes) const;
    // The sleep dashboard displays the latest committed local position.
    const Bookmark* Latest() const { return loaded_ && state_.count ? &state_.entries[0] : nullptr; }
    Result Save(const Bookmark& bookmark);
    Result Sync();
    const Bookmark* remote() const { return remote_revision_ ? &remote_ : nullptr; }
    // Applying phone progress is explicit. Incoming traffic never moves a page.
    Result ApplyRemote();
    // Reset after local peer removal, preserving book positions for the new phone.
    Result ResetPeer();
    bool pending_sync() const { return state_.outbound_revision > queued_revision_; }

private:
    struct State {
        std::array<Bookmark, kCapacity> entries{};
        Bookmark outgoing{};
        std::size_t count = 0;
        uint32_t outbound_revision = 0;
        uint32_t inbound_revision = 0;
    };
    static void Put(State& state, const Bookmark& bookmark);
    Result Commit(const State& state);
    BookmarkStore& store_;
    State state_{};
    Bookmark remote_{};
    uint32_t remote_revision_ = 0;
    uint32_t queued_revision_ = 0;
    bool loaded_ = false;
};

}  // namespace zectrix::reader
