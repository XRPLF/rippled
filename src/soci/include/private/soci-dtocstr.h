//
// Copyright (C) 2014 Vadim Zeitlin.
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef SOCI_PRIVATE_SOCI_DTOCSTR_H_INCLUDED
#define SOCI_PRIVATE_SOCI_DTOCSTR_H_INCLUDED

#include "soci/soci-platform.h"
#include "soci/error.h"

#include <stdlib.h>
#include <stdio.h>

namespace soci
{

namespace details
{

// Locale-independent, i.e. always using "C" locale, function for converting
// floating point number to string.
//
// The resulting string will contain the floating point number in "C" locale,
// i.e. will always use point as decimal separator independently of the current
// locale.
inline
std::string double_to_cstring(double d)
{
    // See comments in cstring_to_double() in soci-cstrtod.h, we're dealing
    // with the same issues here.

    static size_t const bufSize = 32;
    char buf[bufSize];
    snprintf(buf, bufSize, "%.20g", d);

    // Replace any commas which can be used as decimal separator with points.
    for (char* p = buf; *p != '\0'; p++ )
    {
        if (*p == ',')
        {
            *p = '.';

            // There can be at most one comma in this string anyhow.
            break;
        }
    }

    return buf;
}

} // namespace details

} // namespace soci

#endif // SOCI_PRIVATE_SOCI_DTOCSTR_H_INCLUDED
