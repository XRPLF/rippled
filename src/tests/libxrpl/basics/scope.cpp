//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2021 Ripple Labs Inc.

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

#include <xrpl/basics/scope.h>

#include <doctest/doctest.h>

using namespace ripple;

TEST_CASE("scope_exit")
{
    // scope_exit always executes the functor on destruction,
    // unless release() is called
    int i = 0;
    {
        scope_exit x{[&i]() { i = 1; }};
    }
    CHECK(i == 1);
    {
        scope_exit x{[&i]() { i = 2; }};
        x.release();
    }
    CHECK(i == 1);
    {
        scope_exit x{[&i]() { i += 2; }};
        auto x2 = std::move(x);
    }
    CHECK(i == 3);
    {
        scope_exit x{[&i]() { i = 4; }};
        x.release();
        auto x2 = std::move(x);
    }
    CHECK(i == 3);
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
    CHECK(i == 5);
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
    CHECK(i == 5);
}

TEST_CASE("scope_fail")
{
    // scope_fail executes the functor on destruction only
    // if an exception is unwinding, unless release() is called
    int i = 0;
    {
        scope_fail x{[&i]() { i = 1; }};
    }
    CHECK(i == 0);
    {
        scope_fail x{[&i]() { i = 2; }};
        x.release();
    }
    CHECK(i == 0);
    {
        scope_fail x{[&i]() { i = 3; }};
        auto x2 = std::move(x);
    }
    CHECK(i == 0);
    {
        scope_fail x{[&i]() { i = 4; }};
        x.release();
        auto x2 = std::move(x);
    }
    CHECK(i == 0);
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
    CHECK(i == 5);
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
    CHECK(i == 5);
}

TEST_CASE("scope_success")
{
    // scope_success executes the functor on destruction only
    // if an exception is not unwinding, unless release() is called
    int i = 0;
    {
        scope_success x{[&i]() { i = 1; }};
    }
    CHECK(i == 1);
    {
        scope_success x{[&i]() { i = 2; }};
        x.release();
    }
    CHECK(i == 1);
    {
        scope_success x{[&i]() { i += 2; }};
        auto x2 = std::move(x);
    }
    CHECK(i == 3);
    {
        scope_success x{[&i]() { i = 4; }};
        x.release();
        auto x2 = std::move(x);
    }
    CHECK(i == 3);
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
    CHECK(i == 3);
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
    CHECK(i == 3);
}
