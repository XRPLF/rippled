//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2018 Ripple Labs Inc.

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

#ifndef RIPPLE_BASICS_BYTEUTILITIES_H_INCLUDED
#define RIPPLE_BASICS_BYTEUTILITIES_H_INCLUDED

namespace ripple {

    template<class T>
    constexpr auto kilobytes(T value) noexcept
    {
        return value * 1024;
    }

    template<class T>
    constexpr auto megabytes(T value) noexcept
    {
        return kilobytes(kilobytes(value));
    }

    static_assert(kilobytes(2) == 2048, "kilobytes(2) == 2048");
    static_assert(megabytes(3) == 3145728, "megabytes(3) == 3145728");
}

#endif
