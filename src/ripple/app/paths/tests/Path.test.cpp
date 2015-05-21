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

#include <ripple/app/ledger/tests/common_ledger.h>
#include <ripple/basics/TestSuite.h>

namespace ripple {
namespace test {

class Path_test : public TestSuite
{
private:
    using Accounts = std::map <std::string, TestAccount>;

    Ledger::pointer ledger_;
    Accounts accounts_;

    TestAccount&
    trusts (std::string from, std::string currency,
        std::vector <std::pair <std::string, double> > issuers)
    {
        auto& a = accounts_[from];
        for (auto& _ : issuers)
            trust (a, accounts_[_.first], currency, _.second, ledger_);
        return a;
    }

    void
    initAccounts (std::vector <std::string> accountNames)
    {
        auto master = createAccount ("masterpassphrase", KeyType::ed25519);

        Ledger::pointer LCL;
        std::uint64_t const xrp = std::mega::num;
        std::tie (LCL, ledger_) = createGenesisLedger (100000 * xrp, master);

        accounts_ = createAndFundAccountsWithFlags (master,
            accountNames, KeyType::ed25519, 10000 * xrp, ledger_,
            LCL, asfDefaultRipple);

        for (auto& a : accounts_)
        {
            expectNotEquals (a.second.pk.humanAccountID (),
                master.pk.humanAccountID ());
        }
    }

    void
    test_no_direct_path_no_intermediary_no_alternatives ()
    {
        testcase ("no direct path no intermediary no alternatives");
        initAccounts ({"alice", "bob", "mtgox"});

        auto& alice = accounts_["alice"];
        auto alternatives = findPath (ledger_, alice, accounts_["bob"],
            {Currency ("USD")}, Amount (5, "USD", alice), log.stream ());
        log << "proposed: " << alternatives;
        expectEquals (alternatives.size (), 0);
    }

    void
    test_direct_path_no_intermediary ()
    {
        testcase ("direct path no intermediary");
        initAccounts ({"alice", "bob"});

        // Set credit limit
        auto& alice = accounts_["alice"];
        auto& bob = accounts_["bob"];
        trust (bob, alice, "USD", 5, ledger_);

        // Find path from alice to bob
        auto alternatives = findPath (ledger_, alice, bob, {Currency ("USD")},
            Amount (5, "USD", bob), log.stream ());
        log << "proposed: " << alternatives;
        expectEquals (alternatives.size (), 1);

        auto const& alt = alternatives[0u];
        expectEquals (alt[jss::paths_canonical].size (), 0);
        expectEquals (alt[jss::paths_computed].size (), 0);
        auto const& srcAmount = alt[jss::source_amount];
        expectEquals (srcAmount[jss::currency], "USD");
        expectEquals (srcAmount[jss::value], "5");
        expectEquals (srcAmount[jss::issuer], alice.pk.humanAccountID ());
    }

    void
    test_payment_auto_path_find_using_build_path ()
    {
        testcase ("payment auto path find (using build_path)");
        initAccounts ({"alice", "bob", "mtgox"});

        // Set credit limits
        auto& alice = accounts_["alice"];
        auto& bob = accounts_["bob"];
        auto& mtgox = accounts_["mtgox"];
        trust (alice, mtgox, "USD", 70, ledger_);
        trust (bob, mtgox, "USD", 70, ledger_);

        // Distribute funds
        pay (mtgox, alice, "USD", "70", ledger_);

        // Verify balances
        verifyBalance (ledger_, alice, Amount (70, "USD", mtgox));
        verifyBalance (ledger_, mtgox, Amount (-70, "USD", alice));

        // Payment with path
        payWithPath (alice, bob, "USD", "24", ledger_);

        // Verify balances
        verifyBalance (ledger_, alice, Amount (46, "USD", mtgox));
        verifyBalance (ledger_, mtgox, Amount (-46, "USD", alice));
        verifyBalance (ledger_, bob, Amount (24, "USD", mtgox));
        verifyBalance (ledger_, mtgox, Amount (-24, "USD", bob));
    }

