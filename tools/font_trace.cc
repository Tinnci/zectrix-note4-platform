// Host preview instrumentation; deliberately outside ESP-IDF components.
#include "canvas.h"
#if defined(NOTE4_FONT_TRACE) && NOTE4_FONT_TRACE
#include <algorithm>
#include <array>
#include <climits>

namespace note4::ui::font_trace {
namespace {
struct Record {
    const Canvas* owner;
    uint32_t cp;
    unsigned run;
    const char* role;
    const char* source;
    int size, x, y, width, height, left, top, right, bottom;
    bool inverted;
    std::array<uint16_t, 16> source_rows{};
    int source_width = 0;
    unsigned style = 0;
};
std::array<Record, 16384> records{};
size_t count = 0;
unsigned serial = 0;
struct Dropped { const Canvas* owner = nullptr; unsigned count = 0; };
std::array<Dropped, 128> dropped{};
}

unsigned BeginRun() { return ++serial; }

void Glyph(const Canvas& canvas, uint32_t cp, unsigned run, const char* role,
           const char* source, int size, int64_t x, int64_t y, int width,
           int height, bool inverted, const uint16_t* source_rows,
           int source_width, unsigned style) {
    const auto clip = canvas.clip();
    const int64_t left = std::max<int64_t>(x, clip.x), top = std::max<int64_t>(y, clip.y);
    const int64_t right = std::min<int64_t>(x + width, clip.x + clip.width);
    const int64_t bottom = std::min<int64_t>(y + height, clip.y + clip.height);
    if (left >= right || top >= bottom || x < INT_MIN || x > INT_MAX || y < INT_MIN || y > INT_MAX) return;
    if (count == records.size()) {
        for (auto& item : dropped) if (item.owner == &canvas || item.owner == nullptr) {
            item.owner = &canvas;
            ++item.count;
            break;
        }
        return;
    }
    records[count++] = {&canvas, cp, run, role, source, size, static_cast<int>(x), static_cast<int>(y),
        width, height, static_cast<int>(left), static_cast<int>(top), static_cast<int>(right), static_cast<int>(bottom), inverted};
    if (source_rows) {
        auto& record = records[count - 1];
        std::copy_n(source_rows, 16, record.source_rows.begin());
        record.source_width = source_width;
        record.style = style;
    }
}

void Clear(const Canvas& canvas) {
    const auto clip = canvas.clip();
    const bool full = clip.x == 0 && clip.y == 0 && clip.width == canvas.width() && clip.height == canvas.height();
    size_t kept = 0;
    for (size_t i = 0; i < count; ++i) {
        const auto& r = records[i];
        const bool overlaps = r.left < clip.x + clip.width && r.right > clip.x &&
                              r.top < clip.y + clip.height && r.bottom > clip.y;
        // Partial redraw clears whole touched paint events; no stale glyph frequency.
        if (r.owner != &canvas || (!full && !overlaps)) records[kept++] = r;
    }
    count = kept;
    if (full) for (auto& item : dropped) if (item.owner == &canvas) item = {};
}

void Write(const Canvas& canvas, FILE* out, const char* language, int width, int height) {
    unsigned lost = 0;
    for (const auto& item : dropped) if (item.owner == &canvas) lost += item.count;
    std::fprintf(out, "{\"width\":%d,\"height\":%d,\"language\":\"%s\",\"dropped\":%u,\"glyphs\":[",
                 width, height, language, lost);
    bool first = true;
    for (size_t i = 0; i < count; ++i) {
        const auto& r = records[i];
        if (r.owner != &canvas) continue;
        const int left = std::max(0, r.left), top = std::max(0, r.top);
        const int right = std::min(width, r.right), bottom = std::min(height, r.bottom);
        if (left >= right || top >= bottom) continue;
        std::fprintf(out, "%s{\"cp\":%u,\"run\":%u,\"role\":\"%s\",\"source\":\"%s\",\"size\":%d,"
            "\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d,\"clip_left\":%d,\"clip_top\":%d,"
            "\"clip_right\":%d,\"clip_bottom\":%d,\"inverted\":%s", first ? "" : ",", r.cp, r.run,
            r.role, r.source, r.size, r.x, r.y, r.width, r.height, left, top, right, bottom, r.inverted ? "true" : "false");
        if (r.source_width) {
            std::fprintf(out, ",\"source_width\":%d,\"style\":%u,\"source_rows\":[", r.source_width, r.style);
            for (size_t row = 0; row < r.source_rows.size(); ++row)
                std::fprintf(out, "%s%u", row ? "," : "", r.source_rows[row]);
            std::fputc(']', out);
        }
        std::fputc('}', out);
        first = false;
    }
    std::fputs("]}\n", out);
}
}  // namespace note4::ui::font_trace
#endif
