#include "reader_internal.h"

#include <algorithm>
#include <cstring>
#include <new>

namespace zectrix::reader {
namespace {
using detail::Token;
bool Opening(uint32_t cp) {
    constexpr uint32_t chars[] = {'(', '[', '{', 0x2018, 0x201c, 0x3008, 0x300a,
        0x300c, 0x300e, 0x3010, 0x3014, 0xff08, 0xff3b, 0xff5b};
    for (auto c : chars) if (c == cp) return true;
    return false;
}
bool Closing(uint32_t cp) {
    constexpr uint32_t chars[] = {')', ']', '}', ',', '.', ':', ';', '!', '?',
        0x2019, 0x201d, 0x2026, 0x3001, 0x3002, 0x3009, 0x300b, 0x300d,
        0x300f, 0x3011, 0x3015, 0xff01, 0xff09, 0xff0c, 0xff0e, 0xff1a,
        0xff1b, 0xff1f, 0xff3d, 0xff5d};
    for (auto c : chars) if (c == cp) return true;
    return false;
}
bool BreakBetween(uint32_t left, uint32_t right) {
    if (Opening(left) || Closing(right) || left == 0xa0 || right == 0xa0) return false;
    return left == ' ' || right == ' ' || left == '-' || IsCjk(left) || IsCjk(right);
}

std::size_t PreviousScalar(const uint8_t* bytes, std::size_t size, uint32_t* cp) {
    std::size_t first = size - 1;
    while (first && (bytes[first] & 0xc0) == 0x80 && size - first < 4) --first;
    const auto lead = bytes[first];
    const unsigned length = lead < 0x80 ? 1 : lead >= 0xc2 && lead <= 0xdf ? 2 :
        lead >= 0xe0 && lead <= 0xef ? 3 : lead >= 0xf0 && lead <= 0xf4 ? 4 : 0;
    *cp = lead & (length == 1 ? 127 : length == 2 ? 31 : length == 3 ? 15 : 7);
    if (length != size - first) { *cp = 0xfffd; return size - 1; }
    for (std::size_t i = first + 1; i < size; ++i) {
        if ((bytes[i] & 0xc0) != 0x80) { *cp = 0xfffd; return size - 1; }
        *cp = (*cp << 6) | (bytes[i] & 63);
    }
    if ((length > 1 && *cp < (length == 2 ? 0x80U : length == 3 ? 0x800U : 0x10000U)) ||
        *cp > 0x10ffff || (*cp >= 0xd800 && *cp <= 0xdfff)) *cp = 0xfffd;
    return first;
}
}  // namespace

const char* ResultName(Result result) {
    switch (result) {
        case Result::Ok: return "Ready";
        case Result::Pending: return "Loading...";
        case Result::End: return "End of book";
        case Result::Invalid: return "Invalid book data";
        case Result::Unsupported: return "Unsupported book format";
        case Result::TooLarge: return "Book structure too large";
        case Result::IoError: return "Book read failed";
        case Result::NoMemory: return "Not enough memory";
    }
    return "Reader error";
}

bool MemorySource::Read(uint32_t offset, void* output, std::size_t size) {
    if (offset > size_ || size > size_ - offset || (size && (!data_ || !output))) return false;
    if (size) std::memcpy(output, data_ + offset, size);
    return true;
}

struct Engine::Impl {
    detail::Book book;
    detail::Decoder decoder;
    Page page;
    Page work;
    std::array<Token, 64> line{};
    std::array<Position, 64> history{};
    std::size_t history_size = 0;
    std::size_t line_size = 0;
    int line_width = 0;
    int line_height = 0;
    int indent = 0;
    int y = 0;
    uint16_t chapter = 0;
    Position anchor{};
    Position previous_target{};
    enum class Job : uint8_t { Seek, Next, PreviousCached, PreviousScan };
    Job job = Job::Seek;
    bool active = false;
    bool opened = false;
    bool opening = false;
    bool visible = false;
    bool stream_live = false;
    bool chapter_end = false;
    bool seeking = false;
    bool text_context_pending = false;
    uint32_t text_cursor = 0;

    Result TextContext() {
        if (text_cursor) {
            std::array<uint8_t, 4> bytes{};
            const auto size = std::min<uint32_t>(text_cursor, bytes.size());
            if (!book.source->Read(text_cursor - size, bytes.data(), size)) return Result::IoError;
            uint32_t cp = 0;
            text_cursor = text_cursor - size + PreviousScalar(bytes.data(), size, &cp);
            const bool ignored = cp == ' ' || (cp < 0x20 && cp != '\r' && cp != '\n') ||
                cp == 0x7f || cp == 0xfeff || cp == 0xad || cp == 0x200b;
            if (ignored && text_cursor) return Result::Pending;
        }
        text_context_pending = false;
        // Replay just the preceding visible scalar or paragraph break to
        // recover indentation and CRLF state without reading the book prefix.
        return book.Start(0, text_cursor);
    }

