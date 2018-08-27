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

#include <test/jtx.h>
#include <ripple/app/ledger/BuildLedger.h>
#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/ledger/LedgerReplay.h>

namespace ripple {
namespace test {

struct LedgerReplay_test : public beast::unit_test::suite
{
    void run() override
    {
        testcase("Replay ledger");

        using namespace jtx;

        // Build a ledger normally
        auto const alice = Account("alice");
        auto const bob = Account("bob");

        Env env(*this);
        env.fund(XRP(100000), alice, bob);
        env.close();

        LedgerMaster& ledgerMaster = env.app().getLedgerMaster();
        auto const lastClosed = ledgerMaster.getClosedLedger();
        auto const lastClosedParent =
            ledgerMaster.getLedgerByHash(lastClosed->info().parentHash);

        auto const replayed = buildLedger(
            LedgerReplay(lastClosedParent,lastClosed),
            tapNONE,
            env.app(),
            env.journal);

        BEAST_EXPECT(replayed->info().hash == lastClosed->info().hash);
    }
};

BEAST_DEFINE_TESTSUITE(LedgerReplay,app,ripple);

} // test
} // ripple
