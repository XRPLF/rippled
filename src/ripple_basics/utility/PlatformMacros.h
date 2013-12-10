//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#ifndef RIPPLE_PLATFORMMACROS_H
#define RIPPLE_PLATFORMMACROS_H

#define FUNCTION_TYPE beast::function
#define BIND_TYPE     beast::bind
#define P_1           beast::_1
#define P_2           beast::_2
#define P_3           beast::_3
#define P_4           beast::_4

// VFALCO TODO Clean this up

#if (!defined(FORCE_NO_C11X) && (__cplusplus > 201100L)) || defined(FORCE_C11X)

// VFALCO TODO Get rid of the C11X macro
#define C11X
#define UPTR_T          std::unique_ptr
#define MOVE_P(p)       std::move(p)

#else

//#define UPTR_T          std::auto_ptr
#define UPTR_T          ScopedPointer
#define MOVE_P(p)       (p)

#endif

// VFALCO TODO Clean this stuff up. Remove as much as possible
#define nothing()           do {} while (0)
#define fallthru()          do {} while (0)
#define NUMBER(x)           (sizeof(x)/sizeof((x)[0]))
#define isSetBit(x,y)       (!!((x) & (y)))

#endif
