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

#include <BeastConfig.h>
#include <ripple/rpc/Coroutine.h>
#include <ripple/rpc/Yield.h>
#include <ripple/rpc/tests/TestOutputSuite.test.h>

namespace ripple {
namespace RPC {

class Coroutine_test : public TestOutputSuite
{
public:
    using Strings = std::vector <std::string>;

    void test (std::string const& name, int chunkSize, Strings const& expected)
    {
        setup (name);

        std::string buffer;
        Json::Output output = Json::stringOutput (buffer);

        auto coroutine = Coroutine ([=] (Yield yield)
        {
            auto out = chunkedYieldingOutput (output, yield, chunkSize);
            out ("hello ");
            out ("there ");
            out ("world.");
        });

        Strings result;
        while (coroutine)
        {
            coroutine();
            result.push_back (buffer);
        }

        expectCollectionEquals (result, expected);
    }

    void run() override
    {
        test ("zero", 0, {"hello ", "hello there ", "hello there world."});
        test ("three", 3, {"hello ", "hello there ", "hello there world."});
        test ("five", 5, {"hello ", "hello there ", "hello there world."});
        test ("seven", 7, {"hello there ", "hello there world."});
        test ("ten", 10, {"hello there ", "hello there world."});
        test ("thirteen", 13, {"hello there world."});
        test ("fifteen", 15, {"hello there world."});
    }
};

BEAST_DEFINE_TESTSUITE(Coroutine, RPC, ripple);

} // RPC
} // ripple
