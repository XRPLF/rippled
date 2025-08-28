//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012 Ripple Labs Inc.

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

#include <xrpl/basics/contract.h>

#include <doctest/doctest.h>

#include <stdexcept>
#include <string>

using namespace ripple;

TEST_CASE("contract")
{
    try
    {
        Throw<std::runtime_error>("Throw test");
    }
    catch (std::runtime_error const& e1)
    {
        CHECK(std::string(e1.what()) == "Throw test");

        try
        {
            Rethrow();
        }
        catch (std::runtime_error const& e2)
        {
            CHECK(std::string(e2.what()) == "Throw test");
        }
        catch (...)
        {
            CHECK(false);
        }
    }
    catch (...)
    {
        CHECK(false);
    }
}
