//
// Copyright (C) 2015 Vadim Zeitlin.
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef SOCI_PRIVATE_STATIC_ASSERT_H_INCLUDED
#define SOCI_PRIVATE_STATIC_ASSERT_H_INCLUDED

#include "soci-cpp.h"

// This is a simple approximation for C++11 static_assert: generate a
// compile-time error if the given expression evaluates to 0 and make the
// identifier (not string!) msg appear in the error message.
#define SOCI_STATIC_ASSERT(expr) \
    struct SOCI_MAKE_UNIQUE_NAME(SociAssert) { unsigned msg: expr; }

#endif // SOCI_PRIVATE_STATIC_ASSERT_H_INCLUDED
