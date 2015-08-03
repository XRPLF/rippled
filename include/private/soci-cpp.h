//
// Copyright (C) 2015 Vadim Zeitlin
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef SOCI_PRIVATE_SOCI_CPP_H_INCLUDED
#define SOCI_PRIVATE_SOCI_CPP_H_INCLUDED

// Some very common preprocessor helpers.

// SOCI_CONCAT() pastes together two tokens after expanding them.
#define SOCI_CONCAT_IMPL(x, y) x ## y
#define SOCI_CONCAT(x, y) SOCI_CONCAT_IMPL(x, y)

// SOCI_STRINGIZE() makes a string of its argument after expanding it.
#define SOCI_STRINGIZE_IMPL(x) #x
#define SOCI_STRINGIZE(x) SOCI_STRINGIZE_IMPL(x)

// SOCI_MAKE_UNIQUE_NAME() creates a uniquely named identifier with the given
// prefix.
//
// It uses __COUNTER__ macro to avoid problems with broken __LINE__ in MSVC
// when using "Edit and Continue" (/ZI) option as there are no compilers known
// to work with SOCI and not support it. If one such is ever discovered, we
// should use __LINE__ for it instead.
#define SOCI_MAKE_UNIQUE_NAME(name) SOCI_CONCAT(name, __COUNTER__)

#endif // SOCI_PRIVATE_SOCI_CPP_H_INCLUDED
