//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <BeastConfig.h>
#include <ripple/beast/clock/chrono_util.h>
#include <ripple/basics/chrono.h>
#include <iomanip>
#include <sstream>
#include <tuple>

namespace ripple {

static
std::tuple<int, unsigned, unsigned>
civil_from_days(int z) noexcept
{
    z += 719468;
    const int era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);          // [0, 146096]
    const unsigned yoe = (doe - doe/1460 + doe/36524 - doe/146096) / 365;  // [0, 399]
    const int y = static_cast<int>(yoe) + era * 400;
    const unsigned doy = doe - (365*yoe + yoe/4 - yoe/100);                // [0, 365]
    const unsigned mp = (5*doy + 2)/153;                                   // [0, 11]
    const unsigned d = doy - (153*mp+2)/5 + 1;                             // [1, 31]
    const unsigned m = mp + (mp < 10 ? 3 : -9);                            // [1, 12]
    return std::tuple<int, unsigned, unsigned>(y + (m <= 2), m, d);
}

std::string
to_string(std::chrono::system_clock::time_point tp)
{
    const char* months[] =
        {
            "Jan", "Feb", "Mar", "Apr", "May", "Jun",
            "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
        };
    using namespace std::chrono;
    auto s = floor<seconds>(tp.time_since_epoch());
    auto sd = floor<days>(s);  // number of days
    s -= sd;  // time of day in seconds
    auto h = floor<hours>(s);
    s -= h;
    auto m = floor<minutes>(s);
    s -= m;
    int y;
    unsigned mn, d;
    std::tie(y, mn, d) = civil_from_days(static_cast<int>(sd.count()));
    // Date-time in y/mn/d h:m:s
    std::ostringstream str;
    str.fill('0');
    str.flags(std::ios::dec | std::ios::right);
    using std::setw;
    str << y << '-' << months[mn-1] << '-' << setw(2) << d << ' '
        << setw(2) << h.count() << ':'
        << setw(2) << m.count() << ':'
        << setw(2) << s.count();
    return str.str();
}

std::string
to_string(NetClock::time_point tp)
{
    using namespace std::chrono;
    return to_string(system_clock::time_point{tp.time_since_epoch() + 946684800s});
}

} // ripple
