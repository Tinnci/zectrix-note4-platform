#ifndef UI_CANVAS_H_
#define UI_CANVAS_H_

#include <array>
#include <cstddef>
#include <cstdint>

#include "note4/sdk/text_style.h"
#include "note4_digit_style.h"
#include "layout.h"

class Canvas {
public:
    using TextStyle = note4::sdk::TextStyle;
    enum class UiFace : uint8_t { Caption, Navigation, Selected, Heading, Compact, Label, Micro };
    using Clip = note4::ui::Rect;
    static constexpr int kWidth = 400;
    static constexpr int kHeight = 300;
    static constexpr int kStride = kWidth / 8;
    static constexpr int kFrameBytes = kStride * kHeight;
    static constexpr int kLargeNumberWidth = 90;
    static constexpr int kLargeNumberHeight = 48;

    void SetClip(Clip clip);
    // Portrait frames use tightly packed scanlines (no per-row padding).
    void SetPortrait(bool portrait) { portrait_ = portrait; ResetClip(); }
    bool portrait() const { return portrait_; }
    int width() const { return portrait_ ? kHeight : kWidth; }
    int height() const { return portrait_ ? kWidth : kHeight; }
    void ResetClip() { clip_ = {0, 0, width(), height()}; }
    Clip clip() const { return clip_; }

    void Clear(bool white = true);
    void Pixel(int x, int y, bool black);
    void FillRect(int x, int y, int width, int height, bool black);
    void Rect(int x, int y, int width, int height, bool black = true);
    void Line(int x0, int y0, int x1, int y1, bool black = true);
    void Text(int x, int y, const char* text, int scale = 1,
              bool inverted = false, TextStyle style = TextStyle::Regular);
    void TextCentered(int y, const char* text, int scale = 1,
                      bool inverted = false, TextStyle style = TextStyle::Regular);
    void SetDigitStyle(note4::ui::DigitStyle style) { digit_style_ = note4::ui::NormalizeDigitStyle(static_cast<unsigned>(style)); }
    note4::ui::DigitStyle digit_style() const { return digit_style_; }
    int LargeNumberWidth(unsigned value) const;
    // Two proportional native-size digits (00–99); no runtime font scaling.
    void LargeNumber(int x, int y, unsigned value, bool inverted = false);
    int TextWidth(const char* text, int scale = 1, TextStyle style = TextStyle::Regular) const;
    int TextHeight(const char* text, int scale = 1, TextStyle style = TextStyle::Regular) const;
    void TextFitted(int x, int y, const char* text, int max_width,
                    bool inverted = false, TextStyle style = TextStyle::Regular);
    int UiTextWidth(const char* text, UiFace face) const;
    static int UiTextHeight(UiFace face);
    void UiText(int x, int y, const char* text, int max_width, UiFace face,
                bool inverted = false);

    uint8_t* data() { return pixels_.data(); }
    const uint8_t* data() const { return pixels_.data(); }
    size_t size() const { return pixels_.size(); }

private:
    note4::ui::DigitStyle digit_style_ = note4::ui::DigitStyle::Serif;
    std::array<uint8_t, kFrameBytes> pixels_ = {};
    Clip clip_{0, 0, kWidth, kHeight};
    bool portrait_ = false;
};


#endif  // UI_CANVAS_H_
