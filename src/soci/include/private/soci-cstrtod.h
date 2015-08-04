//
// Copyright (C) 2014 Vadim Zeitlin.
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef SOCI_PRIVATE_SOCI_CSTRTOD_H_INCLUDED
#define SOCI_PRIVATE_SOCI_CSTRTOD_H_INCLUDED

#include "soci/error.h"

#include <stdlib.h>
#include <string.h>

namespace soci
{

namespace details
{

// Locale-independent, i.e. always using "C" locale, function for converting
// strings to numbers.
//
// The string must contain a floating point number in "C" locale, i.e. using
// point as decimal separator, and nothing but it. If it does, the converted
// number is returned, otherwise an exception is thrown.
inline
double cstring_to_double(char const* s)
{
    // Unfortunately there is no clean way to parse a number in C locale
    // without this hack: normally, using std::istringstream with classic
    // locale should work, but some standard library implementations are buggy
    // and handle non-default locale in thread-unsafe way, by changing the
    // global C locale which is unacceptable as it introduces subtle bugs in
    // multi-thread programs. So we rely on just the standard C functions and
    // try to make them work by tweaking the input into the form appropriate
    // for the current locale.

    // First try with the original input.
    char* end;
    double d = strtod(s, &end);

    bool parsedOK;
    if (*end == '.')
    {
        // Parsing may have stopped because the current locale uses something
        // different from the point as decimal separator, retry with a comma.
        //
        // In principle, values other than point or comma are possible but they
        // don't seem to be used in practice, so for now keep things simple.
        size_t const bufSize = strlen(s) + 1;
        char* const buf = new char[bufSize];
        strcpy(buf, s);
        buf[end - s] = ',';
        d = strtod(buf, &end);
        parsedOK = end != buf && *end == '\0';
        delete [] buf;
    }
    else
    {
        // Notice that we must detect false positives as well: parsing a string
        // using decimal comma should fail when using this function.
        parsedOK = end != s && *end == '\0' && !strchr(s, ',');
    }

    if (!parsedOK)
    {
      throw soci_error(std::string("Cannot convert data: string \"") + s + "\" "
                       "is not a number.");
    }

    return d;
}

} // namespace details

} // namespace soci

#endif // SOCI_PRIVATE_SOCI_CSTRTOD_H_INCLUDED
