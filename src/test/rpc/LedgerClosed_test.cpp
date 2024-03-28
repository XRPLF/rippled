//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2016 Ripple Labs Inc.

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

#include <ripple/protocol/Feature.h>
#include <ripple/protocol/jss.h>
#include <test/jtx.h>

namespace ripple {

class LedgerClosed_test : public beast::unit_test::suite
{
public:
    void
    testMonitorRoot()
    {
        using namespace test::jtx;
        Env env{*this, FeatureBitset{}};
        Account const alice{"alice"};
        env.fund(XRP(10000), alice);

        auto lc_result = env.rpc("ledger_closed")[jss::result];
        BEAST_EXPECT(
            lc_result[jss::ledger_hash] ==
            "CCC3B3E88CCAC17F1BE6B4A648A55999411F19E3FE55EB721960EB0DF28EDDA5");
        BEAST_EXPECT(lc_result[jss::ledger_index] == 2);

        env.close();
        auto const ar_master = env.le(env.master);
        BEAST_EXPECT(ar_master->getAccountID(sfAccount) == env.master.id());
        BEAST_EXPECT((*ar_master)[sfBalance] == drops(99999989999999980));

        auto const ar_alice = env.le(alice);
        BEAST_EXPECT(ar_alice->getAccountID(sfAccount) == alice.id());
        BEAST_EXPECT((*ar_alice)[sfBalance] == XRP(10000));

        lc_result = env.rpc("ledger_closed")[jss::result];
        BEAST_EXPECT(
            lc_result[jss::ledger_hash] ==
            "E86DE7F3D7A4D9CE17EF7C8BA08A8F4D8F643B9552F0D895A31CDA78F541DE4E");
        BEAST_EXPECT(lc_result[jss::ledger_index] == 3);
    }

    void
    run() override
    {
        testMonitorRoot();
    }
};

BEAST_DEFINE_TESTSUITE(LedgerClosed, app, ripple);

}  // namespace ripple
