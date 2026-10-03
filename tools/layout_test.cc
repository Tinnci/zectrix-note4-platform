#include "layout.h"
#include "page_shell.h"
#include <cassert>
#include <cstdio>

using zectrix::ui::Insets;
using zectrix::ui::Rect;
using zectrix::ui::UniformGrid;

void TestRectBasics() {
    Rect r(10, 20, 100, 200);
    assert(!r.IsEmpty());
    assert(r.right() == 110);
    assert(r.bottom() == 220);
    assert(r.center_x() == 60);
    assert(r.center_y() == 120);

    Rect empty;
    assert(empty.IsEmpty());
}

void TestInsetsAndCutting() {
    Rect r(0, 0, 400, 300);

    Rect inset = r.Inset(10);
    assert(inset.x == 10 && inset.y == 10 && inset.width == 380 && inset.height == 280);

    Rect inset_hv = r.Inset(20, 30);
    assert(inset_hv.x == 20 && inset_hv.y == 30 && inset_hv.width == 360 && inset_hv.height == 240);

    // CutTop
    auto [top, rem_top] = r.CutTop(50, 10);
    assert(top.x == 0 && top.y == 0 && top.width == 400 && top.height == 50);
    assert(rem_top.x == 0 && rem_top.y == 60 && rem_top.width == 400 && rem_top.height == 240);

    // CutBottom
    auto [bottom, rem_bot] = r.CutBottom(40, 5);
    assert(bottom.x == 0 && bottom.y == 260 && bottom.width == 400 && bottom.height == 40);
    assert(rem_bot.x == 0 && rem_bot.y == 0 && rem_bot.width == 400 && rem_bot.height == 255);

    // CutLeft
    auto [left, rem_left] = r.CutLeft(80, 15);
    assert(left.x == 0 && left.y == 0 && left.width == 80 && left.height == 300);
    assert(rem_left.x == 95 && rem_left.y == 0 && rem_left.width == 305 && rem_left.height == 300);

    // CutRight
    auto [right, rem_right] = r.CutRight(70, 10);
    assert(right.x == 330 && right.y == 0 && right.width == 70 && right.height == 300);
    assert(rem_right.x == 0 && rem_right.y == 0 && rem_right.width == 320 && rem_right.height == 300);

    // Cut more than available
    auto [over_top, over_rem] = r.CutTop(500);
    assert(over_top.height == 300);
    assert(over_rem.height == 0);
}

void TestUniformGrid() {
    // Landscape container: 384 px wide, accommodates 2 columns with preferred 160px
    Rect landscape_area(12, 138, 384, 126);
    auto grid_land = UniformGrid::Fit(landscape_area, 6, /*min_cell_width=*/160, /*gap_x=*/8, /*gap_y=*/6);
    assert(grid_land.columns() == 2);
    assert(grid_land.rows() == 3);
    assert(grid_land.cell_width() == (384 - 8) / 2); // 188
    assert(grid_land.cell_height() == (126 - 12) / 3); // 38
    auto c0 = grid_land.CellAt(0);
    assert(c0.x == 12 && c0.y == 138);
    auto c1 = grid_land.CellAt(1);
    assert(c1.x == 12 + 188 + 8 && c1.y == 138);
    auto c2 = grid_land.CellAt(2);
    assert(c2.x == 12 && c2.y == 138 + 38 + 6);

    // Portrait container: 276 px wide, drops to 1 column with preferred 160px
    Rect portrait_area(12, 170, 276, 180);
    auto grid_port = UniformGrid::Fit(portrait_area, 6, /*min_cell_width=*/160, /*gap_x=*/8, /*gap_y=*/4);
    assert(grid_port.columns() == 1);
    assert(grid_port.rows() == 6);
    assert(grid_port.cell_width() == 276);
    assert(grid_port.cell_height() == (180 - 20) / 6);
    auto p0 = grid_port.CellAt(0);
    assert(p0.x == 12 && p0.y == 170 && p0.width == 276);
}

void TestPageSpec() {
    using zectrix::ui::PageSpec;
    PageSpec spec = PageSpec()
        .Title("Test Title")
        .CenterTitle()
        .Footer("Test Footer")
        .Badge("1/3")
        .Portrait(true);
    assert(spec.title != nullptr);
    assert(spec.center_title == true);
    assert(spec.footer != nullptr);
    assert(spec.badge != nullptr);
    assert(spec.portrait_capable == true);
}

int main() {
    TestRectBasics();
    TestInsetsAndCutting();
    TestUniformGrid();
    TestPageSpec();
    std::printf("PASS: zero-allocation layout algebra, responsive grid and page spec tests.\n");
    return 0;
}
