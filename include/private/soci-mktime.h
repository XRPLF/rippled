//
// Copyright (C) 2015 Vadim Zeitlin.
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef SOCI_PRIVATE_SOCI_MKTIME_H_INCLUDED
#define SOCI_PRIVATE_SOCI_MKTIME_H_INCLUDED

// Not <ctime> because we also want to get timegm() if available.
#include <time.h>

namespace soci
{

namespace details
{

// Fill the provided struct tm with the values corresponding to the given date
// in UTC.
//
// Notice that both years and months are normal human 1-based values here and
// not 1900 or 0-based as in struct tm itself.
inline
void
mktime_from_ymdhms(tm& t,
                   int year, int month, int day,
                   int hour, int minute, int second)
{
    t.tm_isdst = -1;
    t.tm_year = year - 1900;
    t.tm_mon  = month - 1;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min  = minute;
    t.tm_sec  = second;

    mktime(&t);
}

// Helper function for parsing datetime values.
//
// Throws if the string in buf couldn't be parsed as a date or a time string.
SOCI_DECL void parse_std_tm(char const *buf, std::tm &t);

} // namespace details

} // namespace soci

#endif // SOCI_PRIVATE_SOCI_MKTIME_H_INCLUDED
