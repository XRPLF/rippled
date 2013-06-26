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

#ifndef BEAST_STANDARDHEADER_BEASTHEADER
#define BEAST_STANDARDHEADER_BEASTHEADER

//==============================================================================
/** Current BEAST version number.

    See also SystemStats::getBeastVersion() for a string version.
*/
#define BEAST_MAJOR_VERSION      0
#define BEAST_MINOR_VERSION      0
#define BEAST_BUILDNUMBER        1

/** Current Beast version number.

    Bits 16 to 32 = major version.
    Bits 8 to 16 = minor version.
    Bits 0 to 8 = point release.

    See also SystemStats::getBeastVersion() for a string version.
*/
#define BEAST_VERSION   ((BEAST_MAJOR_VERSION << 16) + (BEAST_MINOR_VERSION << 8) + BEAST_BUILDNUMBER)


//==============================================================================
#include "beast_TargetPlatform.h"  // (sets up the various BEAST_WINDOWS, BEAST_MAC, etc flags)
#include "beast_PlatformDefs.h"

//==============================================================================
// Now we'll include some common OS headers..
#if BEAST_MSVC
 #pragma warning (push)
 #pragma warning (disable: 4514 4245 4100)
#endif

#include <cstdlib>
#include <cstdarg>
#include <climits>
#include <limits>
#include <cmath>
#include <cwchar>
#include <stdexcept>
#include <typeinfo>
#include <cstring>
#include <cstdio>
#include <iostream>
#include <vector>

#if BEAST_USE_INTRINSICS
 #include <intrin.h>
#endif

#if BEAST_MAC || BEAST_IOS
 #include <libkern/OSAtomic.h>
#endif

#if BEAST_LINUX
 #include <signal.h>

 #if __INTEL_COMPILER
  #if __ia64__
   #include <ia64intrin.h>
  #else
   #include <ia32intrin.h>
  #endif
 #endif
#endif

#if BEAST_MSVC && BEAST_DEBUG
 #include <crtdbg.h>
#endif

#if BEAST_MSVC
 #pragma warning (pop)
#endif

#if BEAST_ANDROID
 #include <sys/atomics.h>
 #include <byteswap.h>
#endif

// undef symbols that are sometimes set by misguided 3rd-party headers..
#undef check
#undef TYPE_BOOL
#undef max
#undef min

//==============================================================================
// DLL building settings on Windows
#if BEAST_MSVC
 #ifdef BEAST_DLL_BUILD
  #define BEAST_API __declspec (dllexport)
  #pragma warning (disable: 4251)
 #elif defined (BEAST_DLL)
  #define BEAST_API __declspec (dllimport)
  #pragma warning (disable: 4251)
 #endif
 #ifdef __INTEL_COMPILER
  #pragma warning (disable: 1125) // (virtual override warning)
 #endif
#elif defined (BEAST_DLL) || defined (BEAST_DLL_BUILD)
 #define BEAST_API __attribute__ ((visibility("default")))
#endif

//==============================================================================
#ifndef BEAST_API
 #define BEAST_API  /**< This macro is added to all beast public class declarations. */
#endif

#if BEAST_MSVC && BEAST_DLL_BUILD
 #define BEAST_PUBLIC_IN_DLL_BUILD(declaration)  public: declaration; private:
#else
 #define BEAST_PUBLIC_IN_DLL_BUILD(declaration)  declaration;
#endif

/** This macro is added to all beast public function declarations. */
#define BEAST_PUBLIC_FUNCTION        BEAST_API BEAST_CALLTYPE

#if (! defined (BEAST_CATCH_DEPRECATED_CODE_MISUSE)) && BEAST_DEBUG && ! DOXYGEN
 /** This turns on some non-essential bits of code that should prevent old code from compiling
     in cases where method signatures have changed, etc.
 */
 #define BEAST_CATCH_DEPRECATED_CODE_MISUSE 1
#endif

#ifndef DOXYGEN
 #define BEAST_NAMESPACE beast  // This old macro is deprecated: you should just use the beast namespace directly.
#endif

//==============================================================================
// Now include some common headers...
namespace beast
{
    extern BEAST_API bool BEAST_CALLTYPE beast_isRunningUnderDebugger();
    extern BEAST_API void BEAST_CALLTYPE logAssertion (const char* file, int line) noexcept;

    #include "../memory/beast_Memory.h"
    #include "../maths/beast_MathsFunctions.h"
    #include "../memory/beast_ByteOrder.h"
    #include "../logging/beast_Logger.h"
    #include "../memory/beast_LeakedObjectDetector.h"
}

#endif   // BEAST_STANDARDHEADER_BEASTHEADER
