//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2016 Ripple Labs Inc.

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
#include <ripple/test/jtx.h>

namespace ripple {
namespace test {

class SetTrust_test : public beast::unit_test::suite
{
public:
    void testFreeTrustlines()
    {
        testcase("Allow 2 free trust lines before requiring reserve");

        using namespace jtx;
        Env env(*this);

        auto const gwA = Account{ "gatewayA" };
        auto const gwB = Account{ "gatewayB" };
        auto const gwC = Account{ "gatewayC" };
        auto const alice = Account{ "alice" };

        auto const txFee = env.current()->fees().base;
        auto const baseReserve = env.current()->fees().accountReserve(0);
        auto const threelineReserve = env.current()->fees().accountReserve(3);

        env.fund(XRP(10000), gwA, gwB, gwC);

        // Fund alice with ...
        env.fund(baseReserve /* enough to hold an account */
            + drops(3*txFee) /* and to pay for 3 transactions */, alice);

        env(trust(alice, gwA["USD"](100)), require(lines(alice,1)));
        env(trust(alice, gwB["USD"](100)), require(lines(alice,2)));

        // Alice does not have enough for the third trust line
        env(trust(alice, gwC["USD"](100)), ter(tecNO_LINE_INSUF_RESERVE), require(lines(alice,2)));

        // Fund Alice additional amount to cover
        env(pay(env.master, alice, STAmount{ threelineReserve - baseReserve }));
        env(trust(alice, gwC["USD"](100)), require(lines(alice,3)));
    }


    void run()
    {
        testFreeTrustlines();
    }
};
BEAST_DEFINE_TESTSUITE(SetTrust, app, ripple);
} // test
} // ripple
