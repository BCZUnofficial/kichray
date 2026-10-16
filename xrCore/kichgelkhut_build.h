#pragma once

// Gregorian calendar arithmetic, independent of timezone and runtime date.
namespace kichgelkhut
{
inline int calendar_day(int year, int month, int day)
{
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned y = static_cast<unsigned>(year - era * 400);
    const unsigned m = static_cast<unsigned>(month + (month > 2 ? -3 : 9));
    const unsigned d = (153 * m + 2) / 5 + day - 1;
    return era * 146097 + static_cast<int>(y * 365 + y / 4 - y / 100 + d);
}

inline unsigned build_number(const char* date)
{
    static const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    int month = 0;
    for (int i = 0; i < 12; ++i)
        if (date[0] == months[i][0] && date[1] == months[i][1] && date[2] == months[i][2])
            month = i + 1;
    if (!month) return 0;
    const int day = (date[4] == ' ' ? 0 : date[4] - '0') * 10 + date[5] - '0';
    const int year = (date[7] - '0') * 1000 + (date[8] - '0') * 100 +
        (date[9] - '0') * 10 + date[10] - '0';
    const int elapsed = calendar_day(year, month, day) - calendar_day(2026, 1, 6);
    return elapsed > 0 ? static_cast<unsigned>(elapsed) : 0;
}
}
