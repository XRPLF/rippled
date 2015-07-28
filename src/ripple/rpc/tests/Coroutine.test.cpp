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

    void test (int chunkSize, Strings const& expected)
    {
        auto name = std::to_string (chunkSize);
        setup (name);

        std::string buffer;
        Json::Output output = Json::stringOutput (buffer);

        auto makeContinuation = [&] (std::string const& data) {
            return Continuation ([=] (Callback const& cb) {
                output (data + " ");
                cb();
            });
        };

        Strings result;
        Coroutine coroutine ([&] (Suspend const& suspend)
        {
            Callback yield ([=] () { suspend (makeContinuation ("*")); });
            auto out = chunkedYieldingOutput (output, yield, chunkSize);
            out ("hello ");
            result.push_back (buffer);

            suspend (makeContinuation("HELLO"));
            result.push_back (buffer);

            out ("there ");
            result.push_back (buffer);

            suspend (makeContinuation("THERE"));
            result.push_back (buffer);

            out ("world ");
            result.push_back (buffer);

            suspend (makeContinuation("WORLD"));
            result.push_back (buffer);
        });

        runOnCoroutine(UseCoroutines::yes, coroutine);
        expectCollectionEquals (result, expected);
    }

    void run() override
    {
        test (0, {"hello ",
                  "hello HELLO ",
                  "hello HELLO * there ",
                  "hello HELLO * there THERE ",
                  "hello HELLO * there THERE * world ",
                  "hello HELLO * there THERE * world WORLD "
                  });
        test (3, {"hello ",
                  "hello HELLO ",
                  "hello HELLO * there ",
                  "hello HELLO * there THERE ",
                  "hello HELLO * there THERE * world ",
                  "hello HELLO * there THERE * world WORLD "
                  });
        test (5, {"hello ",
                  "hello HELLO ",
                  "hello HELLO * there ",
                  "hello HELLO * there THERE ",
                  "hello HELLO * there THERE * world ",
                  "hello HELLO * there THERE * world WORLD "
                  });
        test (7, {"hello ",
                  "hello HELLO ",
                  "hello HELLO there ",
                  "hello HELLO there THERE ",
                  "hello HELLO there THERE * world ",
                  "hello HELLO there THERE * world WORLD "
                  });
        test (10, {"hello ",
                   "hello HELLO ",
                   "hello HELLO there ",
                   "hello HELLO there THERE ",
                   "hello HELLO there THERE * world ",
                   "hello HELLO there THERE * world WORLD "
                  });
        test (13, {"hello ",
                   "hello HELLO ",
                   "hello HELLO there ",
                   "hello HELLO there THERE ",
                   "hello HELLO there THERE world ",
                   "hello HELLO there THERE world WORLD "
                  });
        test (15, {"hello ",
                   "hello HELLO ",
                   "hello HELLO there ",
                   "hello HELLO there THERE ",
                   "hello HELLO there THERE world ",
                   "hello HELLO there THERE world WORLD "
                  });
  }
};

BEAST_DEFINE_TESTSUITE(Coroutine, RPC, ripple);

} // RPC
} // ripple
