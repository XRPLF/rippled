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

#ifndef BEAST_CORE_SYSTEM_FUNCTIONALINCLUDES_H_INCLUDED
#define BEAST_CORE_SYSTEM_FUNCTIONALINCLUDES_H_INCLUDED

// Choose a source of bind, placeholders, and function

#if !BEAST_FUNCTIONAL_USES_STD && !BEAST_FUNCTIONAL_USES_TR1 && !BEAST_FUNCTIONAL_USES_BOOST
# if BEAST_USE_BOOST_FEATURES
#  define BEAST_FUNCTIONAL_USES_BOOST 1
# elif BEAST_MSVC
#  define BEAST_FUNCTIONAL_USES_STD 1
# elif BEAST_IOS || BEAST_MAC
#  include <ciso646>                        // detect version of std::lib
#  if BEAST_IOS && BEAST_USE_BOOST_FEATURES // Work-around for iOS bugs with bind.
#   define BEAST_FUNCTIONAL_USES_BOOST 1
#  elif _LIBCPP_VERSION // libc++
#   define BEAST_FUNCTIONAL_USES_STD 1
#  else // libstdc++ (GNU)
#   define BEAST_FUNCTIONAL_USES_TR1 1
#  endif
# elif BEAST_LINUX || BEAST_BSD
#  define BEAST_FUNCTIONAL_USES_TR1 1
# else
#  define BEAST_FUNCTIONAL_USES_STD 1
# endif
#endif

#if BEAST_FUNCTIONAL_USES_STD
# include <functional>
#elif BEAST_FUNCTIONAL_USES_TR1
# include <tr1/functional>
#elif BEAST_FUNCTIONAL_USES_BOOST
// included in BoostPlaceholdersFix.h
#endif

#endif
