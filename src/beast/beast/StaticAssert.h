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

#ifndef BEAST_STATICASSERT_H_INCLUDED
#define BEAST_STATICASSERT_H_INCLUDED

#ifndef DOXYGEN
namespace beast
{
    template <bool b>
    struct BeastStaticAssert;

    template <>
    struct BeastStaticAssert <true>
    {
        static void dummy() {}
    };
}
#endif

/** A compile-time assertion macro.
    If the expression parameter is false, the macro will cause a compile error.
    (The actual error message that the compiler generates may be completely
    bizarre and seem to have no relation to the place where you put the
    static_assert though!)
*/
#define static_bassert(expression) beast::BeastStaticAssert<expression>::dummy();

#endif
