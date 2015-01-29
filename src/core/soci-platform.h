//
// Copyright (C) 2006-2008 Mateusz Loskot
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef SOCI_PLATFORM_H_INCLUDED
#define SOCI_PLATFORM_H_INCLUDED

#if defined(_MSC_VER) || defined(__MINGW32__)
#define LL_FMT_FLAGS "I64"
#else
#define LL_FMT_FLAGS "ll"
#endif

// Portability hacks for Microsoft Visual C++ compiler
#ifdef _MSC_VER
#include <stdlib.h>

// Define if you have the vsnprintf variants.
#if _MSC_VER < 1500
# define HAVE_VSNPRINTF 1
# define vsnprintf _vsnprintf
#endif

// Define if you have the snprintf variants.
#define HAVE_SNPRINTF 1
#define snprintf _snprintf

// Define if you have the strtoll and strtoull variants.
#if _MSC_VER >= 1300
# define HAVE_STRTOLL 1
# define HAVE_STRTOULL 1

#if _MSC_VER < 1800
namespace std {
    inline long long strtoll(char const* str, char** str_end, int base)
    {
        return _strtoi64(str, str_end, base);
    }

    inline unsigned long long strtoull(char const* str, char** str_end, int base)
    {
        return _strtoui64(str, str_end, base);
    }
}
#endif

#else
# undef HAVE_STRTOLL
# undef HAVE_STRTOULL
# error "Visual C++ versions prior 1300 don't support _strtoi64 and _strtoui64"
#endif // _MSC_VER >= 1300
#endif // _MSC_VER

#endif // SOCI_PLATFORM_H_INCLUDED
