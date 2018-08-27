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

#include <ripple/basics/qalloc.h>
#include <ripple/beast/unit_test.h>
#include <type_traits>

namespace ripple {

struct qalloc_test : beast::unit_test::suite
{
    void
    testBasicProperties()
    {
        BEAST_EXPECT(std::is_default_constructible<qalloc>{});
        BEAST_EXPECT(std::is_copy_constructible<qalloc>{});
        BEAST_EXPECT(std::is_copy_assignable<qalloc>{});
        BEAST_EXPECT(std::is_nothrow_move_constructible<qalloc>{});
        BEAST_EXPECT(std::is_nothrow_move_assignable<qalloc>{});
    }

    void
    run() override
    {
        testBasicProperties();
    }
};

BEAST_DEFINE_TESTSUITE(qalloc, ripple_basics, ripple);

}  // namespace ripple
