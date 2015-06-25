//------------------------------------------------------------------------------
/*
  This file is part of rippled: https://github.com/ripple/rippled
  Copyright (c) 2012-2015 Ripple Labs Inc.

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

#include <ripple/app/ledger/tests/common_ledger.h>

namespace ripple {
namespace test {

#if 0

class DeferredCredits_test : public beast::unit_test::suite
{
    /*
      Create paths so one path funds another path.

      Two accounts: sender and receiver.
      Two gateways: gw1 and gw2.
      Sender and receiver both have trust lines to the gateways.
      Sender has 2 gw1/USD and 4 gw2/USD.
      Sender has offer to exchange 2 gw1 for gw2 and gw2 for gw1 1-for-1.
      Paths are:
      1) GW1 -> [OB GW1/USD->GW2/USD] -> GW2
      2) GW2 -> [OB GW2/USD->GW1/USD] -> GW1

      sender pays receiver 4 USD.
      Path 1:
      1) Sender exchanges 2 GW1/USD for 2 GW2/USD
      2) Old code: the 2 GW1/USD is available to sender
         New code: the 2 GW1/USD is not available until the
         end of the transaction.
      3) Receiver gets 2 GW2/USD
      Path 2:
      1) Old code: Sender exchanges 2 GW2/USD for 2 GW1/USD
      2) Old code: Receiver get 2 GW1
      2) New code: Path is dry because sender does not have any
         GW1 to spend until the end of the transaction.
    */
    void testSelfFunding ()
    {
        testcase ("selfFunding");

        auto const keyType = KeyType::ed25519;
        std::uint64_t const xrp = std::mega::num;

        auto master = createAccount ("masterpassphrase", keyType);

        std::shared_ptr<Ledger const> LCL;
        Ledger::pointer ledger;
        std::tie (LCL, ledger) = createGenesisLedger (100000 * xrp, master);

        auto accounts =
            createAndFundAccountsWithFlags (master,
                                            {"snd", "rcv", "gw1", "gw2"},
                                            keyType,
                                            10000 * xrp,
                                            ledger,
                                            LCL,
                                            asfDefaultRipple);
        auto& gw1 = accounts["gw1"];
        auto& gw2 = accounts["gw2"];
        auto& snd = accounts["snd"];
        auto& rcv = accounts["rcv"];

        close_and_advance (ledger, LCL);

        trust (snd, gw1, "USD", 10, ledger);
        trust (snd, gw2, "USD", 10, ledger);
        trust (rcv, gw1, "USD", 100, ledger);
        trust (rcv, gw2, "USD", 100, ledger);

        pay (gw1, snd, "USD", "2", ledger);
        pay (gw2, snd, "USD", "4", ledger);

        verifyBalance (ledger, snd, Amount (2, "USD", gw1));
        verifyBalance (ledger, snd, Amount (4, "USD", gw2));

        close_and_advance (ledger, LCL);

        createOfferWithFlags (snd,
                              Amount (2, "USD", gw1),
                              Amount (2, "USD", gw2),
                              ledger,
                              tfPassive);
        createOfferWithFlags (snd,
                              Amount (2, "USD", gw2),
                              Amount (2, "USD", gw1),
                              ledger,
                              tfPassive);

        close_and_advance (ledger, LCL);

        Json::Value path;
        path.append (createPath (gw1, OfferPathNode ("USD", gw2), gw2));
        path.append (createPath (gw2, OfferPathNode ("USD", gw1), gw1));

        payWithPath (snd, rcv, "USD", "4", ledger, path,
                     tfNoRippleDirect | tfPartialPayment);

        verifyBalance (ledger, rcv, Amount (0, "USD", gw1));
        verifyBalance (ledger, rcv, Amount (2, "USD", gw2));

        pass ();
    }

    void testSubtractCredits ()
    {
        testcase ("subtractCredits");

        auto const keyType = KeyType::ed25519;
        std::uint64_t const xrp = std::mega::num;

        auto master = createAccount ("masterpassphrase", keyType);

        std::shared_ptr<Ledger const> LCL;
        Ledger::pointer ledger;
        std::tie (LCL, ledger) = createGenesisLedger (100000 * xrp, master);

        auto accounts =
                createAndFundAccountsWithFlags (master,
                                                {"alice", "gw1", "gw2"},
                                                keyType,
                                                10000 * xrp,
                                                ledger,
                                                LCL,
                                                asfDefaultRipple);
        auto& gw1 = accounts["gw1"];
        auto& gw2 = accounts["gw2"];
        auto& alice = accounts["alice"];

        close_and_advance (ledger, LCL);

        trust (alice, gw1, "USD", 100, ledger);
        trust (alice, gw2, "USD", 100, ledger);

        pay (gw1, alice, "USD", "50", ledger);
        pay (gw2, alice, "USD", "50", ledger);

        verifyBalance (ledger, alice, Amount (50, "USD", gw1));
        verifyBalance (ledger, alice, Amount (50, "USD", gw2));

        AccountID const gw1Acc (gw1.pk.getAccountID ());
        AccountID const aliceAcc (alice.pk.getAccountID ());
        ripple::Currency const usd (to_currency ("USD"));
        ripple::Issue const issue (usd, gw1Acc);
        STAmount const toCredit (issue, 30);
        STAmount const toDebit (issue, 20);
        {
            // accountSend, no FT
            MetaView les (ledger, tapNONE);

            expect (!les.areCreditsDeferred ());

            STAmount const startingAmount =
                accountHolds (les, aliceAcc, usd, gw1Acc, fhIGNORE_FREEZE, getConfig());

            accountSend (les, gw1Acc, aliceAcc, toCredit, {});
            expect (accountHolds (les, aliceAcc, usd, gw1Acc, fhIGNORE_FREEZE, getConfig()) ==
                    startingAmount + toCredit);

            accountSend (les, aliceAcc, gw1Acc, toDebit, {});
            expect (accountHolds (les, aliceAcc, usd, gw1Acc, fhIGNORE_FREEZE, getConfig()) ==
                    startingAmount + toCredit - toDebit);
        }

        {
            // rippleCredit, no FT
            MetaView les (ledger, tapNONE);

            expect (!les.areCreditsDeferred ());

            STAmount const startingAmount =
                accountHolds (les, aliceAcc, usd, gw1Acc, fhIGNORE_FREEZE, getConfig());

            rippleCredit (les, gw1Acc, aliceAcc, toCredit, true, {});
            expect (accountHolds (les, aliceAcc, usd, gw1Acc, fhIGNORE_FREEZE, getConfig()) ==
                    startingAmount + toCredit);

            rippleCredit (les, aliceAcc, gw1Acc, toDebit, true, {});
            expect (accountHolds (les, aliceAcc, usd, gw1Acc, fhIGNORE_FREEZE, getConfig()) ==
                    startingAmount + toCredit - toDebit);
        }

        {
            // accountSend, w/ FT
            MetaView les (ledger, tapNONE);
            les.enableDeferredCredits ();
            expect (les.areCreditsDeferred ());

            STAmount const startingAmount =
                accountHolds (les, aliceAcc, usd, gw1Acc, fhIGNORE_FREEZE, getConfig());

            accountSend (les, gw1Acc, aliceAcc, toCredit, {});
            expect (accountHolds (les, aliceAcc, usd, gw1Acc, fhIGNORE_FREEZE, getConfig()) ==
                    startingAmount);

            accountSend (les, aliceAcc, gw1Acc, toDebit, {});
            expect (accountHolds (les, aliceAcc, usd, gw1Acc, fhIGNORE_FREEZE, getConfig()) ==
                    startingAmount - toDebit);
        }

        {
            // rippleCredit, w/ FT
            MetaView les (ledger, tapNONE);
            les.enableDeferredCredits ();
            expect (les.areCreditsDeferred ());

            STAmount const startingAmount =
                accountHolds (les, aliceAcc, usd, gw1Acc, fhIGNORE_FREEZE, getConfig());

            rippleCredit (les, gw1Acc, aliceAcc, toCredit, true, {});
            expect (accountHolds (les, aliceAcc, usd, gw1Acc, fhIGNORE_FREEZE, getConfig()) ==
                    startingAmount);

            rippleCredit (les, aliceAcc, gw1Acc, toDebit, true, {});
            expect (accountHolds (les, aliceAcc, usd, gw1Acc, fhIGNORE_FREEZE, getConfig()) ==
                    startingAmount - toDebit);
        }

        {
            // rippleCredit, w/ FT & ScopedDeferCredits
            MetaView les (ledger, tapNONE);

            STAmount const startingAmount =
                accountHolds (les, aliceAcc, usd, gw1Acc, fhIGNORE_FREEZE, getConfig());
            {
                ScopedDeferCredits g (les);
                rippleCredit (les, gw1Acc, aliceAcc, toCredit, true, {});
                expect (
                    accountHolds (les, aliceAcc, usd, gw1Acc, fhIGNORE_FREEZE, getConfig()) ==
                    startingAmount);
            }
            expect (accountHolds (les, aliceAcc, usd, gw1Acc, fhIGNORE_FREEZE, getConfig()) ==
                    startingAmount + toCredit);
        }

        {
            // issueIOU
            MetaView les (ledger, tapNONE);
            STAmount const startingAmount =
                accountHolds (les, aliceAcc, usd, gw1Acc, fhIGNORE_FREEZE, getConfig());
            les.enableDeferredCredits ();
            expect (les.areCreditsDeferred ());

            redeemIOU (les, aliceAcc, toDebit, issue, {});
            expect (accountHolds (les, aliceAcc, usd, gw1Acc, fhIGNORE_FREEZE, getConfig()) ==
                    startingAmount - toDebit);
        }
        {
            // redeemIOU
            MetaView les (ledger, tapNONE);
            STAmount const startingAmount =
                accountHolds (les, aliceAcc, usd, gw1Acc, fhIGNORE_FREEZE, getConfig());
            {
                ScopedDeferCredits g (les);
                expect (les.areCreditsDeferred ());

                issueIOU (les, aliceAcc, toCredit, issue, {});
                expect (
                    accountHolds (les, aliceAcc, usd, gw1Acc, fhIGNORE_FREEZE, getConfig()) ==
                    startingAmount);
            }
            expect (accountHolds (les, aliceAcc, usd, gw1Acc, fhIGNORE_FREEZE, getConfig()) ==
                    startingAmount + toCredit);
        }
    }

public:
    void run ()
    {
        testSelfFunding ();
        testSubtractCredits ();
    }
};

BEAST_DEFINE_TESTSUITE (DeferredCredits, ledger, ripple);

#endif

}  // test
}  // ripple
