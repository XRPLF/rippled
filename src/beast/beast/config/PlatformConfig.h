//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

    Portions of this file are from JUCE.
    Copyright (c) 2013 - Raw Material Software Ltd.
    Please visit http://www.juce.com

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

#ifndef BEAST_CONFIG_PLATFORMCONFIG_H_INCLUDED
#define BEAST_CONFIG_PLATFORMCONFIG_H_INCLUDED

//==============================================================================
/*  This file figures out which platform is being built, and defines some macros
    that the rest of the code can use for OS-specific compilation.

    Macros that will be set here are:

    - One of BEAST_WINDOWS, BEAST_MAC BEAST_LINUX, BEAST_IOS, BEAST_ANDROID, etc.
    - Either BEAST_32BIT or BEAST_64BIT, depending on the architecture.
    - Either BEAST_LITTLE_ENDIAN or BEAST_BIG_ENDIAN.
    - Either BEAST_INTEL or BEAST_PPC
    - Either BEAST_GCC or BEAST_MSVC
*/

//==============================================================================
#if (defined (_WIN32) || defined (_WIN64))
  #define       BEAST_WIN32 1
  #define       BEAST_WINDOWS 1
#elif defined (BEAST_ANDROID)
  #undef        BEAST_ANDROID
  #define       BEAST_ANDROID 1
#elif defined (LINUX) || defined (__linux__)
  #define     BEAST_LINUX 1
#elif defined (__APPLE_CPP__) || defined(__APPLE_CC__)
  #define Point CarbonDummyPointName // (workaround to avoid definition of "Point" by old Carbon headers)
  #define Component CarbonDummyCompName
  #include <CoreFoundation/CoreFoundation.h> // (needed to find out what platform we're using)
  #undef Point
  #undef Component

  #if TARGET_OS_IPHONE || TARGET_IPHONE_SIMULATOR
    #define     BEAST_IPHONE 1
    #define     BEAST_IOS 1
  #else
    #define     BEAST_MAC 1
  #endif
#elif defined (__FreeBSD__)
  #define BEAST_BSD 1
#else
  #error "Unknown platform!"
#endif

//==============================================================================
#if BEAST_WINDOWS
  #ifdef _MSC_VER
    #ifdef _WIN64
      #define BEAST_64BIT 1
    #else
      #define BEAST_32BIT 1
    #endif
  #endif

  #ifdef _DEBUG
    #define BEAST_DEBUG 1
  #endif

  #ifdef __MINGW32__
    #define BEAST_MINGW 1
    #ifdef __MINGW64__
      #define BEAST_64BIT 1
    #else
      #define BEAST_32BIT 1
    #endif
  #endif

  /** If defined, this indicates that the processor is little-endian. */
  #define BEAST_LITTLE_ENDIAN 1

  #define BEAST_INTEL 1
#endif

//==============================================================================
#if BEAST_MAC || BEAST_IOS

  #if defined (DEBUG) || defined (_DEBUG) || ! (defined (NDEBUG) || defined (_NDEBUG))
    #define BEAST_DEBUG 1
  #endif

  #ifdef __LITTLE_ENDIAN__
    #define BEAST_LITTLE_ENDIAN 1
  #else
    #define BEAST_BIG_ENDIAN 1
  #endif
#endif

#if BEAST_MAC

  #if defined (__ppc__) || defined (__ppc64__)
    #define BEAST_PPC 1
  #elif defined (__arm__)
    #define BEAST_ARM 1
  #else
    #define BEAST_INTEL 1
  #endif

  #ifdef __LP64__
    #define BEAST_64BIT 1
  #else
    #define BEAST_32BIT 1
  #endif

  #if MAC_OS_X_VERSION_MIN_REQUIRED < MAC_OS_X_VERSION_10_4
    #error "Building for OSX 10.3 is no longer supported!"
  #endif

  #ifndef MAC_OS_X_VERSION_10_5
    #error "To build with 10.4 compatibility, use a 10.5 or 10.6 SDK and set the deployment target to 10.4"
  #endif

#endif

//==============================================================================
#if BEAST_LINUX || BEAST_ANDROID || BEAST_BSD

  #ifdef _DEBUG
    #define BEAST_DEBUG 1
  #endif

  // Allow override for big-endian Linux platforms
  #if defined (__LITTLE_ENDIAN__) || ! defined (BEAST_BIG_ENDIAN)
    #define BEAST_LITTLE_ENDIAN 1
    #undef BEAST_BIG_ENDIAN
  #else
    #undef BEAST_LITTLE_ENDIAN
    #define BEAST_BIG_ENDIAN 1
  #endif

  #if defined (__LP64__) || defined (_LP64)
    #define BEAST_64BIT 1
  #else
    #define BEAST_32BIT 1
  #endif

  #if __MMX__ || __SSE__ || __amd64__
    #ifdef __arm__
      #define BEAST_ARM 1
    #else
      #define BEAST_INTEL 1
    #endif
  #endif
#endif

//==============================================================================
// Compiler type macros.

#ifdef __clang__
 #define BEAST_CLANG 1
#elif defined (__GNUC__)
  #define BEAST_GCC 1
#elif defined (_MSC_VER)
  #define BEAST_MSVC 1

  #if _MSC_VER < 1500
    #define BEAST_VC8_OR_EARLIER 1

    #if _MSC_VER < 1400
      #define BEAST_VC7_OR_EARLIER 1

      #if _MSC_VER < 1300
        #warning "MSVC 6.0 is no longer supported!"
      #endif
    #endif
  #endif

  #if BEAST_64BIT || ! BEAST_VC7_OR_EARLIER
    #define BEAST_USE_INTRINSICS 1
  #endif
#else
  #error unknown compiler
#endif

//------------------------------------------------------------------------------

// Handy macro that lets pragma warnings be clicked in the output window
//
// Usage: #pragma message(BEAST_FILEANDLINE_ "Advertise here!")
//
//        Note that a space following the macro is mandatory for C++11.
//
// This is here so it can be used in C compilations that include this directly.
//
#define BEAST_PP_STR2_(x) #x
#define BEAST_PP_STR1_(x) BEAST_PP_STR2_(x)
#define BEAST_FILEANDLINE_ __FILE__ "(" BEAST_PP_STR1_(__LINE__) "): warning:"

#endif
