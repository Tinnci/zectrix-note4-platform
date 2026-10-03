#pragma once

#include <algorithm>
#include <cstddef>

#include "esp_err.h"
#include "layout.h"

class Canvas;
class UiEngine;

namespace note4::ui {

// Declarative page configuration with fluent builder.
struct PageSpec {
    const char* title = "";
    const char* footer = "";
    const char* badge = nullptr;
    bool center_title = false;
    bool portrait_capable = true;

    constexpr PageSpec() = default;

    constexpr PageSpec& Title(const char* t) { title = t; return *this; }
    constexpr PageSpec& Footer(const char* f) { footer = f; return *this; }
    constexpr PageSpec& Badge(const char* b) { badge = b; return *this; }
    constexpr PageSpec& CenterTitle(bool center = true) { center_title = center; return *this; }
    constexpr PageSpec& Portrait(bool capable = true) { portrait_capable = capable; return *this; }
};

using PageConfig = PageSpec;

// Zero-allocation, RAII-safe page shell managing page geometry, header badges,
// scrollbars, and canvas clipping scopes.
class PageShell {
public:
    PageShell(UiEngine& engine, Canvas& canvas, Rect body, const PageSpec& spec);
    ~PageShell();

    PageShell(const PageShell&) = delete;
    PageShell& operator=(const PageShell&) = delete;
    PageShell(PageShell&& other) noexcept;
    // The canvas reference cannot be rebound to another engine's framebuffer.
    PageShell& operator=(PageShell&& other) = delete;

    // Client drawing area (pre-clipped between header and footer)
    const Rect& body() const { return body_; }
    Canvas& canvas() { return canvas_; }
    const Canvas& canvas() const { return canvas_; }

    int width() const;
    int height() const;
    bool portrait() const;

    // Dynamic vertical shift for portrait centering
    int dy() const;
    int center_y(int landscape_y) const;

    // Header badge (e.g. page count "1/3" or progress "42%")
    void DrawBadge(const char* text);

    // Responsive scrollbar with automatic thumb sizing and positioning
    void DrawScrollbar(size_t visible_count, size_t total_count, size_t first_index,
                       int start_y = -1, int track_height = -1);

    // Commit page and trigger auto/full refresh
    esp_err_t Commit(bool full_refresh);

private:
    UiEngine* engine_ = nullptr;
    Canvas& canvas_;
    Rect body_{};
    bool committed_ = false;
};

}  // namespace note4::ui
