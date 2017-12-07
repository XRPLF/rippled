//
// Copyright (C) 2006-2008 Mateusz Loskot
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef SOCI_PLATFORM_H_INCLUDED
#define SOCI_PLATFORM_H_INCLUDED

//disable MSVC deprecated warnings
#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdarg.h>
#include <string.h>
#include <string>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <memory>

#include "soci/soci-config.h" // for SOCI_HAVE_CXX_C11

#if defined(_MSC_VER)
#define LL_FMT_FLAGS "I64"
#else
#define LL_FMT_FLAGS "ll"
#endif

// Portability hacks for Microsoft Visual C++ compiler
#ifdef _MSC_VER
#include <stdlib.h>

//Disables warnings about STL objects need to have dll-interface and/or
//base class must have dll interface
#pragma warning(disable:4251 4275)


// Define if you have the vsnprintf variants.
#if _MSC_VER < 1500
# define vsnprintf _vsnprintf
#endif

// Define if you have the snprintf variants.
#define snprintf _snprintf

// Define if you have the strtoll and strtoull variants.
#if _MSC_VER < 1300
# error "Visual C++ versions prior 1300 don't support _strtoi64 and _strtoui64"
#elif _MSC_VER >= 1300 && _MSC_VER < 1800
namespace std {
    inline long long strtoll(char const* str, char** str_end, int base)
    {
        return _strtoi64(str, str_end, base);
    }

    inline unsigned long long strtoull(char const* str, char** str_end, int base)
    {
        return _strtoui64(str, str_end, base);
    }
}
#endif // _MSC_VER < 1800
#endif // _MSC_VER

#if defined(__CYGWIN__) || defined(__MINGW32__)
#include <stdlib.h>
namespace std {
    using ::strtoll;
    using ::strtoull;
}
#endif

//define DLL import/export on WIN32
#ifdef _WIN32
# ifndef _WIN32_WINNT
#   define _WIN32_WINNT 0x0502 //_WIN32_WINNT_WS03, VS2015 support: https://msdn.microsoft.com/de-de/library/6sehtctf.aspx
# endif // _WIN32_WINNT
# ifdef SOCI_DLL
#  ifdef SOCI_SOURCE
#   define SOCI_DECL __declspec(dllexport)
#  else
#   define SOCI_DECL __declspec(dllimport)
#  endif // SOCI_SOURCE
# endif // SOCI_DLL
#endif // _WIN32
//
// If SOCI_DECL isn't defined yet define it now
#ifndef SOCI_DECL
# define SOCI_DECL
#endif

// C++11 features are always available in MSVS as it has no separate C++98
// mode, we just need to check for the minimal compiler version supporting them
// (see https://msdn.microsoft.com/en-us/library/hh567368.aspx).

#if defined(SOCI_HAVE_CXX_C11) || (defined(_MSC_VER) && _MSC_VER >= 1800)
# define SOCI_OVERRIDE override
#else
# define SOCI_OVERRIDE
#endif

namespace soci
{

namespace cxx_details
{

#if defined(SOCI_HAVE_CXX_C11) || (defined(_MSC_VER) && _MSC_VER >= 1800)
    template <typename T>
    using auto_ptr = std::unique_ptr<T>;
#else // std::unique_ptr<> not available
    using std::auto_ptr;
#endif

} // namespace cxx_details

} // namespace soci

#if defined(SOCI_HAVE_CXX_C11) || (defined(_MSC_VER) && _MSC_VER >= 1800)
    #define SOCI_NOT_ASSIGNABLE(classname) \
        classname& operator=(const classname&) = delete;
    #define SOCI_NOT_COPYABLE(classname) \
        classname(const classname&) = delete; \
        SOCI_NOT_ASSIGNABLE(classname)
#else // no C++11 deleted members support
    #define SOCI_NOT_ASSIGNABLE(classname) \
        classname& operator=(const classname&);
    #define SOCI_NOT_COPYABLE(classname) \
        classname(const classname&); \
        SOCI_NOT_ASSIGNABLE(classname)
#endif // C++11 deleted members available

#define SOCI_UNUSED(x) (void)x;

#if defined(SOCI_HAVE_CXX_C11) || (defined(_MSC_VER) && _MSC_VER >= 1900)
    #define SOCI_NOEXCEPT_FALSE noexcept(false)
#else
    #define SOCI_NOEXCEPT_FALSE
#endif

#endif // SOCI_PLATFORM_H_INCLUDED