    void
    test_path_find ()
    {
        testcase ("path find");
        initAccounts ({"alice", "bob", "mtgox"});

        // Set credit limits
        auto& alice = accounts_["alice"];
        auto& bob = accounts_["bob"];
        auto& mtgox = accounts_["mtgox"];
        trust (alice, mtgox, "USD", 600, ledger_);
        trust (bob, mtgox, "USD", 700, ledger_);

        // Distribute funds
        pay (mtgox, alice, "USD", "70", ledger_);
        pay (mtgox, bob, "USD", "50", ledger_);

        // Verify balances
        verifyBalance (ledger_, alice, Amount (70, "USD", mtgox));
        verifyBalance (ledger_, mtgox, Amount (-70, "USD", alice));
        verifyBalance (ledger_, bob, Amount (50, "USD", mtgox));
        verifyBalance (ledger_, mtgox, Amount (-50, "USD", bob));

        // Find path from alice to mtgox
        auto alternatives = findPath (ledger_, alice, bob, {Currency ("USD")},
            Amount (5, "USD", mtgox), log.stream ());
        log << "Path find alternatives: " << alternatives;
        expectEquals(alternatives.size(), 1);

        auto const& alt = alternatives[0u];
        expectEquals (alt["paths_canonical"].size (), 0);
        expectEquals (alt["paths_computed"].size (), 0);
        auto const& srcAmount = alt[jss::source_amount];
        expectEquals (srcAmount[jss::currency], "USD");
        expectEquals (srcAmount[jss::value], "5");
        expectEquals (srcAmount[jss::issuer], alice.pk.humanAccountID ());
    }

    void
    test_path_find_cosume_all ()
    {
        testcase ("path find consume all");
        initAccounts ({"alice", "bob", "mtgox"});

        // Set credit limits
        auto& alice = accounts_["alice"];
        auto& bob = accounts_["bob"];
        auto& mtgox = accounts_["mtgox"];
        trust (alice, mtgox, "USD", 70, ledger_);
        trust (bob, mtgox, "USD", 70, ledger_);

        // Distribute funds
        pay (mtgox, alice, "USD", "70", ledger_);
        pay (mtgox, bob, "USD", "50", ledger_);

        // Verify balances
        verifyBalance (ledger_, alice, Amount (70, "USD", mtgox));
        verifyBalance (ledger_, mtgox, Amount (-70, "USD", alice));
        verifyBalance (ledger_, bob, Amount (50, "USD", mtgox));
        verifyBalance (ledger_, mtgox, Amount (-50, "USD", bob));

        // Find path from alice to mtgox
        auto alternatives = findPath (ledger_, alice, bob, {Currency ("USD")},
            Amount (1, "USD", mtgox), log.stream ());
        log << "Path find alternatives: " << alternatives;
        expectEquals (alternatives.size (), 1);

        auto const& alt = alternatives[0u];
        expectEquals (alt["paths_canonical"].size (), 0);
        expectEquals (alt["paths_computed"].size (), 0);

        auto const& srcAmount = alt[jss::source_amount];
        expectEquals (srcAmount[jss::currency], "USD");
        expectEquals (srcAmount[jss::value], "1");
        expectEquals (srcAmount[jss::issuer], alice.pk.humanAccountID ());
    }

    void
    test_alternative_path_consume_both ()
    {
        testcase ("path find consume all");
        initAccounts ({"alice", "bob", "mtgox", "bitstamp"});

        // Set credit limits
        auto& alice = trusts ("alice", "USD", {{"mtgox", 600}, {"bitstamp", 800}});
        auto& bob = trusts ("bob", "USD", {{"mtgox", 700}, {"bitstamp", 900}});
        auto& mtgox = accounts_["mtgox"];
        auto& bitstamp = accounts_["bitstamp"];

        // Distribute funds
        pay (mtgox, alice, "USD", "70", ledger_);
        pay (bitstamp, alice, "USD", "70", ledger_);

        // Verify balances
        verifyBalance (ledger_, alice, Amount (70, "USD", mtgox));
        verifyBalance (ledger_, alice, Amount (70, "USD", bitstamp));

        // Payment with path
        payWithPath (alice, bob, "USD", "140", ledger_);

        // Verify balances
        verifyBalance (ledger_, alice, Amount (0, "USD", mtgox));
        verifyBalance (ledger_, alice, Amount (0, "USD", bitstamp));
        verifyBalance (ledger_, bob, Amount (70, "USD", mtgox));
        verifyBalance (ledger_, bob, Amount (70, "USD", bitstamp));
        verifyBalance (ledger_, mtgox, Amount (0, "USD", alice));
        verifyBalance (ledger_, mtgox, Amount (-70, "USD", bob));
        verifyBalance (ledger_, bitstamp, Amount (0, "USD", alice));
        verifyBalance (ledger_, bitstamp, Amount (-70, "USD", bob));
    }