    void ResetPage(Position start, FontSize font, bool keep_line) {
        work.count = 0;
        work.start = work.next = start;
        work.font = font;
        work.end = false;
        work.progress_per_mille = 0;
        y = 0;
        if (!keep_line) { line_size = 0; line_width = line_height = indent = 0; }
        else LayoutLine();
    }

    Result Start(Position position, FontSize font, Job kind, bool forward = false) {
        job = kind;
        anchor = position;
        seeking = !forward;
        active = false;
        text_context_pending = false;
        if (!forward) {
            stream_live = false;
            if (book.format == Format::Text && position.offset) {
                text_cursor = position.offset;
                if (text_cursor < book.source->Size()) {
                    std::array<uint8_t, 4> bytes{};
                    const uint32_t begin = text_cursor - std::min<uint32_t>(text_cursor, 3);
                    const auto size = text_cursor - begin + 1;
                    if (!book.source->Read(begin, bytes.data(), size)) return Result::IoError;
                    if ((bytes[size - 1] & 0xc0) == 0x80) {
                        std::size_t i = size - 1;
                        while (i && (bytes[i] & 0xc0) == 0x80) --i;
                        if (bytes[i] >= 0xc2 && bytes[i] <= 0xf4) text_cursor = begin + i;
                    }
                }
                text_context_pending = true;
            } else {
                const auto result = book.Start(position.chapter);
                if (result != Result::Ok) return result;
            }
            chapter = position.chapter;
            decoder.Reset(chapter, book.format);
            chapter_end = false;
        }
        ResetPage(position, font, forward);
        active = stream_live = true;
        return Result::Pending;
    }

    bool PageFull() const { return y + FontHeight(work.font) > Page::kHeight; }

    void MeasureLine() {
        while (line_size && line[0].codepoint == ' ') {
            std::move(line.begin() + 1, line.begin() + line_size, line.begin());
            --line_size;
        }
        indent = line_size && line[0].paragraph ? 2 * FontHeight(work.font) : 0;
        line_width = indent;
        line_height = 0;
        for (std::size_t i = 0; i < line_size; ++i) {
            line_width += GlyphWidth(line[i].codepoint, work.font, line[i].style);
            line_height = std::max(line_height, GlyphHeight(line[i].codepoint, work.font, line[i].style));
        }
    }

    void Flush(std::size_t count) {
        std::size_t printed = count;
        while (printed && line[printed - 1].codepoint == ' ') --printed;
        int x = indent;
        for (std::size_t i = 0; i < printed; ++i) {
            const auto cp = line[i].codepoint;
            if (work.count < work.glyphs.size())
                work.glyphs[work.count++] = {cp, static_cast<uint16_t>(x), static_cast<uint16_t>(y), line[i].style};
            x += GlyphWidth(cp, work.font, line[i].style);
        }
        // Use the spare body height for a wider CJK gap while retaining seven large-font rows.
        if (printed) y += FontHeight(work.font) + (work.font == FontSize::Large ? 8 : 4);
        std::move(line.begin() + count, line.begin() + line_size, line.begin());
        line_size -= count;
        MeasureLine();
    }

    bool Add(const Token& token) {
        work.next = token.after;
        if (token.codepoint == '\n') {
            if (line_size) Flush(line_size);
            return PageFull();
        }
        if (!line_size && token.codepoint == ' ') return false;
        if (!line_size) indent = line_width = token.paragraph ? 2 * FontHeight(work.font) : 0;
        line[line_size++] = token;
        line_width += GlyphWidth(token.codepoint, work.font, token.style);
        line_height = std::max(line_height, GlyphHeight(token.codepoint, work.font, token.style));
        return LayoutLine();
    }

    bool LayoutLine() {
        // A decorated bottom line moves intact to the next page. Its buffered
        // tokens retain styles even when the stream has already closed a tag.
        if (y + line_height > Page::kHeight) {
            work.next = line[0].start;
            return true;
        }
        if (line_width <= Page::kWidth) return false;
        std::size_t split = 0;
        for (std::size_t i = 1; i < line_size; ++i)
            if (BreakBetween(line[i - 1].codepoint, line[i].codepoint)) split = i;
        // An overlong Latin word must still advance by complete Unicode scalars.
        if (!split) split = line_size - 1;
        Flush(split);
        if (PageFull() || y + line_height > Page::kHeight) {
            if (line_size) work.next = line[0].start;
            return true;
        }
        return false;
    }

    void Progress() {
        uint64_t consumed = work.next.offset;
        for (uint16_t i = 0; i < work.next.chapter; ++i) consumed += book.sections[i].entry.size;
        work.progress_per_mille = work.end || !book.total ? 1000 :
            static_cast<uint16_t>(std::min<uint64_t>(999, consumed * 1000 / book.total));
    }

