//
// Copyright (C) 2015 Vadim Zeitlin
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef SOCI_PRIVATE_SOCI_COMPILER_H_INCLUDED
#define SOCI_PRIVATE_SOCI_COMPILER_H_INCLUDED

#include "soci-cpp.h"

// CHECK_GCC(major,minor) evaluates to 1 when using g++ of at least this
// version or 0 when using g++ of lesser version or not using g++ at all.
#if defined(__GNUC__) && defined(__GNUC_MINOR__)
#   define CHECK_GCC(major, minor) \
        ((__GNUC__ > (major)) \
            || (__GNUC__ == (major) && __GNUC_MINOR__ >= (minor)))
#else
#   define CHECK_GCC(major, minor) 0
#endif

// GCC_WARNING_{SUPPRESS,RESTORE} macros can be used to bracket the code
// producing a specific warning to disable it.
//
// They only work with g++ 4.6+ or clang, warnings are not disabled for earlier
// g++ versions.
#if defined(__clang__) || CHECK_GCC(4, 6)
#   define GCC_WARNING_SUPPRESS(x) \
        _Pragma (SOCI_STRINGIZE(GCC diagnostic push)) \
        _Pragma (SOCI_STRINGIZE(GCC diagnostic ignored SOCI_STRINGIZE(SOCI_CONCAT(-W,x))))
#   define GCC_WARNING_RESTORE(x) \
        _Pragma (SOCI_STRINGIZE(GCC diagnostic pop))
#else /* gcc < 4.6 or not gcc and not clang at all */
#   define GCC_WARNING_SUPPRESS(x)
#   define GCC_WARNING_RESTORE(x)
#endif

#endif // SOCI_PRIVATE_SOCI_COMPILER_H_INCLUDED
