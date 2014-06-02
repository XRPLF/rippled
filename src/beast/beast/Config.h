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

#ifndef BEAST_CONFIG_H_INCLUDED
#define BEAST_CONFIG_H_INCLUDED

// VFALCO NOTE this is analogous to <boost/config.hpp>

// Assert to boost that we always have std::array support
#define BOOST_ASIO_HAS_STD_ARRAY 1

#if !defined(BEAST_COMPILER_CONFIG) && !defined(BEAST_NO_COMPILER_CONFIG) && !defined(BEAST_NO_CONFIG)
#include <beast/config/SelectCompilerConfig.h>
#endif
#ifdef   BEAST_COMPILER_CONFIG
#include BEAST_COMPILER_CONFIG
#endif

#if !defined(BEAST_STDLIB_CONFIG) && !defined(BEAST_NO_STDLIB_CONFIG) && !defined(BEAST_NO_CONFIG) && defined(__cplusplus)
#include <beast/config/SelectStdlibConfig.h>
#endif
#ifdef   BEAST_STDLIB_CONFIG
#include BEAST_STDLIB_CONFIG
#endif

#if !defined(BEAST_PLATFORM_CONFIG) && !defined(BEAST_NO_PLATFORM_CONFIG) && !defined(BEAST_NO_CONFIG)
#include <beast/config/SelectCompilerConfig.h>
#endif
#ifdef   BEAST_PLATFORM_CONFIG
#include BEAST_PLATFORM_CONFIG
#endif

// Legacy
#include <beast/Version.h>
#include <beast/config/PlatformConfig.h>
#include <beast/config/CompilerConfig.h>
#include <beast/config/StandardConfig.h>
#include <beast/config/ConfigCheck.h>

// Suffix
#include <beast/config/Suffix.h>
    
#endif