    void
    test_alternative_paths_consume_best_transfer ()
    {
        testcase ("alternative paths - consume best transfer");
        initAccounts ({"alice", "bob", "mtgox", "bitstamp"});

        // Set credit limits
        auto& alice = trusts ("alice", "USD", {{"mtgox", 600}, {"bitstamp", 800}});
        auto& bob = trusts ("bob", "USD", {{"mtgox", 700}, {"bitstamp", 900}});
        auto& mtgox = accounts_["mtgox"];
        auto& bitstamp = accounts_["bitstamp"];

        // Set transfer rate
        setTransferRate (bitstamp, ledger_, 1e9 * 1.1);

        // Distribute funds
        pay (mtgox, alice, "USD", "70", ledger_);
        pay (bitstamp, alice, "USD", "70", ledger_);

        // Verify balances
        verifyBalance (ledger_, alice, Amount (70, "USD", mtgox));
        verifyBalance (ledger_, alice, Amount (70, "USD", bitstamp));

        // Payment with path
        payWithPath (alice, bob, "USD", "70", ledger_);

        // Verify balances
        verifyBalance (ledger_, alice, Amount (0, "USD", mtgox));
        verifyBalance (ledger_, alice, Amount (70, "USD", bitstamp));
        verifyBalance (ledger_, bob, Amount (70, "USD", mtgox));
        verifyBalance (ledger_, bob, Amount (0, "USD", bitstamp));
        verifyBalance (ledger_, mtgox, Amount (0, "USD", alice));
        verifyBalance (ledger_, mtgox, Amount (-70, "USD", bob));
        verifyBalance (ledger_, bitstamp, Amount (-70, "USD", alice));
        verifyBalance (ledger_, bitstamp, Amount (0, "USD", bob));
    }

    void
    test_alternative_paths_consume_best_transfer_first ()
    {
        testcase ("alternative paths - consume best transfer first");
        initAccounts ({"alice", "bob", "mtgox", "bitstamp"});

        // Set credit limits
        auto& alice = trusts ("alice", "USD", {{"mtgox", 600}, {"bitstamp", 800}});
        auto& bob = trusts ("bob", "USD", {{"mtgox", 700}, {"bitstamp", 900}});
        auto& mtgox = accounts_["mtgox"];
        auto& bitstamp = accounts_["bitstamp"];

        // Set transfer rate
        setTransferRate (bitstamp, ledger_, 1e9 * 1.1);

        // Distribute funds
        pay (mtgox, alice, "USD", "70", ledger_);
        pay (bitstamp, alice, "USD", "70", ledger_);

        // Verify balances
        verifyBalance (ledger_, alice, Amount (70, "USD", mtgox));
        verifyBalance (ledger_, alice, Amount (70, "USD", bitstamp));

        // Payment with path
        payWithPath (alice, bob, "USD", "77", Amount (100, "USD", alice), ledger_);

        // Verify balances
        verifyBalance (ledger_, alice, Amount (0, "USD", mtgox));
        verifyBalance (ledger_, alice, Amount (62.3, "USD", bitstamp));
        verifyBalance (ledger_, bob, Amount (70, "USD", mtgox));
        verifyBalance (ledger_, bob, Amount (7, "USD", bitstamp));
        verifyBalance (ledger_, mtgox, Amount (0, "USD", alice));
        verifyBalance (ledger_, mtgox, Amount (-70, "USD", bob));
        verifyBalance (ledger_, bitstamp, Amount (-62.3, "USD", alice));
        verifyBalance (ledger_, bitstamp, Amount (-7, "USD", bob));
    }

