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

#include <ripple/rpc/Yield.h>
#include <ripple/rpc/impl/TestOutputSuite.h>

namespace ripple {
namespace RPC {

class Yield_test : public New::TestOutputSuite
{
public:
    using Strings = std::vector <std::string>;

    void runTest (std::string const& name, int chunkSize, Strings const& result)
    {
        setup (name);
        auto f = [=](Output out)
        {
            out("hello ");
            out("there ");
            out("world.");
        };

        std::string s;
        Output output = stringOutput (s);
        auto c = yieldingOutputCoroutine (f, output, chunkSize);
        auto count = 0;
        for (auto count = 0; c; ++count, c()) {
            if (count >= result.size())
            {
                expect (false, "Result was too long");
                break;
            }
            expectResult (s, result[count]);
        }
        expect (count < result.size(), "Result was too short");
    }

    void run() override
    {
        runTest ("zero", 0, {"hello ", "hello there ", "hello there world."});
        runTest ("three", 3, {"hello ", "hello there ", "hello there world."});
        runTest ("five", 5, {"hello ", "hello there ", "hello there world."});
        runTest ("seven", 7, {"hello there ", "hello there world."});
        runTest ("ten", 10, {"hello there ", "hello there world."});
        runTest ("thirteen", 13, {"hello there world."});
        runTest ("fifteen", 15, {"hello there world."});
    }
};

BEAST_DEFINE_TESTSUITE(Yield, RPC, ripple);

} // RPC
} // ripple
