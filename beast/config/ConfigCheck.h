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

#ifndef BEAST_CONFIG_CONFIGCHECK_H_INCLUDED
#define BEAST_CONFIG_CONFIGCHECK_H_INCLUDED

// This file makes sure that BeastConfig.h was included.
// It also sets defaults for all config options.

/*  If you fail to make sure that all your compile units are building Beast with
    the same set of option flags, then there's a risk that different compile
    units will treat the classes as having different memory layouts, leading to
    very nasty memory corruption errors when they all get linked together.
    That's why it's best to always include the BeastConfig.h file before any
    beast headers.
*/
#ifndef BEAST_BEASTCONFIG_H_INCLUDED
# ifdef _MSC_VER
#  pragma message ("Have you included your BeastConfig.h file before including the Beast headers?")
# else
#  warning "Have you included your BeastConfig.h file before including the Beast headers?"
# endif
# error "BeastConfig.h must be included before any Beast headers!"
#endif

//
// Apply sensible defaults for the configuration settings
//

#ifndef BEAST_FORCE_DEBUG
#define BEAST_FORCE_DEBUG 0
#endif

#ifndef BEAST_LOG_ASSERTIONS
# if BEAST_ANDROID
#  define BEAST_LOG_ASSERTIONS 1
# else
#  define BEAST_LOG_ASSERTIONS 0
# endif
#endif

#if BEAST_DEBUG && ! defined (BEAST_CHECK_MEMORY_LEAKS)
#define BEAST_CHECK_MEMORY_LEAKS 1
#endif

#ifndef BEAST_DISABLE_CONTRACT_CHECKS
#define BEAST_DISABLE_CONTRACT_CHECKS 0
#endif

#ifndef BEAST_COMPILER_CHECKS_SOCKET_OVERRIDES
#define BEAST_COMPILER_CHECKS_SOCKET_OVERRIDES 0
#endif

//------------------------------------------------------------------------------

#ifndef BEAST_DONT_AUTOLINK_TO_WIN32_LIBRARIES
#define BEAST_DONT_AUTOLINK_TO_WIN32_LIBRARIES 0
#endif

#ifndef BEAST_INCLUDE_ZLIB_CODE
#define BEAST_INCLUDE_ZLIB_CODE 1
#endif

#ifndef BEAST_ZLIB_INCLUDE_PATH
#define BEAST_ZLIB_INCLUDE_PATH <zlib.h>
#endif

#ifndef BEAST_SQLITE_FORCE_NDEBUG
#define BEAST_SQLITE_FORCE_NDEBUG 0
#endif

#ifndef BEAST_STRING_UTF_TYPE
#define BEAST_STRING_UTF_TYPE 8
#endif

//------------------------------------------------------------------------------

#ifndef BEAST_USE_BOOST_FEATURES
#define BEAST_USE_BOOST_FEATURES 0
#endif

#endif