    void
    test_alternative_paths_limit_returned_paths_to_best_quality ()
    {
        testcase ("alternative paths - limit returned paths to best quality");
        initAccounts ({"alice", "bob", "carol", "dan", "mtgox", "bitstamp"});

        // Set credit limits
        auto& alice = trusts ("alice", "USD", {{"carol", 800}, {"dan", 800},
            {"mtgox", 800}, {"bitstamp", 800}});
        auto& bob = trusts ("bob", "USD", {{"carol", 800}, {"dan", 800},
            {"mtgox", 800}, {"bitstamp", 800}});
        auto& carol = accounts_["carol"];
        auto& dan = trusts ("dan", "USD", {{"alice", 800}, {"bob", 800}});
        auto& mtgox = accounts_["mtgox"];
        auto& bitstamp = accounts_["bitstamp"];

        // Set transfer rate
        setTransferRate (carol, ledger_, 1e9 * 1.1);

        // Distribute funds
        pay (carol, alice, "USD", "100", ledger_);
        pay (mtgox, alice, "USD", "100", ledger_);
        pay (bitstamp, alice, "USD", "100", ledger_);

        // Verify balances
        verifyBalance (ledger_, alice, Amount (100, "USD", carol));
        verifyBalance (ledger_, alice, Amount (100, "USD", mtgox));
        verifyBalance (ledger_, alice, Amount (100, "USD", bitstamp));

        // Find path from alice to bob
        auto alternatives = findPath (ledger_, alice, bob, {Currency ("USD")},
            Amount (5, "USD", bob), log.stream ());
        log << "Path find alternatives: " << alternatives;
        expectEquals (alternatives.size (), 1);
        expectEquals (alternatives[0u]["paths_canonical"].size (), 0);
    }

    void
    test_issues_path_negative_issue ()// #5
    {
        testcase ("issues path negative: Issue #5");
        initAccounts ({"alice", "bob", "carol", "dan"});

        // Set credit limits
        auto& alice = trusts ("alice", "USD", {{"bob", 100}});
        auto& bob = accounts_["bob"];
        auto& carol = trusts ("carol", "USD", {{"bob", 100}});
        auto& dan = trusts ("dan", "USD", {{"alice", 100}, {"bob", 100},
            {"carol", 100}});

        // Distribute funds
        pay (bob, carol, "USD", "75", ledger_);

        // Verify balances
        verifyBalance (ledger_, bob, Amount (-75, "USD", carol));
        verifyBalance (ledger_, carol, Amount (75, "USD", bob));

        // Find path from alice to bob
        auto alternatives = findPath (ledger_, alice, bob, {Currency ("USD")},
            Amount (25, "USD", bob), log.stream ());
        log << "Path find alternatives: " << alternatives;
        expectEquals (alternatives.size (), 0);

        // alice fails to send to bob
        alternatives = findPath (ledger_, alice, bob, { Currency ("USD") },
            Amount (25, "USD", alice), log.stream ());
        log << "Path find alternatives: " << alternatives;
        //  callback(m.engine_result != = 'tecPATH_DRY');

        // Verify balances
        verifyBalance (ledger_, alice, Amount (0, "USD", bob));
        verifyBalance (ledger_, alice, Amount (0, "USD", dan));
        verifyBalance (ledger_, bob, Amount (0, "USD", alice));
        verifyBalance (ledger_, bob, Amount (-75, "USD", carol));
        verifyBalance (ledger_, bob, Amount (0, "USD", dan));
        verifyBalance (ledger_, carol, Amount (75, "USD", bob));
        verifyBalance (ledger_, carol, Amount (0, "USD", dan));
        verifyBalance (ledger_, dan, Amount (0, "USD", alice));
        verifyBalance (ledger_, dan, Amount (0, "USD", bob));
        verifyBalance (ledger_, dan, Amount (0, "USD", carol));
    }

