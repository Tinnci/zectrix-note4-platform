#pragma once

#include <algorithm>
#include <cstdint>
#include <utility>

namespace zectrix::ui {

struct Insets {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;

    constexpr Insets() = default;
    constexpr explicit Insets(int all) : left(all), top(all), right(all), bottom(all) {}
    constexpr Insets(int horizontal, int vertical)
        : left(horizontal), top(vertical), right(horizontal), bottom(vertical) {}
    constexpr Insets(int l, int t, int r, int b) : left(l), top(t), right(r), bottom(b) {}
};

// Zero-allocation value type for geometric layout boxes and cutting algebra.
struct Rect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    constexpr Rect() = default;
    constexpr Rect(int x, int y, int width, int height)
        : x(x), y(y), width(width), height(height) {}

    constexpr bool IsEmpty() const { return width <= 0 || height <= 0; }
    constexpr int right() const { return x + width; }
    constexpr int bottom() const { return y + height; }
    constexpr int center_x() const { return x + width / 2; }
    constexpr int center_y() const { return y + height / 2; }

    constexpr Rect Inset(const Insets& insets) const {
        const int new_w = std::max(0, width - insets.left - insets.right);
        const int new_h = std::max(0, height - insets.top - insets.bottom);
        return {x + insets.left, y + insets.top, new_w, new_h};
    }

    constexpr Rect Inset(int pad) const {
        return Inset(Insets(pad));
    }

    constexpr Rect Inset(int horizontal, int vertical) const {
        return Inset(Insets(horizontal, vertical));
    }

    // Cuts clamp sizes and gaps to the available extent; negative gaps never overlap.
    // Cut from top: returns {top_box, remaining_box}
    constexpr std::pair<Rect, Rect> CutTop(int h, int gap = 0) const {
        const int extent = std::max(0, height);
        const int actual_h = std::clamp(h, 0, extent);
        const int spacing = std::clamp(gap, 0, extent - actual_h);
        const int rem_y = y + actual_h + spacing;
        const int rem_h = extent - actual_h - spacing;
        return {{x, y, width, actual_h}, {x, rem_y, width, rem_h}};
    }

    // Cut from bottom: returns {bottom_box, remaining_box}
    constexpr std::pair<Rect, Rect> CutBottom(int h, int gap = 0) const {
        const int extent = std::max(0, height);
        const int actual_h = std::clamp(h, 0, extent);
        const int bottom_y = y + extent - actual_h;
        const int rem_h = extent - actual_h - std::clamp(gap, 0, extent - actual_h);
        return {{x, bottom_y, width, actual_h}, {x, y, width, rem_h}};
    }

    // Cut from left: returns {left_box, remaining_box}
    constexpr std::pair<Rect, Rect> CutLeft(int w, int gap = 0) const {
        const int extent = std::max(0, width);
        const int actual_w = std::clamp(w, 0, extent);
        const int spacing = std::clamp(gap, 0, extent - actual_w);
        const int rem_x = x + actual_w + spacing;
        const int rem_w = extent - actual_w - spacing;
        return {{x, y, actual_w, height}, {rem_x, y, rem_w, height}};
    }

    // Cut from right: returns {right_box, remaining_box}
    constexpr std::pair<Rect, Rect> CutRight(int w, int gap = 0) const {
        const int extent = std::max(0, width);
        const int actual_w = std::clamp(w, 0, extent);
        const int right_x = x + extent - actual_w;
        const int rem_w = extent - actual_w - std::clamp(gap, 0, extent - actual_w);
        return {{right_x, y, actual_w, height}, {x, y, rem_w, height}};
    }
};

// Container-query-aware grid layout with deterministic zero-allocation cells.
class UniformGrid {
public:
    constexpr UniformGrid() = default;
    constexpr UniformGrid(Rect bounds, int cols, int rows, int gap_x = 0, int gap_y = 0)
        : bounds_(bounds.x, bounds.y, std::max(0, bounds.width), std::max(0, bounds.height)),
          cols_(std::max(1, cols)), rows_(std::max(1, rows)),
          gap_x_(AxisGap(bounds_.width, cols_, gap_x)),
          gap_y_(AxisGap(bounds_.height, rows_, gap_y)) {}

    // Responsive container query: picks columns based on available container width
    static UniformGrid Fit(Rect bounds, int item_count, int min_cell_width, int gap_x = 0, int gap_y = 0) {
        if (item_count <= 0) return UniformGrid(bounds, 1, 1, gap_x, gap_y);
        const int64_t gap = std::max(0, gap_x);
        const int64_t preferred_width = std::max(1, min_cell_width);
        const int cols = static_cast<int>(std::clamp<int64_t>(
            (std::max(0, bounds.width) + gap) / (preferred_width + gap), 1, item_count));
        const int rows = 1 + (item_count - 1) / cols;
        return UniformGrid(bounds, cols, rows, gap_x, gap_y);
    }

    constexpr int columns() const { return cols_; }
    constexpr int rows() const { return rows_; }
    constexpr int cell_width() const {
        return (bounds_.width - (cols_ - 1) * gap_x_) / cols_;
    }
    constexpr int cell_height() const {
        return (bounds_.height - (rows_ - 1) * gap_y_) / rows_;
    }

    // Out-of-range cells are empty rather than extending outside the container.
    constexpr Rect CellAt(int col, int row) const {
        if (col < 0 || col >= cols_ || row < 0 || row >= rows_) return Rect();
        const int cw = cell_width();
        const int ch = cell_height();
        const int cx = bounds_.x + col * (cw + gap_x_);
        const int cy = bounds_.y + row * (ch + gap_y_);
        return {cx, cy, cw, ch};
    }

    constexpr Rect CellAt(int index) const {
        if (index < 0) return Rect();
        return CellAt(index % cols_, index / cols_);
    }

    constexpr Rect Cell(int index) const { return CellAt(index); }
    constexpr Rect Cell(int col, int row) const { return CellAt(col, row); }

private:
    // Keep spacing inside the container even when it cannot fit the requested grid.
    static constexpr int AxisGap(int extent, int count, int gap) {
        return count > 1 ? std::min(std::max(0, gap), extent / (count - 1)) : 0;
    }
    Rect bounds_{};
    int cols_ = 1;
    int rows_ = 1;
    int gap_x_ = 0;
    int gap_y_ = 0;
};

}  // namespace zectrix::ui
