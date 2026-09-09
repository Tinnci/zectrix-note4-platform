#include "zectrix_reader.h"
#include "zectrix_reader_bookmarks.h"
#include "reader_internal.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace zectrix::reader;

namespace {
class Bytes final : public Source {
public:
    explicit Bytes(const std::string& text) : bytes(text.begin(), text.end()) {}
    std::vector<uint8_t> bytes;
    std::size_t largest_read = 0;
    uint64_t total_read = 0;
    bool fail = false;
    uint32_t Size() const override { return bytes.size(); }
    bool Read(uint32_t offset, void* output, std::size_t size) override {
        largest_read = std::max(largest_read, size);
        total_read += size;
        if (fail || offset > bytes.size() || size > bytes.size() - offset) return false;
        std::memcpy(output, bytes.data() + offset, size);
        return true;
    }
};

Result Finish(Engine& engine, std::size_t budget = 4096) {
    for (unsigned i = 0; i < 100000; ++i) {
        const auto result = engine.Poll(budget);
        if (result != Result::Pending) return result;
    }
    assert(false && "Reader failed to make progress");
    return Result::Invalid;
}

std::u32string Text(const Page& page) {
    std::u32string text;
    for (std::size_t i = 0; i < page.count; ++i) {
        const auto& glyph = page.glyphs[i];
        assert(glyph.x + GlyphWidth(glyph.codepoint, page.font) <= Page::kWidth);
        assert(glyph.y + FontHeight(page.font) <= Page::kHeight);
        text += glyph.codepoint;
    }
    return text;
}

std::string ReadFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    assert(file.good());
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void TextTests() {
    std::string book = "\xef\xbb\xbf第一段。你好，世界！This is an English paragraph.\r\n";
    for (int i = 0; i < 900; ++i) book += "第二段含中文标点（不能拆错）。 English words stay together.\n";
    Bytes source(book);
    Engine engine;
    assert(engine.Open(source, Format::Text) == Result::Ok);
    assert(engine.Seek({}, FontSize::Small) == Result::Pending);
    assert(engine.Poll(1) == Result::Pending);
    assert(Finish(engine, 7) == Result::Ok);
    const auto first = Text(engine.page());
    assert(first.find(U"第一段。你好，世界！") == 0);
    assert(engine.page().glyphs[0].x == 32);
    assert(engine.page().start == Position{});
    assert(engine.page().progress_per_mille < 1000);
    const Position second = engine.page().next;
    assert(engine.Next() == Result::Pending);
    assert(Finish(engine) == Result::Ok);
    const auto second_text = Text(engine.page());
    assert(engine.page().start == second);
    assert(engine.Previous() == Result::Pending);
    assert(Finish(engine) == Result::Ok);
    assert(Text(engine.page()) == first);
    assert(engine.Next() == Result::Pending);
    assert(Finish(engine) == Result::Ok);
    assert(Text(engine.page()) == second_text);
    assert(engine.SetFont(FontSize::Large) == Result::Pending);
    assert(Finish(engine) == Result::Ok);
    assert(engine.page().start == second);
    const auto large = Text(engine.page());
    assert(large.size() < second_text.size());
    assert(large.substr(0, 15) == second_text.substr(0, 15));
    assert(engine.Previous() == Result::Pending);
    assert(Finish(engine, 13) == Result::Ok);
    assert(engine.page().start < second);

    assert(engine.Seek({static_cast<uint32_t>(book.size() - 1000), 0}, FontSize::Small) == Result::Pending);
    assert(engine.Poll(64) == Result::Pending);
    engine.Cancel();
    assert(!engine.busy());
    assert(engine.Seek({}, FontSize::Small) == Result::Pending);
    source.fail = true;
    assert(Finish(engine) == Result::IoError);
    source.fail = false;
    assert(engine.Seek({}, FontSize::Small) == Result::Pending);
    assert(Finish(engine) == Result::Ok);
    assert(source.largest_read <= 1045);

    Bytes invalid(std::string("\xff\xe2") + "A\xc0\xaf\xf0\x9f");
    assert(engine.Open(invalid, Format::Text) == Result::Ok);
    assert(engine.Seek({}, FontSize::Small) == Result::Pending);
    assert(Finish(engine, 1) == Result::Ok);
    assert(Text(engine.page()) == U"\ufffd\ufffdA\ufffd\ufffd\ufffd");
    assert(engine.page().end && engine.Next() == Result::End);

    Bytes word(std::string(4000, 'w'));
    assert(engine.Open(word, Format::Text) == Result::Ok);
    assert(engine.Seek({}, FontSize::Small) == Result::Pending);
    std::size_t letters = 0;
    do {
        assert(Finish(engine) == Result::Ok);
        letters += Text(engine.page()).size();
    } while (engine.Next() == Result::Pending);
    assert(letters == 4000);
    assert(engine.page().progress_per_mille == 1000);

    Bytes empty("");
    assert(engine.Open(empty, Format::Text) == Result::Ok);
    assert(engine.Seek({}, FontSize::Small) == Result::Pending);
    assert(Finish(engine) == Result::Ok);
    assert(engine.page().count == 0 && engine.page().end);
}

std::u32string EpubTest(const std::string& path) {
    Bytes source(ReadFile(path));
    Engine engine;
    assert(engine.Open(source, Format::Epub) == Result::Ok);
    assert(engine.chapters() == 2);
    assert(!engine.ValidPosition({0, 2}));
    assert(engine.Seek({}, FontSize::Small) == Result::Pending);
    assert(Finish(engine, 3) == Result::Ok);
    const auto first = Text(engine.page());
    assert(first.find(U"第一章。你好，世界！English") == 0);
    assert(first.find(U"& entities 中文.") != std::u32string::npos);
    const auto second_start = engine.page().next;
    assert(engine.Next() == Result::Pending);
    assert(Finish(engine) == Result::Ok);
    const auto second = Text(engine.page());
    assert(engine.Seek(second_start, FontSize::Small) == Result::Pending);
    assert(Finish(engine, 1) == Result::Ok);
    assert(Text(engine.page()) == second);
    assert(engine.Previous() == Result::Pending);
    assert(Finish(engine) == Result::Ok);
    assert(Text(engine.page()) == first);
    std::u32string complete;
    unsigned pages = 0;
    do {
        assert(Finish(engine) == Result::Ok);
        complete += Text(engine.page());
        assert(++pages < 400);
    } while (engine.Next() == Result::Pending);
    assert(complete.find(U"HIDDEN") == std::u32string::npos);
    assert(complete.find(U"SECOND CHAPTER") != std::u32string::npos);
    assert(complete.find(U"日本語と한국어。最后一页。") != std::u32string::npos);
    assert(source.largest_read <= 1045);
    assert(engine.page().end && engine.page().progress_per_mille == 1000);
    return complete;
}

class Store final : public BookmarkStore {
public:
    std::vector<uint8_t> local, outbound, inbound;
    uint32_t sent = 0, received = 0;
    unsigned writes = 0;
    bool fail_save = false, offline = false;
    Result Load(uint8_t* output, std::size_t capacity, std::size_t* size) override {
        if (local.empty()) return Result::End;
        if (local.size() > capacity) return Result::TooLarge;
        *size = local.size(); std::memcpy(output, local.data(), *size); return Result::Ok;
    }
    Result Save(const uint8_t* bytes, std::size_t size) override {
        if (fail_save) return Result::IoError;
        local.assign(bytes, bytes + size); ++writes; return Result::Ok;
    }
    Result Publish(uint32_t revision, const uint8_t* bytes, std::size_t size) override {
        if (offline) return Result::Pending;
        const std::vector<uint8_t> value(bytes, bytes + size);
        assert(revision >= sent);
        if (revision == sent) assert(value == outbound);
        sent = revision; outbound = value; return Result::Ok;
    }
    Result Receive(uint32_t* revision, uint8_t* output, std::size_t capacity, std::size_t* size) override {
        if (inbound.empty()) return Result::End;
        assert(inbound.size() <= capacity);
        *revision = received; *size = inbound.size(); std::memcpy(output, inbound.data(), *size); return Result::Ok;
    }
};

void BookmarkTests() {
    Store store;
    Bookmarks marks(store);
    assert(marks.Load() == Result::Ok);
    Bookmark mark;
    std::strcpy(mark.book_id.data(), "test.epub");
    mark.source_bytes = 30000; mark.position = {451, 2}; mark.progress_per_mille = 45;
    std::array<uint8_t, kBookmarkBytes> bytes{};
    std::size_t size = 0;
    assert(EncodeBookmark(mark, bytes.data(), bytes.size(), &size));
    Bookmark copy;
    assert(DecodeBookmark(bytes.data(), size, &copy) && copy == mark);
    assert(!DecodeBookmark(bytes.data(), size - 1, &copy));
    assert(marks.Save(mark) == Result::Ok && marks.pending_sync());
    assert(marks.Save(mark) == Result::Ok && store.writes == 1);
    store.offline = true;
    assert(marks.Sync() == Result::Pending && marks.pending_sync());
    Bookmarks reboot(store);
    assert(reboot.Load() == Result::Ok);
    assert(*reboot.Find("test.epub", 30000) == mark);
    assert(!reboot.Find("test.epub", 31000));
    store.offline = false;
    assert(reboot.Sync() == Result::Ok && !reboot.pending_sync());
    assert(store.sent == 1);
    mark.position.offset = 900;
    store.fail_save = true;
    assert(reboot.Save(mark) == Result::IoError);
    assert(reboot.Find("test.epub", 30000)->position.offset == 451);
    store.fail_save = false;
    assert(reboot.Save(mark) == Result::Ok);
    assert(reboot.Sync() == Result::Ok && store.sent == 2);
    mark.position.offset = 1234;
    assert(EncodeBookmark(mark, bytes.data(), bytes.size(), &size));
    store.inbound.assign(bytes.begin(), bytes.begin() + size);
    store.received = 1;
    assert(reboot.Sync() == Result::Ok);
    assert(reboot.remote()->position.offset == 1234);
    assert(reboot.Find("test.epub", 30000)->position.offset == 900);
    store.fail_save = true;
    assert(reboot.ApplyRemote() == Result::IoError && reboot.remote());
    store.fail_save = false;
    assert(reboot.ApplyRemote() == Result::Ok && !reboot.remote());
    assert(reboot.Find("test.epub", 30000)->position.offset == 1234);
    Bookmarks again(store);
    assert(again.Load() == Result::Ok && again.Sync() == Result::Ok && !again.remote());
    assert(store.sent == 2);
    assert(again.Find("test.epub", 30000)->position.offset == 1234);
    for (unsigned i = 0; i < 10; ++i) {
        std::snprintf(mark.book_id.data(), mark.book_id.size(), "book%u.txt", i);
        assert(again.Save(mark) == Result::Ok);
    }
    assert(!again.Find("test.epub", 30000));
    store.local[0] = 99;
    const auto saved = store.local;
    Bookmarks corrupt(store);
    assert(corrupt.Load() == Result::Invalid);
    assert(corrupt.Save(mark) == Result::Invalid);
    assert(store.local == saved);
}
}  // namespace

