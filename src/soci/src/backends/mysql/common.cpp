//
// Copyright (C) 2004-2006 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#include "common.h"
#include "soci-backend.h"
#include <ciso646>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace // anonymous
{

// helper function for parsing decimal data (for std::tm)
long parse10(char const *&p1, char *&p2, const char *msg)
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


void soci::details::mysql::parse_std_tm(char const *buf, std::tm &t)
{
    char const *p1 = buf;
    char *p2;
    long year, month, day;
    long hour = 0, minute = 0, second = 0;

    const char *errMsg = "Cannot convert data to std::tm.";

    if (strchr(buf, '-') != NULL)
    {
      year  = parse10(p1, p2, errMsg);
      month = parse10(p1, p2, errMsg);
      day   = parse10(p1, p2, errMsg);
    }
    else
    {
      year  = 2000;
      month = 1;
      day   = 1;
    }


    if (strchr(buf, ':') != NULL)
    {
        // there is also the time of day available
        hour   = parse10(p1, p2, errMsg);
        minute = parse10(p1, p2, errMsg);
        second = parse10(p1, p2, errMsg);
    }

    t.tm_isdst = -1;
    t.tm_year = year - 1900;
    t.tm_mon  = month - 1;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min  = minute;
    t.tm_sec  = second;

    std::mktime(&t);
}

char * soci::details::mysql::quote(MYSQL * conn, const char *s, int len)
{
    char *retv = new char[2 * len + 3];
    retv[0] = '\'';
    int len_esc = mysql_real_escape_string(conn, retv + 1, s, len);
    retv[len_esc + 1] = '\'';
    retv[len_esc + 2] = '\0';

    return retv;
}
