#include "reader_internal.h"

#include <algorithm>
#include <cstring>

namespace zectrix::reader::detail {
namespace {
bool Space(uint32_t c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
int Hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
bool Scalar(uint32_t c) { return c && c <= 0x10ffff && !(c >= 0xd800 && c <= 0xdfff); }

uint32_t Entity(const char* name) {
    struct Named { const char* name; uint32_t value; };
    constexpr Named names[] = {
        {"amp", '&'}, {"lt", '<'}, {"gt", '>'}, {"quot", '"'}, {"apos", '\''},
        {"nbsp", 0xa0}, {"ensp", 0x2002}, {"emsp", 0x2003}, {"thinsp", 0x2009},
        {"ndash", 0x2013}, {"mdash", 0x2014}, {"lsquo", 0x2018}, {"rsquo", 0x2019},
        {"ldquo", 0x201c}, {"rdquo", 0x201d}, {"hellip", 0x2026}, {"copy", 0xa9},
        {"reg", 0xae}, {"bull", 0x2022}, {"middot", 0xb7}, {"shy", 0xad},
    };
    for (const auto& item : names) if (std::strcmp(name, item.name) == 0) return item.value;
    if (name[0] != '#') return 0xfffd;
    const bool hex = name[1] == 'x' || name[1] == 'X';
    const char* p = name + (hex ? 2 : 1);
    uint32_t value = 0;
    if (!*p) return 0xfffd;
    for (; *p; ++p) {
        const int digit = hex ? Hex(*p) : (*p >= '0' && *p <= '9' ? *p - '0' : -1);
        if (digit < 0 || value > (0x10ffffU - digit) / (hex ? 16 : 10)) return 0xfffd;
        value = value * (hex ? 16 : 10) + digit;
    }
    return Scalar(value) ? value : 0xfffd;
}

bool AppendUtf8(uint32_t cp, char* output, std::size_t capacity, std::size_t* used) {
    const unsigned count = cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;
    if (*used + count >= capacity) return false;
    if (count == 1) output[(*used)++] = static_cast<char>(cp);
    else {
        output[(*used)++] = static_cast<char>((count == 2 ? 0xc0 : count == 3 ? 0xe0 : 0xf0) |
                                             (cp >> (6 * (count - 1))));
        for (unsigned i = count - 1; i > 0; --i)
            output[(*used)++] = static_cast<char>(0x80 | ((cp >> (6 * (i - 1))) & 63));
    }
    output[*used] = 0;
    return true;
}

bool Block(const char* tag) {
    constexpr const char* names[] = {"p", "div", "br", "hr", "li", "ul", "ol", "blockquote",
        "h1", "h2", "h3", "h4", "h5", "h6", "section", "article", "tr", "pre", "dt", "dd"};
    for (const auto* name : names) if (TagIs(tag, name) || TagIs(tag, name, true)) return true;
    return false;
}
}  // namespace

bool Xml::complete() const { return state_ == State::Text; }

Result Xml::Feed(uint8_t c, uint32_t offset, XmlEvent* event) {
    *event = {};
    if (state_ == State::Text) {
        if (c == '<' || c == '&') {
            state_ = c == '<' ? State::Tag : State::Entity;
            size_ = 0;
            start_ = offset;
            quote_ = 0;
        } else *event = {XmlEvent::Kind::Byte, c, offset};
        return Result::Ok;
    }
    if (state_ == State::Comment) {
        if (c == '>' && dashes_ >= 2) { state_ = State::Text; dashes_ = 0; }
        else dashes_ = c == '-' ? std::min<unsigned>(2, dashes_ + 1) : 0;
        return Result::Ok;
    }
    if (state_ == State::Declaration) {
        if (quote_) { if (c == quote_) quote_ = 0; }
        else if (c == '\'' || c == '"') quote_ = c;
        else if (c == '[') { if (++brackets_ > 8) return Result::Invalid; }
        else if (c == ']' && brackets_) --brackets_;
        else if (c == '>' && !brackets_) state_ = State::Text;
        return Result::Ok;
    }
    if (state_ == State::Entity) {
        if (c == ';') {
            buffer_[size_] = 0;
            *event = {XmlEvent::Kind::Scalar, Entity(buffer_.data()), start_};
            state_ = State::Text;
            return Result::Ok;
        }
        if (size_ == 24 || Space(c) || c == '<' || c == '&') return Result::Invalid;
    } else {
        if (quote_) { if (c == quote_) quote_ = 0; }
        else if (c == '\'' || c == '"') quote_ = c;
        else if (c == '>') {
            buffer_[size_] = 0;
            state_ = State::Text;
            if (size_ && buffer_[0] != '?' && buffer_[0] != '!')
                *event = {XmlEvent::Kind::Tag, 0, start_};
            return Result::Ok;
        }
    }
    if (size_ + 1 == buffer_.size()) return Result::TooLarge;
    buffer_[size_++] = static_cast<char>(c);
    if (state_ == State::Tag && size_ == 3 && std::memcmp(buffer_.data(), "!--", 3) == 0) {
        state_ = State::Comment;
        dashes_ = 0;
    } else if (state_ == State::Tag && size_ == 8 &&
               std::memcmp(buffer_.data(), "![CDATA[", 8) == 0) {
        return Result::Unsupported;
    } else if (state_ == State::Tag && size_ == 8 && buffer_[0] == '!') {
        state_ = State::Declaration;
        brackets_ = 0;
        quote_ = 0;
    }
    return Result::Ok;
}

bool TagIs(const char* tag, const char* name, bool closing) {
    if (!tag || !name) return false;
    if (closing) { if (*tag++ != '/') return false; }
    else if (*tag == '/') return false;
    const char* end = tag;
    for (; *end && !Space(*end) && *end != '/'; ++end) if (*end == ':') tag = end + 1;
    return static_cast<std::size_t>(end - tag) == std::strlen(name) &&
        std::memcmp(tag, name, end - tag) == 0;
}

bool Attribute(const char* tag, const char* name, char* output, std::size_t capacity) {
    if (!tag || !output || !capacity) return false;
    output[0] = 0;
    const char* p = tag;
    while (*p && !Space(*p)) ++p;
    bool found = false;
    while (*p) {
        while (Space(*p)) ++p;
        if (!*p || *p == '/') break;
        const char* key = p;
        while (*p && !Space(*p) && *p != '=') ++p;
        const auto length = p - key;
        while (Space(*p)) ++p;
        if (*p++ != '=') return false;
        while (Space(*p)) ++p;
        const char quote = *p++;
        if (quote != '\'' && quote != '"') return false;
        const bool match = static_cast<std::size_t>(length) == std::strlen(name) &&
                           std::memcmp(key, name, length) == 0;
        if (match && found) return false;
        std::size_t used = 0;
        while (*p && *p != quote) {
            if (match && *p == '&') {
                char entity[25]{};
                std::size_t size = 0;
                ++p;
                while (*p && *p != ';' && size + 1 < sizeof(entity)) entity[size++] = *p++;
                if (*p++ != ';' || !AppendUtf8(Entity(entity), output, capacity, &used)) return false;
            } else {
                if (match) {
                    if (used + 1 >= capacity) return false;
                    output[used++] = *p;
                    output[used] = 0;
                }
                ++p;
            }
        }
        if (!*p) return false;
        ++p;
        found |= match;
    }
    return found;
}

bool ResolvePath(const char* base, const char* href, char* output, std::size_t capacity) {
    if (!base || !href || !*href || !output || capacity == 0 || *href == '/') return false;
    char decoded[kPathCapacity]{};
    std::size_t size = 0;
    const char* slash = std::strrchr(base, '/');
    if (slash) {
        size = slash + 1 - base;
        if (size >= sizeof(decoded)) return false;
        std::memcpy(decoded, base, size);
    }
    for (const char* p = href; *p && *p != '#'; ++p) {
        unsigned char c = *p;
        if (c == '%') {
            if (!p[1] || !p[2] || Hex(p[1]) < 0 || Hex(p[2]) < 0) return false;
            c = static_cast<unsigned char>(Hex(p[1]) * 16 + Hex(p[2]));
            p += 2;
        }
        if (c < 0x20 || c == '\\' || c == ':' || c == '?' || size + 1 >= sizeof(decoded)) return false;
        decoded[size++] = c;
    }
    if (!size || decoded[0] == '/') return false;
    std::size_t used = 0;
    for (std::size_t start = 0; start < size;) {
        std::size_t end = start;
        while (end < size && decoded[end] != '/') ++end;
        const auto length = end - start;
        if (length == 2 && decoded[start] == '.' && decoded[start + 1] == '.') {
            if (!used) return false;
            while (used && output[--used] != '/') {}
        } else if (length && !(length == 1 && decoded[start] == '.')) {
            if (used) { if (used + 1 >= capacity) return false; output[used++] = '/'; }
            if (used + length >= capacity) return false;
            std::memcpy(output + used, decoded + start, length);
            used += length;
        }
        start = end + 1;
    }
    output[used] = 0;
    return used != 0;
}

void Decoder::Reset(uint16_t chapter, Format format) {
    *this = Decoder{};
    chapter_ = chapter;
    format_ = format;
}

bool Decoder::Emit(uint32_t cp, uint32_t start, uint32_t after, Token* token) {
    if (cp == 0xfeff || cp == 0xad || cp == 0x200b) return false;
    if (cp == '\r') { cp = '\n'; cr_ = true; }
    else { const bool skip = cr_ && cp == '\n'; cr_ = false; if (skip) return false; }
    if (format_ == Format::Epub && Space(cp)) cp = ' ';
    if (cp == '\n') {
        const bool emit = !paragraph_;
        paragraph_ = space_ = true;
        if (!emit) return false;
    } else if (Space(cp)) {
        cp = ' ';
        if (space_) return false;
        space_ = true;
    } else if (cp < 0x20 || cp == 0x7f) return false;
    *token = {cp, {start, chapter_}, {after, chapter_}, paragraph_};
    if (cp != '\n' && cp != ' ') { paragraph_ = false; space_ = false; }
    return true;
}

Result Decoder::Step(Stream& stream, Token* token, bool* emitted) {
    *emitted = false;
    XmlEvent event;
    uint32_t after = 0;
    if (pending_) {
        event = pending_event_;
        after = pending_after_;
        pending_ = false;
    } else {
        uint8_t byte;
        const auto offset = stream.offset();
        const auto read = stream.Byte(&byte);
        if (read == Result::End) {
            if (utf8_remaining_) {
                utf8_remaining_ = 0;
                *emitted = Emit(0xfffd, utf8_start_, offset, token);
                return Result::Ok;
            }
            if (format_ == Format::Epub && (!xml_.complete() || hidden_depth_)) return Result::Invalid;
            return Result::End;
        }
        if (read != Result::Ok) return read;
        after = stream.offset();
        event = {XmlEvent::Kind::Byte, byte, offset};
        if (format_ == Format::Epub) {
            const auto parsed = xml_.Feed(byte, offset, &event);
            if (parsed != Result::Ok) return parsed;
        }
    }
    if (event.kind == XmlEvent::Kind::None) return Result::Ok;
    if (event.kind == XmlEvent::Kind::Tag) {
        const char* tag = xml_.tag();
        if (hidden_depth_) {
            if (TagIs(tag, hidden_.data(), true)) --hidden_depth_;
            else if (TagIs(tag, hidden_.data()) && ++hidden_depth_ > 32) return Result::Invalid;
            return Result::Ok;
        }
        constexpr const char* hidden[] = {"head", "script", "style", "svg", "rt"};
        for (const auto* name : hidden) {
            if (TagIs(tag, name)) {
                const auto length = std::strlen(tag);
                if (length && tag[length - 1] != '/') {
                    std::strcpy(hidden_.data(), name);
                    hidden_depth_ = 1;
                }
                return Result::Ok;
            }
        }
        if (Block(tag)) {
            if (!paragraph_) {
                *token = {'\n', {event.start, chapter_}, {after, chapter_}, true};
                *emitted = true;
            }
            paragraph_ = space_ = true;
        } else if (TagIs(tag, "td", true) || TagIs(tag, "th", true)) {
            *emitted = Emit(' ', event.start, after, token);
        }
        return Result::Ok;
    }
    if (hidden_depth_) return Result::Ok;
    uint32_t cp = event.value;
    if (utf8_remaining_) {
        if (event.kind == XmlEvent::Kind::Byte && cp >= 0x80 && cp <= 0xbf) {
            utf8_value_ = (utf8_value_ << 6) | (cp & 63);
            if (--utf8_remaining_) return Result::Ok;
            cp = Scalar(utf8_value_) && utf8_value_ >= utf8_min_ ? utf8_value_ : 0xfffd;
            event.start = utf8_start_;
        } else {
            pending_ = true;
            pending_event_ = event;
            pending_after_ = after;
            utf8_remaining_ = 0;
            *emitted = Emit(0xfffd, utf8_start_, event.start, token);
            return Result::Ok;
        }
    } else if (event.kind == XmlEvent::Kind::Byte && cp >= 0x80) {
        if (cp >= 0xc2 && cp <= 0xf4) {
            utf8_remaining_ = cp < 0xe0 ? 1 : cp < 0xf0 ? 2 : 3;
            utf8_min_ = cp < 0xe0 ? 0x80 : cp < 0xf0 ? 0x800 : 0x10000;
            utf8_value_ = cp & (cp < 0xe0 ? 31 : cp < 0xf0 ? 15 : 7);
            utf8_start_ = event.start;
            return Result::Ok;
        }
        cp = 0xfffd;
    }
    *emitted = Emit(cp, event.start, after, token);
    return Result::Ok;
}

}  // namespace zectrix::reader::detail