    /* alice -- limit 40 --> bob
       alice --> carol --> dan --> bob
       Balance of 100 USD Bob - Balance of 37 USD -> Rod
    */
    void
    test_path_negative ()
    {
        testcase ("path negative: ripple-client issue #23: smaller");
        initAccounts ({"alice", "bob", "carol", "dan"});

        // Set credit limits
        auto& alice = accounts_["alice"];
        auto& bob = trusts ("bob", "USD", {{"alice", 40}, {"dan", 20}});
        auto& carol = trusts ("carol", "USD", {{"alice", 20}});
        auto& dan = trusts ("dan", "USD", {{"carol", 20}});

        // Payment with path
        payWithPath (alice, bob, "USD", "55", ledger_);

        // Verify balances
        verifyBalance (ledger_, alice, Amount (-40, "USD", bob));
        verifyBalance (ledger_, bob, Amount (40, "USD", alice));
        verifyBalance (ledger_, bob, Amount (15, "USD", dan));
        verifyBalance (ledger_, dan, Amount (-15, "USD", bob));
    }

    /* alice -120 USD-> amazon -25 USD-> bob
       alice -25 USD-> carol -75 USD -> dan -100 USD-> bob
    */
    void
    test_path_negative_2 ()
    {
        testcase ("path negative: ripple-client issue #23: larger");
        initAccounts ({"alice", "bob", "carol", "dan", "amazon"});

        // Set credit limits
        auto& amazon = trusts ("amazon", "USD", {{"alice", 120}});
        auto& alice = accounts_["alice"];
        auto& bob = trusts ("bob", "USD", {{"amazon", 25}, {"dan", 100}});
        auto& carol = trusts ("carol", "USD", {{"alice", 25}});
        auto& dan = trusts ("dan", "USD", {{"carol", 75}});

        // Payment with path
        payWithPath (alice, bob, "USD", "50", ledger_);

        // Verify balances
        verifyBalance (ledger_, alice, Amount (-25, "USD", amazon));
        verifyBalance (ledger_, alice, Amount (-25, "USD", carol));
        verifyBalance (ledger_, bob, Amount (25, "USD", amazon));
        verifyBalance (ledger_, bob, Amount (25, "USD", dan));
        verifyBalance (ledger_, carol, Amount (25, "USD", alice));
        verifyBalance (ledger_, carol, Amount (-25, "USD", dan));
        verifyBalance (ledger_, dan, Amount (25, "USD", carol));
        verifyBalance (ledger_, dan, Amount (-25, "USD", bob));
    }

    /* carol holds mtgoxAUD, sells mtgoxAUD for XRP
       bob will hold mtgoxAUD
       alice pays bob mtgoxAUD using XRP
    */
    void
    test_via_offers_via_gateway ()
    {
        testcase ("via offers via gateway");
        initAccounts ({"alice", "bob", "carol", "mtgox"});

        // Set credit limits
        auto& alice = accounts_["alice"];
        auto& bob = trusts ("bob", "AUD", { { "mtgox", 100 } });
        auto& carol = trusts ("carol", "AUD", {{"mtgox", 100}});
        auto& mtgox = accounts_["mtgox"];

        // Set transfer rate
        setTransferRate (mtgox, ledger_, 1005000000);

        // Distribute funds
        pay (mtgox, carol, "AUD", "50", ledger_);

        // Carol create offer
        createOffer (carol, 50000000, Amount (50, "AUD", mtgox), ledger_, true);

        // Alice sends bob 10/AUD/mtgox using XRP
        payWithPath (alice, bob, "XRP", "AUD", "10", 100000000, ledger_);

        // Verify balances
        verifyBalance (ledger_, bob, Amount (10, "AUD", mtgox));
        verifyBalance (ledger_, carol, Amount (39.95, "AUD", mtgox));

        // Find path from alice to bob
        // acct 1 sent a 25 usd iou to acct 2
        auto alternatives = findPath (ledger_, alice, bob, {Currency ("USD")},
            Amount (25, "USD", alice), log.stream ());
        log << "Path find alternatives: " << alternatives;
        expectEquals (alternatives.size (), 0);
    }

    void
    test_indirect_paths_path_find ()
    {
        testcase ("Indirect paths path find");
        initAccounts ({"alice", "bob", "carol"});

        // Set credit limits
        auto& alice = accounts_["alice"];
        auto& bob = trusts ("bob", "USD", {{"alice", 1000}});
        auto& carol = trusts ("carol", "USD", {{"bob", 2000}});

        // Find path from alice to carol
        auto alternatives = findPath (ledger_, alice, carol, {Currency ("USD")},
            Amount (5, "USD", carol), log.stream ());
        log << "Path find alternatives: " << alternatives;
        expectEquals (alternatives.size (), 1);
        expectEquals (alternatives[0u]["paths_canonical"].size (), 0);
    }

