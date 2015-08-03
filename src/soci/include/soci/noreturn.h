//
// Copyright (C) 2015 Vadim Zeitlin
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef SOCI_NORETURN_H_INCLUDED
#define SOCI_NORETURN_H_INCLUDED

// Define a portable SOCI_NORETURN macro.
//
// TODO-C++11: Use [[noreturn]] attribute.
#if defined(__GNUC__)
#   define SOCI_NORETURN __attribute__((noreturn)) void
#elif defined(_MSC_VER)
#   define SOCI_NORETURN __declspec(noreturn) void
#else
#   define SOCI_NORETURN void
#endif

#endif // SOCI_NORETURN_H_INCLUDED
