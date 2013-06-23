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

#ifndef BEAST_BEASTCONFIG_HEADER
#define BEAST_BEASTCONFIG_HEADER

// beast_core flags:

#ifndef    BEAST_FORCE_DEBUG
 //#define BEAST_FORCE_DEBUG
#endif

#ifndef    BEAST_LOG_ASSERTIONS
 //#define BEAST_LOG_ASSERTIONS 1
#endif

#ifndef    BEAST_CHECK_MEMORY_LEAKS
 //#define BEAST_CHECK_MEMORY_LEAKS
#endif

#ifndef    BEAST_DONT_AUTOLINK_TO_WIN32_LIBRARIES
 //#define BEAST_DONT_AUTOLINK_TO_WIN32_LIBRARIES
#endif

// beast_basics flags

#ifndef BEAST_USE_BOOST
#define BEAST_USE_BOOST 0
#endif

#ifndef BEAST_USE_LEAKCHECKED
#define BEAST_USE_LEAKCHECKED BEAST_CHECK_MEMORY_LEAKS
#endif

#endif
