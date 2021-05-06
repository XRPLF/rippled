//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github0.com/ripple/rippled
    Copyright (c) 2021 Ripple Inc.

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

#include <ripple/basics/scope.h>
#include <ripple/beast/unit_test.h>

namespace ripple {
namespace test {

struct scope_test : beast::unit_test::suite
{
    void
    test_scope_exit()
    {
        // scope_exit always executes the functor on destruction,
        // unless release() is called
        int i = 0;
        {
            scope_exit x{[&i]() { i = 1; }};
        }
        BEAST_EXPECT(i == 1);
        {
            scope_exit x{[&i]() { i = 2; }};
            x.release();
        }
        BEAST_EXPECT(i == 1);
        {
            scope_exit x{[&i]() { i = 3; }};
            auto x2 = std::move(x);
        }
        BEAST_EXPECT(i == 3);
        {
            scope_exit x{[&i]() { i = 4; }};
            x.release();
            auto x2 = std::move(x);
        }
        BEAST_EXPECT(i == 3);
        {
            try
            {
                scope_exit x{[&i]() { i = 5; }};
                throw 1;
            }
            catch (...)
            {
            }
        }
        BEAST_EXPECT(i == 5);
        {
            try
            {
                scope_exit x{[&i]() { i = 6; }};
                x.release();
                throw 1;
            }
            catch (...)
            {
            }
        }
        BEAST_EXPECT(i == 5);
    }

    void
    test_scope_fail()
    {
        // scope_fail executes the functor on destruction only
        // if an exception is unwinding, unless release() is called
        int i = 0;
        {
            scope_fail x{[&i]() { i = 1; }};
        }
        BEAST_EXPECT(i == 0);
        {
            scope_fail x{[&i]() { i = 2; }};
            x.release();
        }
        BEAST_EXPECT(i == 0);
        {
            scope_fail x{[&i]() { i = 3; }};
            auto x2 = std::move(x);
        }
        BEAST_EXPECT(i == 0);
        {
            scope_fail x{[&i]() { i = 4; }};
            x.release();
            auto x2 = std::move(x);
        }
        BEAST_EXPECT(i == 0);
        {
            try
            {
                scope_fail x{[&i]() { i = 5; }};
                throw 1;
            }
            catch (...)
            {
            }
        }
        BEAST_EXPECT(i == 5);
        {
            try
            {
                scope_fail x{[&i]() { i = 6; }};
                x.release();
                throw 1;
            }
            catch (...)
            {
            }
        }
        BEAST_EXPECT(i == 5);
    }

    void
    test_scope_success()
    {
        // scope_success executes the functor on destruction only
        // if an exception is not unwinding, unless release() is called
        int i = 0;
        {
            scope_success x{[&i]() { i = 1; }};
        }
        BEAST_EXPECT(i == 1);
        {
            scope_success x{[&i]() { i = 2; }};
            x.release();
        }
        BEAST_EXPECT(i == 1);
        {
            scope_success x{[&i]() { i = 3; }};
            auto x2 = std::move(x);
        }
        BEAST_EXPECT(i == 3);
        {
            scope_success x{[&i]() { i = 4; }};
            x.release();
            auto x2 = std::move(x);
        }
        BEAST_EXPECT(i == 3);
        {
            try
            {
                scope_success x{[&i]() { i = 5; }};
                throw 1;
            }
            catch (...)
            {
            }
        }
        BEAST_EXPECT(i == 3);
        {
            try
            {
                scope_success x{[&i]() { i = 6; }};
                x.release();
                throw 1;
            }
            catch (...)
            {
            }
        }
        BEAST_EXPECT(i == 3);
    }

    void
    run() override
    {
        test_scope_exit();
        test_scope_fail();
        test_scope_success();
    }
};

BEAST_DEFINE_TESTSUITE(scope, ripple_basics, ripple);

}  // namespace test
}  // namespace ripple