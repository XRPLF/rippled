//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2025 Ripple Labs Inc.

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

#include <test/jtx.h>

#include <xrpl/protocol/jss.h>

namespace ripple {

class Connect_test : public beast::unit_test::suite
{
    void
    testErrors()
    {
        testcase("Errors");

        using namespace test::jtx;

        {
            // standalone mode should fail
            Env env{*this};
            BEAST_EXPECT(env.app().config().standalone());

            auto const result = env.rpc("json", "connect", "{}");
            BEAST_EXPECT(result[jss::result][jss::status] == "error");
            BEAST_EXPECT(result[jss::result].isMember(jss::error));
            BEAST_EXPECT(result[jss::result][jss::error] == "notSynced");
            BEAST_EXPECT(
                result[jss::result][jss::error_message] ==
                "Not synced to the network.");
        }
    }

public:
    void
    run() override
    {
        testErrors();
    }
};

BEAST_DEFINE_TESTSUITE(Connect, rpc, ripple);

}  // namespace ripple
