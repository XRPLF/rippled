//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

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

#ifndef BEAST_CXX14_CONFIG_H_INCLUDED
#define BEAST_CXX14_CONFIG_H_INCLUDED

// Sets C++14 compatibility configuration macros based on build environment

// Disables beast c++14 compatibility additions when set to 1
// Note, some compatibilty features are enabled or disabled individually.
//
#ifndef BEAST_NO_CXX14_COMPATIBILITY
# ifdef _MSC_VER
#  define BEAST_NO_CXX14_COMPATIBILITY 1
# elif defined(__clang__) && defined(_LIBCPP_VERSION) && __cplusplus >= 201305
#  define BEAST_NO_CXX14_COMPATIBILITY 1
# else
#  define BEAST_NO_CXX14_COMPATIBILITY 0
# endif
#endif

// Disables beast's std::make_unique
#ifndef BEAST_NO_CXX14_MAKE_UNIQUE
# ifdef _MSC_VER
#  define BEAST_NO_CXX14_MAKE_UNIQUE 1
# elif defined(__clang__) && defined(_LIBCPP_VERSION) && __cplusplus >= 201305
#  define BEAST_NO_CXX14_MAKE_UNIQUE 1
# else
#  define BEAST_NO_CXX14_MAKE_UNIQUE 0
# endif
#endif

// Disables beast's std::equal safe iterator overloads
#ifndef BEAST_NO_CXX14_EQUAL
# define BEAST_NO_CXX14_EQUAL 0
#endif

#endif
