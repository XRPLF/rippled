//
// Copyright (C) 2004-2008 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#include "soci/soci-platform.h"
#include "soci/soci-backend.h"
#include "soci-mktime.h"
#include <cstdlib>
#include <ctime>
#include "common.h"

namespace // anonymous
{

// helper function for parsing decimal data (for std::tm)
long parse10(char const * & p1, char * & p2, char const * msg)
{
    long v = std::strtol(p1, &p2, 10);
    if (p2 != p1)
    {
        p1 = p2 + 1;
        return v;
    }
    else
    {
        throw soci::soci_error(msg);
    }
}

} // namespace anonymous


void soci::details::postgresql::parse_std_tm(char const * buf, std::tm & t)
{
    char const * p1 = buf;
    char * p2;
    char separator;
    long a, b, c;
    long year = 1900, month = 1, day = 1;
    long hour = 0, minute = 0, second = 0;

    char const * errMsg = "Cannot convert data to std::tm.";

    a = parse10(p1, p2, errMsg);
    separator = *p2;
    b = parse10(p1, p2, errMsg);
    c = parse10(p1, p2, errMsg);

    if (*p2 == ' ')
    {
        // there are more elements to parse
        // - assume that what was already parsed is a date part
        // and that the remaining elements describe the time of day
        year = a;
        month = b;
        day = c;
        hour   = parse10(p1, p2, errMsg);
        minute = parse10(p1, p2, errMsg);
        second = parse10(p1, p2, errMsg);
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

    details::mktime_from_ymdhms(t, year, month, day, hour, minute, second);
}
