//
// Copyright (C) 2004-2008 Maciej Sobczak, Stephen Hutton
// Copyright (C) 2017 Vadim Zeitlin.
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#define SOCI_SOURCE
#include "soci/error.h"
#include "soci-mktime.h"
#include <climits>
#include <cstdlib>
#include <ctime>

namespace // anonymous
{

// helper function for parsing decimal data (for std::tm)
int parse10(char const * & p1, char * & p2)
{
    long v = std::strtol(p1, &p2, 10);
    if (p2 != p1)
    {
        if (v < 0)
            throw soci::soci_error("Negative date/time field component.");

        if (v > INT_MAX)
            throw soci::soci_error("Out of range date/time field component.");

        p1 = p2 + 1;

        // Cast is safe due to check above.
        return static_cast<int>(v);
    }
    else
    {
        throw soci::soci_error("Cannot parse date/time field component.");

    }
}

} // namespace anonymous

void soci::details::parse_std_tm(char const * buf, std::tm & t)
{
    char const * p1 = buf;
    char * p2;
    char separator;
    int a, b, c;
    int year = 1900, month = 1, day = 1;
    int hour = 0, minute = 0, second = 0;

    a = parse10(p1, p2);
    separator = *p2;
    b = parse10(p1, p2);
    c = parse10(p1, p2);

    if (*p2 == ' ')
    {
        // there are more elements to parse
        // - assume that what was already parsed is a date part
        // and that the remaining elements describe the time of day
        year = a;
        month = b;
        day = c;
        hour   = parse10(p1, p2);
        minute = parse10(p1, p2);
        second = parse10(p1, p2);
    }
    else
    {
        // only three values have been parsed
        if (separator == '-')
        {
            // assume the date value was read
            // (leave the time of day as 00:00:00)
            year = a;
            month = b;
            day = c;
        }
        else
        {
            // assume the time of day was read
            // (leave the date part as 1900-01-01)
            hour = a;
            minute = b;
            second = c;
        }
    }

    mktime_from_ymdhms(t, year, month, day, hour, minute, second);
}
