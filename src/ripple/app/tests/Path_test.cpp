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
#include <ripple/crypto/KeyType.h>
#include <ripple/json/json_writer.h>
#include <ripple/basics/TestSuite.h>

// probably going to be moved to common_ledger.h

namespace ripple {
namespace test {

class Path_test : public TestSuite
{
    void
    test_no_direct_path_no_intermediary_no_alternatives()
    {
        testcase("no direct path no intermediary no alternatives");
        std::uint64_t const xrp = std::mega::num;

        auto master = createAccount("masterpassphrase", KeyType::ed25519);

        Ledger::pointer LCL;
        Ledger::pointer ledger;
        std::tie(LCL, ledger) = createGenesisLedger(100000 * xrp, master);

        auto accounts = createAndFundAccounts(master, { "alice", "bob" }, 
            KeyType::ed25519, 10000 * xrp, ledger);
        auto& alice = accounts["alice"];
        auto& bob = accounts["bob"];
        expectNotEquals(alice.pk.humanAccountID(), master.pk.humanAccountID());
        
        auto alternatives = findPath(ledger, alice, bob, { Currency("USD") },
            Amount(5, "USD", alice), log.stream());
        log << "ripplePathFind alternatives: " << alternatives;

        expectEquals(alternatives.size(), 0);
    }

    void
    test_direct_path_no_intermediary()
    {
        testcase("direct path no intermediary");
        std::uint64_t const xrp = std::mega::num;

        auto master = createAccount("masterpassphrase", KeyType::ed25519);

        Ledger::pointer LCL;
        Ledger::pointer ledger;
        std::tie(LCL, ledger) = createGenesisLedger(100000 * xrp, master);

        // Create accounts
        auto accounts = createAndFundAccounts(master, { "alice", "bob" },
            KeyType::ed25519, 10000 * xrp, ledger);
        auto& alice = accounts["alice"];
        auto& bob = accounts["bob"];
        expectNotEquals(alice.pk.humanAccountID(), master.pk.humanAccountID());

        // Set credit limit
        makeTrustSet(bob, alice, "USD", 700, ledger);

        // Find path from alice to bob
        auto alternatives = findPath(ledger, alice, bob, { Currency("USD") },
            Amount(5, "USD", bob), log.stream());
        log << "ripplePathFind alternatives: " << alternatives;

        expectEquals(alternatives.size(), 1);
        auto alt = alternatives[0u];
        expectEquals(alt[jss::paths_canonical].size(), 0);
        expectEquals(alt[jss::paths_computed].size(), 0);
        auto srcAmount = alt[jss::source_amount];
        expectEquals(srcAmount[jss::currency], "USD");
        expectEquals(srcAmount[jss::value], "5");
        expectEquals(srcAmount[jss::issuer], alice.pk.humanAccountID());
    }

    void
    test_payment_auto_path_find_using_build_path()
    {
        testcase("payment auto path find (using build_path)");

        std::uint64_t const xrp = std::mega::num;

        auto master = createAccount("masterpassphrase", KeyType::ed25519);

        Ledger::pointer LCL;
        Ledger::pointer ledger;
        std::tie(LCL, ledger) = createGenesisLedger(100000 * xrp, master);

        // Create accounts
        auto accounts = createAndFundAccounts(master, { "alice", "bob", "mtgox" },
            KeyType::ed25519, 10000 * xrp, ledger);
        auto& alice = accounts["alice"];
        auto& bob = accounts["bob"];
        auto& mtgox = accounts["mtgox"];
        expectNotEquals(alice.pk.humanAccountID(), master.pk.humanAccountID());

        // Set credit limits
        makeTrustSet(alice, mtgox, "USD", 600, ledger);
        makeTrustSet(bob, mtgox, "USD", 700, ledger);

        // Distribute funds.
        makeAndApplyPayment(mtgox, alice, "USD", "70", ledger);

        verifyBalance(ledger, alice, Amount(70, "USD", mtgox));

        // Payment with path.
        makeAndApplyPayment(alice, bob, "USD", "24", ledger, build_path);

        // Verify balances
        verifyBalance(ledger, alice, Amount(46, "USD", mtgox));
        verifyBalance(ledger, mtgox, Amount(-46, "USD", alice));
        verifyBalance(ledger, mtgox, Amount(-24, "USD", bob));
        verifyBalance(ledger, bob, Amount(24, "USD", mtgox));
    }

    void
    test_path_find()
    {
        testcase("path find");

        std::uint64_t const xrp = std::mega::num;

        auto master = createAccount("masterpassphrase", KeyType::ed25519);

        Ledger::pointer LCL;
        Ledger::pointer ledger;
        std::tie(LCL, ledger) = createGenesisLedger(100000 * xrp, master);

        // Create accounts
        auto accounts = createAndFundAccounts(master, { "alice", "bob", "mtgox" },
            KeyType::ed25519, 10000 * xrp, ledger);
        auto& alice = accounts["alice"];
        auto& bob = accounts["bob"];
        auto& mtgox = accounts["mtgox"];
        expectNotEquals(alice.pk.humanAccountID(), master.pk.humanAccountID());

        // Set credit limits
        makeTrustSet(alice, mtgox, "USD", 600, ledger);
        makeTrustSet(bob, mtgox, "USD", 700, ledger);

        // Distribute funds.
        makeAndApplyPayment(mtgox, alice, "USD", "70", ledger);
        makeAndApplyPayment(mtgox, bob, "USD", "50", ledger);

        verifyBalance(ledger, alice, Amount(70, "USD", mtgox));
        verifyBalance(ledger, bob, Amount(50, "USD", mtgox));

        // Find path from alice to mtgox
        auto alternatives = findPath(ledger, alice, bob, { Currency("USD") },
            Amount(5, "USD", mtgox), log.stream());
        log << "Path find alternatives: " << alternatives;
        expectEquals(alternatives.size(), 1);
        auto alt = alternatives[0u];
        expectEquals(alt["paths_canonical"].size(), 0);
        expectEquals(alt["paths_computed"].size(), 1);
        auto computedPaths = alt["paths_computed"];
        expectEquals(computedPaths.size(), 1);
        auto computedPath = computedPaths[0u];
        expectEquals(computedPath.size(), 1);
        auto computedPathStep = computedPath[0u];
        expectEquals(computedPathStep[jss::account], 
            mtgox.pk.humanAccountID());
        expectEquals(computedPathStep[jss::type], 1);
        expectEquals(computedPathStep[jss::type_hex],
            "0000000000000001");
        auto srcAmount = alt[jss::source_amount];
        expectEquals(srcAmount[jss::currency], "USD");
        expectEquals(srcAmount[jss::value], "5");
        expectEquals(srcAmount[jss::issuer], 
            alice.pk.humanAccountID());
    }


public:
    void run()
    {
        test_no_direct_path_no_intermediary_no_alternatives();
        test_direct_path_no_intermediary();
        test_payment_auto_path_find_using_build_path();
        test_path_find();
    }
};

BEAST_DEFINE_TESTSUITE(Path, ripple_app, ripple);

} // test
} // ripple
