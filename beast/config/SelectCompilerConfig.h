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

// Intel
#ifndef BEAST_CONFIG_SELECTCOMPILERCONFIG_H_INCLUDED
#define BEAST_CONFIG_SELECTCOMPILERCONFIG_H_INCLUDED

#if defined(__INTEL_COMPILER) || defined(__ICL) || defined(__ICC) || defined(__ECC)
#define BEAST_COMPILER_CONFIG "config/compiler/Intel.h"

// Clang C++ emulates GCC, so it has to appear early.
#elif defined __clang__
#define BEAST_COMPILER_CONFIG "config/compiler/Clang.h"

//  GNU C++:
#elif defined __GNUC__
#define BEAST_COMPILER_CONFIG "config/compiler/Gcc.h"

//  Microsoft Visual C++
//
//  Must remain the last #elif since some other vendors (Metrowerks, for
//  example) also #define _MSC_VER
#elif defined _MSC_VER
#define BEAST_COMPILER_CONFIG "config/compiler/VisualC.h"

#else
#error "Unsupported compiler."
#endif

#endif
