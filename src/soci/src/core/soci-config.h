//
// Copyright (C) 2006-2008 Mateusz Loskot
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef SOCI_CONFIG_H_INCLUDED
#define SOCI_CONFIG_H_INCLUDED

//
// On Windows platform, define SOCI_DECL depending on
// static or dynamic (SOCI_DLL) linkage.
//
// For details, see
// http://www.boost.org/more/separate_compilation.html
//

#ifdef _WIN32
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

#ifdef _MSC_VER
#pragma warning(disable:4251 4275)
#endif // _MSC_VER

#endif // SOCI_CONFIG_H_INCLUDED
