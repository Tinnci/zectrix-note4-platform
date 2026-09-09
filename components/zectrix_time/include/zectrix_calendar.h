#pragma once

#include <cstdint>

namespace zectrix::time {

struct DateTime {
    int year = 2000;
    int month = 1;
    int day = 1;
    int weekday = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
};

constexpr int32_t kMaximumUtcOffsetSeconds = 14 * 3600;

constexpr bool IsLeapYear(int year) {
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

constexpr int DaysInMonth(int year, int month) {
    constexpr int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return month < 1 || month > 12 ? 0 : days[month - 1] + (month == 2 && IsLeapYear(year));
}

constexpr bool IsValid(const DateTime& value) {
    return value.year >= 2000 && value.year <= 2099 &&
           value.month >= 1 && value.month <= 12 &&
           value.day >= 1 && value.day <= DaysInMonth(value.year, value.month) &&
           value.weekday >= 0 && value.weekday <= 6 &&
           value.hour >= 0 && value.hour <= 23 &&
           value.minute >= 0 && value.minute <= 59 &&
           value.second >= 0 && value.second <= 59;
}

constexpr bool IsValidUtcOffset(int32_t seconds) {
    return seconds >= -kMaximumUtcOffsetSeconds && seconds <= kMaximumUtcOffsetSeconds;
}

// Interpret validated calendar fields as UTC without changing the process TZ.
constexpr int64_t CalendarSeconds(const DateTime& value) {
    int64_t days = 0;
    for (int year = 1970; year < value.year; ++year) days += IsLeapYear(year) ? 366 : 365;
    for (int month = 1; month < value.month; ++month) days += DaysInMonth(value.year, month);
    return (days + value.day - 1) * 86400 + value.hour * 3600 + value.minute * 60 + value.second;
}

}  // namespace zectrix::time
