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

#ifndef BEAST_CONFIG_COMPILERCONFIG_H_INCLUDED
#define BEAST_CONFIG_COMPILERCONFIG_H_INCLUDED

#include <assert.h>
#include <beast/config/PlatformConfig.h>

// This file defines miscellaneous macros for debugging, assertions, etc.

#if BEAST_FORCE_DEBUG
# undef BEAST_DEBUG
# define BEAST_DEBUG 1
#endif

/** This macro defines the C calling convention used as the standard for Beast calls.
*/
#if BEAST_MSVC
# define BEAST_CDECL      __cdecl
#else
# define BEAST_CDECL
#endif

/** This macro fixes C++'s constexpr for VS2012, which doesn't understand it.
*/
#if BEAST_MSVC
# define BEAST_CONSTEXPR const
#else
# define BEAST_CONSTEXPR constexpr
#endif


// Debugging and assertion macros

#if BEAST_LOG_ASSERTIONS || BEAST_DEBUG
#define beast_LogCurrentAssertion    beast::logAssertion (__FILE__, __LINE__);
#else
#define beast_LogCurrentAssertion
#endif

#if BEAST_IOS || BEAST_LINUX || BEAST_ANDROID || BEAST_PPC
/** This will try to break into the debugger if the app is currently being debugged.
    If called by an app that's not being debugged, the behaiour isn't defined - it may crash or not, depending
    on the platform.
    @see bassert()
*/
# define beast_breakDebugger        { ::kill (0, SIGTRAP); }
#elif BEAST_USE_INTRINSICS
# ifndef __INTEL_COMPILER
#  pragma intrinsic (__debugbreak)
# endif
# define beast_breakDebugger        { __debugbreak(); }
#elif BEAST_GCC || BEAST_MAC
# if BEAST_NO_INLINE_ASM
#  define beast_breakDebugger       { }
# else
#  define beast_breakDebugger       { asm ("int $3"); }
# endif
#else
#  define beast_breakDebugger       { __asm int 3 }
#endif

#if BEAST_CLANG && defined (__has_feature) && ! defined (BEAST_ANALYZER_NORETURN)
# if __has_feature (attribute_analyzer_noreturn)
   inline void __attribute__((analyzer_noreturn)) beast_assert_noreturn() {}
#  define BEAST_ANALYZER_NORETURN beast_assert_noreturn();
# endif
#endif

#ifndef BEAST_ANALYZER_NORETURN
#define BEAST_ANALYZER_NORETURN
#endif

//------------------------------------------------------------------------------

#ifdef __cplusplus
extern "C" {
#endif
/** Report a fatal error message and terminate the application.
    Normally you won't call this directly.
*/
extern void beast_reportFatalError (char const* message, char const* fileName, int lineNumber);
#ifdef __cplusplus
}
#endif

#if BEAST_DEBUG || DOXYGEN

/** Writes a string to the standard error stream.
    This is only compiled in a debug build.
*/
#define BDBG(dbgtext) { \
    beast::String tempDbgBuf; \
    tempDbgBuf << dbgtext; \
    beast::outputDebugString (tempDbgBuf.toStdString ()); \
}

#if 0
/** This will always cause an assertion failure.
    It is only compiled in a debug build, (unless BEAST_LOG_ASSERTIONS is enabled for your build).
    @see bassert
*/
#define bassertfalse          { beast_LogCurrentAssertion; if (beast::beast_isRunningUnderDebugger()) beast_breakDebugger; BEAST_ANALYZER_NORETURN }

/** Platform-independent assertion macro.
    This macro gets turned into a no-op when you're building with debugging turned off, so be
    careful that the expression you pass to it doesn't perform any actions that are vital for the
    correct behaviour of your program!
    @see bassertfalse
  */
#define bassert(expression)   { if (! (expression)) beast_reportFatalError(#expression,__FILE__,__LINE__); }
#else

#define bassertfalse assert(false)
#define bassert(expression) assert(expression)
#endif

#else

// If debugging is disabled, these dummy debug and assertion macros are used..

#define BDBG(dbgtext)
#define bassertfalse              { beast_LogCurrentAssertion }

# if BEAST_LOG_ASSERTIONS
#  define bassert(expression)      { if (! (expression)) bassertfalse; }
# else
#  define bassert(a)               {}
# endif

#endif

//------------------------------------------------------------------------------

#if ! DOXYGEN
 #define BEAST_JOIN_MACRO_HELPER(a, b) a ## b
 #define BEAST_STRINGIFY_MACRO_HELPER(a) #a
#endif

/** A good old-fashioned C macro concatenation helper.
    This combines two items (which may themselves be macros) into a single string,
    avoiding the pitfalls of the ## macro operator.
*/
#define BEAST_JOIN_MACRO(item1, item2)  BEAST_JOIN_MACRO_HELPER (item1, item2)

