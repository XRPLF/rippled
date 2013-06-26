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

#ifndef BEAST_BIND_BEASTHEADER
#define BEAST_BIND_BEASTHEADER

/* Brings functional support into our namespace, based on environment.
*/
#if BEAST_MSVC
// Visual Studio has these in std.
using std::ref;
using std::bind;
using std::function;
using std::placeholders::_1;
using std::placeholders::_2;

#elif BEAST_IOS
#if BEAST_USE_BOOST
/* If boost is activated, use it. This works
   around a bug with the iOS implementation of bind.
*/
using boost::ref
using boost::bind;
using boost::function;
using ::_1;
using ::_2;
#else
#if _LIBCPP_VERSION // libc++
using std::ref;
using std::bind;
using std::function;
using std::placeholders::_1;
using std::placeholders::_2;
#else // libstdc++ (GNU)
using std::tr1::ref;
using std::tr1::bind;
using std::tr1::function;
using std::tr1::placeholders::_1;
using std::tr1::placeholders::_2;
#endif
#endif

#elif BEAST_MAC
#if _LIBCPP_VERSION // libc++
using std::ref;
using std::bind;
using std::function;
using std::placeholders::_1;
using std::placeholders::_2;
#else // libstdc++ (GNU)
using std::tr1::ref;
using std::tr1::bind;
using std::tr1::function;
using std::tr1::placeholders::_1;
using std::tr1::placeholders::_2;
#endif

#elif BEAST_LINUX || BEAST_BSD
using std::tr1::bind;
using std::tr1::placeholders::_1;
using std::tr1::placeholders::_2;

#else
#error Unknown platform in beast_Bind.h

#endif

/** Max number of arguments to bind, total.
*/
#if BEAST_MSVC
# ifdef _VARIADIC_MAX
#  define BEAST_VARIADIC_MAX _VARIADIC_MAX
# else
#  define BEAST_VARIADIC_MAX 9
# endif
#else
# define BEAST_VARIADIC_MAX 9
#endif

#endif