    void
    test_indirect_paths_quality_paths ()
    {
        testcase ("Indirect paths quality set and test");
        initAccounts ({"alice", "bob"});

        // Set credit limits extended
        auto& alice = accounts_["alice"];
        auto& bob = accounts_["bob"];
        trust (bob, alice, "USD", 1000, 2000, 1.4 * 1e9, ledger_);

        // Verify credit limits extended
        verifyLimit (ledger_, bob, Amount (1000, "USD", alice), 2000, 1.4 * 1e9);
    }

    void
    test_indirect_paths_quality_payment ()
    {
        testcase ("Indirect paths quality payment (BROKEN DUE TO ROUNDING)");
        initAccounts ({"alice", "bob"});

        // Set credit limits extended
        auto& alice = accounts_["alice"];
        auto& bob = accounts_["bob"];
        trust (bob, alice, "USD", 1000, .9 * 1e9, 1.1 * 1e9, ledger_);

        // Verify credit limits extended
        verifyLimit (ledger_, bob, Amount (1000, "USD", alice), .9 * 1e9,
            1.1 * 1e9);

        // Payment with path
        // payWithPath (alice, bob, "USD", "100", Amount (120, "USD", alice),
        //   ledger_);
    }

    void
    test_trust_normal_clear ()
    {
        testcase ("trust normal clear");
        initAccounts ({"alice", "bob"});

        // Set credit limits
        auto& alice = trusts ("alice", "USD", {{ "bob", 1000 }});
        auto& bob = trusts ("bob", "USD", {{"alice", 1000}});

        // Verify credit limits
        verifyLimit (ledger_, bob, Amount (1000, "USD", alice));

        // Clear credit limits
        trust (alice, bob, "USD", 0, ledger_);
        trust (bob, alice, "USD", 0, ledger_);

        // Verify credit limits
        try
        {
            verifyLimit (ledger_, bob, Amount (0, "USD", alice));
        }
        catch (std::runtime_error const& e)
        {
            if (std::string (e.what ()) != "!sle")
                throw std::runtime_error (e.what ());
        }
    }

    void
    test_trust_auto_clear_2 ()
    {
        testcase ("trust auto clear");
        initAccounts ({"alice", "bob"});

        // Set credit limits
        auto& alice = trusts ("alice", "USD", {{ "bob", 1000 }});
        auto& bob = accounts_["bob"];

        // Distribute funds
        pay (bob, alice, "USD", "50", ledger_);

        // Clear credit limits
        trust (alice, bob, "USD", 0, ledger_);

        // Verify credit limits
        verifyLimit (ledger_, alice, Amount (0, "USD", bob));

        // Return funds
        pay (alice, bob,"USD", "50", ledger_);

        // Verify credit limit gone
        try
        {
            verifyLimit (ledger_, bob, Amount (0, "USD", alice));
        }
        catch (std::runtime_error const& e)
        {
            if (std::string (e.what ()) != "!sle")
                throw std::runtime_error (e.what ());
        }
    }

public:
    void run ()
    {
//        deprecatedLogs ()["RippleCalc"].severity (
//            beast::Journal::Severity::kAll);

        test_no_direct_path_no_intermediary_no_alternatives ();
        test_direct_path_no_intermediary ();
        test_payment_auto_path_find_using_build_path ();
        test_path_find ();
        test_path_find_cosume_all ();
        test_alternative_path_consume_both ();
        test_alternative_paths_consume_best_transfer ();
        test_alternative_paths_consume_best_transfer_first ();
        test_alternative_paths_limit_returned_paths_to_best_quality ();
        test_issues_path_negative_issue ();
        test_path_negative ();
        test_path_negative_2 ();
        test_via_offers_via_gateway ();
        test_indirect_paths_path_find ();
        test_indirect_paths_quality_paths ();
        test_indirect_paths_quality_payment ();
        test_trust_normal_clear ();
        test_trust_auto_clear_2 ();
    }
};

BEAST_DEFINE_TESTSUITE (Path, app, ripple);

} // test
} // ripple
