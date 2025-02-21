//------------------------------------------------------------------------------
/*
  This file is part of rippled: https://github.com/ripple/rippled
  Copyright (c) 2024 Ripple Labs Inc.
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
#include <test/jtx/AMM.h>
#include <test/jtx/AMMTest.h>
#include <test/jtx/AccountPermission.h>
#include <test/jtx/Oracle.h>
#include <test/jtx/PathSet.h>
#include <test/jtx/check.h>
#include <test/jtx/xchain_bridge.h>
#include <xrpl/basics/random.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/PayChan.h>
#include <xrpl/protocol/Quality.h>
#include <xrpl/protocol/jss.h>
#include <chrono>

namespace ripple {
namespace test {
class AccountPermission_test : public beast::unit_test::suite
{
    void
    testFeatureDisabled(FeatureBitset features)
    {
        testcase("test featureAccountPermission is not enabled");
        using namespace jtx;

        Env env(*this, features);
        Account gw{"gateway"};
        Account alice{"alice"};
        Account bob{"bob"};
        env.fund(XRP(1000000), gw, alice, bob);
        env.close();

        // can not set account permission when feature disabled
        env(account_permission::accountPermissionSet(gw, alice, {"Payment"}),
            ter(temDISABLED));

        // can not send transaction on behalf of other account when feature
        // disabled, onBehalfOf, delegateSequence, and delegateTicketSequence
        // should not appear in the request.
        env(pay(bob, alice, XRP(50)), onBehalfOf(gw), ter(temDISABLED));
        env(pay(bob, alice, XRP(50)), delegateSequence(1), ter(temDISABLED));
        env(pay(bob, alice, XRP(50)),
            delegateTicketSequence(1),
            ter(temDISABLED));
    }

    void
    testInvalidRequest(FeatureBitset features)
    {
        testcase("test invalid request");
        using namespace jtx;

        Env env(*this, features);
        Account gw{"gateway"};
        Account alice{"alice"};
        env.fund(XRP(100000), gw, alice);
        env.close();

        // when permissions size exceeds the limit 10, should return
        // temARRAY_TOO_LARGE.
        {
            env(account_permission::accountPermissionSet(
                    gw,
                    alice,
                    {"Payment",
                     "EscrowCreate",
                     "EscrowFinish",
                     "EscrowCancel",
                     "CheckCreate",
                     "CheckCash",
                     "CheckCancel",
                     "DepositPreauth",
                     "TrustSet",
                     "NFTokenMint",
                     "NFTokenBurn"}),
                ter(temARRAY_TOO_LARGE));
        }

        // alice can not authorize herself
        {
            env(account_permission::accountPermissionSet(
                    alice, alice, {"Payment"}),
                ter(temMALFORMED));
        }

        // when provided permissions contains some permission which does not
        // exists.
        {
            try
            {
                env(account_permission::accountPermissionSet(
                    gw, alice, {"Payment1"}));
            }
            catch (std::exception const& e)
            {
                BEAST_EXPECT(
                    e.what() ==
                    std::string("invalidParamsError at "
                                "'tx_json.Permissions.[0].Permission'. Field "
                                "'tx_json.Permissions.[0].Permission."
                                "PermissionValue' has invalid data."));
            }
        }

        // when provided permissions contains duplicate values, should return
        // temMALFORMED.
        {
            env(account_permission::accountPermissionSet(
                    gw,
                    alice,
                    {"Payment",
                     "EscrowCreate",
                     "EscrowFinish",
                     "TrustlineAuthorize",
                     "CheckCreate",
                     "TrustlineAuthorize"}),
                ter(temMALFORMED));
        }

        // when authorizing account which does not exist, should return
        // terNO_ACCOUNT.
        {
            env(account_permission::accountPermissionSet(
                    gw, Account("unknown"), {"Payment"}),
                ter(terNO_ACCOUNT));
        }

        // for security reasons, AccountSet, SetRegularKey, SignerListSet,
        // AccountPermissionSet are prohibited to be delegated to other accounts
        {
            auto testProhibitedTrans = [&](std::string const& permission) {
                try
                {
                    env(account_permission::accountPermissionSet(
                        gw, alice, {permission}));
                }
                catch (std::exception const& e)
                {
                    BEAST_EXPECT(
                        e.what() ==
                        std::string(
                            "invalidParamsError at "
                            "'tx_json.Permissions.[0].Permission'. Field "
                            "'tx_json.Permissions.[0].Permission."
                            "PermissionValue' has invalid data."));
                }
            };

            testProhibitedTrans("SetRegularKey");
            testProhibitedTrans("AccountSet");
            testProhibitedTrans("SignerListSet");
            testProhibitedTrans("AccountPermissionSet");
            testProhibitedTrans("AccountDelete");
        }
    }

    void
    testReserve(FeatureBitset features)
    {
        testcase("test reserve");
        using namespace jtx;

        // test reserve for AccountPermissionSet
        {
            Env env(*this, features);
            Account alice{"alice"};
            Account bob{"bob"};
            Account carol{"carol"};

            env.fund(drops(env.current()->fees().accountReserve(0)), alice);
            env.fund(
                drops(env.current()->fees().accountReserve(1)), bob, carol);
            env.close();

            // alice does not have enough reserve to create account permission
            env(account_permission::accountPermissionSet(
                    alice, bob, {"Payment"}),
                ter(tecINSUFFICIENT_RESERVE));

            // bob has enough reserve
            env(account_permission::accountPermissionSet(
                bob, alice, {"Payment"}));
            env.close();

            // now bob create another account permission, he does not have
            // enough reserve
            env(account_permission::accountPermissionSet(
                    bob, carol, {"Payment"}),
                ter(tecINSUFFICIENT_RESERVE));
        }

        // test reserve when sending transaction on behalf of other account
        {
            Env env(*this, features);
            Account alice{"alice"};
            Account bob{"bob"};

            env.fund(drops(env.current()->fees().accountReserve(1)), alice);
            env.fund(drops(env.current()->fees().accountReserve(2)), bob);
            env.close();

            // alice gives bob permission
            env(account_permission::accountPermissionSet(
                alice, bob, {"DIDSet", "DIDDelete"}));

            // bob set DID on behalf of alice, but alice does not have enough
            // reserve
            env(did::set(bob),
                did::uri("uri"),
                onBehalfOf(alice),
                ter(tecINSUFFICIENT_RESERVE));

            // bob can set DID for himself because he has enough reserve
            env(did::set(bob), did::uri("uri"));
            env.close();
        }
    }

    void
    testAccountDelete(FeatureBitset features)
    {
        testcase("test delete account");
        using namespace jtx;

        Env env(*this, features);
        Account alice{"alice"};
        Account bob{"bob"};
        env.fund(XRP(100000), alice, bob);
        env.close();

        env(account_permission::accountPermissionSet(alice, bob, {"Payment"}));
        env.close();
        BEAST_EXPECT(env.closed()->exists(
            keylet::accountPermission(alice.id(), bob.id())));

        for (std::uint32_t i = 0; i < 256; ++i)
            env.close();

        auto const aliceBalance = env.balance(alice);
        auto const bobBalance = env.balance(bob);

        // alice deletes account
        auto const deleteFee = drops(env.current()->fees().increment);
        env(acctdelete(alice, bob), fee(deleteFee));
        env.close();

        BEAST_EXPECT(!env.closed()->exists(keylet::account(alice.id())));
        BEAST_EXPECT(!env.closed()->exists(keylet::ownerDir(alice.id())));
        BEAST_EXPECT(env.balance(bob) == bobBalance + aliceBalance - deleteFee);

        BEAST_EXPECT(!env.closed()->exists(
            keylet::accountPermission(alice.id(), bob.id())));
    }

    void
    testAccountPermissionSet(FeatureBitset features)
    {
        testcase("test valid request creating, updating, deleting permissions");
        using namespace jtx;

        Env env(*this, features);
        Account gw{"gateway"};
        Account alice{"alice"};
        env.fund(XRP(100000), gw, alice);
        env.close();

        auto const permissions = std::list<std::string>{
            "Payment",
            "EscrowCreate",
            "EscrowFinish",
            "TrustlineAuthorize",
            "CheckCreate"};
        env(account_permission::accountPermissionSet(gw, alice, permissions));
        env.close();

        // this lambda function is used to error message when the user tries to
        // get ledger entry with invalid parameters.
        auto testInvalidParams =
            [&](std::optional<std::string> const& account,
                std::optional<std::string> const& authorize) -> std::string {
            Json::Value jvParams;
            std::string error;
            jvParams[jss::ledger_index] = jss::validated;
            if (account)
                jvParams[jss::account_permission][jss::account] = *account;
            if (authorize)
                jvParams[jss::account_permission][jss::authorize] = *authorize;
            auto const& response =
                env.rpc("json", "ledger_entry", to_string(jvParams));
            if (response[jss::result].isMember(jss::error))
                error = response[jss::result][jss::error].asString();
            return error;
        };

        // get ledger entry with invalid parameters should return error.
        BEAST_EXPECT(
            testInvalidParams(std::nullopt, alice.human()) ==
            "malformedRequest");
        BEAST_EXPECT(
            testInvalidParams(gw.human(), std::nullopt) == "malformedRequest");
        BEAST_EXPECT(
            testInvalidParams("-", alice.human()) == "malformedAccount");
        BEAST_EXPECT(
            testInvalidParams(gw.human(), "-") == "malformedAuthorize");

        // this lambda function is used to compare the json value of ledger
        // entry response with the given list of permission strings.
        auto comparePermissions = [&](Json::Value const& jle,
                                      std::list<std::string> const& permissions,
                                      Account const& account,
                                      Account const& authorize) {
            BEAST_EXPECT(
                !jle[jss::result].isMember(jss::error) &&
                jle[jss::result].isMember(jss::node));
            BEAST_EXPECT(
                jle[jss::result][jss::node]["LedgerEntryType"] ==
                jss::AccountPermission);
            BEAST_EXPECT(
                jle[jss::result][jss::node][jss::Account] == account.human());
            BEAST_EXPECT(
                jle[jss::result][jss::node][jss::Authorize] ==
                authorize.human());

            auto const& jPermissions =
                jle[jss::result][jss::node][jss::Permissions];
            unsigned i = 0;
            for (auto const& permission : permissions)
            {
                auto const granularVal =
                    Permission::getInstance().getGranularValue(permission);
                if (granularVal)
                    BEAST_EXPECT(
                        jPermissions[i][jss::Permission]
                                    [jss::PermissionValue] == *granularVal);
                else
                {
                    auto const transVal =
                        TxFormats::getInstance().findTypeByName(permission);
                    BEAST_EXPECT(
                        jPermissions[i][jss::Permission]
                                    [jss::PermissionValue] == transVal + 1);
                }
                i++;
            }
        };

        // get ledger entry with valid parameter
        comparePermissions(
            account_permission::ledgerEntry(env, gw, alice),
            permissions,
            gw,
            alice);

        // gw update permission
        auto const newPermissions = std::list<std::string>{
            "Payment", "AMMCreate", "AMMDeposit", "AMMWithdraw"};
        env(account_permission::accountPermissionSet(
            gw, alice, newPermissions));
        env.close();

        // get ledger entry again, permissions should be updated to
        // newPermissions
        comparePermissions(
            account_permission::ledgerEntry(env, gw, alice),
            newPermissions,
            gw,
            alice);

        // gw delete all permissions delegated to alice, this will delete the
        // ledger entry
        env(account_permission::accountPermissionSet(gw, alice, {}));
        env.close();
        auto const jle = account_permission::ledgerEntry(env, gw, alice);
        BEAST_EXPECT(jle[jss::result][jss::error] == "entryNotFound");

        // alice can delegate permissions to gw as well
        env(account_permission::accountPermissionSet(alice, gw, permissions));
        env.close();
        comparePermissions(
            account_permission::ledgerEntry(env, alice, gw),
            permissions,
            alice,
            gw);
        auto const response = account_permission::ledgerEntry(env, gw, alice);
        // alice is not delegated any permissions by gw, should return
        // entryNotFound
        BEAST_EXPECT(response[jss::result][jss::error] == "entryNotFound");
    }

    void
    testDelegateSequenceAndTicket(FeatureBitset features)
    {
        testcase("test delegating sequence and ticket");
        using namespace jtx;

        Env env(*this, features);
        Account alice{"alice"};
        Account bob{"bob"};
        Account carol{"carol"};
        env.fund(XRP(10000), alice, bob, carol);
        env.close();

        env(account_permission::accountPermissionSet(
            alice, bob, {"CheckCreate"}));
        env.close();

        // add initial sequences and add sequence distance between alice and bob
        for (int i = 0; i < 20; i++)
        {
            env(check::create(alice, carol, XRP(1)));
        }
        env(check::create(bob, carol, XRP(1)));
        env.close();
        auto aliceSequence = env.seq(alice);
        auto bobSequence = env.seq(bob);

        // non existing delegating account
        Account bad{"bad"};
        env(check::create(bob, carol, XRP(1)),
            onBehalfOf(bad),
            delegateSequence(1),
            ter(terNO_ACCOUNT));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSequence);
        BEAST_EXPECT(env.seq(bob) == bobSequence);

        // missing delegating sequence
        env(check::create(bob, carol, XRP(1)),
            onBehalfOf(alice),
            delegateSequence(none),
            ter(temBAD_SEQUENCE));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSequence);
        BEAST_EXPECT(env.seq(bob) == bobSequence);

        // delegating sequence smaller than current
        env(check::create(bob, carol, XRP(1)),
            onBehalfOf(alice),
            delegateSequence(1),
            ter(tefPAST_SEQ));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSequence);
        BEAST_EXPECT(env.seq(bob) == bobSequence);

        // delegating sequence larger than current
        env(check::create(bob, carol, XRP(1)),
            onBehalfOf(alice),
            delegateSequence(100),
            ter(terPRE_SEQ));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSequence);
        BEAST_EXPECT(env.seq(bob) == bobSequence);

        // delegating sequence is consumed after transaction success
        env(check::create(bob, carol, XRP(1)),
            onBehalfOf(alice),
            delegateSequence(aliceSequence),
            ter(tesSUCCESS));
        env.close();
        aliceSequence++;
        bobSequence++;
        BEAST_EXPECT(env.seq(alice) == aliceSequence);
        BEAST_EXPECT(env.seq(bob) == bobSequence);

        // delegating sequence is consumed if transaction calls
        // Transactor::reset(XRPAmount) and return some special tec codes
        env(check::create(bob, carol, XRP(1)),
            check::expiration(env.now()),
            onBehalfOf(alice),
            delegateSequence(autofill),
            ter(tecEXPIRED));
        env.close();
        aliceSequence++;
        bobSequence++;
        BEAST_EXPECT(env.seq(alice) == aliceSequence);
        BEAST_EXPECT(env.seq(bob) == bobSequence);

        // use both delegating sequence and delegating ticket
        env(check::create(bob, carol, XRP(1)),
            onBehalfOf(alice),
            delegateSequence(autofill),
            delegateTicketSequence(aliceSequence),
            ter(temSEQ_AND_TICKET));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSequence);
        BEAST_EXPECT(env.seq(bob) == bobSequence);

        // set delegating sequence to 0 without delegating tickcet
        env(check::create(bob, carol, XRP(1)),
            onBehalfOf(alice),
            delegateSequence(0),
            ter(tefPAST_SEQ));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSequence);
        BEAST_EXPECT(env.seq(bob) == bobSequence);

        // use current or future sequence as delegating ticket
        env(check::create(bob, carol, XRP(1)),
            onBehalfOf(alice),
            delegateSequence(0),
            delegateTicketSequence(aliceSequence),
            ter(terPRE_TICKET));
        env.close();
        env(check::create(bob, carol, XRP(1)),
            onBehalfOf(alice),
            delegateSequence(0),
            delegateTicketSequence(aliceSequence + 1),
            ter(terPRE_TICKET));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSequence);
        BEAST_EXPECT(env.seq(bob) == bobSequence);
        // proceed one sequence so terPRE_TICKET won't be retried
        env(check::create(alice, carol, XRP(1)));
        aliceSequence += 1;
        BEAST_EXPECT(env.seq(alice) == aliceSequence);

        // degelating ticket is consumed after transaction success
        env(ticket::create(alice, 1));
        env.close();
        auto aliceTicket = aliceSequence + 1;
        aliceSequence += 2;
        BEAST_EXPECT(env.seq(alice) == aliceSequence);
        env(check::create(bob, carol, XRP(1)),
            onBehalfOf(alice),
            delegateSequence(0),
            delegateTicketSequence(aliceTicket),
            ter(tesSUCCESS));
        env.close();
        bobSequence++;
        BEAST_EXPECT(env.seq(alice) == aliceSequence);
        BEAST_EXPECT(env.seq(bob) == bobSequence);

        // delegating ticket is consumed if transaction calls
        // Transactor::reset(XRPAmount) and return some special tec codes
        env(ticket::create(alice, 1));
        env.close();
        aliceTicket = aliceSequence + 1;
        aliceSequence += 2;
        BEAST_EXPECT(env.seq(alice) == aliceSequence);
        env(check::create(bob, carol, XRP(1)),
            check::expiration(env.now()),
            onBehalfOf(alice),
            delegateSequence(0),
            delegateTicketSequence(aliceTicket),
            ter(tecEXPIRED));
        env.close();
        bobSequence++;
        BEAST_EXPECT(env.seq(alice) == aliceSequence);
        BEAST_EXPECT(env.seq(bob) == bobSequence);

        // use an already consumed delegating ticket
        env(check::create(bob, carol, XRP(1)),
            onBehalfOf(alice),
            delegateSequence(0),
            delegateTicketSequence(aliceTicket),
            ter(tefNO_TICKET));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSequence);
        BEAST_EXPECT(env.seq(bob) == bobSequence);
    }

    void
    testAMM(FeatureBitset features)
    {
        testcase(
            "test AMMCreate, AMMDeposit, AMMWithdraw, AMMClawback, AMMVote, "
            "AMMDelete and AMMBid");
        using namespace jtx;

        // test AMMCreate, AMMDeposit, AMMWithdraw, AMMClawback
        {
            Env env(*this, features);
            Account gw{"gateway"};
            Account alice{"alice"};
            Account bob{"bob"};
            env.fund(XRP(1000000000), gw, alice, bob);
            env.close();

            env(fset(gw, asfAllowTrustLineClawback));
            env.close();
            env.require(flags(gw, asfAllowTrustLineClawback));

            auto const USD = gw["USD"];
            env.trust(USD(10000), alice);
            env(pay(gw, alice, USD(3000)));
            env.trust(USD(10000), bob);
            env(pay(gw, bob, USD(3000)));
            env.close();

            // alice delegates AMMCreate, AMMDeposit, AMMWithdraw to bob
            env(account_permission::accountPermissionSet(
                alice, bob, {"AMMCreate", "AMMDeposit", "AMMWithdraw"}));
            env.close();

            auto aliceXrpBalance = env.balance(alice, XRP);
            auto bobXrpBalance = env.balance(bob, XRP);

            AMM amm(env, bob, USD(1000), XRP(2000), alice, ter(tesSUCCESS));
            env.close();

            BEAST_EXPECT(amm.expectBalances(
                USD(1000), XRP(2000), IOUAmount{1414213562373095, -9}));

            // bob sends the AMMCreate on behalf of alice, so alice holds all
            // the lptokens, bob holds 0.
            BEAST_EXPECT(
                amm.expectLPTokens(alice, IOUAmount{1414213562373095, -9}));
            BEAST_EXPECT(amm.expectLPTokens(bob, IOUAmount(0)));

            // alice initially has 3000USD, 1000USD is deducted to create the
            // AMM pool, 2000USD left
            env.require(balance(alice, USD(2000)));
            env.require(balance(bob, USD(3000)));

            // alice spent 2000XRP to create the AMM
            env.require(balance(alice, aliceXrpBalance - XRP(2000)));
            // bob sent the transaction, bob pays the fee
            env.require(balance(bob, bobXrpBalance - XRP(50)));

            // update alice and bob balance variables
            aliceXrpBalance = env.balance(alice, XRP);
            bobXrpBalance = env.balance(bob, XRP);

            // bob deposit 1000USD/2000XRP on behalf of alice
            amm.deposit(
                bob,
                USD(1000),
                XRP(2000),
                std::nullopt,
                std::nullopt,
                ter(tesSUCCESS),
                alice);
            env.close();

            BEAST_EXPECT(amm.expectBalances(
                USD(2000), XRP(4000), IOUAmount{2828427124746190, -9}));

            // alice holds all the lptokens, and bob has 0 in the pool
            BEAST_EXPECT(
                amm.expectLPTokens(alice, IOUAmount{2828427124746190, -9}));
            BEAST_EXPECT(amm.expectLPTokens(bob, IOUAmount(0)));

            // alice spent another 1000USD and 2000XRP to deposit
            env.require(balance(alice, USD(1000)));
            env.require(balance(bob, USD(3000)));
            env.require(balance(alice, aliceXrpBalance - XRP(2000)));
            // bob sent the transaction, bob pays another 10 drop XRP fee
            env.require(balance(bob, bobXrpBalance - drops(10)));

            // update alice and bob balance variables
            aliceXrpBalance = env.balance(alice, XRP);
            bobXrpBalance = env.balance(bob, XRP);

            // bob can deposit for himself
            amm.deposit(bob, USD(1000), XRP(2000));
            env.close();

            BEAST_EXPECT(amm.expectBalances(
                USD(3000), XRP(6000), IOUAmount{4242640687119285, -9}));
            ;
            BEAST_EXPECT(
                amm.expectLPTokens(alice, IOUAmount{2828427124746190, -9}));
            BEAST_EXPECT(
                amm.expectLPTokens(bob, IOUAmount{1414213562373095, -9}));

            env.require(balance(alice, USD(1000)));
            env.require(balance(bob, USD(2000)));

            // alice's XRP balance keeps the same

            env.require(balance(alice, aliceXrpBalance));
            // bob spent 2000XRP to deposit and also pays 10 drops fee
            env.require(balance(bob, bobXrpBalance - XRP(2000) - drops(10)));

            // update alice and bob balance variables
            aliceXrpBalance = env.balance(alice, XRP);
            bobXrpBalance = env.balance(bob, XRP);

            // bob withdraw 1000USD/2000XRP on behalf of alice
            amm.withdraw(
                bob,
                USD(1000),
                XRP(2000),
                std::nullopt,
                ter(tesSUCCESS),
                alice);
            env.close();

            // the 1000USD/2000XRP is withdrawn from alice, so alice's
            // lptoken is deducted by half, bob's lptoken balance remains the
            // same.
            BEAST_EXPECT(amm.expectBalances(
                USD(2000), XRP(4000), IOUAmount{2828427124746190, -9}));
            ;
            BEAST_EXPECT(
                amm.expectLPTokens(alice, IOUAmount{1414213562373095, -9}));
            BEAST_EXPECT(
                amm.expectLPTokens(bob, IOUAmount{1414213562373095, -9}));

            // alice gets 1000 USD back so she has 2000 USD now
            env.require(balance(alice, USD(2000)));
            env.require(balance(bob, USD(2000)));

            // alice gets 2000 XRP back
            env.require(balance(alice, aliceXrpBalance + XRP(2000)));
            // bob pays 10 drops fee
            env.require(balance(bob, bobXrpBalance - drops(10)));

            // update alice and bob balance variables
            aliceXrpBalance = env.balance(alice, XRP);
            bobXrpBalance = env.balance(bob, XRP);

            // bob can withdraw 1000USD/2000XRP for himself
            amm.withdraw(bob, USD(1000), XRP(2000));
            env.close();

            BEAST_EXPECT(amm.expectBalances(
                USD(1000), XRP(2000), IOUAmount{1414213562373095, -9}));
            ;
            BEAST_EXPECT(
                amm.expectLPTokens(alice, IOUAmount{1414213562373095, -9}));
            BEAST_EXPECT(amm.expectLPTokens(bob, IOUAmount(0)));
            env.require(balance(alice, USD(2000)));
            env.require(balance(bob, USD(3000)));
            env.require(balance(alice, aliceXrpBalance));
            // bob gets 2000XRP back and pays 10 drops fee
            env.require(balance(bob, bobXrpBalance + XRP(2000) - drops(10)));

            // alice can not AMMClawback from herself on behalf of gw
            env(amm::ammClawback(alice, alice, USD, XRP, USD(1000), gw),
                ter(tecNO_PERMISSION));
            env.close();

            // gw give permission to alice for AMMClawback transaction
            env(account_permission::accountPermissionSet(
                gw, alice, {"AMMClawback"}));
            env.close();

            aliceXrpBalance = env.balance(alice, XRP);
            bobXrpBalance = env.balance(bob, XRP);

            // now alice can AMMClawback from herself onbehalf of gw
            env(amm::ammClawback(alice, alice, USD, XRP, USD(500), gw));
            env.close();

            BEAST_EXPECT(amm.expectBalances(
                USD(500), XRP(1000), IOUAmount{7071067811865475, -10}));
            BEAST_EXPECT(
                amm.expectLPTokens(alice, IOUAmount{7071067811865475, -10}));
            env.require(balance(alice, USD(2000)));
            // alice gets 1000 XRP back and pays 10 drops fee as the sender
            env.require(
                balance(alice, aliceXrpBalance + XRP(1000) - drops(10)));

            // bob deposit for himself
            amm.deposit(bob, USD(1000), XRP(2000));
            env.close();

            // there's some rounding happening
            BEAST_EXPECT(amm.expectBalances(
                STAmount{USD, UINT64_C(1499999999999999), -12},
                XRP(3000),
                IOUAmount{2121320343559642, -9}));
            BEAST_EXPECT(
                amm.expectLPTokens(alice, IOUAmount{7071067811865475, -10}));
            BEAST_EXPECT(
                amm.expectLPTokens(bob, IOUAmount{1414213562373094, -9}));
            env.require(balance(alice, USD(2000)));
            env.require(
                balance(bob, STAmount{USD, UINT64_C(2000000000000001), -12}));
            env.require(balance(bob, bobXrpBalance - XRP(2000) - drops(10)));

            aliceXrpBalance = env.balance(alice, XRP);
            bobXrpBalance = env.balance(bob, XRP);

            // alice AMMClawback all bob's USD on behalf of gw
            env(amm::ammClawback(alice, bob, USD, XRP, std::nullopt, gw));
            env.close();

            BEAST_EXPECT(amm.expectBalances(
                STAmount{USD, UINT64_C(5000000000000001), -13},
                XRP(1000),
                IOUAmount{7071067811865480, -10}));
            BEAST_EXPECT(
                amm.expectLPTokens(alice, IOUAmount{7071067811865475, -10}));
            BEAST_EXPECT(amm.expectLPTokens(bob, IOUAmount(0)));
            env.require(balance(alice, USD(2000)));
            env.require(
                balance(bob, STAmount{USD, UINT64_C(2000000000000001), -12}));
            env.require(balance(alice, aliceXrpBalance - drops(10)));
            env.require(balance(bob, bobXrpBalance + XRP(2000)));
        }

        // test AMMVote
        {
            Env env(*this, features);
            Account gw{"gateway"};
            Account alice{"alice"};
            Account bob{"bob"};
            env.fund(XRP(1000000000), gw, alice, bob);
            env.close();

            auto const USD = gw["USD"];
            env.trust(USD(10000), alice);
            env(pay(gw, alice, USD(3000)));
            env.trust(USD(10000), bob);
            env(pay(gw, bob, USD(3000)));
            env.close();

            // alice delegates AMMVote to bob
            env(account_permission::accountPermissionSet(
                alice, bob, {"AMMVote"}));
            env.close();

            AMM amm(env, alice, USD(1000), XRP(2000), ter(tesSUCCESS));
            env.close();

            auto aliceXrpBalance = env.balance(alice, XRP);
            auto bobXrpBalance = env.balance(bob, XRP);

            BEAST_EXPECT(amm.expectTradingFee(0));
            amm.vote(alice, 100);
            env.close();
            BEAST_EXPECT(amm.expectTradingFee(100));
            // alice is the sender who pays the fee
            env.require(balance(alice, aliceXrpBalance - drops(10)));
            env.require(balance(bob, bobXrpBalance));

            aliceXrpBalance = env.balance(alice, XRP);
            bobXrpBalance = env.balance(bob, XRP);

            // bob vote onbehalf of alice
            amm.vote(
                bob,
                500,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tesSUCCESS),
                alice);
            env.close();
            BEAST_EXPECT(amm.expectTradingFee(500));
            // bob is the sender who pays the fee
            env.require(balance(alice, aliceXrpBalance));
            env.require(balance(bob, bobXrpBalance - drops(10)));

            aliceXrpBalance = env.balance(alice, XRP);
            bobXrpBalance = env.balance(bob, XRP);

            // bob vote again onbehalf of alice
            amm.vote(
                bob,
                1000,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tesSUCCESS),
                alice);
            env.close();
            BEAST_EXPECT(amm.expectTradingFee(1000));
            env.require(balance(alice, aliceXrpBalance));
            env.require(balance(bob, bobXrpBalance - drops(10)));
        }

        // test AMMDelete
        {
            Env env(*this, features);
            Account gw{"gateway"};
            Account alice{"alice"};
            env.fund(XRP(1000000000), gw, alice);
            env.close();

            auto const USD = gw["USD"];
            env.trust(USD(10000), alice);
            env(pay(gw, alice, USD(3000)));
            env.close();

            // gw delegates AMMDelete to alice
            env(account_permission::accountPermissionSet(
                gw, alice, {"AMMDelete"}));
            env.close();

            AMM amm(env, gw, USD(1000), XRP(2000), ter(tesSUCCESS));
            env.close();
            // create a lot of trust lines with the lptoken issuer
            for (auto i = 0; i < maxDeletableAMMTrustLines * 2 + 10; ++i)
            {
                Account const a{std::to_string(i)};
                env.fund(XRP(1'000), a);
                env(trust(a, STAmount{amm.lptIssue(), 10'000}));
                env.close();
            }

            // there are lots of trustlines so the amm still exists
            amm.withdrawAll(gw);
            BEAST_EXPECT(amm.ammExists());

            auto gwXrpBalance = env.balance(gw, XRP);
            auto aliceXrpBalance = env.balance(alice, XRP);

            // gw delete amm, but at most 512 trustlines are deleted at once, so
            // it's incomplete
            amm.ammDelete(gw, ter(tecINCOMPLETE));
            BEAST_EXPECT(amm.ammExists());
            // alice is the sender who pays the fee
            env.require(balance(gw, gwXrpBalance - drops(10)));
            env.require(balance(alice, aliceXrpBalance));

            gwXrpBalance = env.balance(gw, XRP);
            aliceXrpBalance = env.balance(alice, XRP);

            // alice delete amm onbehalf of gw
            amm.ammDelete(alice, ter(tesSUCCESS), gw);
            BEAST_EXPECT(!amm.ammExists());
            BEAST_EXPECT(!env.le(keylet::ownerDir(amm.ammAccount())));
            env.require(balance(gw, gwXrpBalance));
            // alice is the sender who pays the fee
            env.require(balance(alice, aliceXrpBalance - drops(10)));

            // Try redundant delete
            amm.ammDelete(alice, ter(terNO_AMM));
        }

        // test AMMBid
        {
            Env env(*this, features);
            Account gw{"gateway"};
            Account alice{"alice"};
            Account bob{"bob"};
            Account carol{"carol"};
            env.fund(XRP(1000000000), gw, alice, bob, carol);
            env.close();

            auto const USD = gw["USD"];
            env.trust(USD(10000), alice);
            env(pay(gw, alice, USD(3000)));
            env.close();

            // alice delegates AMMBid to bob
            env(account_permission::accountPermissionSet(
                alice, bob, {"AMMBid"}));
            env.close();

            AMM amm(env, gw, USD(1000), XRP(2000), ter(tesSUCCESS));
            env.close();

            auto aliceXrpBalance = env.balance(alice, XRP);
            auto bobXrpBalance = env.balance(bob, XRP);

            env(amm.bid(
                {.account = gw, .bidMin = 110, .authAccounts = {alice}}));
            BEAST_EXPECT(amm.expectAuctionSlot(0, 0, IOUAmount{110}));
            BEAST_EXPECT(amm.expectAuctionSlot({alice}));

            amm.deposit(alice, 1'000'000);

            // because bob is not lp, can not bid
            env(amm.bid({.account = bob, .authAccounts = {bob}}),
                ter(tecAMM_INVALID_TOKENS));

            // but bob can bid onbehalf of alice who is the lp
            env(amm.bid(
                {.account = bob,
                 .authAccounts = {alice, bob, carol},
                 .onBehalfOf = alice}));
            env.close();
            BEAST_EXPECT(amm.expectAuctionSlot(0, 0, IOUAmount(1155, -1)));
            BEAST_EXPECT(amm.expectAuctionSlot({alice, bob, carol}));
        }
    }

    void
    testCheck(FeatureBitset features)
    {
        testcase("test CheckCreate, CheckCash and CheckCancel");
        using namespace jtx;

        // test create and cash check of XRP on behalf of another account
        {
            Env env(*this, features);
            XRPAmount const baseFee{env.current()->fees().base};
            STAmount const startBalance{XRP(1000000).value()};

            Account alice{"alice"};
            Account bob{"bob"};
            Account carol{"carol"};
            env.fund(startBalance, alice, bob, carol);
            env.close();

            // bob can not write a check to himself
            env(check::create(bob, bob, XRP(10)), ter(temREDUNDANT));
            env.close();
            BEAST_EXPECT(check::checksOnAccount(env, bob).size() == 0);

            // alice delegates CheckCreate to bob
            env(account_permission::accountPermissionSet(
                alice, bob, {"CheckCreate"}));
            env.close();

            env.require(balance(alice, startBalance - drops(baseFee)));
            env.require(balance(bob, startBalance));

            // now bob send a check on behalf of alice to alice,
            // this should fail as well
            env(check::create(bob, alice, XRP(10)),
                onBehalfOf(alice),
                ter(temREDUNDANT));
            env.close();
            env.require(balance(alice, startBalance - drops(baseFee)));
            env.require(balance(bob, startBalance));
            env.require(balance(carol, startBalance));

            // now bob send a check on behalf of alice to bob himself,
            // this should succeed because it's alice->bob
            uint256 const aliceToBob = keylet::check(alice, env.seq(alice)).key;
            env(check::create(bob, bob, XRP(10)), onBehalfOf(alice));
            env.close();
            BEAST_EXPECT(check::checksOnAccount(env, alice).size() == 1);
            BEAST_EXPECT(check::checksOnAccount(env, bob).size() == 1);
            // alice owns the account permission and check
            BEAST_EXPECT(ownerCount(env, alice) == 2);
            BEAST_EXPECT(ownerCount(env, bob) == 0);
            env.require(balance(alice, startBalance - drops(baseFee)));
            env.require(balance(bob, startBalance - drops(baseFee)));
            env.require(balance(carol, startBalance));

            // bob send a check on behalf of alice to carol, the check is
            // actually alice->carol
            uint256 const aliceToCarol =
                keylet::check(alice, env.seq(alice)).key;
            env(check::create(bob, carol, XRP(100)), onBehalfOf(alice));
            env.close();
            BEAST_EXPECT(check::checksOnAccount(env, alice).size() == 2);
            BEAST_EXPECT(check::checksOnAccount(env, bob).size() == 1);
            BEAST_EXPECT(check::checksOnAccount(env, carol).size() == 1);
            // alice owns the account permission and 2 checks
            BEAST_EXPECT(ownerCount(env, alice) == 3);
            BEAST_EXPECT(ownerCount(env, bob) == 0);
            BEAST_EXPECT(ownerCount(env, carol) == 0);
            env.require(balance(alice, startBalance - drops(baseFee)));
            env.require(balance(bob, startBalance - drops(baseFee * 2)));
            env.require(balance(carol, startBalance));

            // bob cash the check
            env(check::cash(bob, aliceToBob, XRP(10)));
            env.close();
            env.require(
                balance(alice, startBalance - XRP(10) - drops(baseFee)));
            env.require(
                balance(bob, startBalance + XRP(10) - drops(baseFee * 3)));
            env.require(balance(carol, startBalance));
            BEAST_EXPECT(check::checksOnAccount(env, alice).size() == 1);
            BEAST_EXPECT(check::checksOnAccount(env, bob).size() == 0);
            BEAST_EXPECT(check::checksOnAccount(env, carol).size() == 1);
            BEAST_EXPECT(ownerCount(env, alice) == 2);
            BEAST_EXPECT(ownerCount(env, bob) == 0);
            BEAST_EXPECT(ownerCount(env, carol) == 0);

            env(check::cash(bob, aliceToCarol, XRP(10)), ter(tecNO_PERMISSION));
            env.require(
                balance(bob, startBalance + XRP(10) - drops(baseFee * 4)));

            // carol delegates CheckCash to bob
            env(account_permission::accountPermissionSet(
                carol, bob, {"CheckCash"}));
            env.close();
            env.require(
                balance(bob, startBalance + XRP(10) - drops(baseFee * 4)));
            env.require(balance(carol, startBalance - drops(baseFee)));
            BEAST_EXPECT(ownerCount(env, carol) == 1);

            // bob cash the check on behalf of carol
            env(check::cash(bob, aliceToCarol, XRP(100), carol));
            env.close();

            env.require(
                balance(alice, startBalance - XRP(110) - drops(baseFee)));
            env.require(
                balance(bob, startBalance + XRP(10) - drops(baseFee * 5)));
            env.require(
                balance(carol, startBalance + XRP(100) - drops(baseFee)));
            BEAST_EXPECT(check::checksOnAccount(env, alice).size() == 0);
            BEAST_EXPECT(check::checksOnAccount(env, bob).size() == 0);
            BEAST_EXPECT(check::checksOnAccount(env, carol).size() == 0);
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(ownerCount(env, bob) == 0);
            BEAST_EXPECT(ownerCount(env, carol) == 1);
        }

        // test create/cash/cancel check of USD on behalf of another account
        {
            Env env(*this, features);
            XRPAmount const baseFee{env.current()->fees().base};
            STAmount const startBalance{XRP(1000000).value()};

            Account gw{"gw"};
            Account alice{"alice"};
            Account bob{"bob"};
            Account carol{"carol"};
            env.fund(startBalance, gw, alice, bob, carol);
            env.close();

            auto const USD = gw["USD"];

            // alice give CheckCreate permission to bob
            env(account_permission::accountPermissionSet(
                alice, bob, {"CheckCreate"}));
            env.close();
            env.require(balance(alice, startBalance - drops(baseFee)));
            env.require(balance(bob, startBalance));

            // bob writes 10USD check on behalf of alice when alice does not
            // have USD
            uint256 const aliceToCarol =
                keylet::check(alice, env.seq(alice)).key;
            env(check::create(bob, carol, USD(10)), onBehalfOf(alice));
            env.close();
            env.require(balance(alice, startBalance - drops(baseFee)));
            env.require(balance(bob, startBalance - drops(baseFee)));
            BEAST_EXPECT(check::checksOnAccount(env, alice).size() == 1);
            BEAST_EXPECT(check::checksOnAccount(env, bob).size() == 0);
            BEAST_EXPECT(check::checksOnAccount(env, carol).size() == 1);
            BEAST_EXPECT(ownerCount(env, alice) == 2);
            BEAST_EXPECT(ownerCount(env, bob) == 0);
            BEAST_EXPECT(ownerCount(env, carol) == 0);

            // carol give CheckCash permission to bob
            env(account_permission::accountPermissionSet(
                carol, bob, {"CheckCash"}));
            env.close();
            env.require(balance(alice, startBalance - drops(baseFee)));
            env.require(balance(bob, startBalance - drops(baseFee)));
            env.require(balance(carol, startBalance - drops(baseFee)));
            BEAST_EXPECT(ownerCount(env, alice) == 2);
            BEAST_EXPECT(ownerCount(env, bob) == 0);
            BEAST_EXPECT(ownerCount(env, carol) == 1);

            // bob cash the check on behalf of carol should fail bacause alice
            // does not have USD
            env(check::cash(bob, aliceToCarol, USD(10), carol),
                ter(tecPATH_PARTIAL));
            env.close();
            env.require(balance(alice, startBalance - drops(baseFee)));
            env.require(balance(bob, startBalance - drops(2 * baseFee)));
            env.require(balance(carol, startBalance - drops(baseFee)));
            BEAST_EXPECT(ownerCount(env, alice) == 2);
            BEAST_EXPECT(ownerCount(env, bob) == 0);
            BEAST_EXPECT(ownerCount(env, carol) == 1);

            // alice does not have enough USD
            env(trust(alice, USD(100)));
            env(pay(gw, alice, USD(9.5)));
            env.close();
            env.require(balance(alice, startBalance - drops(2 * baseFee)));
            env(check::cash(bob, aliceToCarol, USD(10), carol),
                ter(tecPATH_PARTIAL));
            env.close();
            env.require(balance(bob, startBalance - drops(3 * baseFee)));
            env.require(balance(alice, USD(9.5)));
            BEAST_EXPECT(ownerCount(env, alice) == 3);

            // now alice have enough USD
            env(pay(gw, alice, USD(0.5)));
            env.close();

            // bob cash 9.9 USD on behalf of carol
            env(check::cash(bob, aliceToCarol, USD(9.9), carol));
            env.close();
            env.require(balance(alice, startBalance - drops(2 * baseFee)));
            env.require(balance(bob, startBalance - drops(4 * baseFee)));
            env.require(balance(carol, startBalance - drops(baseFee)));
            env.require(balance(alice, USD(0.1)));
            env.require(balance(carol, USD(9.9)));
            BEAST_EXPECT(ownerCount(env, alice) == 2);
            BEAST_EXPECT(ownerCount(env, bob) == 0);
            // cashing the check automatically creats a trustline for carol
            BEAST_EXPECT(ownerCount(env, carol) == 2);
            BEAST_EXPECT(check::checksOnAccount(env, alice).size() == 0);
            BEAST_EXPECT(check::checksOnAccount(env, bob).size() == 0);
            BEAST_EXPECT(check::checksOnAccount(env, carol).size() == 0);

            // bob trying to cash the same check on behalf of carol should fail
            env(check::cash(bob, aliceToCarol, USD(10), carol),
                ter(tecNO_ENTRY));
            env.require(balance(bob, startBalance - drops(5 * baseFee)));

            // carol does not have permission yet.
            env(check::create(carol, alice, USD(10)),
                onBehalfOf(bob),
                ter(tecNO_PERMISSION));
            // fail again
            env(check::create(carol, alice, USD(10)),
                onBehalfOf(bob),
                ter(tecNO_PERMISSION));
            env.require(balance(carol, startBalance - drops(3 * baseFee)));

            // bob allows carol to send CheckCreate on behalf of himself
            env(account_permission::accountPermissionSet(
                bob, carol, {"CheckCreate"}));
            env.close();
            env.require(balance(bob, startBalance - drops(6 * baseFee)));

            BEAST_EXPECT(ownerCount(env, alice) == 2);
            BEAST_EXPECT(ownerCount(env, bob) == 1);
            BEAST_EXPECT(ownerCount(env, carol) == 2);

            // carol writes two checks on behalf of bob to alice
            uint256 const checkId1 = keylet::check(bob, env.seq(bob)).key;
            env(check::create(carol, alice, USD(20)), onBehalfOf(bob));
            uint256 const checkId2 = keylet::check(bob, env.seq(bob)).key;
            env(check::create(carol, alice, USD(10)), onBehalfOf(bob));
            env.close();
            env.require(balance(alice, startBalance - drops(2 * baseFee)));
            env.require(balance(bob, startBalance - drops(6 * baseFee)));
            env.require(balance(carol, startBalance - drops(5 * baseFee)));
            BEAST_EXPECT(ownerCount(env, alice) == 2);
            BEAST_EXPECT(ownerCount(env, bob) == 3);
            BEAST_EXPECT(ownerCount(env, carol) == 2);
            BEAST_EXPECT(check::checksOnAccount(env, alice).size() == 2);
            BEAST_EXPECT(check::checksOnAccount(env, bob).size() == 2);
            BEAST_EXPECT(check::checksOnAccount(env, carol).size() == 0);

            // alice allows bob to cash check on behalf of herself
            env(account_permission::accountPermissionSet(
                alice, bob, {"CheckCash"}));
            env.close();
            env.require(balance(alice, startBalance - drops(3 * baseFee)));
            // alice already owns AccountPermission object for "alice
            // delegating bob"
            BEAST_EXPECT(ownerCount(env, alice) == 2);

            // alice allows bob to cancel check on behalf of herself.
            env(account_permission::accountPermissionSet(
                alice, bob, {"CheckCash", "CheckCancel"}));
            env.close();
            env.require(balance(alice, startBalance - drops(4 * baseFee)));
            BEAST_EXPECT(ownerCount(env, alice) == 2);

            env(trust(bob, USD(10)));
            env(pay(gw, bob, USD(10)));
            env.close();
            env.require(balance(bob, startBalance - drops(7 * baseFee)));
            BEAST_EXPECT(ownerCount(env, bob) == 4);

            // bob cash check2 on behalf of alice
            env(check::cash(bob, checkId2, USD(10), alice));
            env.close();
            BEAST_EXPECT(check::checksOnAccount(env, alice).size() == 1);
            BEAST_EXPECT(check::checksOnAccount(env, bob).size() == 1);
            BEAST_EXPECT(check::checksOnAccount(env, carol).size() == 0);
            BEAST_EXPECT(ownerCount(env, alice) == 2);
            BEAST_EXPECT(ownerCount(env, bob) == 3);
            BEAST_EXPECT(ownerCount(env, carol) == 2);
            env.require(balance(alice, startBalance - drops(4 * baseFee)));
            env.require(balance(bob, startBalance - drops(8 * baseFee)));
            env.require(balance(carol, startBalance - drops(5 * baseFee)));
            env.require(balance(alice, USD(10.1)));
            env.require(balance(bob, USD(0)));

            // bob cancel check1 on behalf of alice
            env(check::cancel(bob, checkId1, alice));
            env.close();
            BEAST_EXPECT(check::checksOnAccount(env, alice).size() == 0);
            BEAST_EXPECT(check::checksOnAccount(env, bob).size() == 0);
            BEAST_EXPECT(ownerCount(env, alice) == 2);
            BEAST_EXPECT(ownerCount(env, bob) == 2);
        }
    }

    void
    testClawback(FeatureBitset features)
    {
        testcase("test Clawback");
        using namespace jtx;

        Env env(*this, features);
        XRPAmount const baseFee{env.current()->fees().base};
        STAmount const startBalance{XRP(1000000).value()};

        Account gw{"gw"};
        Account alice{"alice"};
        Account bob{"bob"};
        env.fund(startBalance, gw, alice, bob);
        env.close();

        // set asfAllowTrustLineClawback
        env(fset(gw, asfAllowTrustLineClawback));
        env(fset(alice, asfAllowTrustLineClawback));
        env.close();
        env.require(flags(gw, asfAllowTrustLineClawback));
        env.require(flags(alice, asfAllowTrustLineClawback));
        env.require(balance(gw, startBalance - drops(baseFee)));
        env.require(balance(alice, startBalance - drops(baseFee)));

        // gw issues bob 1000USD
        auto const USD = gw["USD"];
        env.trust(USD(10000), bob);
        env(pay(gw, bob, USD(1000)));
        env.close();
        env.require(balance(gw, startBalance - drops(2 * baseFee)));
        BEAST_EXPECT(ownerCount(env, bob) == 1);
        env.require(balance(bob, USD(1000)));

        // alice clawback from bob on behalf of gw should fail
        // because she does not have permission.
        env(claw(alice, bob["USD"](100)),
            onBehalfOf(gw),
            ter(tecNO_PERMISSION));
        env.close();
        env.require(balance(alice, startBalance - drops(2 * baseFee)));
        env.require(balance(bob, startBalance));
        env.require(balance(gw, startBalance - drops(2 * baseFee)));
        env.require(balance(bob, USD(1000)));

        // now gw give permission to alice
        env(account_permission::accountPermissionSet(gw, alice, {"Clawback"}));
        env.close();
        env.require(balance(alice, startBalance - drops(2 * baseFee)));
        env.require(balance(bob, startBalance));
        env.require(balance(gw, startBalance - drops(3 * baseFee)));
        BEAST_EXPECT(ownerCount(env, gw) == 1);
        BEAST_EXPECT(ownerCount(env, bob) == 1);

        // now alice can claw on behalf gw
        env(claw(alice, bob["USD"](100)), onBehalfOf(gw));
        env.close();
        env.require(balance(alice, startBalance - drops(3 * baseFee)));
        env.require(balance(bob, startBalance));
        env.require(balance(gw, startBalance - drops(3 * baseFee)));
        BEAST_EXPECT(ownerCount(env, gw) == 1);
        BEAST_EXPECT(ownerCount(env, bob) == 1);
        env.require(balance(bob, USD(900)));

        // gw claw another 200USD from bob by himself
        env(claw(gw, bob["USD"](200)));
        env.close();
        env.require(balance(alice, startBalance - drops(3 * baseFee)));
        env.require(balance(bob, startBalance));
        env.require(balance(gw, startBalance - drops(4 * baseFee)));
        BEAST_EXPECT(ownerCount(env, gw) == 1);
        BEAST_EXPECT(ownerCount(env, bob) == 1);
        env.require(balance(bob, USD(700)));

        // update limit
        env(trust(bob, USD(0), 0));
        env.close();
        env.require(balance(bob, startBalance - drops(baseFee)));

        // alice claw the remaining balance from bob on behalf gw
        env(claw(alice, bob["USD"](700)), onBehalfOf(gw));
        env.close();
        env.require(balance(alice, startBalance - drops(4 * baseFee)));
        env.require(balance(bob, startBalance - drops(baseFee)));
        env.require(balance(gw, startBalance - drops(4 * baseFee)));
        BEAST_EXPECT(ownerCount(env, gw) == 1);
        // the trustline got deleted
        BEAST_EXPECT(ownerCount(env, bob) == 0);
    }

    void
    testCredentials(FeatureBitset features)
    {
        testcase("test crendentials");
        using namespace jtx;
        Account const subject{"subject"};

        {
            Env env(*this, features);
            Account alice{"alice"};
            Account issuer{"issuer"};
            Account subject{"subject"};
            env.fund(XRP(5'000), alice, issuer, subject);
            env.close();

            const char credType[] = "abcde";
            const char uri[] = "uri";
            auto const credKey =
                credentials::credentialKeylet(subject, issuer, credType);

            // create credential on behalf of another account
            {
                // alice creating credential on behalf of issuer is not
                // permitted
                env(credentials::create(subject, alice, credType),
                    credentials::uri(uri),
                    onBehalfOf(issuer),
                    ter(tecNO_PERMISSION));

                env(account_permission::accountPermissionSet(
                    issuer, alice, {"CredentialCreate"}));
                env.close();
                BEAST_EXPECT(ownerCount(env, issuer) == 1);
                BEAST_EXPECT(ownerCount(env, alice) == 0);

                // alice creates credential on behalf of issuer successfully
                env(credentials::create(subject, alice, credType),
                    credentials::uri(uri),
                    onBehalfOf(issuer));
                env.close();
                BEAST_EXPECT(ownerCount(env, issuer) == 2);

                auto const sleCred = env.le(credKey);
                BEAST_EXPECT(sleCred);
                BEAST_EXPECT(sleCred->getAccountID(sfSubject) == subject.id());
                BEAST_EXPECT(sleCred->getAccountID(sfIssuer) == issuer.id());
                BEAST_EXPECT(!sleCred->getFieldU32(sfFlags));
                BEAST_EXPECT(
                    credentials::checkVL(sleCred, sfCredentialType, credType));
                BEAST_EXPECT(credentials::checkVL(sleCred, sfURI, uri));
            }

            // accept credential on behalf of another account
            {
                env(account_permission::accountPermissionSet(
                    subject, alice, {"CredentialAccept"}));
                env.close();
                BEAST_EXPECT(ownerCount(env, subject) == 1);
                BEAST_EXPECT(ownerCount(env, alice) == 0);

                // alice accept credential on behalf of subject
                env(credentials::accept(alice, issuer, credType),
                    onBehalfOf(subject));
                env.close();
                // owner of credential now is subject, not issuer
                BEAST_EXPECT(ownerCount(env, subject) == 2);
                BEAST_EXPECT(ownerCount(env, issuer) == 1);
                auto const sleCred = env.le(credKey);
                BEAST_EXPECT(sleCred);
                BEAST_EXPECT(sleCred->getAccountID(sfSubject) == subject.id());
                BEAST_EXPECT(sleCred->getAccountID(sfIssuer) == issuer.id());
                BEAST_EXPECT(sleCred->getFieldU32(sfFlags) == lsfAccepted);
                BEAST_EXPECT(
                    credentials::checkVL(sleCred, sfCredentialType, credType));
                BEAST_EXPECT(credentials::checkVL(sleCred, sfURI, uri));
            }

            // delete credential on behalf of another account
            {
                env(account_permission::accountPermissionSet(
                    subject, alice, {"CredentialDelete"}));
                env.close();
                BEAST_EXPECT(ownerCount(env, subject) == 2);
                BEAST_EXPECT(ownerCount(env, issuer) == 1);

                env(credentials::deleteCred(alice, subject, issuer, credType),
                    onBehalfOf(subject));
                env.close();
                BEAST_EXPECT(!env.le(credKey));
                BEAST_EXPECT(ownerCount(env, subject) == 1);
                BEAST_EXPECT(ownerCount(env, issuer) == 1);
            }

            // create and delete credential on behalf of issuer for the issuer
            // himself
            {
                env(account_permission::accountPermissionSet(
                    issuer, alice, {"CredentialCreate", "CredentialDelete"}));
                env.close();
                BEAST_EXPECT(ownerCount(env, issuer) == 1);

                env(credentials::create(issuer, alice, credType),
                    credentials::uri(uri),
                    onBehalfOf(issuer));
                env.close();
                BEAST_EXPECT(ownerCount(env, issuer) == 2);

                auto const credKey =
                    credentials::credentialKeylet(issuer, issuer, credType);

                auto sleCred = env.le(credKey);
                BEAST_EXPECT(sleCred);
                BEAST_EXPECT(sleCred->getAccountID(sfSubject) == issuer.id());
                BEAST_EXPECT(sleCred->getAccountID(sfIssuer) == issuer.id());
                BEAST_EXPECT(
                    credentials::checkVL(sleCred, sfCredentialType, credType));
                BEAST_EXPECT(credentials::checkVL(sleCred, sfURI, uri));
                BEAST_EXPECT(sleCred->getFieldU32(sfFlags) == lsfAccepted);

                env(credentials::deleteCred(alice, issuer, issuer, credType),
                    onBehalfOf(issuer));
                env.close();
                BEAST_EXPECT(!env.le(credKey));
                BEAST_EXPECT(ownerCount(env, issuer) == 1);
            }
        }
    }

    void
    testDepositPreauth(FeatureBitset features)
    {
        testcase("test DepositPreauth");
        using namespace jtx;

        {
            Env env(*this, features);
            XRPAmount const baseFee{env.current()->fees().base};
            STAmount const startBalance{XRP(1000000).value()};

            Account gw{"gw"};
            Account alice{"alice"};
            Account bob{"bob"};
            Account carol{"carol"};
            env.fund(startBalance, gw, alice, bob, carol);
            env.close();

            auto const USD = gw["USD"];
            env.trust(USD(10000), alice);
            env.trust(USD(10000), bob);
            env.trust(USD(10000), carol);
            env.close();

            env(pay(gw, alice, USD(1000)));
            env(pay(gw, bob, USD(1000)));
            env(pay(gw, carol, USD(1000)));
            env.close();
            env.require(balance(alice, startBalance));
            env.require(balance(bob, startBalance));
            env.require(balance(carol, startBalance));
            env.require(balance(alice, USD(1000)));
            env.require(balance(bob, USD(1000)));
            env.require(balance(carol, USD(1000)));
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(ownerCount(env, bob) == 1);
            BEAST_EXPECT(ownerCount(env, carol) == 1);

            // bob requiress authorization for deposits
            env(fset(bob, asfDepositAuth));
            env.close();
            env.require(balance(bob, startBalance - drops(baseFee)));

            // alice and carol can not pay bob
            env(pay(alice, bob, XRP(100)), ter(tecNO_PERMISSION));
            env(pay(alice, bob, USD(100)), ter(tecNO_PERMISSION));
            env(pay(carol, bob, XRP(100)), ter(tecNO_PERMISSION));
            env(pay(carol, bob, USD(100)), ter(tecNO_PERMISSION));
            env.close();
            env.require(balance(alice, startBalance - drops(2 * baseFee)));
            env.require(balance(bob, startBalance - drops(baseFee)));
            env.require(balance(carol, startBalance - drops(2 * baseFee)));

            // bob preauthorizes carol for deposit
            env(deposit::auth(bob, carol));
            env.close();
            env.require(balance(bob, startBalance - drops(2 * baseFee)));
            BEAST_EXPECT(ownerCount(env, bob) == 2);

            // carol can pay bob
            env(pay(carol, bob, XRP(100)));
            env(pay(carol, bob, USD(100)));
            // alice still can not pay
            env(pay(alice, bob, XRP(100)), ter(tecNO_PERMISSION));
            env(pay(alice, bob, USD(100)), ter(tecNO_PERMISSION));
            env.close();
            env.require(balance(alice, startBalance - drops(4 * baseFee)));
            env.require(
                balance(bob, startBalance + XRP(100) - drops(2 * baseFee)));
            env.require(
                balance(carol, startBalance - XRP(100) - drops(4 * baseFee)));
            env.require(balance(alice, USD(1000)));
            env.require(balance(bob, USD(1100)));
            env.require(balance(carol, USD(900)));
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(ownerCount(env, bob) == 2);
            BEAST_EXPECT(ownerCount(env, carol) == 1);

            // bob give permission to carol to preauthorize other accounts for
            // deposit
            env(account_permission::accountPermissionSet(
                bob, carol, {"DepositPreauth"}));
            env.close();
            env.require(
                balance(bob, startBalance + XRP(100) - drops(3 * baseFee)));
            BEAST_EXPECT(ownerCount(env, bob) == 3);
            BEAST_EXPECT(ownerCount(env, carol) == 1);

            // now carol send DepositPreauth on behalf of bob to allow alice to
            // deposit
            env(deposit::auth(carol, alice, bob));
            env.close();
            env.require(balance(alice, startBalance - drops(4 * baseFee)));
            env.require(
                balance(bob, startBalance + XRP(100) - drops(3 * baseFee)));
            env.require(
                balance(carol, startBalance - XRP(100) - drops(5 * baseFee)));
            BEAST_EXPECT(ownerCount(env, bob) == 4);

            // now alice can pay bob
            env(pay(alice, bob, XRP(100)));
            env(pay(alice, bob, USD(100)));
            env.close();
            env.require(
                balance(alice, startBalance - XRP(100) - drops(6 * baseFee)));
            env.require(
                balance(bob, startBalance + XRP(200) - drops(3 * baseFee)));
            env.require(
                balance(carol, startBalance - XRP(100) - drops(5 * baseFee)));
            env.require(balance(alice, USD(900)));
            env.require(balance(bob, USD(1200)));
            env.require(balance(carol, USD(900)));

            // bob give permission to alice to auth/unauth on behalf of himself
            env(account_permission::accountPermissionSet(
                bob, alice, {"DepositPreauth"}));
            env.close();
            env.require(
                balance(bob, startBalance + XRP(200) - drops(4 * baseFee)));
            BEAST_EXPECT(ownerCount(env, bob) == 5);

            // now alice unauthorize carol to pay bob on behalf of bob
            env(deposit::unauth(alice, carol, bob));
            env.close();
            env.require(
                balance(alice, startBalance - XRP(100) - drops(7 * baseFee)));
            BEAST_EXPECT(ownerCount(env, bob) == 4);

            // carol can not pay bob
            env(pay(carol, bob, XRP(100)), ter(tecNO_PERMISSION));
            env(pay(carol, bob, USD(100)), ter(tecNO_PERMISSION));
            env.close();
            env.require(
                balance(carol, startBalance - XRP(100) - drops(7 * baseFee)));

            // alice can still pay bob
            env(pay(alice, bob, XRP(100)));
            env(pay(alice, bob, USD(100)));
            env.close();
            env.require(
                balance(alice, startBalance - XRP(200) - drops(9 * baseFee)));
            env.require(
                balance(bob, startBalance + XRP(300) - drops(4 * baseFee)));
            env.require(balance(alice, USD(800)));
            env.require(balance(bob, USD(1300)));

            // alice unauth herself to pay bob on behalf of bob
            env(deposit::unauth(alice, alice, bob));
            env.close();
            env.require(
                balance(alice, startBalance - XRP(200) - drops(10 * baseFee)));
            env.require(
                balance(bob, startBalance + XRP(300) - drops(4 * baseFee)));
            env.require(
                balance(carol, startBalance - XRP(100) - drops(7 * baseFee)));
            BEAST_EXPECT(ownerCount(env, bob) == 3);

            // now alice can not pay bob
            env(pay(alice, bob, XRP(100)), ter(tecNO_PERMISSION));
            env(pay(alice, bob, USD(100)), ter(tecNO_PERMISSION));
            // carol still can not pay bob
            env(pay(carol, bob, XRP(100)), ter(tecNO_PERMISSION));
            env(pay(carol, bob, USD(100)), ter(tecNO_PERMISSION));
            env.require(
                balance(alice, startBalance - XRP(200) - drops(12 * baseFee)));
            env.require(
                balance(carol, startBalance - XRP(100) - drops(9 * baseFee)));

            env(fclear(bob, asfDepositAuth));
            env.close();

            // now alice and carol can pay bob
            env(pay(alice, bob, XRP(100)));
            env(pay(alice, bob, USD(100)));
            env(pay(carol, bob, XRP(100)));
            env(pay(carol, bob, USD(100)));
            env.close();
        }

        {
            const char credType[] = "abcde";
            const char uri[] = "uri";
            Env env(*this, features);

            Account alice{"alice"};
            Account bob{"bob"};
            Account issuer{"issuer"};
            Account subject{"subject"};
            env.fund(XRP(5000), alice, bob, issuer, subject);
            env.close();

            env(fset(bob, asfDepositAuth));
            env.close();

            env(account_permission::accountPermissionSet(
                issuer, alice, {"CredentialCreate"}));
            env.close();
            BEAST_EXPECT(ownerCount(env, issuer) == 1);
            BEAST_EXPECT(ownerCount(env, alice) == 0);

            // alice creates credential on behalf of issuer successfully
            env(credentials::create(subject, alice, credType),
                credentials::uri(uri),
                onBehalfOf(issuer));
            env.close();
            BEAST_EXPECT(ownerCount(env, issuer) == 2);

            // Get the index of the credentials
            auto const jv =
                credentials::ledgerEntry(env, subject, issuer, credType);
            std::string const credIdx = jv[jss::result][jss::index].asString();

            env(account_permission::accountPermissionSet(
                bob, alice, {"DepositPreauth"}));
            env.close();

            // alice send DepositPreauth on behalf of bob.
            // bob will accept payements from accounts with credentials signed
            // by issuer
            env(deposit::authCredentials(alice, {{issuer, credType}}),
                onBehalfOf(bob));
            env.close();

            auto const jDP = deposit::ledgerEntryDepositPreauth(
                env, bob, {{issuer, credType}});
            BEAST_EXPECT(
                jDP.isObject() && jDP.isMember(jss::result) &&
                !jDP[jss::result].isMember(jss::error) &&
                jDP[jss::result].isMember(jss::node) &&
                jDP[jss::result][jss::node].isMember("LedgerEntryType") &&
                jDP[jss::result][jss::node]["LedgerEntryType"] ==
                    jss::DepositPreauth);

            // credentials are not accepted yet
            env(pay(subject, bob, XRP(100)),
                credentials::ids({credIdx}),
                ter(tecBAD_CREDENTIALS));
            env.close();

            // alice accept credentials on behalf of subject
            env(account_permission::accountPermissionSet(
                subject, alice, {"CredentialAccept"}));
            env.close();

            env(credentials::accept(alice, issuer, credType),
                onBehalfOf(subject));
            env.close();

            // now subject can pay bob
            env(pay(subject, bob, XRP(100)), credentials::ids({credIdx}));
            env.close();

            // subject can pay alice because alice did not enable depositAuth
            env(pay(subject, alice, XRP(250)), credentials::ids({credIdx}));
            env.close();

            Account carol{"carol"};
            env.fund(XRP(5000), carol);
            env.close();

            env(fset(carol, asfDepositAuth));
            env.close();

            // carol did not setup DepositPreauth
            env(pay(subject, carol, XRP(100)),
                credentials::ids({credIdx}),
                ter(tecNO_PERMISSION));

            // bob setup depositPreauth on behalf of carol
            env(account_permission::accountPermissionSet(
                carol, bob, {"DepositPreauth"}));
            env.close();

            env(deposit::authCredentials(bob, {{issuer, credType}}),
                onBehalfOf(carol));
            env.close();

            const char credType2[] = "fghij";
            env(credentials::create(subject, issuer, credType2));
            env.close();
            env(credentials::accept(subject, issuer, credType2));
            env.close();
            auto const jv2 =
                credentials::ledgerEntry(env, subject, issuer, credType2);
            std::string const credIdx2 =
                jv2[jss::result][jss::index].asString();

            // unable to pay with invalid set of credentials
            env(pay(subject, carol, XRP(100)),
                credentials::ids({credIdx, credIdx2}),
                ter(tecNO_PERMISSION));

            env(pay(subject, carol, XRP(100)), credentials::ids({credIdx}));
            env.close();
        }
    }

    void
    testDID(FeatureBitset features)
    {
        testcase("test DIDSet, DIDDelete");
        using namespace jtx;

        Env env(*this, features);
        XRPAmount const baseFee{env.current()->fees().base};
        STAmount const startBalance{XRP(1000000).value()};

        Account alice{"alice"};
        Account bob{"bob"};
        Account carol{"carol"};
        env.fund(startBalance, alice, bob, carol);
        env.close();

        // alice give permission to bob and carol for DIDSet and DIDDelete
        env(account_permission::accountPermissionSet(
            alice, bob, {"DIDSet", "DIDDelete"}));
        env(account_permission::accountPermissionSet(
            alice, carol, {"DIDSet", "DIDDelete"}));
        env.close();
        env.require(balance(alice, startBalance - drops(2 * baseFee)));
        BEAST_EXPECT(ownerCount(env, alice) == 2);

        // bob set uri and doc on behalf of alice
        std::string const uri = "uri";
        std::string const doc = "doc";
        std::string const data = "data";
        env(did::set(bob),
            did::uri(uri),
            did::document(doc),
            onBehalfOf(alice));
        env.close();
        env.require(balance(alice, startBalance - drops(2 * baseFee)));
        env.require(balance(bob, startBalance - drops(baseFee)));
        env.require(balance(carol, startBalance));
        BEAST_EXPECT(ownerCount(env, alice) == 3);
        BEAST_EXPECT(ownerCount(env, bob) == 0);
        BEAST_EXPECT(ownerCount(env, carol) == 0);
        auto sleDID = env.le(keylet::did(alice.id()));
        BEAST_EXPECT(sleDID);
        BEAST_EXPECT(did::checkVL((*sleDID)[sfURI], uri));
        BEAST_EXPECT(did::checkVL((*sleDID)[sfDIDDocument], doc));
        BEAST_EXPECT(!sleDID->isFieldPresent(sfData));

        // carol set data, update document and remove uri on behalf of alice
        std::string const doc2 = "doc2";
        env(did::set(carol),
            did::uri(""),
            did::document(doc2),
            did::data(data),
            onBehalfOf(alice));
        env.close();
        env.require(balance(alice, startBalance - drops(2 * baseFee)));
        env.require(balance(bob, startBalance - drops(baseFee)));
        env.require(balance(carol, startBalance - drops(baseFee)));
        BEAST_EXPECT(ownerCount(env, alice) == 3);
        BEAST_EXPECT(ownerCount(env, bob) == 0);
        BEAST_EXPECT(ownerCount(env, carol) == 0);
        sleDID = env.le(keylet::did(alice.id()));
        BEAST_EXPECT(sleDID);
        BEAST_EXPECT(!sleDID->isFieldPresent(sfURI));
        BEAST_EXPECT(did::checkVL((*sleDID)[sfDIDDocument], doc2));
        BEAST_EXPECT(did::checkVL((*sleDID)[sfData], data));

        // bob delete DID on behalf of alice
        env(did::del(bob, alice));
        env.close();
        env.require(balance(alice, startBalance - drops(2 * baseFee)));
        env.require(balance(bob, startBalance - drops(2 * baseFee)));
        env.require(balance(carol, startBalance - drops(baseFee)));
        BEAST_EXPECT(ownerCount(env, alice) == 2);
        BEAST_EXPECT(ownerCount(env, bob) == 0);
        BEAST_EXPECT(ownerCount(env, carol) == 0);
        sleDID = env.le(keylet::did(alice.id()));
        BEAST_EXPECT(!sleDID);
    }

    void
    testEscrow(FeatureBitset features)
    {
        std::array<std::uint8_t, 4> const fb1 = {{0xA0, 0x02, 0x80, 0x00}};

        std::array<std::uint8_t, 39> const cb1 = {
            {0xA0, 0x25, 0x80, 0x20, 0xE3, 0xB0, 0xC4, 0x42, 0x98, 0xFC,
             0x1C, 0x14, 0x9A, 0xFB, 0xF4, 0xC8, 0x99, 0x6F, 0xB9, 0x24,
             0x27, 0xAE, 0x41, 0xE4, 0x64, 0x9B, 0x93, 0x4C, 0xA4, 0x95,
             0x99, 0x1B, 0x78, 0x52, 0xB8, 0x55, 0x81, 0x01, 0x00}};

        testcase("test EscrowCreate, EscrowCancel, EscrowFinish");
        using namespace jtx;

        Env env(*this, features);
        XRPAmount const baseFee{env.current()->fees().base};

        Account alice{"alice"};
        Account bob{"bob"};
        Account carol{"carol"};
        env.fund(XRP(1000000), alice, bob, carol);
        env.close();

        STAmount aliceXrpBalance, bobXrpBalance, carolXrpBalance;
        auto UpdateXrpBalances = [&]() {
            aliceXrpBalance = env.balance(alice, XRP);
            bobXrpBalance = env.balance(bob, XRP);
            carolXrpBalance = env.balance(carol, XRP);
        };

        env(account_permission::accountPermissionSet(
            alice, bob, {"EscrowCreate", "EscrowCancel", "EscrowFinish"}));
        env(account_permission::accountPermissionSet(
            alice, carol, {"EscrowCreate", "EscrowCancel", "EscrowFinish"}));
        env(account_permission::accountPermissionSet(
            bob, alice, {"EscrowCreate", "EscrowCancel", "EscrowFinish"}));
        env(account_permission::accountPermissionSet(
            bob, carol, {"EscrowCreate", "EscrowCancel", "EscrowFinish"}));
        env(account_permission::accountPermissionSet(
            carol, alice, {"EscrowCreate", "EscrowCancel", "EscrowFinish"}));
        env(account_permission::accountPermissionSet(
            carol, bob, {"EscrowCreate", "EscrowCancel", "EscrowFinish"}));
        env.close();

        BEAST_EXPECT(ownerCount(env, alice) == 2);
        BEAST_EXPECT(ownerCount(env, bob) == 2);
        BEAST_EXPECT(ownerCount(env, carol) == 2);

        // test send basic EscrowCreate, EscrowCancel, EscrowFinish transactions
        // on behalf of others
        {
            UpdateXrpBalances();
            auto const ts = env.now() + std::chrono::seconds(90);
            // bob creates escrow on behalf of alice, destination is carol
            // (alice->carol)
            auto const seq1 = env.seq(alice);
            env(escrow(bob, carol, XRP(1000)),
                onBehalfOf(alice),
                finish_time(ts));
            env.close();
            env.require(balance(alice, aliceXrpBalance - XRP(1000)));
            env.require(balance(bob, bobXrpBalance - drops(baseFee)));
            BEAST_EXPECT(ownerCount(env, alice) == 3);
            BEAST_EXPECT(ownerCount(env, bob) == 2);
            BEAST_EXPECT(ownerCount(env, carol) == 2);

            UpdateXrpBalances();
            // carol creates escrow on behalf of alice, destination is bob
            // (alice->bob)
            auto const seq2 = env.seq(alice);
            env(escrow(carol, bob, XRP(2000)),
                onBehalfOf(alice),
                cancel_time(ts),
                condition(cb1));
            env.close();
            env.require(balance(alice, aliceXrpBalance - XRP(2000)));
            env.require(balance(bob, bobXrpBalance));
            env.require(balance(carol, carolXrpBalance - drops(baseFee)));
            BEAST_EXPECT(ownerCount(env, alice) == 4);
            BEAST_EXPECT(ownerCount(env, bob) == 2);
            BEAST_EXPECT(ownerCount(env, carol) == 2);

            UpdateXrpBalances();
            // bob creates escrow on behalf of alice again, destination is carol
            // (alice->carol)
            auto const seq3 = env.seq(alice);
            env(escrow(bob, carol, XRP(3000)),
                onBehalfOf(alice),
                finish_time(ts));
            env.close();
            env.require(balance(alice, aliceXrpBalance - XRP(3000)));
            env.require(balance(bob, bobXrpBalance - drops(baseFee)));
            env.require(balance(carol, carolXrpBalance));
            BEAST_EXPECT(ownerCount(env, alice) == 5);
            BEAST_EXPECT(ownerCount(env, bob) == 2);
            BEAST_EXPECT(ownerCount(env, carol) == 2);

            // finish and cancel won't complete prematurely.
            for (; env.now() <= ts; env.close())
            {
                // alice finish seq1 on behalf of bob, the escrow's owner is
                // alice
                env(finish(alice, alice, seq1),
                    onBehalfOf(carol),
                    fee(1500),
                    ter(tecNO_PERMISSION));

                // alice cancel seq2 on behalf of bob, the escrow's owner is
                // alice
                env(cancel(alice, alice, seq1),
                    onBehalfOf(bob),
                    fee(1500),
                    ter(tecNO_PERMISSION));

                // bob finish seq3 on behalf of carol, the escrow's owner is
                // alice
                env(finish(bob, alice, seq3),
                    onBehalfOf(carol),
                    fee(1500),
                    ter(tecNO_PERMISSION));
            }

            UpdateXrpBalances();
            // alice finish escrow seq1 on behalf of carol.
            // alice is the owner.
            env(finish(alice, alice, seq1),
                onBehalfOf(carol),
                fee(1500),
                ter(tesSUCCESS));
            env.close();
            env.require(balance(alice, aliceXrpBalance - drops(1500)));
            env.require(balance(bob, bobXrpBalance));
            env.require(balance(carol, carolXrpBalance + XRP(1000)));
            BEAST_EXPECT(ownerCount(env, alice) == 4);

            UpdateXrpBalances();
            // finish won't work for escrow seq2
            env(finish(alice, alice, seq2),
                condition(cb1),
                fulfillment(fb1),
                onBehalfOf(bob),
                fee(1500),
                ter(tecNO_PERMISSION));
            env.close();
            env.require(balance(alice, aliceXrpBalance - drops(1500)));
            env.require(balance(bob, bobXrpBalance));
            env.require(balance(carol, carolXrpBalance));
            BEAST_EXPECT(ownerCount(env, alice) == 4);

            UpdateXrpBalances();
            // alice cancel escrow seq2 on behalf of bob
            env(cancel(alice, alice, seq2), onBehalfOf(bob), fee(1500));
            env.close();
            env.require(
                balance(alice, aliceXrpBalance + XRP(2000) - drops(1500)));
            env.require(balance(bob, bobXrpBalance));
            env.require(balance(carol, carolXrpBalance));
            BEAST_EXPECT(ownerCount(env, alice) == 3);

            UpdateXrpBalances();
            // bob finish escrow seq3 on behalf of carol
            env(finish(bob, alice, seq3),
                onBehalfOf(carol),
                fee(1500),
                ter(tesSUCCESS));
            env.close();
            env.require(balance(alice, aliceXrpBalance));
            env.require(balance(bob, bobXrpBalance - drops(1500)));
            env.require(balance(carol, carolXrpBalance + XRP(3000)));
            BEAST_EXPECT(ownerCount(env, alice) == 2);
        }

        // test escrow with FinishAfter earlier than CancelAfter
        {
            auto const fts = env.now() + std::chrono::seconds(117);
            auto const cts = env.now() + std::chrono::seconds(192);

            UpdateXrpBalances();
            // alice creates escrow on behalf of carol, destination is bob
            // (carol->bob)
            auto const seq = env.seq(carol);
            env(escrow(alice, bob, XRP(1000)),
                onBehalfOf(carol),
                finish_time(fts),
                cancel_time(cts),
                stag(1),
                dtag(2));
            env.close();

            auto const sle = env.le(keylet::escrow(carol.id(), seq));
            BEAST_EXPECT(sle);
            BEAST_EXPECT((*sle)[sfSourceTag] == 1);
            BEAST_EXPECT((*sle)[sfDestinationTag] == 2);

            env.require(balance(alice, aliceXrpBalance - drops(baseFee)));
            env.require(balance(carol, carolXrpBalance - XRP(1000)));

            // finish and cancel won't complete prematurely.
            for (; env.now() <= fts; env.close())
            {
                // bob finish escrow seq on behalf of carol
                env(finish(bob, carol, seq),
                    onBehalfOf(carol),
                    fee(1500),
                    ter(tecNO_PERMISSION));

                // bob cancel escrow seq on behalf of carol
                env(cancel(bob, carol, seq),
                    onBehalfOf(carol),
                    fee(1500),
                    ter(tecNO_PERMISSION));
            }

            UpdateXrpBalances();
            // still can not cancel before CancelAfter time
            env(cancel(alice, carol, seq),
                onBehalfOf(bob),
                fee(1500),
                ter(tecNO_PERMISSION));
            env.close();
            env.require(balance(alice, aliceXrpBalance - drops(1500)));
            env.require(balance(bob, bobXrpBalance));
            env.require(balance(carol, carolXrpBalance));

            // can finish after FinishAfter time
            env(finish(alice, carol, seq), onBehalfOf(bob), fee(1500));
            env.close();
            env.require(balance(alice, aliceXrpBalance - drops(3000)));
            env.require(balance(bob, bobXrpBalance + XRP(1000)));
            env.require(balance(carol, carolXrpBalance));
        }

        // test escrow with asfDepositAuth
        {
            Account gw("gw");
            Account david{"david"};
            Account emma{"emma"};
            Account frank{"frank"};
            env.fund(XRP(5000), gw, david, emma, frank);
            env(fset(david, asfDepositAuth));
            env.close();
            env(deposit::auth(david, emma));
            env.close();

            auto const seq = env.seq(gw);
            auto const fts = env.now() + std::chrono::seconds(5);
            env(escrow(gw, david, XRP(1000)), finish_time(fts));
            env.require(balance(gw, XRP(4000) - drops(baseFee)));
            env.close();

            env(account_permission::accountPermissionSet(
                emma, frank, {"EscrowCreate", "EscrowCancel", "EscrowFinish"}));
            env.close();

            while (env.now() <= fts)
                env.close();

            // gw has no permission
            env(finish(gw, gw, seq), ter(tecNO_PERMISSION));

            auto davidXrpBalance = env.balance(david, XRP);
            // but frank can finish onbehalf of emma because emma is
            // preauthorized
            env(finish(frank, gw, seq), onBehalfOf(emma));
            env.close();
            env.require(balance(david, davidXrpBalance + XRP(1000)));
        }
    }

    void
    testMPToken(FeatureBitset features)
    {
        testcase("test MPT transactions");
        using namespace jtx;

        // test create, authorize on behalf of others
        {
            Env env(*this, features);
            Account alice{"alice"};
            Account bob{"bob"};
            Account carol{"carol"};
            env.fund(XRP(1000000), alice, bob, carol);
            env.close();

            // sender is alice, bob is the issuer
            MPTTester mpt(env, alice, bob);
            env.close();

            env(account_permission::accountPermissionSet(
                bob,
                alice,
                {"MPTokenIssuanceCreate",
                 "MPTokenIssuanceDestroy",
                 "MPTokenAuthorize"}));

            env(account_permission::accountPermissionSet(
                bob,
                carol,
                {"MPTokenIssuanceCreate",
                 "MPTokenIssuanceDestroy",
                 "MPTokenAuthorize"}));

            env(account_permission::accountPermissionSet(
                alice, carol, {"MPTokenAuthorize"}));
            env.close();

            //  bob owns AccountPermission and MPTokenIssuance
            mpt.create(
                {.maxAmt = maxMPTokenAmount,  // 9'223'372'036'854'775'807
                 .assetScale = 1,
                 .transferFee = 10,
                 .metadata = "123",
                 .ownerCount = 3,
                 .flags = tfMPTCanLock | tfMPTCanEscrow | tfMPTCanTrade |
                     tfMPTCanTransfer | tfMPTCanClawback,
                 .onBehalfOf = bob});

            // Get the hash for the most recent transaction.
            std::string const txHash{
                env.tx()->getJson(JsonOptions::none)[jss::hash].asString()};

            Json::Value const result = env.rpc("tx", txHash)[jss::result];
            BEAST_EXPECT(
                result[sfMaximumAmount.getJsonName()] == "9223372036854775807");
            env.close();

            // carol does not have the permission to authorize on behalf of bob
            mpt.authorize(
                {.account = carol, .onBehalfOf = bob, .err = tecNO_PERMISSION});

            // alice has permission, but bob can not hold onto his own token
            mpt.authorize(
                {.account = alice, .onBehalfOf = bob, .err = tecNO_PERMISSION});

            // alice holds the mptoken object, sender is carol
            mpt.authorize({.account = carol, .onBehalfOf = alice});

            // alice cannot create the mptoken again
            mpt.authorize({.account = alice, .err = tecDUPLICATE});

            // bob pays alice 100 tokens
            mpt.pay(bob, alice, 100);

            // alice hold token, can not unauthorize
            mpt.authorize(
                {.account = carol,
                 .flags = tfMPTUnauthorize,
                 .onBehalfOf = alice,
                 .err = tecHAS_OBLIGATIONS});

            // alice pays back 100 tokens
            mpt.pay(alice, bob, 100);

            // now alice can unauthorize, carol sent the request on behalf of
            // her
            mpt.authorize(
                {.account = carol,
                 .flags = tfMPTUnauthorize,
                 .onBehalfOf = alice});

            // now if alice tries to unauthorize by herself, it will fail
            mpt.authorize(
                {.account = alice,
                 .holderCount = 0,
                 .flags = tfMPTUnauthorize,
                 .err = tecOBJECT_NOT_FOUND});
        }

        // test create, destroy, claw with tfMPTRequireAuth
        {
            Env env(*this, features);
            Account alice{"alice"};
            Account bob{"bob"};
            Account carol{"carol"};
            env.fund(XRP(100000), alice, bob, carol);
            env.close();

            // alice gives bob permissions
            env(account_permission::accountPermissionSet(
                alice,
                bob,
                {"MPTokenIssuanceCreate",
                 "MPTokenIssuanceDestroy",
                 "MPTokenAuthorize"}));
            env.close();

            // sender is bob, alice is the issuer
            MPTTester mpt(env, bob, alice);
            env.close();

            // alice owns the mptokenissuance and the account permission
            mpt.create(
                {.ownerCount = 2,
                 .flags = tfMPTRequireAuth | tfMPTCanClawback,
                 .onBehalfOf = alice});
            env.close();

            // bob creates mptoken
            mpt.authorize({.account = bob, .holderCount = 1});

            // bob authorize himself on behalf of alice
            mpt.authorize({.account = bob, .holder = bob, .onBehalfOf = alice});

            mpt.pay(alice, bob, 200);
            mpt.claw(alice, bob, 100);
            mpt.pay(bob, alice, 100);

            // bob unauthorize bob's mptoken on behalf of alice
            mpt.authorize(
                {.account = bob,
                 .holder = bob,
                 .holderCount = 1,
                 .flags = tfMPTUnauthorize,
                 .onBehalfOf = alice});

            // bob gives carol permissions
            env(account_permission::accountPermissionSet(
                bob,
                carol,
                {"MPTokenIssuanceCreate",
                 "MPTokenIssuanceDestroy",
                 "MPTokenAuthorize"}));
            env.close();

            mpt.authorize(
                {.account = carol,
                 .holderCount = 0,
                 .flags = tfMPTUnauthorize,
                 .onBehalfOf = bob});

            // bob destroys the mpt issuance on behalf of alice
            // issuer is alice, she still owns the account permission, so
            // ownerCount is 1.
            mpt.destroy({.issuer = bob, .ownerCount = 1, .onBehalfOf = alice});
        }

        // MPTokenIssuanceSet on behalf of other account
        {
            Env env(*this, features);
            Account alice{"alice"};
            Account bob{"bob"};
            Account carol{"carol"};
            env.fund(XRP(100000), alice, bob, carol);
            env.close();

            // alice gives bob permissions
            env(account_permission::accountPermissionSet(
                alice,
                bob,
                {"MPTokenIssuanceCreate",
                 "MPTokenIssuanceDestroy",
                 "MPTokenAuthorize",
                 "MPTokenIssuanceSet"}));
            env.close();

            // sender is bob, alice is the issuer
            MPTTester mpt(env, bob, alice);
            env.close();

            // alice create with tfMPTCanLock by herself
            // alice owns account permission and mpt issuance
            mpt.create(
                {.ownerCount = 2, .holderCount = 0, .flags = tfMPTCanLock});

            env(account_permission::accountPermissionSet(
                bob,
                carol,
                {"MPTokenIssuanceCreate",
                 "MPTokenIssuanceDestroy",
                 "MPTokenAuthorize"}));
            env.close();

            // carol send auth on behalf of bob
            mpt.authorize(
                {.account = carol, .holderCount = 1, .onBehalfOf = bob});

            env(account_permission::accountPermissionSet(
                alice,
                carol,
                {"MPTokenIssuanceCreate",
                 "MPTokenIssuanceDestroy",
                 "MPTokenIssuanceSet"}));
            env.close();

            // carol locks bob's mptoken on behalf of alice
            mpt.set(
                {.account = carol,
                 .holder = bob,
                 .flags = tfMPTLock,
                 .onBehalfOf = alice});

            // alice locks bob's mptoken again, it remains locked
            mpt.set({.account = alice, .holder = bob, .flags = tfMPTLock});

            // bob locks mptissuance on behalf of alice
            mpt.set({.account = bob, .flags = tfMPTLock, .onBehalfOf = alice});

            // carol unlock bob's mptoken on behalf of alice
            mpt.set(
                {.account = carol,
                 .holder = bob,
                 .flags = tfMPTUnlock,
                 .onBehalfOf = alice});

            // alice unlock mptissuance by herself
            mpt.set({.account = alice, .flags = tfMPTUnlock});

            // alice locks mptissuance
            mpt.set({.account = alice, .flags = tfMPTLock});

            // carol unlock mptissuance on behalf of alice
            mpt.set(
                {.account = carol, .flags = tfMPTUnlock, .onBehalfOf = alice});
        }

        // DepositPreauth and credential
        {
            Env env(*this, features);
            Account alice{"alice"};
            Account bob{"bob"};
            Account carol{"carol"};
            Account david{"david"};
            env.fund(XRP(100000), alice, bob, carol, david);
            env.close();
            const char credType[] = "abcde";

            // alice gives bob permissions
            env(account_permission::accountPermissionSet(
                alice,
                bob,
                {"MPTokenIssuanceCreate",
                 "MPTokenIssuanceDestroy",
                 "MPTokenAuthorize",
                 "MPTokenIssuanceSet"}));
            env.close();

            // sender is bob, alice is the issuer
            MPTTester mpt(env, bob, alice);
            env.close();

            // alice owns the mptokenissuance and the account permission
            mpt.create(
                {.ownerCount = 2,
                 .flags = tfMPTRequireAuth | tfMPTCanTransfer,
                 .onBehalfOf = alice});
            env.close();

            mpt.authorize({.account = bob});
            // bob authorize himself on behalf of alice
            mpt.authorize({.account = bob, .holder = bob, .onBehalfOf = alice});

            // bob require preauthorization
            env(fset(bob, asfDepositAuth));
            env.close();

            // alice try to send 100 MPT to bob, not authorized
            mpt.pay(alice, bob, 100, tecNO_PERMISSION);
            env.close();

            env(account_permission::accountPermissionSet(
                david, carol, {"CredentialCreate", "CredentialAccept"}));
            env.close();

            env(account_permission::accountPermissionSet(
                alice, carol, {"CredentialCreate", "CredentialAccept"}));
            env.close();

            // Create credentials
            env(credentials::create(alice, carol, credType), onBehalfOf(david));
            env.close();
            env(credentials::accept(carol, david, credType), onBehalfOf(alice));
            env.close();
            auto const jv =
                credentials::ledgerEntry(env, alice, david, credType);
            std::string const credIdx = jv[jss::result][jss::index].asString();

            // alice sends 100 MPT to bob with credentials, not authorized
            mpt.pay(alice, bob, 100, tecNO_PERMISSION, {{credIdx}});
            env.close();

            // bob setup depositPreauth on behalf of carol
            env(account_permission::accountPermissionSet(
                bob, carol, {"DepositPreauth"}));
            env.close();

            // bob authorize credentials
            env(deposit::authCredentials(carol, {{david, credType}}),
                onBehalfOf(bob));
            env.close();

            // alice try to send 100 MPT to bob, not authorized
            mpt.pay(alice, bob, 100, tecNO_PERMISSION);
            env.close();

            // alice sends 100 MPT to bob with credentials
            mpt.pay(alice, bob, 100, tesSUCCESS, {{credIdx}});
            env.close();
        }
    }

    void
    testMPTokenIssuanceSetGranular(FeatureBitset features)
    {
        testcase("test MPTokenIssuanceSet granular");
        using namespace jtx;

        // test MPTokenIssuanceUnlock and MPTokenIssuanceLock permissions
        {
            Env env(*this, features);
            Account alice{"alice"};
            Account bob{"bob"};
            env.fund(XRP(100000), alice, bob);
            env.close();

            MPTTester mpt(env, alice, {.fund = false});
            env.close();
            mpt.create({.flags = tfMPTCanLock});
            env.close();

            // alice gives granular permission to bob of MPTokenIssuanceUnlock
            env(account_permission::accountPermissionSet(
                alice, bob, {"MPTokenIssuanceUnlock"}));
            env.close();
            // bob does not have lock permission
            mpt.set(
                {.account = bob,
                 .flags = tfMPTLock,
                 .onBehalfOf = alice,
                 .err = tecNO_PERMISSION});
            // bob now has lock permission, but does not have unlock permission
            env(account_permission::accountPermissionSet(
                alice, bob, {"MPTokenIssuanceLock"}));
            env.close();
            mpt.set({.account = bob, .flags = tfMPTLock, .onBehalfOf = alice});
            mpt.set(
                {.account = bob,
                 .flags = tfMPTUnlock,
                 .onBehalfOf = alice,
                 .err = tecNO_PERMISSION});

            // now bob can lock and unlock
            env(account_permission::accountPermissionSet(
                alice, bob, {"MPTokenIssuanceLock", "MPTokenIssuanceUnlock"}));
            env.close();
            mpt.set(
                {.account = bob, .flags = tfMPTUnlock, .onBehalfOf = alice});
            mpt.set({.account = bob, .flags = tfMPTLock, .onBehalfOf = alice});
            env.close();
        }

        // test mix of granular and transaction level permission
        {
            Env env(*this, features);
            Account alice{"alice"};
            Account bob{"bob"};
            env.fund(XRP(100000), alice, bob);
            env.close();

            MPTTester mpt(env, alice, {.fund = false});
            env.close();
            mpt.create({.flags = tfMPTCanLock});
            env.close();

            // alice gives granular permission to bob of MPTokenIssuanceLock
            env(account_permission::accountPermissionSet(
                alice, bob, {"MPTokenIssuanceLock"}));
            env.close();
            mpt.set({.account = bob, .flags = tfMPTLock, .onBehalfOf = alice});
            // bob does not have unlock permission
            mpt.set(
                {.account = bob,
                 .flags = tfMPTUnlock,
                 .onBehalfOf = alice,
                 .err = tecNO_PERMISSION});

            // alice gives bob some unrelated permission with
            // MPTokenIssuanceLock
            env(account_permission::accountPermissionSet(
                alice,
                bob,
                {"NFTokenMint", "MPTokenIssuanceLock", "NFTokenBurn"}));
            env.close();
            // bob can not unlock
            mpt.set(
                {.account = bob,
                 .flags = tfMPTUnlock,
                 .onBehalfOf = alice,
                 .err = tecNO_PERMISSION});

            // alice add MPTokenIssuanceSet to permissions
            env(account_permission::accountPermissionSet(
                alice,
                bob,
                {"NFTokenMint",
                 "MPTokenIssuanceLock",
                 "NFTokenBurn",
                 "MPTokenIssuanceSet"}));
            mpt.set(
                {.account = bob, .flags = tfMPTUnlock, .onBehalfOf = alice});
            // alice can lock by herself
            mpt.set({.account = alice, .flags = tfMPTLock});
            mpt.set(
                {.account = bob, .flags = tfMPTUnlock, .onBehalfOf = alice});
            mpt.set({.account = bob, .flags = tfMPTLock, .onBehalfOf = alice});
        }
    }

    void
    testNFToken(FeatureBitset features)
    {
        testcase("test NFT transactions");
        using namespace jtx;
        using UriTaxtonPair = std::pair<std::string, std::uint32_t>;

        // test mint on behalf of another account
        {
            Env env(*this, features);
            Account alice{"alice"};
            Account bob{"bob"};
            env.fund(XRP(1000000), alice, bob);
            env.close();

            env(account_permission::accountPermissionSet(
                alice, bob, {"NFTokenMint"}));

            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(ownerCount(env, bob) == 0);

            std::vector<UriTaxtonPair> entries;
            for (std::size_t i = 0; i < 100; i++)
            {
                entries.emplace_back(
                    token::randURI(), rand_int<std::uint32_t>());
            }

            // bob mint 100 nfts on behalf of alice
            for (UriTaxtonPair const& entry : entries)
            {
                if (entry.first.empty())
                    env(token::mint(bob, entry.second), onBehalfOf(alice));
                else
                    env(token::mint(bob, entry.second),
                        token::uri(entry.first),
                        onBehalfOf(alice));

                env.close();
            }

            // bob does not own anything
            BEAST_EXPECT(ownerCount(env, bob) == 0);

            // check alice's NFTs are accurate
            Json::Value aliceNFTs = [&env, &alice]() {
                Json::Value params;
                params[jss::account] = alice.human();
                params[jss::type] = "state";
                return env.rpc("json", "account_nfts", to_string(params));
            }();

            auto const& nfts = aliceNFTs[jss::result][jss::account_nfts];
            BEAST_EXPECT(nfts.size() == entries.size());

            std::vector<Json::Value> sortedNFTs;
            sortedNFTs.reserve(nfts.size());
            for (std::size_t i = 0; i < nfts.size(); ++i)
                sortedNFTs.push_back(nfts[i]);
            std::sort(
                sortedNFTs.begin(),
                sortedNFTs.end(),
                [](Json::Value const& lhs, Json::Value const& rhs) {
                    return lhs[jss::nft_serial] < rhs[jss::nft_serial];
                });

            for (std::size_t i = 0; i < entries.size(); ++i)
            {
                UriTaxtonPair const& entry = entries[i];
                Json::Value const& ret = sortedNFTs[i];

                BEAST_EXPECT(entry.second == ret[sfNFTokenTaxon.jsonName]);
                if (entry.first.empty())
                    BEAST_EXPECT(!ret.isMember(sfURI.jsonName));
                else
                    BEAST_EXPECT(strHex(entry.first) == ret[sfURI.jsonName]);
            }
        }

        // mint on behalf of an authroized minter, create offer and accept offer
        // on behalf of another account, burn nft on behalf of another account
        {
            Env env(*this, features);
            Account alice{"alice"};
            Account bob{"bob"};
            Account carol{"carol"};
            Account minter{"minter"};
            Account const buyer{"buyer"};
            env.fund(XRP(1000000), alice, bob, carol, minter, buyer);
            env.close();

            // alice selects minter as her minter.
            env(token::setMinter(alice, minter));
            env.close();

            // minter authroizes bob
            env(account_permission::accountPermissionSet(
                minter,
                bob,
                {"NFTokenMint", "NFTokenBurn", "NFTokenCreateOffer"}));
            env.close();

            // buyer authroizes alice
            env(account_permission::accountPermissionSet(
                buyer,
                alice,
                {"NFTokenMint", "NFTokenBurn", "NFTokenAcceptOffer"}));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(ownerCount(env, bob) == 0);
            BEAST_EXPECT(ownerCount(env, minter) == 1);
            BEAST_EXPECT(ownerCount(env, buyer) == 1);

            auto buyNFT = [&](std::uint32_t flags) {
                uint256 const nftID{token::getNextID(env, alice, 0u, flags)};

                // bob mint nft on behalf of minter
                env(token::mint(bob, 0u),
                    token::issuer(alice),
                    onBehalfOf(minter),
                    txflags(flags));
                env.close();

                uint256 const offerIndex =
                    keylet::nftoffer(minter, env.seq(minter)).key;

                // bob create offer on behalf of minter
                env(token::createOffer(bob, nftID, XRP(0)),
                    txflags(tfSellNFToken),
                    onBehalfOf(minter));
                env.close();

                // bob accepts offer on behalf of buyer
                env(token::acceptSellOffer(alice, offerIndex),
                    onBehalfOf(buyer));
                env.close();

                return nftID;
            };

            // no flagBurnable, can only be burned by owner
            {
                uint256 const nftID = buyNFT(0);
                env(token::burn(bob, nftID),
                    onBehalfOf(alice),
                    token::owner(buyer),
                    ter(tecNO_PERMISSION));
                env.close();
                env(token::burn(bob, nftID),
                    onBehalfOf(minter),
                    token::owner(buyer),
                    ter(tecNO_PERMISSION));
                env.close();
                BEAST_EXPECT(ownerCount(env, buyer) == 2);
                env(token::burn(alice, nftID),
                    token::owner(buyer),
                    onBehalfOf(buyer));
                env.close();
                BEAST_EXPECT(ownerCount(env, buyer) == 1);
            }

            // enable tfBurnable, issuer alice can burn the nft
            {
                uint256 const nftID = buyNFT(tfBurnable);
                env(account_permission::accountPermissionSet(
                    alice, carol, {"NFTokenMint", "NFTokenBurn"}));
                env.close();

                BEAST_EXPECT(ownerCount(env, buyer) == 2);
                env(token::burn(carol, nftID),
                    onBehalfOf(alice),
                    token::owner(buyer));
                env.close();
                BEAST_EXPECT(ownerCount(env, buyer) == 1);
            }

            // alice set bob as minter and carol burn nft on behalf of bob
            {
                uint256 const nftID = buyNFT(tfBurnable);
                env(token::setMinter(alice, bob));
                env.close();

                env(account_permission::accountPermissionSet(
                    bob, carol, {"NFTokenMint", "NFTokenBurn"}));
                env.close();

                BEAST_EXPECT(ownerCount(env, buyer) == 2);

                // carol burn nft on behalf of bob
                env(token::burn(carol, nftID),
                    onBehalfOf(bob),
                    token::owner(buyer));
                env.close();
                BEAST_EXPECT(ownerCount(env, buyer) == 1);
            }
        }

        // // test dynamic nft, modify onbehalf of other account
        // {
        //     Env env(*this, features);
        //     Account alice{"alice"};
        //     Account bob{"bob"};
        //     env.fund(XRP(1000000), alice, bob);
        //     env.close();

        //     uint256 const nftId{token::getNextID(env, alice, 0u, tfMutable)};
        //     env(token::mint(alice, 0u), txflags(tfMutable));
        //     env.close();

        //     // bob does not have permission to modify the nft
        //     env(token::modify(bob, nftId),
        //         token::owner(alice),
        //         ter(tecNO_PERMISSION));
        //     env.close();

        //     // now alice gives bob permission to modify the nft
        //     env(account_permission::accountPermissionSet(
        //         alice, bob, {"NFTokenModify"}));
        //     env.close();
        //     env(token::modify(bob, nftId), onBehalfOf(alice));
        //     env.close();
        // }

        // mint with flagTransferable
        {
            Env env(*this, features);
            Account alice{"alice"};
            Account bob{"bob"};
            Account carol{"carol"};
            Account const buyer{"buyer"};
            env.fund(XRP(1000000), alice, bob, carol, buyer);
            env.close();

            // alice mint nft by herself
            uint256 const nftAliceID{
                token::getNextID(env, alice, 0u, tfTransferable)};
            env(token::mint(alice, 0u), txflags(tfTransferable));
            env.close();
            BEAST_EXPECT(ownerCount(env, alice) == 1);

            env(account_permission::accountPermissionSet(
                alice,
                bob,
                {"NFTokenMint",
                 "NFTokenBurn",
                 "NFTokenCreateOffer",
                 "NFTokenAcceptOffer"}));
            env.close();
            BEAST_EXPECT(ownerCount(env, alice) == 2);

            env(account_permission::accountPermissionSet(
                bob,
                carol,
                {"NFTokenMint",
                 "NFTokenBurn",
                 "NFTokenCreateOffer",
                 "NFTokenAcceptOffer",
                 "NFTokenCancelOffer"}));
            env.close();
            BEAST_EXPECT(ownerCount(env, bob) == 1);

            // bob creates offer on behalf of alice
            uint256 const aliceSellOfferIndex =
                keylet::nftoffer(alice, env.seq(alice)).key;
            env(token::createOffer(bob, nftAliceID, XRP(20)),
                onBehalfOf(alice),
                txflags(tfSellNFToken));
            env.close();
            BEAST_EXPECT(ownerCount(env, alice) == 3);

            // carol creates offer on behalf of bob
            uint256 const bobBuyOfferIndex =
                keylet::nftoffer(bob, env.seq(bob)).key;
            env(token::createOffer(carol, nftAliceID, XRP(21)),
                onBehalfOf(bob),
                token::owner(alice));
            env.close();
            BEAST_EXPECT(ownerCount(env, bob) == 2);

            // carol accepts offer on behalf of bob
            env(token::acceptSellOffer(carol, aliceSellOfferIndex),
                onBehalfOf(bob));
            env.close();
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(ownerCount(env, bob) == 3);
            BEAST_EXPECT(ownerCount(env, carol) == 0);

            // bob offers to sell the nft by himself
            uint256 const bobSellOfferIndex =
                keylet::nftoffer(bob, env.seq(bob)).key;
            env(token::createOffer(bob, nftAliceID, XRP(22)),
                txflags(tfSellNFToken));
            env.close();
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(ownerCount(env, bob) == 4);
            BEAST_EXPECT(ownerCount(env, carol) == 0);

            env(account_permission::accountPermissionSet(
                buyer,
                alice,
                {"NFTokenMint",
                 "NFTokenBurn",
                 "NFTokenCreateOffer",
                 "NFTokenAcceptOffer"}));
            env.close();

            // alice accepts the offer on behalf of buyer
            env(token::acceptSellOffer(alice, bobSellOfferIndex),
                onBehalfOf(buyer));
            env.close();
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(ownerCount(env, bob) == 2);
            BEAST_EXPECT(ownerCount(env, buyer) == 2);

            // alice sells the nft on behalf of buyer
            uint256 const buyerSellOfferIndex =
                keylet::nftoffer(buyer, env.seq(buyer)).key;
            env(token::createOffer(alice, nftAliceID, XRP(23)),
                onBehalfOf(buyer),
                txflags(tfSellNFToken));
            env.close();
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(ownerCount(env, bob) == 2);
            BEAST_EXPECT(ownerCount(env, buyer) == 3);

            // alice buys back the nft by herself
            env(token::acceptSellOffer(alice, buyerSellOfferIndex));
            env.close();
            BEAST_EXPECT(ownerCount(env, alice) == 2);
            BEAST_EXPECT(ownerCount(env, bob) == 2);
            BEAST_EXPECT(ownerCount(env, buyer) == 1);

            // carol cancel bob's offer on behalf of bob
            env(token::cancelOffer(carol, {bobBuyOfferIndex}), onBehalfOf(bob));
            env.close();
            BEAST_EXPECT(ownerCount(env, alice) == 2);
            BEAST_EXPECT(ownerCount(env, bob) == 1);
            BEAST_EXPECT(ownerCount(env, buyer) == 1);
        }

        // buy and sell nft using IOU
        {
            Env env(*this, features);
            Account gw{"gw"};
            Account alice{"alice"};
            Account bob{"bob"};
            Account carol{"carol"};
            Account const buyer{"buyer"};
            env.fund(XRP(1000000), gw, alice, bob, carol, buyer);
            env.close();

            auto const USD = gw["USD"];
            env(trust(alice, USD(1000)));
            env(trust(bob, USD(1000)));
            env.close();
            env(pay(gw, alice, USD(500)));
            env(pay(gw, bob, USD(500)));
            env.close();

            std::uint16_t transferFee = 5000;

            // alice mint nft by herself
            uint256 const nftAliceID{
                token::getNextID(env, alice, 0u, tfTransferable, transferFee)};
            env(token::mint(alice, 0u),
                token::xferFee(transferFee),
                txflags(tfTransferable));
            env.close();
            BEAST_EXPECT(ownerCount(env, alice) == 2);

            env(account_permission::accountPermissionSet(
                alice,
                bob,
                {"NFTokenMint",
                 "NFTokenBurn",
                 "NFTokenCreateOffer",
                 "NFTokenAcceptOffer"}));
            env.close();
            BEAST_EXPECT(ownerCount(env, alice) == 3);

            env(account_permission::accountPermissionSet(
                bob,
                carol,
                {"NFTokenMint",
                 "NFTokenBurn",
                 "NFTokenCreateOffer",
                 "NFTokenAcceptOffer"}));
            env.close();
            BEAST_EXPECT(ownerCount(env, bob) == 2);

            // bob sells the nft for 200 USD on behalf of alice
            uint256 const aliceSellOfferIndex =
                keylet::nftoffer(alice, env.seq(alice)).key;
            env(token::createOffer(bob, nftAliceID, USD(200)),
                onBehalfOf(alice),
                txflags(tfSellNFToken));
            env.close();

            // carol accept the sell offer on behalf of bob
            env(token::acceptSellOffer(carol, aliceSellOfferIndex),
                onBehalfOf(bob));
            env.close();

            BEAST_EXPECT(env.balance(alice, USD) == USD(700));

            // can not sell for CAD
            env(token::createOffer(carol, nftAliceID, gw["CAD"](50)),
                onBehalfOf(bob),
                txflags(tfSellNFToken),
                ter(tecNO_LINE));
            env.close();
        }
    }

    void
    testOracle(FeatureBitset features)
    {
        testcase("test oracle");
        using namespace jtx;

        Env env(*this, features);
        Account alice{"alice"};
        Account bob{"bob"};
        env.fund(XRP(1'000), alice, bob);

        env(account_permission::accountPermissionSet(
            bob, alice, {"OracleSet", "OracleDelete"}));
        env.close(std::chrono::seconds(maxLastUpdateTimeDelta + 100));

        // alice create oracle on behalf of bob
        oracle::Oracle oracle(
            env,
            {.series = {{"XRP", "USD", 740, 1}},
             .onBehalfOf = bob,
             .sender = alice});
        BEAST_EXPECT(oracle.exists());
        BEAST_EXPECT(ownerCount(env, alice) == 0);
        BEAST_EXPECT(ownerCount(env, bob) == 2);
        // bob delete oracle himself
        oracle.remove({});
        BEAST_EXPECT(!oracle.exists());
        BEAST_EXPECT(ownerCount(env, bob) == 1);

        // alice create oracle2 on behalf of bob
        oracle::Oracle oracle2(env, {.onBehalfOf = bob, .sender = alice});
        BEAST_EXPECT(oracle2.exists());
        BEAST_EXPECT(ownerCount(env, alice) == 0);
        BEAST_EXPECT(ownerCount(env, bob) == 2);

        // alice updates oracle2 on behalf of bob
        oracle2.set(oracle::UpdateArg{
            .series = {{"XRP", "USD", 740, 2}},
            .onBehalfOf = bob,
            .sender = alice});
        BEAST_EXPECT(oracle2.expectPrice({{"XRP", "USD", 740, 2}}));
        BEAST_EXPECT(ownerCount(env, alice) == 0);
        BEAST_EXPECT(ownerCount(env, bob) == 2);

        oracle2.set(oracle::UpdateArg{
            .series = {{"XRP", "EUR", 700, 2}},
            .onBehalfOf = bob,
            .sender = alice});
        BEAST_EXPECT(oracle2.expectPrice(
            {{"XRP", "USD", 0, 0}, {"XRP", "EUR", 700, 2}}));
        BEAST_EXPECT(ownerCount(env, bob) == 2);

        // bob updates oracle2 himself
        oracle2.set(oracle::UpdateArg{
            .series = {{"XRP", "USD", 741, 2}, {"XRP", "EUR", 710, 2}}});
        BEAST_EXPECT(oracle2.expectPrice(
            {{"XRP", "USD", 741, 2}, {"XRP", "EUR", 710, 2}}));
        BEAST_EXPECT(ownerCount(env, bob) == 2);

        // alice updates oracle2 on behalf of bob
        oracle2.set(oracle::UpdateArg{
            .series =
                {
                    {"BTC", "USD", 741, 2},
                    {"ETH", "EUR", 710, 2},
                    {"YAN", "EUR", 710, 2},
                    {"CAN", "EUR", 710, 2},
                },
            .onBehalfOf = bob,
            .sender = alice});
        BEAST_EXPECT(ownerCount(env, bob) == 3);

        oracle2.set(oracle::UpdateArg{
            .series = {{"BTC", "USD", std::nullopt, std::nullopt}}});

        oracle2.set(oracle::UpdateArg{
            .series =
                {{"XRP", "USD", 742, 2},
                 {"XRP", "EUR", 711, 2},
                 {"ETH", "EUR", std::nullopt, std::nullopt},
                 {"YAN", "EUR", std::nullopt, std::nullopt},
                 {"CAN", "EUR", std::nullopt, std::nullopt}},
            .onBehalfOf = bob,
            .sender = alice});
        BEAST_EXPECT(oracle2.expectPrice(
            {{"XRP", "USD", 742, 2}, {"XRP", "EUR", 711, 2}}));

        BEAST_EXPECT(ownerCount(env, bob) == 2);

        auto const index = env.closed()->seq();
        auto const hash = env.closed()->info().hash;
        for (int i = 0; i < 256; ++i)
            env.close();
        auto const acctDelFee{drops(env.current()->fees().increment)};

        // deleting account bob deletes oracle2
        env(acctdelete(bob, alice), fee(acctDelFee));
        env.close();
        BEAST_EXPECT(!oracle2.exists());

        // can still get the oracles via the ledger index or hash
        auto verifyLedgerData = [&](auto const& field, auto const& value) {
            Json::Value jvParams;
            jvParams[field] = value;
            jvParams[jss::binary] = false;
            jvParams[jss::type] = jss::oracle;
            Json::Value jrr = env.rpc(
                "json",
                "ledger_data",
                boost::lexical_cast<std::string>(jvParams));
            BEAST_EXPECT(jrr[jss::result][jss::state].size() == 1);
        };
        verifyLedgerData(jss::ledger_index, index);
        verifyLedgerData(jss::ledger_hash, to_string(hash));
    }

    void
    testTrustSet(FeatureBitset features)
    {
        testcase("test TrustSet");
        using namespace jtx;

        // test create trustline
        {
            Env env(*this, features);
            Account gw{"gw"};
            Account alice{"alice"};
            Account bob{"bob"};
            env.fund(XRP(1'000), gw, alice, bob);

            env(account_permission::accountPermissionSet(
                bob, alice, {"TrustSet"}));

            // alice send trustset on behalf of bob
            env(trust(alice, gw["USD"](50), 0), onBehalfOf(bob));
            env.close();

            env.require(lines(gw, 1));
            env.require(lines(bob, 1));

            Json::Value jv;
            jv["account"] = bob.human();
            auto bobLines = env.rpc("json", "account_lines", to_string(jv));

            jv["account"] = gw.human();
            auto gwLines = env.rpc("json", "account_lines", to_string(jv));

            BEAST_EXPECT(bobLines[jss::result][jss::lines].size() == 1);
            BEAST_EXPECT(gwLines[jss::result][jss::lines].size() == 1);

            // pay exceeding trustline limit
            env(pay(gw, bob, gw["USD"](200)), ter(tecPATH_PARTIAL));
            env.close();

            // smaller payments should succeed
            env(pay(gw, bob, gw["USD"](20)), ter(tesSUCCESS));
            env.close();

            env.require(balance(bob, gw["USD"](20)));
            env.require(balance(gw, bob["USD"](-20)));
        }

        // test requireAuth
        {
            Env env(*this, features);
            Account gw{"gw"};
            Account alice{"alice"};
            Account bob{"bob"};
            Account carol{"carol"};
            env.fund(XRP(1'000), gw, alice, bob, carol);

            env(fset(gw, asfRequireAuth));
            env.close();
            env.require(flags(gw, asfRequireAuth));

            env(account_permission::accountPermissionSet(
                bob, alice, {"TrustSet"}));
            env(account_permission::accountPermissionSet(
                gw, alice, {"TrustSet"}));
            env.close();

            // alice send trustset on behalf of gw, but source can not be the
            // same as destination
            env(trust(alice, gw["USD"](50), 0),
                onBehalfOf(gw),
                ter(temDST_IS_SRC));
            env.close();

            // alice send trustset on behalf of bob
            env(trust(alice, gw["USD"](50), 0), onBehalfOf(bob));
            env.close();

            env(pay(gw, bob, gw["USD"](10)), ter(tecPATH_DRY));
            env.close();

            // alice authorizes bob to hold gw["USD"] on behalf of gw
            env(trust(alice, gw["USD"](0), bob, tfSetfAuth), onBehalfOf(gw));
            env.close();

            env.require(lines(gw, 1));
            env.require(lines(bob, 1));

            Json::Value jv;
            jv["account"] = bob.human();
            auto bobLines = env.rpc("json", "account_lines", to_string(jv));

            jv["account"] = gw.human();
            auto gwLines = env.rpc("json", "account_lines", to_string(jv));

            BEAST_EXPECT(bobLines[jss::result][jss::lines].size() == 1);
            BEAST_EXPECT(gwLines[jss::result][jss::lines].size() == 1);

            // alice resets trust line limit to 0 on behalf of bob
            // this will delete the trust line
            env(trust(alice, gw["USD"](0), 0), onBehalfOf(bob));
            env.close();

            env.require(lines(gw, 0));
            env.require(lines(bob, 0));
        }

        // create trustline to each other
        {
            Env env(*this, features);
            Account gw{"gw"};
            Account alice{"alice"};
            Account bob{"bob"};
            Account carol{"carol"};
            env.fund(XRP(1'000), gw, alice, bob, carol);
            env.close();

            env(account_permission::accountPermissionSet(
                alice, bob, {"TrustSet"}));
            env(account_permission::accountPermissionSet(
                bob, alice, {"TrustSet"}));
            env.close();
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(ownerCount(env, bob) == 1);

            // alice creates trustline to alice on behalf of bob
            env(trust(alice, alice["USD"](100)), onBehalfOf(bob));
            env.close();
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(ownerCount(env, bob) == 2);

            env.require(lines(alice, 1));
            env.require(lines(bob, 1));

            env(pay(alice, bob, alice["USD"](20)), ter(tesSUCCESS));
            env.close();
            env.require(balance(bob, alice["USD"](20)));
            env.require(balance(alice, bob["USD"](-20)));

            env(pay(bob, alice, bob["USD"](10)), ter(tesSUCCESS));
            env.close();
            env.require(balance(bob, alice["USD"](10)));
            env.require(balance(alice, bob["USD"](-10)));

            env(pay(bob, alice, bob["USD"](11)), ter(tecPATH_PARTIAL));
            env.close();
            env.require(balance(bob, alice["USD"](10)));
            env.require(balance(alice, bob["USD"](-10)));

            env(pay(bob, alice, bob["USD"](10)), ter(tesSUCCESS));
            env.close();
            env.require(balance(bob, alice["USD"](0)));
            env.require(balance(alice, bob["USD"](0)));

            env(trust(bob, bob["USD"](100)), onBehalfOf(alice));
            env.close();
            env(pay(bob, alice, bob["USD"](5)), ter(tesSUCCESS));
            env.close();

            env.require(lines(alice, 1));
            env.require(lines(bob, 1));

            env.require(balance(bob, alice["USD"](-5)));
            env.require(balance(alice, bob["USD"](5)));
        }

        // create trustline when asfDisallowIncomingTrustline is set
        // create trustline with tfSetNoRipple
        {
            Env env(*this, features);
            Account gw{"gw"};
            Account alice{"alice"};
            Account bob{"bob"};
            Account carol{"carol"};
            env.fund(XRP(1'000), gw, alice, bob, carol);
            env.close();

            env(fset(gw, asfDisallowIncomingTrustline));
            env.close();

            env(account_permission::accountPermissionSet(
                bob, alice, {"TrustSet"}));
            env(account_permission::accountPermissionSet(
                gw, alice, {"TrustSet"}));
            env.close();

            // can not create trustline when asfDisallowIncomingTrustline is set
            auto const USD = gw["USD"];
            env(trust(alice, USD(1000)),
                onBehalfOf(bob),
                ter(tecNO_PERMISSION));
            env.close();

            env(fclear(gw, asfDisallowIncomingTrustline));
            env.close();

            // alice can create trustline on behalf of bob when
            // asfDisallowIncomingTrustline is cleared
            env(trust(alice, USD(1000)), onBehalfOf(bob));
            env.close();

            env(pay(gw, bob, USD(200)));
            env.close();
            env.require(balance(gw, bob["USD"](-200)));
            env.require(balance(bob, gw["USD"](200)));

            // alice create trustline on behalf of gw to carol with
            // tfSetNoRipple flag
            env(trust(alice, USD(2000), carol, tfSetNoRipple), onBehalfOf(gw));
            env.close();

            Json::Value carolJson;
            carolJson[jss::account] = carol.human();
            Json::Value response =
                env.rpc("json", "account_lines", to_string(carolJson));
            auto const& line = response[jss::result][jss::lines][0u];
            BEAST_EXPECT(line[jss::no_ripple_peer].asBool() == true);
        }
    }

    void
    testXChain(FeatureBitset features)
    {
        testcase("test XChain transactions");
        using namespace jtx;

        // create two chains
        Env env(*this, features);
        Env envX(*this, features);
        XRPAmount const baseFee{env.current()->fees().base};

        // fund initial accounts
        Account door = Account("door");
        Account alice = Account("alice");
        Account bob = Account("bob");
        env.fund(XRP(100000), door, alice, bob);
        env.close();
        Account attesterX = Account("attesterX");
        Account signerX = Account("signerX");
        Account rewardX = Account("rewardX");
        Account aliceX = Account("aliceX");
        Account bobX = Account("bobX");
        Account carolX = Account{"carolX"};
        envX.fund(XRP(100000), attesterX, signerX, rewardX, bobX, carolX);
        envX.close();
        std::vector<jtx::signer> signerXs = {jtx::signer(signerX)};

        auto doorBalance = env.balance(door, XRP);
        auto aliceBalance = env.balance(alice, XRP);
        auto bobBalance = env.balance(bob, XRP);
        // door on the side chain has to be master account for XRP
        auto doorXBalance = envX.balance(Account::master, XRP);
        auto attesterXBalance = envX.balance(attesterX, XRP);
        auto signerXBalance = envX.balance(signerX, XRP);
        auto rewardXBalance = envX.balance(rewardX, XRP);
        auto aliceXBalance = envX.balance(aliceX, XRP);
        auto bobXBalance = envX.balance(bobX, XRP);
        auto carolXBalance = envX.balance(carolX, XRP);

        // XChainCreateBridge
        Json::Value jvBridge =
            bridge(door, xrpIssue(), Account::master, xrpIssue());
        {
            env(bridge_create(bob, jvBridge, XRP(1), XRP(100)),
                onBehalfOf(door),
                ter(tecNO_PERMISSION));
            env.close();
            env.require(balance(bob, bobBalance - drops(baseFee)));
            bobBalance = env.balance(bob, XRP);

            env(account_permission::accountPermissionSet(
                door, bob, {"XChainCreateBridge"}));
            env.close();
            env.require(balance(door, doorBalance - drops(baseFee)));
            doorBalance = env.balance(door, XRP);

            env(bridge_create(bob, jvBridge, XRP(1), XRP(100)),
                onBehalfOf(door));
            env.close();
            env.require(balance(bob, bobBalance - drops(baseFee)));
            bobBalance = env.balance(bob, XRP);
        }
        {
            envX(
                bridge_create(bobX, jvBridge, XRP(1), XRP(100)),
                onBehalfOf(Account::master),
                ter(tecNO_PERMISSION));
            envX.close();
            envX.require(balance(bobX, bobXBalance - drops(baseFee)));
            bobXBalance = envX.balance(bobX, XRP);

            envX(account_permission::accountPermissionSet(
                Account::master, bobX, {"XChainCreateBridge"}));
            envX.close();
            envX.require(
                balance(Account::master, doorXBalance - drops(baseFee)));
            doorXBalance = envX.balance(Account::master, XRP);

            envX(
                bridge_create(bobX, jvBridge, XRP(1), XRP(100)),
                onBehalfOf(Account::master));
            envX.close();
            envX.require(balance(bobX, bobXBalance - drops(baseFee)));
            bobXBalance = envX.balance(bobX, XRP);

            // set up signer on envX
            envX(jtx::signers(Account::master, 1, signerXs));
            envX.close();
            envX.require(
                balance(Account::master, doorXBalance - drops(baseFee)));
            doorXBalance = envX.balance(Account::master, XRP);
        }

        // XChainModifyBridge
        {
            env(bridge_modify(bob, jvBridge, XRP(2), XRP(200)),
                onBehalfOf(door),
                ter(tecNO_PERMISSION));
            env.close();
            env.require(balance(bob, bobBalance - drops(baseFee)));
            bobBalance = env.balance(bob, XRP);

            env(account_permission::accountPermissionSet(
                door, bob, {"XChainModifyBridge"}));
            env.close();
            env.require(balance(door, doorBalance - drops(baseFee)));
            doorBalance = env.balance(door, XRP);

            env(bridge_modify(bob, jvBridge, XRP(2), XRP(200)),
                onBehalfOf(door));
            env.close();
            env.require(balance(bob, bobBalance - drops(baseFee)));
            bobBalance = env.balance(bob, XRP);
        }
        {
            envX(
                bridge_modify(bobX, jvBridge, XRP(2), XRP(200)),
                onBehalfOf(Account::master),
                ter(tecNO_PERMISSION));
            envX.close();
            envX.require(balance(bobX, bobXBalance - drops(baseFee)));
            bobXBalance = envX.balance(bobX, XRP);

            envX(account_permission::accountPermissionSet(
                Account::master, bobX, {"XChainModifyBridge"}));
            envX.close();
            envX.require(
                balance(Account::master, doorXBalance - drops(baseFee)));
            doorXBalance = envX.balance(Account::master, XRP);

            envX(
                bridge_modify(bobX, jvBridge, XRP(2), XRP(200)),
                onBehalfOf(Account::master));
            envX.close();
            envX.require(balance(bobX, bobXBalance - drops(baseFee)));
            bobXBalance = envX.balance(bobX, XRP);
        }

        // XChainAccountCreateCommit
        {
            env(sidechain_xchain_account_create(
                    bob, jvBridge, aliceX, XRP(10000), XRP(2)),
                onBehalfOf(alice),
                ter(tecNO_PERMISSION));
            env.close();
            env.require(balance(bob, bobBalance - drops(baseFee)));
            bobBalance = env.balance(bob, XRP);

            env(account_permission::accountPermissionSet(
                alice, bob, {"XChainAccountCreateCommit"}));
            env.close();
            env.require(balance(alice, aliceBalance - drops(baseFee)));
            aliceBalance = env.balance(alice, XRP);

            env(sidechain_xchain_account_create(
                    bob, jvBridge, aliceX, XRP(10000), XRP(2)),
                onBehalfOf(alice));
            env.close();
            env.require(balance(bob, bobBalance - drops(baseFee)));
            env.require(balance(alice, aliceBalance - XRP(10000) - XRP(2)));
            env.require(balance(door, doorBalance + XRP(10000) + XRP(2)));
            bobBalance = env.balance(bob, XRP);
            aliceBalance = env.balance(alice, XRP);
            doorBalance = env.balance(door, XRP);
        }

        // XChainAddAccountCreateAttestation
        {
            envX(
                create_account_attestation(
                    bobX,
                    jvBridge,
                    alice,
                    XRP(10000),
                    XRP(2),
                    rewardX,
                    true,
                    1,
                    aliceX,
                    signerXs[0]),
                onBehalfOf(attesterX),
                ter(tecNO_PERMISSION));
            envX.close();
            envX.require(balance(bobX, bobXBalance - drops(baseFee)));
            bobXBalance = envX.balance(bobX, XRP);

            envX(account_permission::accountPermissionSet(
                attesterX, bobX, {"XChainAddAccountCreateAttestation"}));
            envX.close();
            envX.require(balance(attesterX, attesterXBalance - drops(baseFee)));
            attesterXBalance = envX.balance(attesterX, XRP);

            envX(
                create_account_attestation(
                    bobX,
                    jvBridge,
                    alice,
                    XRP(10000),
                    XRP(2),
                    rewardX,
                    true,
                    1,
                    aliceX,
                    signerXs[0]),
                onBehalfOf(attesterX));
            envX.close();
            BEAST_EXPECT(envX.le(aliceX));
            envX.require(balance(bobX, bobXBalance - drops(baseFee)));
            envX.require(
                balance(Account::master, doorXBalance - XRP(10000) - XRP(2)));
            envX.require(balance(aliceX, aliceXBalance + XRP(10000)));
            envX.require(balance(rewardX, rewardXBalance + XRP(2)));
            bobXBalance = envX.balance(bobX, XRP);
            doorXBalance = envX.balance(Account::master, XRP);
            aliceXBalance = envX.balance(aliceX, XRP);
            rewardXBalance = envX.balance(rewardX, XRP);
        }
        envX.memoize(aliceX);

        // XChainCreateClaimID
        {
            envX(
                xchain_create_claim_id(bobX, jvBridge, XRP(2), alice),
                onBehalfOf(carolX),
                ter(tecNO_PERMISSION));
            envX.close();
            envX.require(balance(bobX, bobXBalance - drops(baseFee)));
            bobXBalance = envX.balance(bobX, XRP);

            envX(account_permission::accountPermissionSet(
                carolX, bobX, {"XChainCreateClaimID"}));
            envX.close();
            envX.require(balance(carolX, carolXBalance - drops(baseFee)));
            carolXBalance = envX.balance(carolX, XRP);

            envX(
                xchain_create_claim_id(bobX, jvBridge, XRP(2), alice),
                onBehalfOf(carolX));
            envX.close();
            envX.require(balance(bobX, bobXBalance - drops(baseFee)));
            bobXBalance = envX.balance(bobX, XRP);
            BEAST_EXPECT(
                !!envX.le(keylet::xChainClaimID(STXChainBridge(jvBridge), 1)));
        }

        // XChainCommit
        {
            env(xchain_commit(bob, jvBridge, 1, XRP(20000), std::nullopt),
                onBehalfOf(alice),
                ter(tecNO_PERMISSION));
            env.close();
            env.require(balance(bob, bobBalance - drops(baseFee)));
            bobBalance = env.balance(bob, XRP);

            env(account_permission::accountPermissionSet(
                alice, bob, {"XChainCommit"}));
            env.close();
            env.require(balance(alice, aliceBalance - drops(baseFee)));
            aliceBalance = env.balance(alice, XRP);

            env(xchain_commit(bob, jvBridge, 1, XRP(20000), std::nullopt),
                onBehalfOf(alice));
            env.close();
            env.require(balance(bob, bobBalance - drops(baseFee)));
            env.require(balance(alice, aliceBalance - XRP(20000)));
            env.require(balance(door, doorBalance + XRP(20000)));
            bobBalance = env.balance(bob, XRP);
            aliceBalance = env.balance(alice, XRP);
            doorBalance = env.balance(door, XRP);
        }

        // XChainAddClaimAttestation
        {
            envX(
                claim_attestation(
                    bobX,
                    jvBridge,
                    alice,
                    XRP(20000),
                    rewardX,
                    true,
                    1,
                    std::nullopt,
                    signerX),
                onBehalfOf(attesterX),
                ter(tecNO_PERMISSION));
            envX.close();
            envX.require(balance(bobX, bobXBalance - drops(baseFee)));
            bobXBalance = envX.balance(bobX, XRP);

            envX(account_permission::accountPermissionSet(
                attesterX, bobX, {"XChainAddClaimAttestation"}));
            envX.close();
            envX.require(balance(attesterX, attesterXBalance - drops(baseFee)));
            attesterXBalance = envX.balance(attesterX, XRP);

            envX(
                claim_attestation(
                    bobX,
                    jvBridge,
                    alice,
                    XRP(20000),
                    rewardX,
                    true,
                    1,
                    std::nullopt,
                    signerX),
                onBehalfOf(attesterX));
            envX.close();
            envX.require(balance(bobX, bobXBalance - drops(baseFee)));
            bobXBalance = envX.balance(bobX, XRP);
        }

        // XChainClaim
        {
            envX(
                xchain_claim(bobX, jvBridge, 1, XRP(20000), aliceX),
                onBehalfOf(carolX),
                ter(tecNO_PERMISSION));
            envX.close();
            envX.require(balance(bobX, bobXBalance - drops(baseFee)));
            bobXBalance = envX.balance(bobX, XRP);

            envX(account_permission::accountPermissionSet(
                carolX, bobX, {"XChainClaim"}));
            envX.close();
            envX.require(balance(carolX, carolXBalance - drops(baseFee)));
            carolXBalance = envX.balance(carolX, XRP);

            envX(
                xchain_claim(bobX, jvBridge, 1, XRP(20000), aliceX),
                onBehalfOf(carolX));
            envX.close();
            envX.require(balance(bobX, bobXBalance - drops(baseFee)));
            envX.require(balance(carolX, carolXBalance - XRP(2)));
            envX.require(balance(Account::master, doorXBalance - XRP(20000)));
            envX.require(balance(rewardX, rewardXBalance + XRP(2)));
            envX.require(balance(aliceX, aliceXBalance + XRP(20000)));
            bobXBalance = envX.balance(bobX, XRP);
            carolXBalance = envX.balance(carolX, XRP);
            doorXBalance = envX.balance(Account::master, XRP);
            rewardXBalance = envX.balance(rewardX, XRP);
            aliceXBalance = envX.balance(aliceX, XRP);
            BEAST_EXPECT(
                !envX.le(keylet::xChainClaimID(STXChainBridge(jvBridge), 1)));
        }

        env.require(balance(door, doorBalance));
        env.require(balance(alice, aliceBalance));
        env.require(balance(bob, bobBalance));
        envX.require(balance(Account::master, doorXBalance));
        envX.require(balance(attesterX, attesterXBalance));
        envX.require(balance(signerX, signerXBalance));
        envX.require(balance(rewardX, rewardXBalance));
        envX.require(balance(aliceX, aliceXBalance));
        envX.require(balance(bobX, bobXBalance));
        envX.require(balance(carolX, carolXBalance));
    }

    void
    testPaymentChannel(FeatureBitset features)
    {
        testcase("test PaymentChannel transactions");
        using namespace jtx;

        auto signClaimAuth = [&](PublicKey const& pk,
                                 SecretKey const& sk,
                                 uint256 const& channel,
                                 STAmount const& authAmt) {
            Serializer msg;
            serializePayChanAuthorization(msg, channel, authAmt.xrp());
            return sign(pk, sk, msg.slice());
        };

        {
            Env env(*this, features);
            Account alice{"alice"};
            Account bob{"bob"};
            Account carol{"carol"};
            env.fund(XRP(10000), alice, bob, carol);
            env.close();

            env(account_permission::accountPermissionSet(
                alice,
                carol,
                {"PaymentChannelCreate",
                 "PaymentChannelFund",
                 "PaymentChannelClaim"}));

            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(ownerCount(env, carol) == 0);

            auto const settleDelay = std::chrono::seconds(100);
            auto const chan = channel(alice, bob, env.seq(alice));

            // carol creates channel on behalf of alice
            // since carol will send the transaction on behalf of alice, public
            // key is alice's key
            auto const pkAlice = alice.pk();
            env(create(carol, bob, XRP(1000), settleDelay, pkAlice),
                onBehalfOf(alice));
            BEAST_EXPECT(channelExists(*env.current(), chan));
            BEAST_EXPECT(ownerCount(env, alice) == 2);
            BEAST_EXPECT(ownerCount(env, carol) == 0);
            BEAST_EXPECT(channelBalance(*env.current(), chan) == XRP(0));
            BEAST_EXPECT(channelAmount(*env.current(), chan) == XRP(1000));

            {
                // carol fund channel on behalf of alice
                auto const preAlice = env.balance(alice);
                auto const preCarol = env.balance(carol);
                env(fund(carol, chan, XRP(1000)), onBehalfOf(alice));
                auto const feeDrops = env.current()->fees().base;

                BEAST_EXPECT(env.balance(alice) == preAlice - XRP(1000));
                BEAST_EXPECT(env.balance(carol) == preCarol - feeDrops);
                BEAST_EXPECT(channelBalance(*env.current(), chan) == XRP(0));
                BEAST_EXPECT(channelAmount(*env.current(), chan) == XRP(2000));
            }

            env(account_permission::accountPermissionSet(
                bob,
                carol,
                {"PaymentChannelCreate",
                 "PaymentChannelFund",
                 "PaymentChannelClaim"}));

            {
                // carol claim on behalf of bob
                auto preBob = env.balance(bob);
                auto preCarol = env.balance(carol);
                auto const delta = XRP(500);
                auto chanBal = channelBalance(*env.current(), chan);
                auto chanAmt = channelAmount(*env.current(), chan);
                auto const reqBal = chanBal + delta;
                auto const authAmt = reqBal + XRP(100);
                auto const sig =
                    signClaimAuth(alice.pk(), alice.sk(), chan, authAmt);
                env(claim(carol, chan, reqBal, authAmt, Slice(sig), alice.pk()),
                    onBehalfOf(bob));
                BEAST_EXPECT(channelBalance(*env.current(), chan) == reqBal);
                BEAST_EXPECT(channelAmount(*env.current(), chan) == chanAmt);
                auto const feeDrops = env.current()->fees().base;
                BEAST_EXPECT(env.balance(bob) == preBob + delta);
                BEAST_EXPECT(env.balance(carol) == preCarol - feeDrops);
            }
        }
    }

    void
    testPayment(FeatureBitset features)
    {
        testcase("test payment");
        using namespace jtx;

        Env env(*this, features);
        Account alice{"alice"};
        Account bob{"bob"};
        Account carol{"carol"};

        XRPAmount const baseFee{env.current()->fees().base};

        // use different initial amout to distinguish the source balance
        env.fund(XRP(10000), alice);
        env.fund(XRP(20000), bob);
        env.fund(XRP(30000), carol);
        env.close();
        auto aliceBalance = env.balance(alice, XRP);
        auto bobBalance = env.balance(bob, XRP);
        auto carolBalance = env.balance(carol, XRP);

        env(account_permission::accountPermissionSet(alice, bob, {"Payment"}));
        env.close();
        env.require(balance(alice, aliceBalance - drops(baseFee)));
        aliceBalance = env.balance(alice, XRP);

        // bob pay 50 XRP to carol on behalf of alice
        env(pay(bob, carol, XRP(50)), onBehalfOf(alice));
        env.close();
        env.require(balance(alice, aliceBalance - XRP(50)));
        env.require(balance(bob, bobBalance - drops(baseFee)));
        env.require(balance(carol, carolBalance + XRP(50)));
        aliceBalance = env.balance(alice, XRP);
        bobBalance = env.balance(bob, XRP);
        carolBalance = env.balance(carol, XRP);

        // bob pay 50 XRP to bob self on behalf of alice
        env(pay(bob, bob, XRP(50)), onBehalfOf(alice));
        env.close();
        env.require(balance(alice, aliceBalance - XRP(50)));
        env.require(balance(bob, bobBalance + XRP(50) - drops(baseFee)));
        aliceBalance = env.balance(alice, XRP);
        bobBalance = env.balance(bob, XRP);

        // bob pay 50 XRP to alice self on behalf of alice
        env(pay(bob, alice, XRP(50)), onBehalfOf(alice), ter(temREDUNDANT));
        env.close();

        // final balance check
        env.require(balance(alice, aliceBalance));
        env.require(balance(bob, bobBalance));
        env.require(balance(carol, carolBalance));
    }

    void
    testPaymentGranular(FeatureBitset features)
    {
        testcase("test payment granular");
        using namespace jtx;

        // test PaymentMint and PaymentBurn
        {
            Env env(*this, features);
            Account alice{"alice"};
            Account bob{"bob"};
            Account gw{"gateway"};
            auto const USD = gw["USD"];

            env.fund(XRP(10000), alice);
            env.fund(XRP(20000), bob);
            env.fund(XRP(40000), gw);
            env.trust(USD(200), alice);
            env.close();

            XRPAmount const baseFee{env.current()->fees().base};
            auto aliceBalance = env.balance(alice, XRP);
            auto bobBalance = env.balance(bob, XRP);
            auto gwBalance = env.balance(gw, XRP);

            // gw gives bob burn permission
            env(account_permission::accountPermissionSet(
                gw, bob, {"PaymentBurn"}));
            env.close();
            env.require(balance(gw, gwBalance - drops(baseFee)));
            gwBalance = env.balance(gw, XRP);
            // bob can not mint on behalf of gw because he only has burn
            // permission
            env(pay(bob, alice, USD(50)),
                onBehalfOf(gw),
                ter(tecNO_PERMISSION));
            env.close();
            env.require(balance(bob, bobBalance - drops(baseFee)));
            bobBalance = env.balance(bob, XRP);

            // gw gives bob mint permission, alice gives bob burn permission
            env(account_permission::accountPermissionSet(
                gw, bob, {"PaymentMint"}));
            env(account_permission::accountPermissionSet(
                alice, bob, {"PaymentBurn"}));
            env.close();
            env.require(balance(alice, aliceBalance - drops(baseFee)));
            env.require(balance(gw, gwBalance - drops(baseFee)));
            aliceBalance = env.balance(alice, XRP);
            gwBalance = env.balance(gw, XRP);

            // can not send XRP
            env(pay(bob, alice, XRP(50)),
                onBehalfOf(gw),
                ter(tecNO_PERMISSION));
            env.close();
            env.require(balance(bob, bobBalance - drops(baseFee)));
            bobBalance = env.balance(bob, XRP);

            // mint 50 USD
            env(pay(bob, alice, USD(50)), onBehalfOf(gw));
            env.close();
            env.require(balance(bob, bobBalance - drops(baseFee)));
            env.require(balance(gw, alice["USD"](-50)));
            env.require(balance(alice, USD(50)));
            BEAST_EXPECT(env.balance(bob, USD) == USD(0));
            bobBalance = env.balance(bob, XRP);

            // burn 30 USD
            env(pay(bob, gw, USD(30)), onBehalfOf(alice));
            env.close();
            env.require(balance(bob, bobBalance - drops(baseFee)));
            env.require(balance(gw, alice["USD"](-20)));
            env.require(balance(alice, USD(20)));
            BEAST_EXPECT(env.balance(bob, USD) == USD(0));
            bobBalance = env.balance(bob, XRP);

            // final balance check
            env.require(balance(alice, aliceBalance));
            env.require(balance(bob, bobBalance));
            env.require(balance(gw, gwBalance));
        }

        // test PaymentMint won't affect Payment transaction level delegation.
        {
            Env env(*this, features);
            Account alice{"alice"};
            Account bob{"bob"};
            Account gw{"gateway"};
            auto const USD = gw["USD"];

            env.fund(XRP(10000), alice);
            env.fund(XRP(20000), bob);
            env.fund(XRP(40000), gw);
            env.trust(USD(200), alice);
            env.close();

            XRPAmount const baseFee{env.current()->fees().base};

            auto aliceBalance = env.balance(alice, XRP);
            auto bobBalance = env.balance(bob, XRP);
            auto gwBalance = env.balance(gw, XRP);

            // gw gives bob PaymentBurn permission
            env(account_permission::accountPermissionSet(
                gw, bob, {"PaymentBurn"}));
            env.close();
            env.require(balance(gw, gwBalance - drops(baseFee)));
            gwBalance = env.balance(gw, XRP);

            // bob can not mint on behalf of gw because he only has burn
            // permission
            env(pay(bob, alice, USD(50)),
                onBehalfOf(gw),
                ter(tecNO_PERMISSION));
            env.close();
            env.require(balance(bob, bobBalance - drops(baseFee)));
            bobBalance = env.balance(bob, XRP);

            // gw gives bob Payment permission as well
            env(account_permission::accountPermissionSet(
                gw, bob, {"PaymentBurn", "Payment"}));
            env.close();

            // bob now can mint on behalf of gw
            env(pay(bob, alice, USD(50)), onBehalfOf(gw));
            env.close();
            env.require(balance(bob, bobBalance - drops(baseFee)));
            env.require(balance(gw, alice["USD"](-50)));
            env.require(balance(alice, USD(50)));
            BEAST_EXPECT(env.balance(bob, USD) == USD(0));
            bobBalance = env.balance(bob, XRP);
        }
    }

    void
    testOffer(FeatureBitset features)
    {
        testcase("test offer");
        using namespace jtx;

        Env env(*this, features);

        auto const gw = Account{"gateway"};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        auto const USD = gw["USD"];

        env.fund(XRP(10000), alice, bob, gw);
        env.close();
        env.trust(USD(100), alice);
        env.close();
        env(pay(gw, alice, USD(50)));
        env.close();

        env(account_permission::accountPermissionSet(
            alice, bob, {"OfferCreate", "OfferCancel"}));
        env.close();

        // add some distance for alice's sequence
        for (int i = 0; i < 20; i++)
        {
            env(noop(alice));
        }
        env.close();

        // create offer
        auto aliceSeq = env.seq(alice);
        auto bobSeq = env.seq(bob);
        auto const offer1Seq = aliceSeq;
        env(offer(bob, XRP(500), USD(100)), onBehalfOf(alice));
        env.close();
        env.require(offers(alice, 1));
        BEAST_EXPECT(isOffer(env, alice, XRP(500), USD(100)));
        aliceSeq++;
        bobSeq++;
        BEAST_EXPECT(env.seq(alice) == aliceSeq);
        BEAST_EXPECT(env.seq(bob) == bobSeq);

        // create offer while cancelling previous one
        auto const offer2Seq = aliceSeq;
        env(offer(bob, XRP(300), USD(100)),
            json(jss::OfferSequence, offer1Seq),
            onBehalfOf(alice));
        env.close();
        env.require(offers(alice, 1));
        BEAST_EXPECT(
            isOffer(env, alice, XRP(300), USD(100)) &&
            !isOffer(env, alice, XRP(500), USD(100)));
        aliceSeq++;
        bobSeq++;
        BEAST_EXPECT(env.seq(alice) == aliceSeq);
        BEAST_EXPECT(env.seq(bob) == bobSeq);

        // cancel offer
        env(offer_cancel(bob, offer2Seq), onBehalfOf(alice));
        env.close();
        env.require(offers(alice, 0));
        BEAST_EXPECT(!isOffer(env, alice, XRP(300), USD(100)));
        aliceSeq++;
        bobSeq++;
        BEAST_EXPECT(env.seq(alice) == aliceSeq);
        BEAST_EXPECT(env.seq(bob) == bobSeq);
    }

    void
    testTicket(FeatureBitset features)
    {
        testcase("test ticket");
        using namespace jtx;

        Env env(*this, features);
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        env.fund(XRP(10000), alice, bob);
        env.close();

        env(account_permission::accountPermissionSet(
            alice, bob, {"TicketCreate"}));
        env.close();
        env.require(owners(alice, 1), tickets(alice, 0));
        env.require(owners(bob, 0), tickets(bob, 0));

        // add some distance for alice's sequence
        for (int i = 0; i < 20; i++)
        {
            env(noop(alice));
        }
        env.close();

        auto aliceSeq = env.seq(alice);
        auto bobSeq = env.seq(bob);

        // create ticket
        env(ticket::create(bob, 1), onBehalfOf(alice));
        env.close();
        auto aliceTicket1 = aliceSeq + 1;
        aliceSeq += 2;
        bobSeq++;
        BEAST_EXPECT(env.seq(alice) == aliceSeq);
        BEAST_EXPECT(env.seq(bob) == bobSeq);
        env.require(owners(alice, 2), tickets(alice, 1));
        env.require(owners(bob, 0), tickets(bob, 0));

        // use ticket to create tickets
        env(ticket::create(bob, 3),
            onBehalfOf(alice),
            delegateSequence(0),
            delegateTicketSequence(aliceTicket1));
        env.close();
        auto aliceTicket2 = aliceSeq;
        auto aliceTicket3 = aliceSeq + 1;
        auto aliceTicket4 = aliceSeq + 2;
        aliceSeq += 3;
        bobSeq++;
        BEAST_EXPECT(env.seq(alice) == aliceSeq);
        BEAST_EXPECT(env.seq(bob) == bobSeq);
        env.require(owners(alice, 4), tickets(alice, 3));
        env.require(owners(bob, 0), tickets(bob, 0));

        // use tickets
        env(noop(alice), ticket::use(aliceTicket2));
        env(noop(alice), ticket::use(aliceTicket3));
        env(noop(alice), ticket::use(aliceTicket4));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq);
        BEAST_EXPECT(env.seq(bob) == bobSeq);
        env.require(owners(alice, 1), tickets(alice, 0));
        env.require(owners(bob, 0), tickets(bob, 0));

        // create ticket for delegated account
        env(ticket::create(bob, 2));
        env.close();
        auto bobTicket1 = bobSeq + 1;
        auto bobTicket2 = bobSeq + 2;
        bobSeq += 3;
        BEAST_EXPECT(env.seq(bob) == bobSeq);
        env.require(owners(bob, 2), tickets(bob, 2));

        // create ticket with delegated ticket
        env(ticket::create(bob, 1), ticket::use(bobTicket1), onBehalfOf(alice));
        env.close();
        aliceTicket1 = aliceSeq + 1;
        aliceSeq += 2;
        BEAST_EXPECT(env.seq(alice) == aliceSeq);
        BEAST_EXPECT(env.seq(bob) == bobSeq);
        env.require(owners(alice, 2), tickets(alice, 1));
        env.require(owners(bob, 1), tickets(bob, 1));

        // use ticket to create tickets with delegated ticket
        env(ticket::create(bob, 3),
            ticket::use(bobTicket2),
            onBehalfOf(alice),
            delegateSequence(0),
            delegateTicketSequence(aliceTicket1));
        env.close();
        aliceTicket2 = aliceSeq;
        aliceTicket3 = aliceSeq + 1;
        aliceTicket4 = aliceSeq + 2;
        aliceSeq += 3;
        BEAST_EXPECT(env.seq(alice) == aliceSeq);
        BEAST_EXPECT(env.seq(bob) == bobSeq);
        env.require(owners(alice, 4), tickets(alice, 3));
        env.require(owners(bob, 0), tickets(bob, 0));

        // use tickets
        env(noop(alice), ticket::use(aliceTicket2));
        env(noop(alice), ticket::use(aliceTicket3));
        env(noop(alice), ticket::use(aliceTicket4));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq);
        BEAST_EXPECT(env.seq(bob) == bobSeq);
        env.require(owners(alice, 1), tickets(alice, 0));
        env.require(owners(bob, 0), tickets(bob, 0));
    }

    void
    testTrustSetGranular(FeatureBitset features)
    {
        testcase("test TrustSet granular permissions");
        using namespace jtx;

        // test TrustlineUnfreeze, TrustlineFreeze and TrustlineAuthorize
        {
            Env env(*this, features);
            Account gw{"gw"};
            Account alice{"alice"};
            Account bob{"bob"};
            env.fund(XRP(10000), gw, alice, bob);
            env(fset(alice, asfRequireAuth));
            env.close();

            env(account_permission::accountPermissionSet(
                alice, bob, {"TrustlineUnfreeze"}));
            env.close();
            // bob can not create trustline on behalf of alice because he only
            // has unfreeze permission
            env(trust(bob, gw["USD"](50), 0),
                onBehalfOf(alice),
                ter(tecNO_PERMISSION));
            env.close();

            // alice creates trustline by herself
            env(trust(alice, gw["USD"](50), 0));
            env.close();

            // unsupported flags
            env(trust(bob, gw["USD"](50), tfSetNoRipple),
                onBehalfOf(alice),
                ter(tecNO_PERMISSION));
            env(trust(bob, gw["USD"](50), tfClearNoRipple),
                onBehalfOf(alice),
                ter(tecNO_PERMISSION));
            env.close();

            // supported flags with wrong permission
            env(trust(bob, gw["USD"](50), tfSetfAuth),
                onBehalfOf(alice),
                ter(tecNO_PERMISSION));
            env(trust(bob, gw["USD"](50), tfSetFreeze),
                onBehalfOf(alice),
                ter(tecNO_PERMISSION));
            env.close();
            env(account_permission::accountPermissionSet(
                alice, bob, {"TrustlineAuthorize"}));
            env.close();
            env(trust(bob, gw["USD"](50), tfClearFreeze),
                onBehalfOf(alice),
                ter(tecNO_PERMISSION));
            env.close();

            // supported flags with correct permission
            env(trust(bob, gw["USD"](50), tfSetfAuth), onBehalfOf(alice));
            env.close();
            env(account_permission::accountPermissionSet(
                alice, bob, {"TrustlineAuthorize", "TrustlineFreeze"}));
            env.close();
            env(trust(bob, gw["USD"](50), tfSetFreeze), onBehalfOf(alice));
            env.close();
            env(account_permission::accountPermissionSet(
                alice, bob, {"TrustlineAuthorize", "TrustlineUnfreeze"}));
            env.close();
            env(trust(bob, gw["USD"](50), tfClearFreeze), onBehalfOf(alice));
            env.close();
            // but bob can not freeze trustline because he no longer has freeze
            // permission
            env(trust(bob, gw["USD"](50), tfSetFreeze),
                onBehalfOf(alice),
                ter(tecNO_PERMISSION));

            // cannot update LimitAmout with granular permission, both high and
            // low account
            env(trust(gw, alice["USD"](50), 0));
            env(account_permission::accountPermissionSet(
                gw, bob, {"TrustlineUnfreeze"}));
            env.close();
            env(trust(bob, gw["USD"](100)),
                onBehalfOf(alice),
                ter(tecNO_PERMISSION));
            env(trust(bob, alice["USD"](100)),
                onBehalfOf(gw),
                ter(tecNO_PERMISSION));
        }

        // test mix of transaction level delegation and granular delegation
        {
            Env env(*this, features);
            Account gw{"gw"};
            Account alice{"alice"};
            Account bob{"bob"};
            env.fund(XRP(10000), gw, alice, bob);
            env(fset(alice, asfRequireAuth));
            env.close();

            env(account_permission::accountPermissionSet(
                alice, bob, {"TrustlineUnfreeze", "NFTokenCreateOffer"}));
            env.close();
            env(trust(bob, gw["USD"](50), 0),
                onBehalfOf(alice),
                ter(tecNO_PERMISSION));

            // add TrustSet permission and some unrelated permission
            env(account_permission::accountPermissionSet(
                alice,
                bob,
                {"TrustlineUnfreeze",
                 "NFTokenCreateOffer",
                 "TrustSet",
                 "AccountTransferRateSet"}));
            env.close();
            env(trust(bob, gw["USD"](50), 0), onBehalfOf(alice));
            env.close();

            // since bob has TrustSet permission, he does not need
            // TrustlineFreeze granular permission to freeze the trustline
            env(trust(bob, gw["USD"](50), tfSetFreeze), onBehalfOf(alice));
            env(trust(bob, gw["USD"](50), tfClearFreeze), onBehalfOf(alice));
            env(trust(bob, gw["USD"](50), tfSetNoRipple), onBehalfOf(alice));
            env(trust(bob, gw["USD"](50), tfClearNoRipple), onBehalfOf(alice));
            env(trust(bob, gw["USD"](50), tfSetfAuth), onBehalfOf(alice));
        }
    }

    void
    testAccountSetGranular(FeatureBitset features)
    {
        testcase("test AccountSet granular permissions");
        using namespace jtx;

        // test AccountDomainSet, AccountEmailHashSet,
        // AccountMessageKeySet,AccountTransferRateSet, and AccountTickSizeSet
        // granular permissions
        {
            Env env(*this, features);
            auto const alice = Account{"alice"};
            auto const bob = Account{"bob"};
            env.fund(XRP(10000), alice, bob);
            env.close();

            // alice gives bob some random permission, which is not related to
            // the AccountSet transaction
            env(account_permission::accountPermissionSet(
                alice, bob, {"TrustlineUnfreeze"}));
            env.close();

            // bob does not have permission to set domain
            // on behalf of alice
            std::string const domain = "example.com";
            auto jt = noop(bob);
            jt[sfDomain.fieldName] = strHex(domain);
            jt[sfOnBehalfOf.fieldName] = alice.human();

            // add granular permission related to AccountSet but is not the
            // correct permission for domain set
            env(account_permission::accountPermissionSet(
                alice, bob, {"TrustlineUnfreeze", "AccountEmailHashSet"}));
            env.close();
            env(jt, ter(tecNO_PERMISSION));

            // alice give granular permission of AccountDomainSet to bob
            env(account_permission::accountPermissionSet(
                alice, bob, {"AccountDomainSet"}));
            env.close();

            // bob set account domain on behalf of alice
            env(jt);
            BEAST_EXPECT((*env.le(alice))[sfDomain] == makeSlice(domain));

            // bob can reset domain
            jt[sfDomain.fieldName] = "";
            env(jt);
            BEAST_EXPECT(!env.le(alice)->isFieldPresent(sfDomain));

            // bob tries to update domain and set email hash,
            // but he does not have permission to set email hash
            jt[sfDomain.fieldName] = strHex(domain);
            std::string const mh("5F31A79367DC3137FADA860C05742EE6");
            jt[sfEmailHash.fieldName] = mh;
            env(jt, ter(tecNO_PERMISSION));

            // alice give granular permission of AccountEmailHashSet to bob
            env(account_permission::accountPermissionSet(
                alice, bob, {"AccountDomainSet", "AccountEmailHashSet"}));
            env.close();
            env(jt);
            BEAST_EXPECT(to_string((*env.le(alice))[sfEmailHash]) == mh);
            BEAST_EXPECT((*env.le(alice))[sfDomain] == makeSlice(domain));

            // bob does not have permission to set message key for alice
            auto const rkp = randomKeyPair(KeyType::ed25519);
            jt[sfMessageKey.fieldName] = strHex(rkp.first.slice());
            env(jt, ter(tecNO_PERMISSION));

            // alice give granular permission of AccountMessageKeySet to bob
            env(account_permission::accountPermissionSet(
                alice,
                bob,
                {"AccountDomainSet",
                 "AccountEmailHashSet",
                 "AccountMessageKeySet"}));
            env.close();

            // bob can set message key for alice
            env(jt);
            BEAST_EXPECT(
                strHex((*env.le(alice))[sfMessageKey]) ==
                strHex(rkp.first.slice()));
            jt[sfMessageKey.fieldName] = "";
            env(jt);
            BEAST_EXPECT(!env.le(alice)->isFieldPresent(sfMessageKey));

            // bob does not have permission to set transfer rate for alice
            env(rate(bob, 2.0), onBehalfOf(alice), ter(tecNO_PERMISSION));

            // alice give granular permission of AccountTransferRateSet to bob
            env(account_permission::accountPermissionSet(
                alice,
                bob,
                {"AccountDomainSet",
                 "AccountEmailHashSet",
                 "AccountMessageKeySet",
                 "AccountTransferRateSet"}));
            env.close();
            env(rate(bob, 2.0), onBehalfOf(alice));
            BEAST_EXPECT((*env.le(alice))[sfTransferRate] == 2000000000);

            // bob does not have permission to set ticksize for alice
            jt[sfTickSize.fieldName] = 8;
            env(jt, ter(tecNO_PERMISSION));

            // alice give granular permission of AccountTickSizeSet to bob
            env(account_permission::accountPermissionSet(
                alice,
                bob,
                {"AccountDomainSet",
                 "AccountEmailHashSet",
                 "AccountMessageKeySet",
                 "AccountTransferRateSet",
                 "AccountTickSizeSet"}));
            env.close();
            env(jt);
            BEAST_EXPECT((*env.le(alice))[sfTickSize] == 8);

            // can not set asfRequireAuth flag for alice
            // get tecOWNERS because alice owns account permission object
            env(fset(bob, asfRequireAuth), onBehalfOf(alice), ter(tecOWNERS));

            // reset account permission will delete the account permission
            // object
            env(account_permission::accountPermissionSet(alice, bob, {}));
            env.close();
            // bib still does not have permission to set asfRequireAuth for
            // alice
            env(fset(bob, asfRequireAuth),
                onBehalfOf(alice),
                ter(tecNO_PERMISSION));
            // alice can set for herself
            env(fset(alice, asfRequireAuth));
            env.require(flags(alice, asfRequireAuth));
            env.close();

            // can not update tick size because bob no longer has permission
            jt[sfTickSize.fieldName] = 7;
            env(jt, ter(tecNO_PERMISSION));

            env(account_permission::accountPermissionSet(
                alice,
                bob,
                {"AccountDomainSet",
                 "AccountEmailHashSet",
                 "AccountMessageKeySet"}));
            env.close();

            // bob does not have permission to set wallet locater for alice
            std::string const locator =
                "9633EC8AF54F16B5286DB1D7B519EF49EEFC050C0C8AC4384F1D88ACD1BFDF"
                "05";
            auto jt2 = noop(bob);
            jt2[sfDomain.fieldName] = strHex(domain);
            jt2[sfOnBehalfOf.fieldName] = alice.human();
            jt2[sfWalletLocator.fieldName] = locator;
            env(jt2, ter(tecNO_PERMISSION));
        }

        // can not set AccountSet flags on behalf of other account
        {
            Env env(*this, features);
            auto const alice = Account{"alice"};
            auto const bob = Account{"bob"};
            env.fund(XRP(10000), alice, bob);
            env.close();

            auto testSetClearFlag = [&](std::uint32_t flag) {
                // bob can not set flag on behalf of alice
                env(fset(bob, flag), onBehalfOf(alice), ter(tecNO_PERMISSION));
                // alice set by herself
                env(fset(alice, flag));
                env.close();
                env.require(flags(alice, flag));
                // bob can not clear on behalf of alice
                env(fclear(bob, flag),
                    onBehalfOf(alice),
                    ter(tecNO_PERMISSION));
            };

            // testSetClearFlag(asfNoFreeze);
            testSetClearFlag(asfRequireAuth);
            testSetClearFlag(asfAllowTrustLineClawback);

            // alice gives some granular permissions to bob
            env(account_permission::accountPermissionSet(
                alice,
                bob,
                {"AccountDomainSet",
                 "AccountEmailHashSet",
                 "AccountMessageKeySet"}));
            env.close();

            testSetClearFlag(asfDefaultRipple);
            testSetClearFlag(asfDepositAuth);
            testSetClearFlag(asfDisallowIncomingCheck);
            testSetClearFlag(asfDisallowIncomingNFTokenOffer);
            testSetClearFlag(asfDisallowIncomingPayChan);
            testSetClearFlag(asfDisallowIncomingTrustline);
            testSetClearFlag(asfDisallowXRP);
            testSetClearFlag(asfRequireDest);
            testSetClearFlag(asfGlobalFreeze);

            // bob can not set asfAccountTxnID on behalf of alice
            env(fset(bob, asfAccountTxnID),
                onBehalfOf(alice),
                ter(tecNO_PERMISSION));
            env(fset(alice, asfAccountTxnID));
            env.close();
            BEAST_EXPECT(env.le(alice)->isFieldPresent(sfAccountTxnID));
            env(fclear(bob, asfAccountTxnID),
                onBehalfOf(alice),
                ter(tecNO_PERMISSION));

            // bob can not set asfAuthorizedNFTokenMinter on behalf of alice
            Json::Value jt = fset(bob, asfAuthorizedNFTokenMinter);
            jt[sfOnBehalfOf.fieldName] = alice.human();
            jt[sfNFTokenMinter.fieldName] = bob.human();
            env(jt, ter(tecNO_PERMISSION));

            // bob gives alice some permissions
            env(account_permission::accountPermissionSet(
                bob,
                alice,
                {"AccountDomainSet",
                 "AccountEmailHashSet",
                 "AccountMessageKeySet"}));
            env.close();

            // since we can not set asfNoFreeze if asfAllowTrustLineClawback is
            // set, which can not be clear either. Test alice set asfNoFreeze on
            // behalf of bob.
            env(fset(alice, asfNoFreeze),
                onBehalfOf(bob),
                ter(tecNO_PERMISSION));
            env(fset(bob, asfNoFreeze));
            env.close();
            env.require(flags(bob, asfNoFreeze));
            // alice can not clear on behalf of bob
            env(fclear(alice, asfNoFreeze),
                onBehalfOf(bob),
                ter(tecNO_PERMISSION));

            // bob can not set asfDisableMaster on behalf of alice
            Account const bobKey{"bobKey", KeyType::secp256k1};
            env(regkey(bob, bobKey));
            env.close();
            env(fset(bob, asfDisableMaster),
                onBehalfOf(alice),
                sig(bob),
                ter(tecNO_PERMISSION));
        }
    }

    void
    testPath(FeatureBitset features)
    {
        testcase("test paths");
        using namespace jtx;
        Env env(
            *this,
            envconfig([](std::unique_ptr<Config> cfg) {
                cfg->PATH_SEARCH_OLD = 7;
                cfg->PATH_SEARCH = 7;
                cfg->PATH_SEARCH_MAX = 10;
                return cfg;
            }),
            features);

        auto const gw = Account("gateway");
        auto const USD = gw["USD"];
        auto const gw2 = Account("gateway2");
        auto const gw2_USD = gw2["USD"];
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        env.fund(XRP(10000), alice, bob, carol, gw, gw2);
        env.trust(USD(600), alice);
        env.trust(gw2_USD(800), alice);
        env.trust(USD(700), bob);
        env.trust(gw2_USD(900), bob);
        env.close();

        env(account_permission::accountPermissionSet(
            alice, carol, {"Payment"}));
        env.close();

        env(pay(gw, alice, USD(70)));
        env(pay(gw2, alice, gw2_USD(70)));
        env(pay(carol, bob, bob["USD"](140)),
            paths(alice["USD"], alice.human()),
            onBehalfOf(alice));
        env.close();
        env.require(balance(alice, USD(0)));
        env.require(balance(alice, gw2_USD(0)));
        env.require(balance(bob, USD(70)));
        env.require(balance(bob, gw2_USD(70)));
        env.require(balance(gw, alice["USD"](0)));
        env.require(balance(gw, bob["USD"](-70)));
        env.require(balance(gw2, alice["USD"](0)));
        env.require(balance(gw2, bob["USD"](-70)));
    }

    void
    run() override
    {
        FeatureBitset const all{jtx::supported_amendments()};
        testFeatureDisabled(all - featureAccountPermission);
        testInvalidRequest(all);
        testAccountDelete(all);
        testReserve(all);
        testAccountPermissionSet(all);
        // testDelegateSequenceAndTicket(all);
        testAMM(all);
        testCheck(all);
        testClawback(all);
        testCredentials(all);
        testDepositPreauth(all);
        testDID(all);
        testEscrow(all);
        testMPToken(all);
        testNFToken(all);
        testOffer(all);
        testOracle(all);
        testPath(all);
        testPayment(all);
        testPaymentChannel(all);
        testTicket(all);
        testTrustSet(all);
        // testXChain(all);
        testPaymentGranular(all);
        testTrustSetGranular(all);
        testAccountSetGranular(all);
        testMPTokenIssuanceSetGranular(all);
    }
};
BEAST_DEFINE_TESTSUITE(AccountPermission, app, ripple);
}  // namespace test
}  // namespace ripple