int main(int argc, char** argv) {
    assert(argc == 2);
    const std::string dir = argv[1];
    TextTests();
    const auto text = EpubTest(dir + "/stored.epub");
    assert(text == EpubTest(dir + "/deflated.epub"));
    assert(text == EpubTest(dir + "/descriptor.epub"));
    Bytes hidden(ReadFile(dir + "/long-hidden.epub"));
    Engine engine;
    assert(engine.Open(hidden, Format::Epub) == Result::Ok);
    assert(engine.Seek({}, FontSize::Small) == Result::Pending);
    for (unsigned i = 0; i < 10; ++i) assert(engine.Poll(1000) == Result::Pending);
    engine.Cancel();
    assert(!engine.busy());
    assert(engine.Seek({}, FontSize::Small) == Result::Pending);
    assert(Finish(engine) == Result::Ok && Text(engine.page()) == U"VISIBLE");
    Bytes empty(ReadFile(dir + "/empty-chapter.epub"));
    assert(engine.Open(empty, Format::Epub) == Result::Ok);
    assert(engine.Seek({}, FontSize::Small) == Result::Pending);
    assert(Finish(engine) == Result::Ok && Text(engine.page()) == U"Visible chapter");
    Bytes truncated(ReadFile(dir + "/deflated.epub"));
    truncated.bytes.resize(truncated.bytes.size() - 1);
    assert(engine.Open(truncated, Format::Epub) == Result::Invalid);
    char path[192];
    assert(detail::ResolvePath("OPS/package.opf", "text/../chapter%201.xhtml#id", path, sizeof(path)));
    assert(std::strcmp(path, "OPS/chapter 1.xhtml") == 0);
    assert(!detail::ResolvePath("OPS/package.opf", "../../outside", path, sizeof(path)));
    assert(!detail::ResolvePath("", "https://example.com/book", path, sizeof(path)));
    BookmarkTests();
}
