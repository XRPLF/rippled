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

#include <ripple/app/tests/common_ledger.h>

namespace ripple {
namespace test {

class Ledger_test : public beast::unit_test::suite
{
    void test_genesisLedger (bool sign)
    {
        std::uint64_t const xrp = std::mega::num;

        auto master = createAccount ("masterpassphrase");

        Ledger::pointer LCL = createGenesisLedger(100000*xrp, master);

        Ledger::pointer ledger = std::make_shared<Ledger>(false, *LCL);

        // User accounts
        auto gw1 = createAccount ("gw1");
        expect (gw1.pk != master.pk, "gw1.pk != master.pk");
        expect (gw1.sk != master.sk, "gw1.sk != master.sk");
        auto gw2 = createAccount ("gw2");
        auto gw3 = createAccount ("gw3");
        auto alice = createAccount ("alice");
        auto mark = createAccount ("mark");

        // Fund gw1, gw2, gw3, alice, mark from master
        makeAndApplyPayment(master, gw1, 5000 * xrp, ledger, sign);
        makeAndApplyPayment(master, gw2, 4000 * xrp, ledger, sign);
        makeAndApplyPayment(master, gw3, 3000 * xrp, ledger, sign);
        makeAndApplyPayment(master, alice, 2000 * xrp, ledger, sign);
        makeAndApplyPayment(master, mark, 1000 * xrp, ledger, sign);

        LCL = close_and_advance(ledger, LCL);
        ledger = std::make_shared<Ledger>(false, *LCL);

        // alice trusts FOO/gw1
        makeTrustSet (alice, gw1, "FOO", 1, ledger, sign);

        // mark trusts FOO/gw2
        makeTrustSet (mark, gw2, "FOO", 1, ledger, sign);

        // mark trusts FOO/gw3
        makeTrustSet (mark, gw3, "FOO", 1, ledger, sign);

        // gw2 pays mark with FOO
        makeAndApplyPayment(gw2, mark, "FOO", ".1", ledger, sign);

        // gw3 pays mark with FOO
        makeAndApplyPayment(gw3, mark, "FOO", ".2", ledger, sign);

        // gw1 pays alice with FOO
        makeAndApplyPayment(gw1, alice, "FOO", ".3", ledger, sign);

        LCL = close_and_advance(ledger, LCL);
        ledger = std::make_shared<Ledger>(false, *LCL);

        createOffer (mark, Amount (1, "FOO", gw1), Amount (1, "FOO", gw2), ledger, sign);
        createOffer (mark, Amount (1, "FOO", gw2), Amount (1, "FOO", gw3), ledger, sign);
        cancelOffer (mark, ledger, sign);
        freezeAccount (alice, ledger, sign);

        LCL = close_and_advance(ledger, LCL);
        ledger = std::make_shared<Ledger>(false, *LCL);

        makeAndApplyPayment(alice, mark, 1 * xrp, ledger, sign);

        LCL = close_and_advance(ledger, LCL);
        ledger = std::make_shared<Ledger>(false, *LCL);

        pass ();
    }

    void test_unsigned_fails ()
    {
        std::uint64_t const xrp = std::mega::num;

        auto master = createAccount ("masterpassphrase");

        Ledger::pointer LCL = createGenesisLedger (100000 * xrp, master);

        Ledger::pointer ledger = std::make_shared<Ledger> (false, *LCL);

        auto gw1 = createAccount ("gw1");

        auto tx = getPaymentTx(master, gw1, 5000 * xrp, false);

        try
        {
            applyTransaction (ledger, tx, true);
            fail ("apply unsigned transaction should fail");
        }
        catch (std::runtime_error const& e)
        {
            if (std::string (e.what()) != "r != tesSUCCESS")
                throw std::runtime_error(e.what());
        }

        pass ();
    }

    void test_getQuality ()
    {
        uint256 uBig (
            "D2DC44E5DC189318DB36EF87D2104CDF0A0FE3A4B698BEEE55038D7EA4C68000");
        expect (6125895493223874560 == getQuality (uBig));

        pass ();
    }
public:
    void run ()
    {
        testcase ("genesisLedger signed transactions");
        test_genesisLedger (true);

        testcase ("genesisLedger unsigned transactions");
        test_genesisLedger (false);

        testcase ("unsigned invalid");
        test_unsigned_fails ();

        testcase ("getQuality");
        test_getQuality ();
    }
};

BEAST_DEFINE_TESTSUITE(Ledger,ripple_app,ripple);

} // test
} // ripple
