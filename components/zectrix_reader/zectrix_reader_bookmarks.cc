#include "zectrix_reader_bookmarks.h"

#include <algorithm>
#include <cstring>

namespace zectrix::reader {
namespace {
void Put16(uint8_t* p, uint16_t value) { p[0] = value; p[1] = value >> 8; }
void Put32(uint8_t* p, uint32_t value) { Put16(p, value); Put16(p + 2, value >> 16); }
uint16_t Get16(const uint8_t* p) { return p[0] | static_cast<uint16_t>(p[1]) << 8; }
uint32_t Get32(const uint8_t* p) { return Get16(p) | static_cast<uint32_t>(Get16(p + 2)) << 16; }
bool ValidId(const char* id, std::size_t size) {
    if (!size || size > 63 || (size == 1 && id[0] == '.') ||
        (size == 2 && id[0] == '.' && id[1] == '.')) return false;
    for (std::size_t i = 0; i < size; ++i)
        if (static_cast<unsigned char>(id[i]) < 0x20 || id[i] == '/' || id[i] == '\\') return false;
    return true;
}
}  // namespace

bool Bookmark::operator==(const Bookmark& other) const {
    return book_id == other.book_id && source_bytes == other.source_bytes &&
        position == other.position && font == other.font && progress_per_mille == other.progress_per_mille;
}

bool EncodeBookmark(const Bookmark& mark, uint8_t* output, std::size_t capacity, std::size_t* size) {
    const auto end = std::find(mark.book_id.begin(), mark.book_id.end(), '\0');
    const auto length = static_cast<std::size_t>(end - mark.book_id.begin());
    if (!output || !size || !ValidId(mark.book_id.data(), length) || capacity < 15 + length ||
        (mark.font != FontSize::Small && mark.font != FontSize::Large) || mark.progress_per_mille > 1000)
        return false;
    output[0] = 1;
    output[1] = static_cast<uint8_t>(mark.font);
    Put16(output + 2, mark.position.chapter);
    Put32(output + 4, mark.position.offset);
    Put32(output + 8, mark.source_bytes);
    Put16(output + 12, mark.progress_per_mille);
    output[14] = length;
    std::memcpy(output + 15, mark.book_id.data(), length);
    *size = 15 + length;
    return true;
}

bool DecodeBookmark(const uint8_t* input, std::size_t size, Bookmark* output) {
    if (!input || !output || size < 16 || input[0] != 1 || input[1] > 1 ||
        size != 15U + input[14] || !ValidId(reinterpret_cast<const char*>(input + 15), input[14]) ||
        Get16(input + 12) > 1000) return false;
    Bookmark mark;
    std::memcpy(mark.book_id.data(), input + 15, input[14]);
    mark.font = static_cast<FontSize>(input[1]);
    mark.position = {Get32(input + 4), Get16(input + 2)};
    mark.source_bytes = Get32(input + 8);
    mark.progress_per_mille = Get16(input + 12);
    *output = mark;
    return true;
}

Result Bookmarks::Load() {
    if (loaded_) return Result::Ok;
    std::array<uint8_t, kBookmarkStoreBytes> bytes{};
    std::size_t size = 0;
    const auto read = store_.Load(bytes.data(), bytes.size(), &size);
    if (read == Result::End) { loaded_ = true; return Result::Ok; }
    if (read != Result::Ok) return read;
    if (size < 12 || size > bytes.size() || bytes[0] != 1 || bytes[1] > kCapacity || bytes[2] || bytes[3])
        return Result::Invalid;
    State state;
    state.count = bytes[1];
    state.outbound_revision = Get32(bytes.data() + 4);
    state.inbound_revision = Get32(bytes.data() + 8);
    std::size_t offset = 12;
    auto take = [&](Bookmark* mark) {
        if (offset >= size) return false;
        const auto length = bytes[offset++];
        if (length > size - offset || !DecodeBookmark(bytes.data() + offset, length, mark)) return false;
        offset += length;
        return true;
    };
    for (std::size_t i = 0; i < state.count; ++i) {
        if (!take(&state.entries[i])) return Result::Invalid;
        for (std::size_t j = 0; j < i; ++j)
            if (state.entries[i].book_id == state.entries[j].book_id) return Result::Invalid;
    }
    if (state.outbound_revision && !take(&state.outgoing)) return Result::Invalid;
    if (offset != size) return Result::Invalid;
    state_ = state;
    loaded_ = true;
    return Result::Ok;
}

const Bookmark* Bookmarks::Find(const char* id, uint32_t source_bytes) const {
    if (!loaded_ || !id) return nullptr;
    for (std::size_t i = 0; i < state_.count; ++i)
        if (std::strcmp(id, state_.entries[i].book_id.data()) == 0 &&
            source_bytes == state_.entries[i].source_bytes) return &state_.entries[i];
    return nullptr;
}

void Bookmarks::Put(State& state, const Bookmark& mark) {
    std::size_t index = 0;
    while (index < state.count && state.entries[index].book_id != mark.book_id) ++index;
    if (index == state.count && state.count < kCapacity) ++state.count;
    index = std::min(index, kCapacity - 1);
    for (; index; --index) state.entries[index] = state.entries[index - 1];
    state.entries[0] = mark;
}

Result Bookmarks::Commit(const State& state) {
    std::array<uint8_t, kBookmarkStoreBytes> bytes{};
    bytes[0] = 1;
    bytes[1] = state.count;
    Put32(bytes.data() + 4, state.outbound_revision);
    Put32(bytes.data() + 8, state.inbound_revision);
    std::size_t offset = 12;
    auto append = [&](const Bookmark& mark) {
        std::size_t size = 0;
        if (offset >= bytes.size() || !EncodeBookmark(mark, bytes.data() + offset + 1,
            bytes.size() - offset - 1, &size)) return false;
        bytes[offset] = size;
        offset += size + 1;
        return true;
    };
    for (std::size_t i = 0; i < state.count; ++i) if (!append(state.entries[i])) return Result::Invalid;
    if (state.outbound_revision && !append(state.outgoing)) return Result::Invalid;
    const auto saved = store_.Save(bytes.data(), offset);
    if (saved == Result::Ok) state_ = state;
    return saved;
}

Result Bookmarks::Save(const Bookmark& mark) {
    if (!loaded_) return Result::Invalid;
    std::array<uint8_t, kBookmarkBytes> bytes{};
    std::size_t size = 0;
    Bookmark canonical;
    if (!EncodeBookmark(mark, bytes.data(), bytes.size(), &size) ||
        !DecodeBookmark(bytes.data(), size, &canonical)) return Result::Invalid;
    const auto* previous = Find(canonical.book_id.data(), canonical.source_bytes);
    if (previous && *previous == canonical) return Result::Ok;
    if (state_.outbound_revision == UINT32_MAX) return Result::TooLarge;
    State candidate = state_;
    Put(candidate, canonical);
    candidate.outgoing = canonical;
    ++candidate.outbound_revision;
    // The application snapshot and its replay revision commit together before
    // enqueue. A reboot in between retries the exact same durable payload.
    return Commit(candidate);
}

Result Bookmarks::Sync() {
    if (!loaded_) return Result::Invalid;
    Result published = Result::Ok;
    std::array<uint8_t, kBookmarkBytes> bytes{};
    std::size_t size = 0;
    if (pending_sync()) {
        if (!EncodeBookmark(state_.outgoing, bytes.data(), bytes.size(), &size)) return Result::Invalid;
        published = store_.Publish(state_.outbound_revision, bytes.data(), size);
        if (published == Result::Ok) queued_revision_ = state_.outbound_revision;
    }
    uint32_t revision = 0;
    const auto received = store_.Receive(&revision, bytes.data(), bytes.size(), &size);
    if (received == Result::Ok && revision > state_.inbound_revision) {
        Bookmark mark;
        if (!DecodeBookmark(bytes.data(), size, &mark)) return Result::Invalid;
        remote_ = mark;
        remote_revision_ = revision;
    } else if (received != Result::Ok && received != Result::End && published == Result::Ok) return received;
    return published;
}

Result Bookmarks::ApplyRemote() {
    if (!loaded_ || !remote_revision_) return Result::Invalid;
    if (state_.outbound_revision == UINT32_MAX) return Result::TooLarge;
    State candidate = state_;
    Put(candidate, remote_);
    candidate.inbound_revision = remote_revision_;
    candidate.outgoing = remote_;
    ++candidate.outbound_revision;
    const auto saved = Commit(candidate);
    if (saved == Result::Ok) remote_revision_ = 0;
    return saved;
}

Result Bookmarks::ResetPeer() {
    if (!loaded_) return Result::Invalid;
    State candidate = state_;
    candidate.inbound_revision = 0;
    const auto result = state_.inbound_revision ? Commit(candidate) : Result::Ok;
    if (result == Result::Ok) remote_revision_ = queued_revision_ = 0;
    return result;
}

}  // namespace zectrix::reader