    Result Complete() {
        Progress();
        if (job == Job::PreviousScan && work.next < previous_target && !work.end) {
            const auto next = work.next;
            const auto font = work.font;
            ResetPage(next, font, true);
            return Result::Pending;
        }
        if (job == Job::Next && !work.count && visible && work.end) {
            page.end = true;
            page.next = work.next;
            page.progress_per_mille = 1000;
        } else {
            if (job == Job::Next && visible) {
                if (history_size == history.size()) {
                    std::move(history.begin() + 1, history.end(), history.begin());
                    --history_size;
                }
                history[history_size++] = page.start;
            } else if (job == Job::PreviousCached && history_size) --history_size;
            page = work;
        }
        active = false;
        visible = true;
        return Result::Ok;
    }
};

Engine::Engine() : impl_(new (std::nothrow) Impl) {}
Engine::~Engine() = default;

Result Engine::Open(Source& source, Format format) {
    Close();
    if (!impl_) return Result::NoMemory;
    if (format != Format::Text && format != Format::Epub) return Result::Invalid;
    const auto result = impl_->book.Open(source, format);
    impl_->opened = result == Result::Ok;
    impl_->opening = result == Result::Pending;
    return result;
}

void Engine::Close() {
    if (!impl_) return;
    impl_->active = impl_->visible = impl_->opened = impl_->stream_live = false;
    impl_->opening = false;
    impl_->history_size = 0;
    impl_->book.source = nullptr;
}

Result Engine::Seek(Position position, FontSize font) {
    if (!impl_) return Result::NoMemory;
    if (!ValidPosition(position) || (font != FontSize::Small && font != FontSize::Large)) return Result::Invalid;
    impl_->history_size = 0;
    return impl_->Start(position, font, Impl::Job::Seek);
}

Result Engine::Next() {
    if (!impl_ || !impl_->visible || impl_->active) return Result::Invalid;
    if (impl_->page.end) return Result::End;
    return impl_->Start(impl_->page.next, impl_->page.font, Impl::Job::Next, impl_->stream_live);
}

Result Engine::Previous() {
    if (!impl_ || !impl_->visible || impl_->active) return Result::Invalid;
    auto& state = *impl_;
    if (state.page.start == Position{}) return Result::End;
    if (state.history_size)
        return state.Start(state.history[state.history_size - 1], state.page.font, Impl::Job::PreviousCached);
    state.previous_target = state.page.start;
    const uint16_t chapter = state.page.start.offset ? state.page.start.chapter : 0;
    return state.Start({0, chapter}, state.page.font, Impl::Job::PreviousScan);
}

Result Engine::SetFont(FontSize font) {
    if (!impl_ || !impl_->visible) return Result::Invalid;
    return Seek(impl_->page.start, font);
}

Result Engine::Poll(std::size_t byte_budget) {
    if (!impl_) return Result::NoMemory;
    auto& state = *impl_;
    if (state.opening) {
        const auto result = state.book.PollOpen(byte_budget);
        if (result != Result::Pending) {
            state.opening = false;
            state.opened = result == Result::Ok;
        }
        return result;
    }
    if (!state.active) return Result::Ok;
    while (byte_budget--) {
        if (state.text_context_pending) {
            const auto context = state.TextContext();
            if (context == Result::Pending || context == Result::Ok) continue;
            Cancel();
            return context;
        }
        if (state.chapter_end) {
            if (state.chapter + 1 >= state.book.count) return state.Complete();
            ++state.chapter;
            const auto started = state.book.Start(state.chapter);
            if (started != Result::Ok) { Cancel(); return started; }
            state.decoder.Reset(state.chapter, state.book.format);
            state.chapter_end = false;
            state.seeking = false;
        }
        Token token;
        bool emitted = false;
        const auto result = state.decoder.Step(state.book.stream, &token, &emitted);
        if (result == Result::End) {
            state.chapter_end = true;
            if (state.line_size) state.Flush(state.line_size);
            state.work.end = state.chapter + 1 == state.book.count;
            state.work.next = state.work.end ? Position{state.book.sections[state.chapter].entry.size, state.chapter} :
                Position{0, static_cast<uint16_t>(state.chapter + 1)};
            if (state.work.count || state.work.end) {
                const auto complete = state.Complete();
                if (complete != Result::Pending) return complete;
            }
        } else if (result != Result::Ok) {
            if (result == Result::Pending) return result;
            Cancel();
            return result;
        } else if (emitted) {
            if (state.seeking) {
                if (!(state.anchor < token.after)) continue;
                state.seeking = false;
                if (token.start < state.work.start) state.work.start = token.start;
            }
            if (state.Add(token)) {
                const auto complete = state.Complete();
                if (complete != Result::Pending) return complete;
            }
        }
    }
    return Result::Pending;
}

void Engine::Cancel() { if (impl_) impl_->opening = impl_->active = impl_->stream_live = false; }
bool Engine::busy() const { return impl_ && (impl_->opening || impl_->active); }
bool Engine::has_page() const { return impl_ && impl_->visible; }
const Page& Engine::page() const {
    static const Page empty;
    return impl_ ? impl_->page : empty;
}
uint16_t Engine::chapters() const { return impl_ && impl_->opened ? impl_->book.count : 0; }
bool Engine::ValidPosition(Position position) const {
    return impl_ && impl_->opened && position.chapter < impl_->book.count &&
        position.offset <= impl_->book.sections[position.chapter].entry.size;
}

}  // namespace zectrix::reader