/** A handy C macro for stringifying any symbol, rather than just a macro parameter.
*/
#define BEAST_STRINGIFY(item)  BEAST_STRINGIFY_MACRO_HELPER (item)

//------------------------------------------------------------------------------

#if BEAST_MSVC || DOXYGEN
/** This can be placed before a stack or member variable declaration to tell
    the compiler to align it to the specified number of bytes.
*/
#define BEAST_ALIGN(bytes) __declspec (align (bytes))
#else
#define BEAST_ALIGN(bytes) __attribute__ ((aligned (bytes)))
#endif

//------------------------------------------------------------------------------

// Cross-compiler deprecation macros..
#ifdef DOXYGEN
 /** This macro can be used to wrap a function which has been deprecated. */
 #define BEAST_DEPRECATED(functionDef)
#elif BEAST_MSVC && ! BEAST_NO_DEPRECATION_WARNINGS
 #define BEAST_DEPRECATED(functionDef) __declspec(deprecated) functionDef
#elif BEAST_GCC && ! BEAST_NO_DEPRECATION_WARNINGS
 #define BEAST_DEPRECATED(functionDef) functionDef __attribute__ ((deprecated))
#else
 #define BEAST_DEPRECATED(functionDef) functionDef
#endif

//------------------------------------------------------------------------------

#if BEAST_ANDROID && ! DOXYGEN
# define BEAST_MODAL_LOOPS_PERMITTED 0
#elif ! defined (BEAST_MODAL_LOOPS_PERMITTED)
 /** Some operating environments don't provide a modal loop mechanism, so this
     flag can be used to disable any functions that try to run a modal loop.
 */
 #define BEAST_MODAL_LOOPS_PERMITTED 1
#endif

//------------------------------------------------------------------------------

#if BEAST_GCC
# define BEAST_PACKED __attribute__((packed))
#elif ! DOXYGEN
# define BEAST_PACKED
#endif

//------------------------------------------------------------------------------

// Here, we'll check for C++11 compiler support, and if it's not available, define
// a few workarounds, so that we can still use some of the newer language features.
#if defined (__GXX_EXPERIMENTAL_CXX0X__) && defined (__GNUC__) && (__GNUC__ * 100 + __GNUC_MINOR__) >= 405
# define BEAST_COMPILER_SUPPORTS_NOEXCEPT 1
# define BEAST_COMPILER_SUPPORTS_NULLPTR 1
# define BEAST_COMPILER_SUPPORTS_MOVE_SEMANTICS 1
# if (__GNUC__ * 100 + __GNUC_MINOR__) >= 407 && ! defined (BEAST_COMPILER_SUPPORTS_OVERRIDE_AND_FINAL)
#  define BEAST_COMPILER_SUPPORTS_OVERRIDE_AND_FINAL 1
# endif
#endif

#if BEAST_CLANG && defined (__has_feature)
# if __has_feature (cxx_nullptr)
#  define BEAST_COMPILER_SUPPORTS_NULLPTR 1
# endif
# if __has_feature (cxx_noexcept)
#  define BEAST_COMPILER_SUPPORTS_NOEXCEPT 1
# endif
# if __has_feature (cxx_rvalue_references)
#  define BEAST_COMPILER_SUPPORTS_MOVE_SEMANTICS 1
# endif
# ifndef BEAST_COMPILER_SUPPORTS_OVERRIDE_AND_FINAL
#  define BEAST_COMPILER_SUPPORTS_OVERRIDE_AND_FINAL 1
# endif
# ifndef BEAST_COMPILER_SUPPORTS_ARC
#  define BEAST_COMPILER_SUPPORTS_ARC 1
# endif
#endif

#if defined (_MSC_VER) && _MSC_VER >= 1600
# define BEAST_COMPILER_SUPPORTS_NULLPTR 1
# define BEAST_COMPILER_SUPPORTS_MOVE_SEMANTICS 1
#endif

#if defined (_MSC_VER) && _MSC_VER >= 1700
# define BEAST_COMPILER_SUPPORTS_OVERRIDE_AND_FINAL 1
#endif

#if BEAST_COMPILER_SUPPORTS_MOVE_SEMANTICS
# define BEAST_MOVE_ARG(type) type&&
# define BEAST_MOVE_CAST(type) static_cast<type&&>
#else
# define BEAST_MOVE_ARG(type) type
# define BEAST_MOVE_CAST(type) type
#endif

#ifdef __cplusplus
namespace beast {
bool beast_isRunningUnderDebugger();
void logAssertion (char const* file, int line);
}
#endif

#endif
