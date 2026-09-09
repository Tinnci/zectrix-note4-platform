#pragma once

#include <algorithm>
#include <cstddef>

#include "zectrix_calendar.h"

namespace zectrix::app {

// The draft never changes wall time until the user reaches Save and confirms.
class ClockEditor {
public:
    enum Field : std::size_t { Year, Month, Day, Hour, Minute, UtcOffset, Save, Count };

    void Begin(const time::DateTime& value, int32_t offset_seconds) {
        value_ = time::IsValid(value) ? value : time::DateTime{};
        value_.second = 0;
        offset_seconds_ = time::IsValidUtcOffset(offset_seconds) ? offset_seconds : 0;
        field_ = Year;
    }
    void Next() { field_ = static_cast<Field>((field_ + 1) % Count); }
    void Adjust(int direction) {
        if (field_ == Save) { field_ = direction > 0 ? UtcOffset : Year; return; }
        switch (field_) {
            case Year: value_.year = Wrap(value_.year + direction, 2000, 2099); break;
            case Month: value_.month = Wrap(value_.month + direction, 1, 12); break;
            case Day: value_.day = Wrap(value_.day + direction, 1, time::DaysInMonth(value_.year, value_.month)); break;
            case Hour: value_.hour = Wrap(value_.hour + direction, 0, 23); break;
            case Minute: value_.minute = Wrap(value_.minute + direction, 0, 59); break;
            case UtcOffset:
                offset_seconds_ = Wrap(offset_seconds_ / 900 + direction, -56, 56) * 900;
                break;
            default: break;
        }
        value_.day = std::min(value_.day, time::DaysInMonth(value_.year, value_.month));
    }
    Field field() const { return field_; }
    const time::DateTime& value() const { return value_; }
    int32_t offset_seconds() const { return offset_seconds_; }

private:
    static int Wrap(int value, int first, int last) {
        return value < first ? last : value > last ? first : value;
    }
    time::DateTime value_{};
    int32_t offset_seconds_ = 0;
    Field field_ = Year;
};

}  // namespace zectrix::app
