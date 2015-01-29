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

// Ideas from boost

// Android, which must be manually set by defining BEAST_ANDROID
#ifndef BEAST_CONFIG_SELECTPLATFORMCONFIG_H_INCLUDED
#define BEAST_CONFIG_SELECTPLATFORMCONFIG_H_INCLUDED

#if defined(BEAST_ANDROID)
#define BEAST_PLATFORM_CONFIG "config/platform/Android.h"

// linux, also other platforms (Hurd etc) that use GLIBC
#elif (defined(linux) || defined(__linux) || defined(__linux__) || defined(__GNU__) || defined(__GLIBC__)) && !defined(_CRAYC)
#define BEAST_PLATFORM_CONFIG "config/platform/Linux.h"

// BSD
#elif defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFly__)
#define BEAST_PLATFORM_CONFIG "config/platform/Bsd.h"

// win32
#elif defined(_WIN32) || defined(__WIN32__) || defined(WIN32) || defined(_WIN64)
#define BEAST_PLATFORM_CONFIG "config/platform/Win32.h"

// MacOS
#elif defined(macintosh) || defined(__APPLE__) || defined(__APPLE_CC__) || defined(__APPLE_CPP__)
#define BEAST_PLATFORM_CONFIG "config/platform/MacOS.h"

#else
#error "Unsupported platform."
#endif
#endif
