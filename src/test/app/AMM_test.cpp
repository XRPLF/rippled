//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2023 Ripple Labs Inc.

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
#include <ripple/app/misc/AMMHelpers.h>
#include <ripple/app/paths/AMMContext.h>
#include <ripple/app/paths/AMMOffer.h>
#include <ripple/protocol/AMMCore.h>
#include <ripple/protocol/STParsedJSON.h>
#include <ripple/resource/Fees.h>
#include <ripple/rpc/RPCHandler.h>
#include <ripple/rpc/impl/RPCHelpers.h>
#include <test/jtx.h>
#include <test/jtx/AMM.h>
#include <test/jtx/AMMTest.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/sendmax.h>

#include <chrono>
#include <utility>
#include <vector>

namespace ripple {
namespace test {

struct AMM_test : public jtx::AMMTest
{
private:
    void
    testInstanceCreate()
    {
        testcase("Instance Create");

        using namespace jtx;

        // XRP to IOU
        testAMM([&](AMM& ammAlice, Env&) {
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000), USD(10000), IOUAmount{10000000, 0}));
        });

        // IOU to IOU
        testAMM(
            [&](AMM& ammAlice, Env&) {
                BEAST_EXPECT(ammAlice.expectBalances(
                    USD(20000), BTC(0.5), IOUAmount{100, 0}));
            },
            {{USD(20000), BTC(0.5)}});

        // IOU to IOU + transfer fee
        {
            Env env{*this};
            fund(env, gw, {alice}, {USD(25000), BTC(0.625)}, Fund::All);
            env(rate(gw, 1.25));
            env.close();
            AMM ammAlice(env, alice, USD(20000), BTC(0.5));
            BEAST_EXPECT(ammAlice.expectBalances(
                USD(20000), BTC(0.5), IOUAmount{100, 0}));
            // 25,000 - 20,000(AMM) - 0.25*20,000=5,000(fee) = 0
            BEAST_EXPECT(expectLine(env, alice, USD(0)));
            // 0.625 - 0.5(AMM) - 0.25*0.5=0.125(fee) = 0
            BEAST_EXPECT(expectLine(env, alice, BTC(0)));
        }

        // Require authorization is set, account is authorized
        {
            Env env{*this};
            env.fund(XRP(30000), gw, alice);
            env.close();
            env(fset(gw, asfRequireAuth));
            env.close();
            env.trust(USD(30000), alice);
            env.close();
            env(trust(gw, alice["USD"](30000)), txflags(tfSetfAuth));
            env.close();
            env(pay(gw, alice, USD(10000)));
            env.close();
            AMM ammAlice(env, alice, XRP(10000), USD(10000));
        }

        // Cleared global freeze
        {
            Env env{*this};
            env.fund(XRP(30000), gw, alice);
            env.close();
            env.trust(USD(30000), alice);
            env.close();
            env(pay(gw, alice, USD(10000)));
            env.close();
            env(fset(gw, asfGlobalFreeze));
            env.close();
            AMM ammAliceFail(
                env, alice, XRP(10000), USD(10000), ter(tecFROZEN));
            env(fclear(gw, asfGlobalFreeze));
            env.close();
            AMM ammAlice(env, alice, XRP(10000), USD(10000));
        }
    }

    void
    testInvalidInstance()
    {
        testcase("Invalid Instance");

        using namespace jtx;

        // Can't have both XRP tokens
        {
            Env env{*this};
            fund(env, gw, {alice}, {USD(30000)}, Fund::All);
            AMM ammAlice(
                env, alice, XRP(10000), XRP(10000), ter(temBAD_AMM_TOKENS));
            BEAST_EXPECT(!ammAlice.ammExists());
        }

        // Can't have both tokens the same IOU
        {
            Env env{*this};
            fund(env, gw, {alice}, {USD(30000)}, Fund::All);
            AMM ammAlice(
                env, alice, USD(10000), USD(10000), ter(temBAD_AMM_TOKENS));
            BEAST_EXPECT(!ammAlice.ammExists());
        }

        // Can't have zero or negative amounts
        {
            Env env{*this};
            fund(env, gw, {alice}, {USD(30000)}, Fund::All);
            AMM ammAlice(env, alice, XRP(0), USD(10000), ter(temBAD_AMOUNT));
            BEAST_EXPECT(!ammAlice.ammExists());
            AMM ammAlice1(env, alice, XRP(10000), USD(0), ter(temBAD_AMOUNT));
            BEAST_EXPECT(!ammAlice1.ammExists());
            AMM ammAlice2(
                env, alice, XRP(10000), USD(-10000), ter(temBAD_AMOUNT));
            BEAST_EXPECT(!ammAlice2.ammExists());
            AMM ammAlice3(
                env, alice, XRP(-10000), USD(10000), ter(temBAD_AMOUNT));
            BEAST_EXPECT(!ammAlice3.ammExists());
        }

        // Bad currency
        {
            Env env{*this};
            fund(env, gw, {alice}, {USD(30000)}, Fund::All);
            AMM ammAlice(
                env, alice, XRP(10000), BAD(10000), ter(temBAD_CURRENCY));
            BEAST_EXPECT(!ammAlice.ammExists());
        }

        // Insufficient IOU balance
        {
            Env env{*this};
            fund(env, gw, {alice}, {USD(30000)}, Fund::All);
            AMM ammAlice(
                env, alice, XRP(10000), USD(40000), ter(tecUNFUNDED_AMM));
            BEAST_EXPECT(!ammAlice.ammExists());
        }

        // Insufficient XRP balance
        {
            Env env{*this};
            fund(env, gw, {alice}, {USD(30000)}, Fund::All);
            AMM ammAlice(
                env, alice, XRP(40000), USD(10000), ter(tecUNFUNDED_AMM));
            BEAST_EXPECT(!ammAlice.ammExists());
        }

        // Invalid trading fee
        {
            Env env{*this};
            fund(env, gw, {alice}, {USD(30000)}, Fund::All);
            AMM ammAlice(
                env,
                alice,
                XRP(10000),
                USD(10000),
                false,
                65001,
                10,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(temBAD_FEE));
            BEAST_EXPECT(!ammAlice.ammExists());
        }

        // AMM already exists
        testAMM([&](AMM& ammAlice, Env& env) {
            AMM ammCarol(env, carol, XRP(10000), USD(10000), ter(tecDUPLICATE));
        });

        // Invalid flags
        {
            Env env{*this};
            fund(env, gw, {alice}, {USD(30000)}, Fund::All);
            AMM ammAlice(
                env,
                alice,
                XRP(10000),
                USD(10000),
                false,
                0,
                10,
                tfWithdrawAll,
                std::nullopt,
                std::nullopt,
                ter(temINVALID_FLAG));
            BEAST_EXPECT(!ammAlice.ammExists());
        }

        // Invalid Account
        {
            Env env{*this};
            Account bad("bad");
            env.memoize(bad);
            AMM ammAlice(
                env,
                bad,
                XRP(10000),
                USD(10000),
                false,
                0,
                10,
                std::nullopt,
                seq(1),
                std::nullopt,
                ter(terNO_ACCOUNT));
            BEAST_EXPECT(!ammAlice.ammExists());
        }

        // Require authorization is set
        {
            Env env{*this};
            env.fund(XRP(30000), gw, alice);
            env.close();
            env(fset(gw, asfRequireAuth));
            env.close();
            env(trust(gw, alice["USD"](30000)));
            env.close();
            AMM ammAlice(env, alice, XRP(10000), USD(10000), ter(tecNO_AUTH));
            BEAST_EXPECT(!ammAlice.ammExists());
        }

        // Globally frozen
        {
            Env env{*this};
            env.fund(XRP(30000), gw, alice);
            env.close();
            env(fset(gw, asfGlobalFreeze));
            env.close();
            env(trust(gw, alice["USD"](30000)));
            env.close();
            AMM ammAlice(env, alice, XRP(10000), USD(10000), ter(tecFROZEN));
            BEAST_EXPECT(!ammAlice.ammExists());
        }

        // Individually frozen
        {
            Env env{*this};
            env.fund(XRP(30000), gw, alice);
            env.close();
            env(trust(gw, alice["USD"](30000)));
            env.close();
            env(trust(gw, alice["USD"](0), tfSetFreeze));
            env.close();
            AMM ammAlice(env, alice, XRP(10000), USD(10000), ter(tecFROZEN));
            BEAST_EXPECT(!ammAlice.ammExists());
        }

        // Insufficient reserve, XRP/IOU
        {
            Env env(*this);
            auto const starting_xrp =
                XRP(1000) + reserve(env, 3) + env.current()->fees().base * 4;
            env.fund(starting_xrp, gw);
            env.fund(starting_xrp, alice);
            env.trust(USD(2000), alice);
            env.close();
            env(pay(gw, alice, USD(2000)));
            env.close();
            env(offer(alice, XRP(101), USD(100)));
            env(offer(alice, XRP(102), USD(100)));
            AMM ammAlice(
                env, alice, XRP(1000), USD(1000), ter(tecUNFUNDED_AMM));
        }

        // Insufficient reserve, IOU/IOU
        {
            Env env(*this);
            auto const starting_xrp =
                reserve(env, 4) + env.current()->fees().base * 5;
            env.fund(starting_xrp, gw);
            env.fund(starting_xrp, alice);
            env.trust(USD(2000), alice);
            env.trust(EUR(2000), alice);
            env.close();
            env(pay(gw, alice, USD(2000)));
            env(pay(gw, alice, EUR(2000)));
            env.close();
            env(offer(alice, EUR(101), USD(100)));
            env(offer(alice, EUR(102), USD(100)));
            AMM ammAlice(
                env, alice, EUR(1000), USD(1000), ter(tecINSUF_RESERVE_LINE));
        }

        // Insufficient fee
        {
            Env env(*this);
            fund(env, gw, {alice}, XRP(2000), {USD(2000), EUR(2000)});
            AMM ammAlice(
                env,
                alice,
                EUR(1000),
                USD(1000),
                false,
                0,
                ammCrtFee(env).drops() - 1,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(telINSUF_FEE_P));
        }
    }

    void
    testInvalidDeposit()
    {
        testcase("Invalid Deposit");

        using namespace jtx;

        testAMM([&](AMM& ammAlice, Env& env) {
            // Invalid flags
            ammAlice.deposit(
                alice,
                1000000,
                std::nullopt,
                tfWithdrawAll,
                ter(temINVALID_FLAG));

            // Invalid options
            std::vector<std::tuple<
                std::optional<std::uint32_t>,
                std::optional<STAmount>,
                std::optional<STAmount>,
                std::optional<STAmount>>>
                invalidOptions = {
                    // tokens, asset1In, asset2in, EPrice
                    {1000, std::nullopt, USD(100), std::nullopt},
                    {1000, std::nullopt, std::nullopt, STAmount{USD, 1, -1}},
                    {std::nullopt,
                     std::nullopt,
                     USD(100),
                     STAmount{USD, 1, -1}},
                    {std::nullopt, XRP(100), USD(100), STAmount{USD, 1, -1}},
                    {1000, XRP(100), USD(100), std::nullopt}};
            for (auto const& it : invalidOptions)
            {
                ammAlice.deposit(
                    alice,
                    std::get<0>(it),
                    std::get<1>(it),
                    std::get<2>(it),
                    std::get<3>(it),
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    ter(temMALFORMED));
            }

            // Invalid tokens
            ammAlice.deposit(
                alice, 0, std::nullopt, std::nullopt, ter(temBAD_AMM_TOKENS));
            ammAlice.deposit(
                alice,
                IOUAmount{-1},
                std::nullopt,
                std::nullopt,
                ter(temBAD_AMM_TOKENS));

            // Invalid tokens - bogus currency
            {
                auto const iss1 = Issue{Currency(0xabc), gw.id()};
                auto const iss2 = Issue{Currency(0xdef), gw.id()};
                ammAlice.deposit(
                    alice,
                    1000,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    {{iss1, iss2}},
                    std::nullopt,
                    ter(terNO_AMM));
            }

            // Depositing mismatched token, invalid Asset1In.issue
            ammAlice.deposit(
                alice,
                GBP(100),
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(temBAD_AMM_TOKENS));

            // Depositing mismatched token, invalid Asset2In.issue
            ammAlice.deposit(
                alice,
                USD(100),
                GBP(100),
                std::nullopt,
                std::nullopt,
                ter(temBAD_AMM_TOKENS));

            // Depositing mismatched token, Asset1In.issue == Asset2In.issue
            ammAlice.deposit(
                alice,
                USD(100),
                USD(100),
                std::nullopt,
                std::nullopt,
                ter(temBAD_AMM_TOKENS));

            // Invalid amount value
            ammAlice.deposit(
                alice,
                USD(0),
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(temBAD_AMOUNT));
            ammAlice.deposit(
                alice,
                USD(-1000),
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(temBAD_AMOUNT));
            ammAlice.deposit(
                alice,
                USD(10),
                std::nullopt,
                USD(-1),
                std::nullopt,
                ter(temBAD_AMOUNT));

            // Bad currency
            ammAlice.deposit(
                alice,
                BAD(100),
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(temBAD_CURRENCY));

            // Invalid Account
            Account bad("bad");
            env.memoize(bad);
            ammAlice.deposit(
                bad,
                1000000,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                seq(1),
                ter(terNO_ACCOUNT));

            // Invalid AMM
            ammAlice.deposit(
                alice,
                1000,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                {{USD, GBP}},
                std::nullopt,
                ter(terNO_AMM));

            // Single deposit: 100000 tokens worth of USD
            // Amount to deposit exceeds Max
            ammAlice.deposit(
                carol,
                100000,
                USD(200),
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecAMM_FAILED_DEPOSIT));

            // Single deposit: 100000 tokens worth of XRP
            // Amount to deposit exceeds Max
            ammAlice.deposit(
                carol,
                100000,
                XRP(200),
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecAMM_FAILED_DEPOSIT));

            // Deposit amount is invalid
            // Calculated amount to deposit is 98,000,000
            ammAlice.deposit(
                alice,
                USD(0),
                std::nullopt,
                STAmount{USD, 1, -1},
                std::nullopt,
                ter(tecUNFUNDED_AMM));
            // Calculated amount is 0
            ammAlice.deposit(
                alice,
                USD(0),
                std::nullopt,
                STAmount{USD, 2000, -6},
                std::nullopt,
                ter(tecAMM_FAILED_DEPOSIT));

            // Tiny deposit
            ammAlice.deposit(
                carol,
                IOUAmount{1, -4},
                std::nullopt,
                std::nullopt,
                ter(tecAMM_FAILED_DEPOSIT));
            ammAlice.deposit(
                carol,
                STAmount{USD, 1, -12},
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecAMM_FAILED_DEPOSIT));
        });

        // Invalid AMM
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.withdrawAll(alice);
            ammAlice.deposit(
                alice, 10000, std::nullopt, std::nullopt, ter(terNO_AMM));
        });

        // Globally frozen asset
        testAMM([&](AMM& ammAlice, Env& env) {
            env(fset(gw, asfGlobalFreeze));
            // Can deposit non-frozen token
            ammAlice.deposit(carol, XRP(100));
            ammAlice.deposit(
                carol,
                USD(100),
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecFROZEN));
            ammAlice.deposit(
                carol, 1000000, std::nullopt, std::nullopt, ter(tecFROZEN));
        });

        // Individually frozen (AMM) account
        testAMM([&](AMM& ammAlice, Env& env) {
            env(trust(gw, carol["USD"](0), tfSetFreeze));
            env.close();
            // Can deposit non-frozen token
            ammAlice.deposit(carol, XRP(100));
            ammAlice.deposit(
                carol, 1000000, std::nullopt, std::nullopt, ter(tecFROZEN));
            ammAlice.deposit(
                carol,
                USD(100),
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecFROZEN));
            env(trust(gw, carol["USD"](0), tfClearFreeze));
            // Individually frozen AMM
            env(trust(
                gw,
                STAmount{Issue{gw["USD"].currency, ammAlice.ammAccount()}, 0},
                tfSetFreeze));
            env.close();
            // Can deposit non-frozen token
            ammAlice.deposit(carol, XRP(100));
            ammAlice.deposit(
                carol, 1000000, std::nullopt, std::nullopt, ter(tecFROZEN));
            ammAlice.deposit(
                carol,
                USD(100),
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecFROZEN));
        });

        // Insufficient XRP balance
        testAMM([&](AMM& ammAlice, Env& env) {
            env.fund(XRP(1000), bob);
            env.close();
            // Adds LPT trustline
            ammAlice.deposit(bob, XRP(10));
            ammAlice.deposit(
                bob,
                XRP(1000),
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecUNFUNDED_AMM));
        });

        // Insufficient USD balance
        testAMM([&](AMM& ammAlice, Env& env) {
            fund(env, gw, {bob}, {USD(1000)}, Fund::Acct);
            env.close();
            ammAlice.deposit(
                bob,
                USD(1001),
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecUNFUNDED_AMM));
        });

        // Insufficient USD balance by tokens
        testAMM([&](AMM& ammAlice, Env& env) {
            fund(env, gw, {bob}, {USD(1000)}, Fund::Acct);
            env.close();
            ammAlice.deposit(
                bob,
                10000000,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecUNFUNDED_AMM));
        });

        // Insufficient XRP balance by tokens
        testAMM([&](AMM& ammAlice, Env& env) {
            env.fund(XRP(1000), bob);
            env.trust(USD(100000), bob);
            env.close();
            env(pay(gw, bob, USD(90000)));
            env.close();
            ammAlice.deposit(
                bob,
                10000000,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecUNFUNDED_AMM));
        });

        // Insufficient reserve, XRP/IOU
        {
            Env env(*this);
            auto const starting_xrp =
                reserve(env, 4) + env.current()->fees().base * 4;
            env.fund(XRP(10000), gw);
            env.fund(XRP(10000), alice);
            env.fund(starting_xrp, carol);
            env.trust(USD(2000), alice);
            env.trust(USD(2000), carol);
            env.close();
            env(pay(gw, alice, USD(2000)));
            env(pay(gw, carol, USD(2000)));
            env.close();
            env(offer(carol, XRP(100), USD(101)));
            env(offer(carol, XRP(100), USD(102)));
            AMM ammAlice(env, alice, XRP(1000), USD(1000));
            ammAlice.deposit(
                carol,
                XRP(100),
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecINSUF_RESERVE_LINE));
        }

        // Insufficient reserve, IOU/IOU
        {
            Env env(*this);
            auto const starting_xrp =
                reserve(env, 4) + env.current()->fees().base * 4;
            env.fund(XRP(10000), gw);
            env.fund(XRP(10000), alice);
            env.fund(starting_xrp, carol);
            env.trust(USD(2000), alice);
            env.trust(EUR(2000), alice);
            env.trust(USD(2000), carol);
            env.trust(EUR(2000), carol);
            env.close();
            env(pay(gw, alice, USD(2000)));
            env(pay(gw, alice, EUR(2000)));
            env(pay(gw, carol, USD(2000)));
            env(pay(gw, carol, EUR(2000)));
            env.close();
            env(offer(carol, XRP(100), USD(101)));
            env(offer(carol, XRP(100), USD(102)));
            AMM ammAlice(env, alice, XRP(1000), USD(1000));
            ammAlice.deposit(
                carol,
                XRP(100),
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecINSUF_RESERVE_LINE));
        }
    }

    void
    testDeposit()
    {
        testcase("Deposit");

        using namespace jtx;

        // Equal deposit: 1000000 tokens, 10% of the current pool
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.deposit(carol, 1000000);
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(11000), USD(11000), IOUAmount{11000000, 0}));
            // 30,000 less deposited 1,000
            BEAST_EXPECT(expectLine(env, carol, USD(29000)));
            // 30,000 less deposited 1,000 and 10 drops tx fee
            BEAST_EXPECT(
                expectLedgerEntryRoot(env, carol, XRPAmount{28999999990}));
        });

        // Equal limit deposit: deposit USD100 and XRP proportionally
        // to the pool composition not to exceed 100XRP. If the amount
        // exceeds 100XRP then deposit 100XRP and USD proportionally
        // to the pool composition not to exceed 100USD. Fail if exceeded.
        // Deposit 100USD/100XRP
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, USD(100), XRP(100));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10100), USD(10100), IOUAmount{10100000, 0}));
        });

        // Equal limit deposit.
        // Try to deposit 200USD/100XRP. Is truncated to 100USD/100XRP.
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, USD(200), XRP(100));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10100), USD(10100), IOUAmount{10100000, 0}));
        });
        // Try to deposit 100USD/200XRP. Is truncated to 100USD/100XRP.
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, USD(100), XRP(200));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10100), USD(10100), IOUAmount{10100000, 0}));
        });

        // Single deposit: 1000 USD
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, USD(1000));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000),
                STAmount{USD, UINT64_C(1099999999999999), -11},
                IOUAmount{1048808848170151, -8}));
        });

        // Single deposit: 1000 XRP
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, XRP(1000));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(11000), USD(10000), IOUAmount{1048808848170151, -8}));
        });

        // Single deposit: 100000 tokens worth of USD
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, 100000, USD(205));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000), USD(10201), IOUAmount{10100000, 0}));
        });

        // Single deposit: 100000 tokens worth of XRP
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, 100000, XRP(205));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10201), USD(10000), IOUAmount{10100000, 0}));
        });

        // Single deposit with EP not exceeding specified:
        // 100USD with EP not to exceed 0.1 (AssetIn/TokensOut)
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(
                carol, USD(1000), std::nullopt, STAmount{USD, 1, -1});
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000),
                STAmount{USD, UINT64_C(1099999999999999), -11},
                IOUAmount{1048808848170151, -8}));
        });

        // Single deposit with EP not exceeding specified:
        // 100USD with EP not to exceed 0.002004 (AssetIn/TokensOut)
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(
                carol, USD(100), std::nullopt, STAmount{USD, 2004, -6});
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000),
                STAmount{USD, 1008016, -2},
                IOUAmount{10040000, 0}));
        });

        // Single deposit with EP not exceeding specified:
        // 0USD with EP not to exceed 0.002004 (AssetIn/TokensOut)
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(
                carol, USD(0), std::nullopt, STAmount{USD, 2004, -6});
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000),
                STAmount{USD, 1008016, -2},
                IOUAmount{10040000, 0}));
        });

        // IOU to IOU + transfer fee
        {
            Env env{*this};
            fund(env, gw, {alice}, {USD(25000), BTC(0.625)}, Fund::All);
            env(rate(gw, 1.25));
            env.close();
            AMM ammAlice(env, alice, USD(20000), BTC(0.5));
            BEAST_EXPECT(ammAlice.expectBalances(
                USD(20000), BTC(0.5), IOUAmount{100, 0}));
            BEAST_EXPECT(expectLine(env, alice, USD(0)));
            BEAST_EXPECT(expectLine(env, alice, BTC(0)));
            fund(env, gw, {carol}, {USD(2500), BTC(0.0625)}, Fund::Acct);
            ammAlice.deposit(carol, 10);
            BEAST_EXPECT(ammAlice.expectBalances(
                USD(22000), BTC(0.55), IOUAmount{110, 0}));
            // 2,500 - 2,000(AMM) - 0.25*2,000=500(fee)=0
            BEAST_EXPECT(expectLine(env, carol, USD(0)));
            // 0.0625 - 0.05(AMM) - 0.25*0.05=0.0125(fee)=0
            BEAST_EXPECT(expectLine(env, carol, BTC(0)));
        }

        // Tiny deposits
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, IOUAmount{1, -3});
            BEAST_EXPECT(ammAlice.expectBalances(
                XRPAmount{10000000001},
                STAmount{USD, UINT64_C(10000000001), -6},
                IOUAmount{10000000001, -3}));
            BEAST_EXPECT(ammAlice.expectLPTokens(carol, IOUAmount{1, -3}));
        });
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, XRPAmount{1});
            BEAST_EXPECT(ammAlice.expectBalances(
                XRPAmount{10000000001},
                USD(10000),
                IOUAmount{1000000000049999, -8}));
            BEAST_EXPECT(ammAlice.expectLPTokens(carol, IOUAmount{49999, -8}));
        });
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, STAmount{USD, 1, -10});
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000),
                STAmount{USD, UINT64_C(1000000000000008), -11},
                IOUAmount{1000000000000004, -8}));
            BEAST_EXPECT(ammAlice.expectLPTokens(carol, IOUAmount{4, -8}));
        });

        // Issuer create/deposit
        {
            Env env(*this);
            env.fund(XRP(30000), gw);
            AMM ammGw(env, gw, XRP(10000), USD(10000));
            BEAST_EXPECT(
                ammGw.expectBalances(XRP(10000), USD(10000), ammGw.tokens()));
            ammGw.deposit(gw, 1000000);
            BEAST_EXPECT(ammGw.expectBalances(
                XRP(11000), USD(11000), IOUAmount{11000000}));
            ammGw.deposit(gw, USD(1000));
            BEAST_EXPECT(ammGw.expectBalances(
                XRP(11000),
                STAmount{USD, UINT64_C(1199999999999998), -11},
                IOUAmount{1148912529307605, -8}));
        }

        // Issuer deposit
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.deposit(gw, 1000000);
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(11000), USD(11000), IOUAmount{11000000}));
            ammAlice.deposit(gw, USD(1000));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(11000),
                STAmount{USD, UINT64_C(1199999999999998), -11},
                IOUAmount{1148912529307605, -8}));
        });
    }

    void
    testInvalidWithdraw()
    {
        testcase("Invalid Withdraw");

        using namespace jtx;

        testAMM([&](AMM& ammAlice, Env& env) {
            // Invalid flags
            ammAlice.withdraw(
                alice,
                1000000,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                tfBurnable,
                std::nullopt,
                std::nullopt,
                ter(temINVALID_FLAG));

            // Invalid options
            std::vector<std::tuple<
                std::optional<std::uint32_t>,
                std::optional<STAmount>,
                std::optional<STAmount>,
                std::optional<IOUAmount>,
                std::optional<std::uint32_t>,
                NotTEC>>
                invalidOptions = {
                    // tokens, asset1Out, asset2Out, EPrice, flags, ter
                    {std::nullopt,
                     std::nullopt,
                     std::nullopt,
                     std::nullopt,
                     std::nullopt,
                     temMALFORMED},
                    {std::nullopt,
                     std::nullopt,
                     std::nullopt,
                     std::nullopt,
                     tfSingleAsset | tfTwoAsset,
                     temMALFORMED},
                    {1000,
                     std::nullopt,
                     std::nullopt,
                     std::nullopt,
                     tfWithdrawAll,
                     temMALFORMED},
                    {std::nullopt,
                     USD(0),
                     XRP(100),
                     std::nullopt,
                     tfWithdrawAll | tfLPToken,
                     temMALFORMED},
                    {std::nullopt,
                     std::nullopt,
                     USD(100),
                     std::nullopt,
                     tfWithdrawAll,
                     temMALFORMED},
                    {std::nullopt,
                     std::nullopt,
                     std::nullopt,
                     std::nullopt,
                     tfWithdrawAll | tfOneAssetWithdrawAll,
                     temMALFORMED},
                    {std::nullopt,
                     USD(100),
                     std::nullopt,
                     std::nullopt,
                     tfWithdrawAll,
                     temMALFORMED},
                    {std::nullopt,
                     std::nullopt,
                     std::nullopt,
                     std::nullopt,
                     tfOneAssetWithdrawAll,
                     temMALFORMED},
                    {1000,
                     std::nullopt,
                     USD(100),
                     std::nullopt,
                     std::nullopt,
                     temMALFORMED},
                    {std::nullopt,
                     std::nullopt,
                     std::nullopt,
                     IOUAmount{250, 0},
                     tfWithdrawAll,
                     temMALFORMED},
                    {1000,
                     std::nullopt,
                     std::nullopt,
                     IOUAmount{250, 0},
                     std::nullopt,
                     temMALFORMED},
                    {std::nullopt,
                     std::nullopt,
                     USD(100),
                     IOUAmount{250, 0},
                     std::nullopt,
                     temMALFORMED},
                    {std::nullopt,
                     XRP(100),
                     USD(100),
                     IOUAmount{250, 0},
                     std::nullopt,
                     temMALFORMED},
                    {1000,
                     XRP(100),
                     USD(100),
                     std::nullopt,
                     std::nullopt,
                     temMALFORMED},
                    {std::nullopt,
                     XRP(100),
                     USD(100),
                     std::nullopt,
                     tfWithdrawAll,
                     temMALFORMED}};
            for (auto const& it : invalidOptions)
            {
                ammAlice.withdraw(
                    alice,
                    std::get<0>(it),
                    std::get<1>(it),
                    std::get<2>(it),
                    std::get<3>(it),
                    std::get<4>(it),
                    std::nullopt,
                    std::nullopt,
                    ter(std::get<5>(it)));
            }

            // Invalid tokens
            ammAlice.withdraw(
                alice, 0, std::nullopt, std::nullopt, ter(temBAD_AMM_TOKENS));
            ammAlice.withdraw(
                alice,
                IOUAmount{-1},
                std::nullopt,
                std::nullopt,
                ter(temBAD_AMM_TOKENS));

            // Mismatched token, invalid Asset1Out issue
            ammAlice.withdraw(
                alice,
                GBP(100),
                std::nullopt,
                std::nullopt,
                ter(temBAD_AMM_TOKENS));

            // Mismatched token, invalid Asset2Out issue
            ammAlice.withdraw(
                alice,
                USD(100),
                GBP(100),
                std::nullopt,
                ter(temBAD_AMM_TOKENS));

            // Mismatched token, Asset1Out.issue == Asset2Out.issue
            ammAlice.withdraw(
                alice,
                USD(100),
                USD(100),
                std::nullopt,
                ter(temBAD_AMM_TOKENS));

            // Invalid amount value
            ammAlice.withdraw(
                alice, USD(0), std::nullopt, std::nullopt, ter(temBAD_AMOUNT));
            ammAlice.withdraw(
                alice,
                USD(-100),
                std::nullopt,
                std::nullopt,
                ter(temBAD_AMOUNT));
            ammAlice.withdraw(
                alice,
                USD(10),
                std::nullopt,
                IOUAmount{-1},
                ter(temBAD_AMOUNT));

            // Invalid amount/token value, withdraw all tokens from one side
            // of the pool.
            ammAlice.withdraw(
                alice,
                USD(10000),
                std::nullopt,
                std::nullopt,
                ter(tecAMM_FAILED_WITHDRAW));
            ammAlice.withdraw(
                alice,
                XRP(10000),
                std::nullopt,
                std::nullopt,
                ter(tecAMM_FAILED_WITHDRAW));
            ammAlice.withdraw(
                alice,
                std::nullopt,
                USD(0),
                std::nullopt,
                std::nullopt,
                tfOneAssetWithdrawAll,
                std::nullopt,
                std::nullopt,
                ter(tecAMM_FAILED_WITHDRAW));

            // Bad currency
            ammAlice.withdraw(
                alice,
                BAD(100),
                std::nullopt,
                std::nullopt,
                ter(temBAD_CURRENCY));

            // Invalid Account
            Account bad("bad");
            env.memoize(bad);
            ammAlice.withdraw(
                bad,
                1000000,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                seq(1),
                ter(terNO_ACCOUNT));

            // Invalid AMM
            ammAlice.withdraw(
                alice,
                1000,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                {{USD, GBP}},
                std::nullopt,
                ter(terNO_AMM));

            // Carol is not a Liquidity Provider
            ammAlice.withdraw(
                carol, 10000, std::nullopt, std::nullopt, ter(tecAMM_BALANCE));

            // Withdraw entire one side of the pool.
            // Equal withdraw but due to XRP precision limit,
            // this results in full withdraw of XRP pool only,
            // while leaving a tiny amount in USD pool.
            ammAlice.withdraw(
                alice,
                IOUAmount{99999999999, -4},
                std::nullopt,
                std::nullopt,
                ter(tecAMM_FAILED_WITHDRAW));
            // Withdrawing from one side.
            // XRP by tokens
            ammAlice.withdraw(
                alice,
                IOUAmount(99999999999, -4),
                XRP(0),
                std::nullopt,
                ter(tecAMM_FAILED_WITHDRAW));
            // USD by tokens
            ammAlice.withdraw(
                alice,
                IOUAmount(99999999, -1),
                USD(0),
                std::nullopt,
                ter(tecAMM_FAILED_WITHDRAW));
            // XRP
            ammAlice.withdraw(
                alice,
                XRP(10000),
                std::nullopt,
                std::nullopt,
                ter(tecAMM_FAILED_WITHDRAW));
            // USD
            ammAlice.withdraw(
                alice,
                STAmount{USD, UINT64_C(99999999999999999), -13},
                std::nullopt,
                std::nullopt,
                ter(tecAMM_FAILED_WITHDRAW));
        });

        // Invalid AMM
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.withdrawAll(alice);
            ammAlice.withdraw(
                alice, 10000, std::nullopt, std::nullopt, ter(terNO_AMM));
        });

        // Globally frozen asset
        testAMM([&](AMM& ammAlice, Env& env) {
            env(fset(gw, asfGlobalFreeze));
            env.close();
            // Can withdraw non-frozen token
            ammAlice.withdraw(alice, XRP(100));
            ammAlice.withdraw(
                alice, USD(100), std::nullopt, std::nullopt, ter(tecFROZEN));
            ammAlice.withdraw(
                alice, 1000, std::nullopt, std::nullopt, ter(tecFROZEN));
        });

        // Individually frozen (AMM) account
        testAMM([&](AMM& ammAlice, Env& env) {
            env(trust(gw, alice["USD"](0), tfSetFreeze));
            env.close();
            // Can withdraw non-frozen token
            ammAlice.withdraw(alice, XRP(100));
            ammAlice.withdraw(
                alice, 1000, std::nullopt, std::nullopt, ter(tecFROZEN));
            ammAlice.withdraw(
                alice, USD(100), std::nullopt, std::nullopt, ter(tecFROZEN));
            env(trust(gw, alice["USD"](0), tfClearFreeze));
            // Individually frozen AMM
            env(trust(
                gw,
                STAmount{Issue{gw["USD"].currency, ammAlice.ammAccount()}, 0},
                tfSetFreeze));
            // Can withdraw non-frozen token
            ammAlice.withdraw(alice, XRP(100));
            ammAlice.withdraw(
                alice, 1000, std::nullopt, std::nullopt, ter(tecFROZEN));
            ammAlice.withdraw(
                alice, USD(100), std::nullopt, std::nullopt, ter(tecFROZEN));
        });

        // Carol withdraws more than she owns
        testAMM([&](AMM& ammAlice, Env&) {
            // Single deposit of 100000 worth of tokens,
            // which is 10% of the pool. Carol is LP now.
            ammAlice.deposit(carol, 1000000);
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(11000), USD(11000), IOUAmount{11000000, 0}));

            ammAlice.withdraw(
                carol,
                2000000,
                std::nullopt,
                std::nullopt,
                ter(tecAMM_INVALID_TOKENS));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(11000), USD(11000), IOUAmount{11000000, 0}));
        });

        // Withdraw with EPrice limit. Fails to withdraw, calculated tokens
        // to withdraw are 0.
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, 1000000);
            ammAlice.withdraw(
                carol,
                USD(100),
                std::nullopt,
                IOUAmount{500, 0},
                ter(tecAMM_FAILED_WITHDRAW));
        });

        // Withdraw with EPrice limit. Fails to withdraw, calculated tokens
        // to withdraw are greater than the LP shares.
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, 1000000);
            ammAlice.withdraw(
                carol,
                USD(100),
                std::nullopt,
                IOUAmount{600, 0},
                ter(tecAMM_FAILED_WITHDRAW));
        });

        // Withdraw with EPrice limit. Fails to withdraw, amount1
        // to withdraw is less than 1700USD.
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, 1000000);
            ammAlice.withdraw(
                carol,
                USD(1700),
                std::nullopt,
                IOUAmount{520, 0},
                ter(tecAMM_FAILED_WITHDRAW));
        });

        // Deposit/Withdraw the same amount with the trading fee
        testAMM(
            [&](AMM& ammAlice, Env&) {
                ammAlice.deposit(carol, USD(1000));
                ammAlice.withdraw(
                    carol,
                    USD(1000),
                    std::nullopt,
                    std::nullopt,
                    ter(tecAMM_FAILED_WITHDRAW));
            },
            std::nullopt,
            1000);
        testAMM(
            [&](AMM& ammAlice, Env&) {
                ammAlice.deposit(carol, XRP(1000));
                ammAlice.withdraw(
                    carol,
                    XRP(1000),
                    std::nullopt,
                    std::nullopt,
                    ter(tecAMM_FAILED_WITHDRAW));
            },
            std::nullopt,
            1000);

        // Deposit/Withdraw the same amount fails due to the tokens adjustment
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, STAmount{USD, 1, -6});
            ammAlice.withdraw(
                carol,
                STAmount{USD, 1, -6},
                std::nullopt,
                std::nullopt,
                ter(tecAMM_FAILED_WITHDRAW));
        });

        // Withdraw close to one side of the pool. Account's LP tokens
        // are rounded to all LP tokens.
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.withdraw(
                alice,
                STAmount{USD, UINT64_C(9999999999999999), -12},
                std::nullopt,
                std::nullopt,
                ter(tecAMM_FAILED_WITHDRAW));
        });

        // Tiny withdraw
        testAMM([&](AMM& ammAlice, Env&) {
            // XRP amount to withdraw is 0
            ammAlice.withdraw(
                alice,
                IOUAmount{1, -5},
                std::nullopt,
                std::nullopt,
                ter(tecAMM_FAILED_WITHDRAW));
            // Calculated tokens to withdraw are 0
            ammAlice.withdraw(
                alice,
                std::nullopt,
                STAmount{USD, 1, -11},
                std::nullopt,
                ter(tecAMM_FAILED_WITHDRAW));
            ammAlice.deposit(carol, STAmount{USD, 1, -10});
            ammAlice.withdraw(
                carol,
                std::nullopt,
                STAmount{USD, 1, -9},
                std::nullopt,
                ter(tecAMM_FAILED_WITHDRAW));
            ammAlice.withdraw(
                carol,
                std::nullopt,
                XRPAmount{1},
                std::nullopt,
                ter(tecAMM_FAILED_WITHDRAW));
        });
    }

    void
    testWithdraw()
    {
        testcase("Withdraw");

        using namespace jtx;

        // Equal withdrawal by Carol: 1000000 of tokens, 10% of the current
        // pool
        testAMM([&](AMM& ammAlice, Env& env) {
            // Single deposit of 100000 worth of tokens,
            // which is 10% of the pool. Carol is LP now.
            ammAlice.deposit(carol, 1000000);
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(11000), USD(11000), IOUAmount{11000000, 0}));
            BEAST_EXPECT(ammAlice.expectLPTokens(carol, IOUAmount{1000000, 0}));
            // 30,000 less deposited 1,000
            BEAST_EXPECT(expectLine(env, carol, USD(29000)));
            // 30,000 less deposited 1,000 and 10 drops tx fee
            BEAST_EXPECT(
                expectLedgerEntryRoot(env, carol, XRPAmount{28999999990}));

            // Carol withdraws all tokens
            ammAlice.withdraw(carol, 1000000);
            BEAST_EXPECT(
                ammAlice.expectLPTokens(carol, IOUAmount(beast::Zero())));
            BEAST_EXPECT(expectLine(env, carol, USD(30000)));
            BEAST_EXPECT(
                expectLedgerEntryRoot(env, carol, XRPAmount{29999999980}));
        });

        // Equal withdrawal by tokens 1000000, 10%
        // of the current pool
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.withdraw(alice, 1000000);
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(9000), USD(9000), IOUAmount{9000000, 0}));
        });

        // Equal withdrawal with a limit. Withdraw XRP200.
        // If proportional withdraw of USD is less than 100
        // then withdraw that amount, otherwise withdraw USD100
        // and proportionally withdraw XRP. It's the latter
        // in this case - XRP100/USD100.
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.withdraw(alice, XRP(200), USD(100));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(9900), USD(9900), IOUAmount{9900000, 0}));
        });

        // Equal withdrawal with a limit. XRP100/USD100.
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.withdraw(alice, XRP(100), USD(200));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(9900), USD(9900), IOUAmount{9900000, 0}));
        });

        // Single withdrawal by amount XRP1000
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.withdraw(alice, XRP(1000));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(9000), USD(10000), IOUAmount{948683298050514, -8}));
        });

        // Single withdrawal by tokens 10000.
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.withdraw(alice, 10000, USD(0));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000), USD(9980.01), IOUAmount{9990000, 0}));
        });

        // Withdraw all tokens.
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.withdrawAll(alice);
            BEAST_EXPECT(!ammAlice.ammExists());

            // Can create AMM for the XRP/USD pair
            AMM ammCarol(env, carol, XRP(10000), USD(10000));
            BEAST_EXPECT(ammCarol.expectBalances(
                XRP(10000), USD(10000), IOUAmount{10000000, 0}));
        });

        // Single deposit 1000USD, withdraw all tokens in USD
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.deposit(carol, USD(1000));
            ammAlice.withdrawAll(carol, USD(0));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000), USD(10000), IOUAmount{10000000, 0}));
            BEAST_EXPECT(
                ammAlice.expectLPTokens(carol, IOUAmount(beast::Zero())));
        });

        // Single deposit 1000USD, withdraw all tokens in XRP
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, USD(1000));
            ammAlice.withdrawAll(carol, XRP(0));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRPAmount(9090909091),
                STAmount{USD, UINT64_C(1099999999999999), -11},
                IOUAmount{10000000, 0}));
        });

        // Single deposit/withdraw by the same account
        testAMM([&](AMM& ammAlice, Env&) {
            // Since a smaller amount might be deposited due to
            // the lp tokens adjustment, withdrawing by tokens
            // is generally preferred to withdrawing by amount.
            auto lpTokens = ammAlice.deposit(carol, USD(1000));
            ammAlice.withdraw(carol, lpTokens, USD(0));
            lpTokens = ammAlice.deposit(carol, STAmount(USD, 1, -6));
            ammAlice.withdraw(carol, lpTokens, USD(0));
            lpTokens = ammAlice.deposit(carol, XRPAmount(1));
            ammAlice.withdraw(carol, lpTokens, XRPAmount(0));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000), USD(10000), ammAlice.tokens()));
            BEAST_EXPECT(ammAlice.expectLPTokens(carol, IOUAmount{0}));
        });

        // Single deposit by different accounts and then withdraw
        // in reverse.
        testAMM([&](AMM& ammAlice, Env&) {
            auto const carolTokens = ammAlice.deposit(carol, USD(1000));
            auto const aliceTokens = ammAlice.deposit(alice, USD(1000));
            ammAlice.withdraw(alice, aliceTokens, USD(0));
            ammAlice.withdraw(carol, carolTokens, USD(0));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000), USD(10000), ammAlice.tokens()));
            BEAST_EXPECT(ammAlice.expectLPTokens(carol, IOUAmount{0}));
            BEAST_EXPECT(ammAlice.expectLPTokens(alice, ammAlice.tokens()));
        });

        // Equal deposit 10%, withdraw all tokens
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, 1000000);
            ammAlice.withdrawAll(carol);
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000), USD(10000), IOUAmount{10000000, 0}));
        });

        // Equal deposit 10%, withdraw all tokens in USD
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, 1000000);
            ammAlice.withdrawAll(carol, USD(0));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(11000),
                STAmount{USD, UINT64_C(9090909090909092), -12},
                IOUAmount{10000000, 0}));
        });

        // Equal deposit 10%, withdraw all tokens in XRP
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, 1000000);
            ammAlice.withdrawAll(carol, XRP(0));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRPAmount(9090909091), USD(11000), IOUAmount{10000000, 0}));
        });

        // Withdraw with EPrice limit.
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, 1000000);
            ammAlice.withdraw(carol, USD(100), std::nullopt, IOUAmount{520, 0});
            BEAST_EXPECT(
                ammAlice.expectBalances(
                    XRPAmount(11000000000),
                    STAmount{USD, UINT64_C(9372781065088757), -12},
                    IOUAmount{1015384615384616, -8}) &&
                ammAlice.expectLPTokens(carol, IOUAmount{15384615384616, -8}));
            ammAlice.withdrawAll(carol);
            BEAST_EXPECT(ammAlice.expectLPTokens(carol, IOUAmount{0}));
        });

        // Withdraw with EPrice limit. AssetOut is 0.
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.deposit(carol, 1000000);
            ammAlice.withdraw(carol, USD(0), std::nullopt, IOUAmount{520, 0});
            BEAST_EXPECT(
                ammAlice.expectBalances(
                    XRPAmount(11000000000),
                    STAmount{USD, UINT64_C(9372781065088757), -12},
                    IOUAmount{1015384615384616, -8}) &&
                ammAlice.expectLPTokens(carol, IOUAmount{15384615384616, -8}));
        });

        // IOU to IOU + transfer fee
        {
            Env env{*this};
            fund(env, gw, {alice}, {USD(25000), BTC(0.625)}, Fund::All);
            env(rate(gw, 1.25));
            env.close();
            AMM ammAlice(env, alice, USD(20000), BTC(0.5));
            BEAST_EXPECT(ammAlice.expectBalances(
                USD(20000), BTC(0.5), IOUAmount{100, 0}));
            BEAST_EXPECT(expectLine(env, alice, USD(0)));
            BEAST_EXPECT(expectLine(env, alice, BTC(0)));
            fund(env, gw, {carol}, {USD(2500), BTC(0.0625)}, Fund::Acct);
            ammAlice.deposit(carol, 10);
            BEAST_EXPECT(ammAlice.expectBalances(
                USD(22000), BTC(0.55), IOUAmount{110, 0}));
            BEAST_EXPECT(expectLine(env, carol, USD(0)));
            BEAST_EXPECT(expectLine(env, carol, BTC(0)));
            // LP withdraws, AMM doesn't pay the transfer fee.
            ammAlice.withdraw(carol, 10);
            BEAST_EXPECT(ammAlice.expectBalances(
                USD(20000), BTC(0.5), IOUAmount{100, 0}));
            BEAST_EXPECT(ammAlice.expectLPTokens(carol, IOUAmount{0, 0}));
            // 2,500 - 0.25*2,000=500(deposit fee)=2,000
            BEAST_EXPECT(expectLine(env, carol, USD(2000)));
            // 0.0625 - 0.025*0.5=0.0125(deposit fee)=0.05
            BEAST_EXPECT(expectLine(env, carol, BTC(0.05)));
        }

        // Tiny withdraw
        testAMM([&](AMM& ammAlice, Env&) {
            // By tokens
            ammAlice.withdraw(alice, IOUAmount{1, -3});
            BEAST_EXPECT(ammAlice.expectBalances(
                XRPAmount{9999999999},
                STAmount{USD, UINT64_C(9999999999), -6},
                IOUAmount{9999999999, -3}));
        });
        testAMM([&](AMM& ammAlice, Env&) {
            // Single XRP pool
            ammAlice.withdraw(alice, std::nullopt, XRPAmount{1});
            BEAST_EXPECT(ammAlice.expectBalances(
                XRPAmount{9999999999}, USD(10000), IOUAmount{99999999995, -4}));
        });
        testAMM([&](AMM& ammAlice, Env&) {
            // Single USD pool
            ammAlice.withdraw(alice, std::nullopt, STAmount{USD, 1, -10});
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000),
                STAmount{USD, UINT64_C(99999999999999), -10},
                IOUAmount{999999999999995, -8}));
        });

        // Withdraw close to entire pool
        // Equal by tokens
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.withdraw(alice, IOUAmount{9999999999, -3});
            BEAST_EXPECT(ammAlice.expectBalances(
                XRPAmount{1}, STAmount{USD, 1, -6}, IOUAmount{1, -3}));
        });
        // USD by tokens
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.withdraw(alice, IOUAmount{9999999}, USD(0));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000), STAmount{USD, 1, -10}, IOUAmount{1}));
        });
        // XRP by tokens
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.withdraw(alice, IOUAmount{9999900}, XRP(0));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRPAmount{1}, USD(10000), IOUAmount{100}));
        });
        // USD
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.withdraw(
                alice, STAmount{USD, UINT64_C(999999999999999), -11});
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000), STAmount{USD, 1, -11}, IOUAmount{316227765, -9}));
        });
        // XRP
        testAMM([&](AMM& ammAlice, Env&) {
            ammAlice.withdraw(alice, XRPAmount{9999999999});
            BEAST_EXPECT(ammAlice.expectBalances(
                XRPAmount{1}, USD(10000), IOUAmount{100}));
        });
    }

    void
    testInvalidFeeVote()
    {
        testcase("Invalid Fee Vote");
        using namespace jtx;

        testAMM([&](AMM& ammAlice, Env& env) {
            // Invalid flags
            ammAlice.vote(
                std::nullopt,
                1000,
                tfWithdrawAll,
                std::nullopt,
                std::nullopt,
                ter(temINVALID_FLAG));

            // Invalid fee.
            ammAlice.vote(
                std::nullopt,
                1001,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(temBAD_FEE));
            BEAST_EXPECT(ammAlice.expectTradingFee(0));

            // Invalid Account
            Account bad("bad");
            env.memoize(bad);
            ammAlice.vote(
                bad,
                1000,
                std::nullopt,
                seq(1),
                std::nullopt,
                ter(terNO_ACCOUNT));

            // Invalid AMM
            ammAlice.vote(
                alice,
                1000,
                std::nullopt,
                std::nullopt,
                {{USD, GBP}},
                ter(terNO_AMM));

            // Account is not LP
            ammAlice.vote(
                carol,
                1000,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecAMM_INVALID_TOKENS));
        });

        // Invalid AMM
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.withdrawAll(alice);
            ammAlice.vote(
                alice,
                1000,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(terNO_AMM));
        });
    }

    void
    testFeeVote()
    {
        testcase("Fee Vote");
        using namespace jtx;

        // One vote sets fee to 1%.
        testAMM([&](AMM& ammAlice, Env& env) {
            BEAST_EXPECT(ammAlice.expectAuctionSlot(0, 0, IOUAmount{0}));
            ammAlice.vote({}, 1000);
            BEAST_EXPECT(ammAlice.expectTradingFee(1000));
            // Discounted fee is 1/10 of trading fee.
            BEAST_EXPECT(ammAlice.expectAuctionSlot(100, 0, IOUAmount{0}));
        });

        auto vote = [&](AMM& ammAlice,
                        Env& env,
                        int i,
                        int fundUSD = 100'000,
                        std::uint32_t tokens = 10'000'000,
                        std::vector<Account>* accounts = nullptr) {
            Account a(std::to_string(i));
            fund(env, gw, {a}, {USD(fundUSD)}, Fund::Acct);
            ammAlice.deposit(a, tokens);
            ammAlice.vote(a, 50 * (i + 1));
            if (accounts)
                accounts->push_back(std::move(a));
        };

        // Eight votes fill all voting slots, set fee 0.175%.
        testAMM([&](AMM& ammAlice, Env& env) {
            for (int i = 0; i < 7; ++i)
                vote(ammAlice, env, i, 10'000);
            BEAST_EXPECT(ammAlice.expectTradingFee(175));
        });

        // Eight votes fill all voting slots, set fee 0.175%.
        // New vote, same account, sets fee 0.225%
        testAMM([&](AMM& ammAlice, Env& env) {
            for (int i = 0; i < 7; ++i)
                vote(ammAlice, env, i);
            BEAST_EXPECT(ammAlice.expectTradingFee(175));
            Account const a("0");
            ammAlice.vote(a, 450);
            BEAST_EXPECT(ammAlice.expectTradingFee(225));
        });

        // Eight votes fill all voting slots, set fee 0.175%.
        // New vote, new account, higher vote weight, set higher fee 0.244%
        testAMM([&](AMM& ammAlice, Env& env) {
            for (int i = 0; i < 7; ++i)
                vote(ammAlice, env, i);
            BEAST_EXPECT(ammAlice.expectTradingFee(175));
            vote(ammAlice, env, 7, 100'000, 20'000'000);
            BEAST_EXPECT(ammAlice.expectTradingFee(244));
        });

        // Eight votes fill all voting slots, set fee 0.219%.
        // New vote, new account, higher vote weight, set smaller fee 0.206%
        testAMM([&](AMM& ammAlice, Env& env) {
            for (int i = 7; i > 0; --i)
                vote(ammAlice, env, i);
            BEAST_EXPECT(ammAlice.expectTradingFee(219));
            vote(ammAlice, env, 0, 100'000, 20'000'000);
            BEAST_EXPECT(ammAlice.expectTradingFee(206));
        });

        // Eight votes fill all voting slots. The accounts then withdraw all
        // tokens. An account sets a new fee and the previous slots are
        // deleted.
        testAMM([&](AMM& ammAlice, Env& env) {
            std::vector<Account> accounts;
            for (int i = 0; i < 7; ++i)
                vote(ammAlice, env, i, 100'000, 10'000'000, &accounts);
            BEAST_EXPECT(ammAlice.expectTradingFee(175));
            for (int i = 0; i < 7; ++i)
                ammAlice.withdrawAll(accounts[i]);
            ammAlice.deposit(carol, 10000000);
            ammAlice.vote(carol, 1000);
            // The initial LP set the fee to 1000. Carol gets 50% voting
            // power, and the new fee is 500.
            BEAST_EXPECT(ammAlice.expectTradingFee(500));
        });

        // Eight votes fill all voting slots. The accounts then withdraw some
        // tokens. The new vote doesn't get the voting power but
        // the slots are refreshed and the fee is updated.
        testAMM([&](AMM& ammAlice, Env& env) {
            std::vector<Account> accounts;
            for (int i = 0; i < 7; ++i)
                vote(ammAlice, env, i, 100'000, 10'000'000, &accounts);
            BEAST_EXPECT(ammAlice.expectTradingFee(175));
            for (int i = 0; i < 7; ++i)
                ammAlice.withdraw(accounts[i], 9000000);
            ammAlice.deposit(carol, 1000);
            // The vote is not added to the slots
            ammAlice.vote(carol, 1000);
            auto const info = ammAlice.ammRpcInfo()[jss::amm][jss::vote_slots];
            for (std::uint16_t i = 0; i < info.size(); ++i)
                BEAST_EXPECT(info[i][jss::account] != carol.human());
            // But the slots are refreshed and the fee is changed
            BEAST_EXPECT(ammAlice.expectTradingFee(82));
        });
    }

    void
    testInvalidBid()
    {
        testcase("Invalid Bid");
        using namespace jtx;
        using namespace std::chrono;

        testAMM([&](AMM& ammAlice, Env& env) {
            // Invalid flags
            ammAlice.bid(
                carol,
                0,
                std::nullopt,
                {},
                tfWithdrawAll,
                std::nullopt,
                std::nullopt,
                ter(temINVALID_FLAG));

            ammAlice.deposit(carol, 1000000);
            // Invalid Bid price <= 0
            for (auto bid : {0, -100})
            {
                ammAlice.bid(
                    carol,
                    bid,
                    std::nullopt,
                    {},
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    ter(temBAD_AMOUNT));
                ammAlice.bid(
                    carol,
                    std::nullopt,
                    bid,
                    {},
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    ter(temBAD_AMOUNT));
            }

            // Invlaid Min/Max combination
            ammAlice.bid(
                carol,
                200,
                100,
                {},
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecAMM_INVALID_TOKENS));

            // Invalid Account
            Account bad("bad");
            env.memoize(bad);
            ammAlice.bid(
                bad,
                std::nullopt,
                100,
                {},
                std::nullopt,
                seq(1),
                std::nullopt,
                ter(terNO_ACCOUNT));

            // Account is not LP
            Account const dan("dan");
            env.fund(XRP(1000), dan);
            ammAlice.bid(
                dan,
                100,
                std::nullopt,
                {},
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecAMM_INVALID_TOKENS));
            ammAlice.bid(
                dan,
                std::nullopt,
                std::nullopt,
                {},
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecAMM_INVALID_TOKENS));

            // Auth account is invalid.
            ammAlice.bid(
                carol,
                100,
                std::nullopt,
                {bob},
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(terNO_ACCOUNT));

            // Invalid Assets
            ammAlice.bid(
                alice,
                std::nullopt,
                100,
                {},
                std::nullopt,
                std::nullopt,
                {{USD, GBP}},
                ter(terNO_AMM));

            // Invalid Min/Max issue
            ammAlice.bid(
                alice,
                std::nullopt,
                STAmount{USD, 100},
                {},
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(temBAD_AMM_TOKENS));
            ammAlice.bid(
                alice,
                STAmount{USD, 100},
                std::nullopt,
                {},
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(temBAD_AMM_TOKENS));
        });

        // Invalid AMM
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.withdrawAll(alice);
            ammAlice.bid(
                alice,
                std::nullopt,
                100,
                {},
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(terNO_AMM));
        });

        // More than four Auth accounts.
        testAMM([&](AMM& ammAlice, Env& env) {
            Account ed("ed");
            Account bill("bill");
            Account scott("scott");
            Account james("james");
            env.fund(XRP(1000), bob, ed, bill, scott, james);
            env.close();
            ammAlice.deposit(carol, 1000000);
            ammAlice.bid(
                carol,
                100,
                std::nullopt,
                {bob, ed, bill, scott, james},
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(temMALFORMED));
        });

        // Bid price exceeds LP owned tokens
        testAMM([&](AMM& ammAlice, Env& env) {
            fund(env, gw, {bob}, XRP(1000), {USD(100)}, Fund::Acct);
            ammAlice.deposit(carol, 1000000);
            ammAlice.deposit(bob, 10);
            ammAlice.bid(
                carol,
                1000001,
                std::nullopt,
                {},
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecAMM_INVALID_TOKENS));
            ammAlice.bid(
                carol,
                std::nullopt,
                1000001,
                {},
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecAMM_INVALID_TOKENS));
            ammAlice.bid(carol, 1000);
            BEAST_EXPECT(ammAlice.expectAuctionSlot(0, 0, IOUAmount{1000}));
            // Slot purchase price is more than 1000 but bob only has 10 tokens
            ammAlice.bid(
                bob,
                std::nullopt,
                std::nullopt,
                {},
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecAMM_INVALID_TOKENS));
        });

        // Bid all tokens, still own the slot
        {
            Env env(*this);
            fund(env, gw, {alice, bob}, XRP(1000), {USD(1000)});
            AMM amm(env, gw, XRP(10), USD(1000));
            auto const lpIssue = amm.lptIssue();
            env.trust(STAmount{lpIssue, 100}, alice);
            env.trust(STAmount{lpIssue, 50}, bob);
            env(pay(gw, alice, STAmount{lpIssue, 100}));
            env(pay(gw, bob, STAmount{lpIssue, 50}));
            amm.bid(alice, 100);
            // Alice doesn't have any more tokens, but
            // she still owns the slot.
            amm.bid(
                bob,
                std::nullopt,
                50,
                {},
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecAMM_FAILED_BID));
        }
    }

    void
    testBid()
    {
        testcase("Bid");
        using namespace jtx;
        using namespace std::chrono;

        // Auction slot initially is owned by AMM creator, who pays 0 price.

        // Bid 110 tokens. Pay bidMin.
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.deposit(carol, 1000000);
            ammAlice.bid(carol, 110);
            BEAST_EXPECT(ammAlice.expectAuctionSlot(0, 0, IOUAmount{110}));
            // 110 tokens are burned.
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(11000), USD(11000), IOUAmount{10999890, 0}));
        });

        // Bid with min/max when the pay price is less than min.
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.deposit(carol, 1000000);
            // Bid exactly 110. Pay 110 because the pay price is < 110.
            ammAlice.bid(carol, 110, 110);
            BEAST_EXPECT(ammAlice.expectAuctionSlot(0, 0, IOUAmount{110}));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(11000), USD(11000), IOUAmount{10999890, 0}));
            // Bid exactly 180-200. Pay 180 because the pay price is < 180.
            ammAlice.bid(alice, 180, 200);
            BEAST_EXPECT(ammAlice.expectAuctionSlot(0, 0, IOUAmount{180}));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(11000), USD(11000), IOUAmount{109998145, -1}));
        });

        // Start bid at bidMin 110.
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.deposit(carol, 1000000);
            // Bid, pay bidMin.
            ammAlice.bid(carol, 110);
            BEAST_EXPECT(ammAlice.expectAuctionSlot(0, 0, IOUAmount{110}));

            fund(env, gw, {bob}, {USD(10000)}, Fund::Acct);
            ammAlice.deposit(bob, 1000000);
            // Bid, pay the computed price.
            ammAlice.bid(bob);
            BEAST_EXPECT(ammAlice.expectAuctionSlot(0, 0, IOUAmount(1155, -1)));

            // Bid bidMax fails because the computed price is higher.
            ammAlice.bid(
                carol,
                std::nullopt,
                120,
                {},
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecAMM_FAILED_BID));
            // Bid MaxSlotPrice succeeds - pay computed price
            ammAlice.bid(carol, std::nullopt, 600);
            BEAST_EXPECT(
                ammAlice.expectAuctionSlot(0, 0, IOUAmount{121275, -3}));

            // Bid Min/MaxSlotPrice fails because the computed price is not in
            // range
            ammAlice.bid(
                carol,
                10,
                100,
                {},
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecAMM_FAILED_BID));
            // Bid Min/MaxSlotPrice succeeds - pay computed price
            ammAlice.bid(carol, 100, 600);
            BEAST_EXPECT(
                ammAlice.expectAuctionSlot(0, 0, IOUAmount{12733875, -5}));
        });

        // Slot states.
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.deposit(carol, 1000000);

            fund(env, gw, {bob}, {USD(10000)}, Fund::Acct);
            ammAlice.deposit(bob, 1000000);
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(12000), USD(12000), IOUAmount{12000000, 0}));

            // Initial state. Pay bidMin.
            ammAlice.bid(carol, 110);
            BEAST_EXPECT(ammAlice.expectAuctionSlot(0, 0, IOUAmount{110}));

            // 1st Interval after close, price for 0th interval.
            ammAlice.bid(bob);
            env.close(seconds(AUCTION_SLOT_INTERVAL_DURATION + 1));
            BEAST_EXPECT(ammAlice.expectAuctionSlot(0, 1, IOUAmount{1155, -1}));

            // 10th Interval after close, price for 1st interval.
            ammAlice.bid(carol);
            env.close(seconds(10 * AUCTION_SLOT_INTERVAL_DURATION + 1));
            BEAST_EXPECT(
                ammAlice.expectAuctionSlot(0, 10, IOUAmount{121275, -3}));

            // 20th Interval (expired) after close, price for 10th interval.
            ammAlice.bid(bob);
            env.close(seconds(
                AUCTION_SLOT_TIME_INTERVALS * AUCTION_SLOT_INTERVAL_DURATION +
                1));
            BEAST_EXPECT(ammAlice.expectAuctionSlot(
                0, std::nullopt, IOUAmount{12733875, -5}));

            // 0 Interval.
            ammAlice.bid(carol, 110);
            BEAST_EXPECT(
                ammAlice.expectAuctionSlot(0, std::nullopt, IOUAmount{110}));
            // ~321.09 tokens burnt on bidding fees.
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(12000), USD(12000), IOUAmount{1199967891, -2}));
        });

        // Pool's fee 1%. Bid bidMin.
        // Auction slot owner and auth account trade at discounted fee -
        // 1/10 of the trading fee.
        // Other accounts trade at 1% fee.
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                Account const dan("dan");
                Account const ed("ed");
                fund(env, gw, {bob, dan, ed}, {USD(20000)}, Fund::Acct);
                ammAlice.deposit(bob, 1000000);
                ammAlice.deposit(ed, 1000000);
                ammAlice.deposit(carol, 500000);
                ammAlice.deposit(dan, 500000);
                auto ammTokens = ammAlice.getLPTokensBalance();
                ammAlice.bid(carol, 120, std::nullopt, {bob, ed});
                auto const slotPrice = IOUAmount{5200};
                ammTokens -= slotPrice;
                BEAST_EXPECT(ammAlice.expectAuctionSlot(100, 0, slotPrice));
                BEAST_EXPECT(
                    ammAlice.expectBalances(XRP(13000), USD(13000), ammTokens));
                // Discounted trade
                for (int i = 0; i < 10; ++i)
                {
                    auto tokens = ammAlice.deposit(carol, USD(100));
                    ammAlice.withdraw(carol, tokens, USD(0));
                    tokens = ammAlice.deposit(bob, USD(100));
                    ammAlice.withdraw(bob, tokens, USD(0));
                    tokens = ammAlice.deposit(ed, USD(100));
                    ammAlice.withdraw(ed, tokens, USD(0));
                }
                // carol, bob, and ed pay ~0.99USD in fees.
                BEAST_EXPECT(
                    env.balance(carol, USD) ==
                    STAmount(USD, UINT64_C(2949900572620545), -11));
                BEAST_EXPECT(
                    env.balance(bob, USD) ==
                    STAmount(USD, UINT64_C(1899900572616195), -11));
                BEAST_EXPECT(
                    env.balance(ed, USD) ==
                    STAmount(USD, UINT64_C(1899900572611841), -11));
                // USD pool is slightly higher because of the fees.
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(13000),
                    STAmount(USD, UINT64_C(1300298282151419), -11),
                    ammTokens));
                ammTokens = ammAlice.getLPTokensBalance();
                // Trade with the fee
                for (int i = 0; i < 10; ++i)
                {
                    auto const tokens = ammAlice.deposit(dan, USD(100));
                    ammAlice.withdraw(dan, tokens, USD(0));
                }
                // dan pays ~9.94USD, which is ~10 times more in fees than
                // carol, bob, ed. the discounted fee is 10 times less
                // than the trading fee.
                BEAST_EXPECT(
                    env.balance(dan, USD) ==
                    STAmount(USD, UINT64_C(19490056722744), -9));
                // USD pool gains more in dan's fees.
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(13000),
                    STAmount{USD, UINT64_C(1301292609877019), -11},
                    ammTokens));
                // Discounted fee payment
                ammAlice.deposit(carol, USD(100));
                ammTokens = ammAlice.getLPTokensBalance();
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(13000),
                    STAmount{USD, UINT64_C(1311292609877019), -11},
                    ammTokens));
                env(pay(carol, bob, USD(100)), path(~USD), sendmax(XRP(110)));
                env.close();
                // carol pays 100000 drops in fees
                // 99900668XRP swapped in for 100USD
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRPAmount{13100000668},
                    STAmount{USD, UINT64_C(1301292609877019), -11},
                    ammTokens));
                // Payment with the trading fee
                env(pay(alice, carol, XRP(100)), path(~XRP), sendmax(USD(110)));
                env.close();
                // alice pays ~1.011USD in fees, which is ~10 times more
                // than carol's fee
                // 100.099431529USD swapped in for 100XRP
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRPAmount{13000000668},
                    STAmount{USD, UINT64_C(1311403663047264), -11},
                    ammTokens));
                // Auction slot expired, no discounted fee
                env.close(seconds(TOTAL_TIME_SLOT_SECS + 1));
                // clock is parent's based
                env.close();
                BEAST_EXPECT(
                    env.balance(carol, USD) ==
                    STAmount(USD, UINT64_C(2939900572620545), -11));
                ammTokens = ammAlice.getLPTokensBalance();
                for (int i = 0; i < 10; ++i)
                {
                    auto const tokens = ammAlice.deposit(carol, USD(100));
                    ammAlice.withdraw(carol, tokens, USD(0));
                }
                // carol pays ~9.94USD in fees, which is ~10 times more in
                // trading fees vs discounted fee.
                BEAST_EXPECT(
                    env.balance(carol, USD) ==
                    STAmount(USD, UINT64_C(2938906197177128), -11));
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRPAmount{13000000668},
                    STAmount{USD, UINT64_C(1312398038490681), -11},
                    ammTokens));
                env(pay(carol, bob, USD(100)), path(~USD), sendmax(XRP(110)));
                env.close();
                // carol pays ~1.008XRP in trading fee, which is
                // ~10 times more than the discounted fee.
                // 99.815876XRP is swapped in for 100USD
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRPAmount(13100824790),
                    STAmount{USD, UINT64_C(1302398038490681), -11},
                    ammTokens));
            },
            std::nullopt,
            1000);

        // Bid tiny amount
        testAMM([&](AMM& ammAlice, Env&) {
            // Bid a tiny amount
            auto const tiny = Number{STAmount::cMinValue, STAmount::cMinOffset};
            ammAlice.bid(alice, IOUAmount{tiny});
            // Auction slot purchase price is equal to the tiny amount
            // since the minSlotPrice is 0 with no trading fee.
            BEAST_EXPECT(ammAlice.expectAuctionSlot(0, 0, IOUAmount{tiny}));
            // The purchase price is too small to affect the total tokens
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000), USD(10000), ammAlice.tokens()));
            // Bid the tiny amount
            ammAlice.bid(
                alice, IOUAmount{STAmount::cMinValue, STAmount::cMinOffset});
            // Pay slightly higher price
            BEAST_EXPECT(ammAlice.expectAuctionSlot(
                0, 0, IOUAmount{tiny * Number{105, -2}}));
            // The purchase price is still too small to affect the total tokens
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000), USD(10000), ammAlice.tokens()));
        });

        // Reset auth account
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.bid(alice, IOUAmount{100}, std::nullopt, {carol});
            BEAST_EXPECT(ammAlice.expectAuctionSlot({carol}));
            ammAlice.bid(alice, IOUAmount{100});
            BEAST_EXPECT(ammAlice.expectAuctionSlot({}));
            Account bob("bob");
            Account dan("dan");
            fund(env, {bob, dan}, XRP(1000));
            ammAlice.bid(alice, IOUAmount{100}, std::nullopt, {bob, dan});
            BEAST_EXPECT(ammAlice.expectAuctionSlot({bob, dan}));
        });

        // Bid all tokens, still own the slot and trade at a discount
        {
            Env env(*this);
            fund(env, gw, {alice, bob}, XRP(2000), {USD(2000)});
            AMM amm(env, gw, XRP(1000), USD(1010), false, 1000);
            auto const lpIssue = amm.lptIssue();
            env.trust(STAmount{lpIssue, 500}, alice);
            env.trust(STAmount{lpIssue, 50}, bob);
            env(pay(gw, alice, STAmount{lpIssue, 500}));
            env(pay(gw, bob, STAmount{lpIssue, 50}));
            // Alice doesn't have anymore lp tokens
            amm.bid(alice, 500);
            BEAST_EXPECT(amm.expectAuctionSlot(100, 0, IOUAmount{500}));
            BEAST_EXPECT(expectLine(env, alice, STAmount{lpIssue, 0}));
            // But trades with the discounted fee since she still owns the slot.
            // Alice pays 10011 drops in fees
            env(pay(alice, bob, USD(10)), path(~USD), sendmax(XRP(11)));
            BEAST_EXPECT(amm.expectBalances(
                XRPAmount{1010010011},
                USD(1000),
                IOUAmount{1004487562112089, -9}));
            // Bob pays the full fee ~0.1USD
            env(pay(bob, alice, XRP(10)), path(~XRP), sendmax(USD(11)));
            BEAST_EXPECT(amm.expectBalances(
                XRPAmount{1000010011},
                STAmount{USD, UINT64_C(101010090898081), -11},
                IOUAmount{1004487562112089, -9}));
        }
    }

    void
    testInvalidAMMPayment()
    {
        testcase("Invalid AMM Payment");
        using namespace jtx;
        using namespace std::chrono;
        using namespace std::literals::chrono_literals;

        // Can't pay into AMM account.
        // Can't pay out since there is no keys
        for (auto const& acct : {gw, alice})
        {
            {
                Env env(*this);
                fund(env, gw, {alice, carol}, XRP(1000), {USD(100)});
                // XRP balance is below reserve
                AMM ammAlice(env, acct, XRP(10), USD(10));
                // Pay below reserve
                env(pay(carol, ammAlice.ammAccount(), XRP(10)),
                    ter(tecNO_PERMISSION));
                // Pay above reserve
                env(pay(carol, ammAlice.ammAccount(), XRP(300)),
                    ter(tecNO_PERMISSION));
                // Pay IOU
                env(pay(carol, ammAlice.ammAccount(), USD(10)),
                    ter(tecNO_PERMISSION));
            }
            {
                Env env(*this);
                fund(env, gw, {alice, carol}, XRP(10000000), {USD(10000)});
                // XRP balance is above reserve
                AMM ammAlice(env, acct, XRP(1000000), USD(100));
                // Pay below reserve
                env(pay(carol, ammAlice.ammAccount(), XRP(10)),
                    ter(tecNO_PERMISSION));
                // Pay above reserve
                env(pay(carol, ammAlice.ammAccount(), XRP(1000000)),
                    ter(tecNO_PERMISSION));
            }
        }

        // Can't pay into AMM with escrow.
        testAMM([&](AMM& ammAlice, Env& env) {
            env(escrow(carol, ammAlice.ammAccount(), XRP(1)),
                condition(cb1),
                finish_time(env.now() + 1s),
                cancel_time(env.now() + 2s),
                fee(1500),
                ter(tecNO_PERMISSION));
        });

        // Can't pay into AMM with paychan.
        testAMM([&](AMM& ammAlice, Env& env) {
            auto const pk = carol.pk();
            auto const settleDelay = 100s;
            NetClock::time_point const cancelAfter =
                env.current()->info().parentCloseTime + 200s;
            env(create(
                    carol,
                    ammAlice.ammAccount(),
                    XRP(1000),
                    settleDelay,
                    pk,
                    cancelAfter),
                ter(tecNO_PERMISSION));
        });

        // Pay amounts close to one side of the pool
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                // Can't consume whole pool
                env(pay(alice, carol, USD(100)),
                    path(~USD),
                    sendmax(XRP(1000000000)),
                    ter(tecPATH_PARTIAL));
                env(pay(alice, carol, XRP(100)),
                    path(~XRP),
                    sendmax(USD(1000000000)),
                    ter(tecPATH_PARTIAL));
                // Overflow
                env(pay(alice, carol, STAmount{USD, UINT64_C(99999999999), -9}),
                    path(~USD),
                    sendmax(XRP(1000000000)),
                    ter(tecPATH_PARTIAL));
                env(pay(alice, carol, STAmount{USD, UINT64_C(99999999999), -8}),
                    path(~USD),
                    sendmax(XRP(1000000000)),
                    ter(tecPATH_PARTIAL));
                env(pay(alice, carol, STAmount{xrpIssue(), 99999999}),
                    path(~XRP),
                    sendmax(USD(1000000000)),
                    ter(tecPATH_PARTIAL));
                // Sender doesn't have enough funds
                env(pay(alice, carol, USD(99.99)),
                    path(~USD),
                    sendmax(XRP(1000000000)),
                    ter(tecPATH_PARTIAL));
                env(pay(alice, carol, STAmount{xrpIssue(), 99990000}),
                    path(~XRP),
                    sendmax(USD(1000000000)),
                    ter(tecPATH_PARTIAL));
            },
            {{XRP(100), USD(100)}});

        // Globally frozen
        testAMM([&](AMM& ammAlice, Env& env) {
            env(fset(gw, asfGlobalFreeze));
            env.close();
            env(pay(alice, carol, USD(1)),
                path(~USD),
                txflags(tfPartialPayment | tfNoRippleDirect),
                sendmax(XRP(10)),
                ter(tecPATH_DRY));
            env(pay(alice, carol, XRP(1)),
                path(~XRP),
                txflags(tfPartialPayment | tfNoRippleDirect),
                sendmax(USD(10)),
                ter(tecPATH_DRY));
        });

        // Individually frozen AMM
        testAMM([&](AMM& ammAlice, Env& env) {
            env(trust(
                gw,
                STAmount{Issue{gw["USD"].currency, ammAlice.ammAccount()}, 0},
                tfSetFreeze));
            env.close();
            env(pay(alice, carol, USD(1)),
                path(~USD),
                txflags(tfPartialPayment | tfNoRippleDirect),
                sendmax(XRP(10)),
                ter(tecPATH_DRY));
            env(pay(alice, carol, XRP(1)),
                path(~XRP),
                txflags(tfPartialPayment | tfNoRippleDirect),
                sendmax(USD(10)),
                ter(tecPATH_DRY));
        });

        // Individually frozen accounts
        testAMM([&](AMM& ammAlice, Env& env) {
            env(trust(gw, carol["USD"](0), tfSetFreeze));
            env(trust(gw, alice["USD"](0), tfSetFreeze));
            env.close();
            env(pay(alice, carol, XRP(1)),
                path(~XRP),
                sendmax(USD(10)),
                txflags(tfNoRippleDirect | tfPartialPayment),
                ter(tecPATH_DRY));
        });
    }

    void
    testBasicPaymentEngine()
    {
        testcase("Basic Payment");
        using namespace jtx;

        // Payment 100USD for 100XRP.
        // Force one path with tfNoRippleDirect.
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                env.fund(jtx::XRP(30000), bob);
                env.close();
                env(pay(bob, carol, USD(100)),
                    path(~USD),
                    sendmax(XRP(100)),
                    txflags(tfNoRippleDirect));
                env.close();
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(10100), USD(10000), ammAlice.tokens()));
                // Initial balance 30,000 + 100
                BEAST_EXPECT(expectLine(env, carol, USD(30100)));
                // Initial balance 30,000 - 100(sendmax) - 10(tx fee)
                BEAST_EXPECT(expectLedgerEntryRoot(
                    env, bob, XRP(30000) - XRP(100) - txfee(env, 1)));
            },
            {{XRP(10000), USD(10100)}});

        // Payment 100USD for 100XRP, use default path.
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                env.fund(jtx::XRP(30000), bob);
                env.close();
                env(pay(bob, carol, USD(100)), sendmax(XRP(100)));
                env.close();
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(10100), USD(10000), ammAlice.tokens()));
                // Initial balance 30,000 + 100
                BEAST_EXPECT(expectLine(env, carol, USD(30100)));
                // Initial balance 30,000 - 100(sendmax) - 10(tx fee)
                BEAST_EXPECT(expectLedgerEntryRoot(
                    env, bob, XRP(30000) - XRP(100) - txfee(env, 1)));
            },
            {{XRP(10000), USD(10100)}});

        // This payment is identical to above. While it has
        // both default path and path, activeStrands has one path.
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                env.fund(jtx::XRP(30000), bob);
                env.close();
                env(pay(bob, carol, USD(100)), path(~USD), sendmax(XRP(100)));
                env.close();
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(10100), USD(10000), ammAlice.tokens()));
                // Initial balance 30,000 + 100
                BEAST_EXPECT(expectLine(env, carol, USD(30100)));
                // Initial balance 30,000 - 100(sendmax) - 10(tx fee)
                BEAST_EXPECT(expectLedgerEntryRoot(
                    env, bob, XRP(30000) - XRP(100) - txfee(env, 1)));
            },
            {{XRP(10000), USD(10100)}});

        // Payment with limitQuality set.
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                env.fund(jtx::XRP(30000), bob);
                env.close();
                // Pays 10USD for 10XRP. A larger payment of ~99.11USD/100XRP
                // would have been sent has it not been for limitQuality.
                env(pay(bob, carol, USD(100)),
                    path(~USD),
                    sendmax(XRP(100)),
                    txflags(
                        tfNoRippleDirect | tfPartialPayment | tfLimitQuality));
                env.close();
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(10010), USD(10000), ammAlice.tokens()));
                // Initial balance 30,000 + 10(limited by limitQuality)
                BEAST_EXPECT(expectLine(env, carol, USD(30010)));
                // Initial balance 30,000 - 10(limited by limitQuality) - 10(tx
                // fee)
                BEAST_EXPECT(expectLedgerEntryRoot(
                    env, bob, XRP(30000) - XRP(10) - txfee(env, 1)));

                // Fails because of limitQuality. Would have sent
                // ~98.91USD/110XRP has it not been for limitQuality.
                env(pay(bob, carol, USD(100)),
                    path(~USD),
                    sendmax(XRP(100)),
                    txflags(
                        tfNoRippleDirect | tfPartialPayment | tfLimitQuality),
                    ter(tecPATH_DRY));
                env.close();
            },
            {{XRP(10000), USD(10010)}});

        // Fail when partial payment is not set.
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                env.fund(jtx::XRP(30000), bob);
                env.close();
                env(pay(bob, carol, USD(100)),
                    path(~USD),
                    sendmax(XRP(100)),
                    txflags(tfNoRippleDirect),
                    ter(tecPATH_PARTIAL));
            },
            {{XRP(10000), USD(10000)}});

        // Non-default path (with AMM) has a better quality than default path.
        // The max possible liquidity is taken out of non-default
        // path ~29.9XRP/29.9EUR, ~29.9EUR/~29.99USD. The rest
        // is taken from the offer.
        {
            Env env(*this);
            fund(env, gw, {alice, carol}, {USD(30000), EUR(30000)}, Fund::All);
            env.close();
            env.fund(XRP(1000), bob);
            env.close();
            auto ammEUR_XRP = AMM(env, alice, XRP(10000), EUR(10000));
            auto ammUSD_EUR = AMM(env, alice, EUR(10000), USD(10000));
            env(offer(alice, XRP(101), USD(100)), txflags(tfPassive));
            env.close();
            env(pay(bob, carol, USD(100)),
                path(~EUR, ~USD),
                sendmax(XRP(102)),
                txflags(tfPartialPayment));
            env.close();
            BEAST_EXPECT(ammEUR_XRP.expectBalances(
                XRPAmount(10030082730),
                STAmount(EUR, UINT64_C(9970007498125468), -12),
                ammEUR_XRP.tokens()));
            BEAST_EXPECT(ammUSD_EUR.expectBalances(
                STAmount(USD, UINT64_C(9970097277662122), -12),
                STAmount(EUR, UINT64_C(1002999250187452), -11),
                ammUSD_EUR.tokens()));
            BEAST_EXPECT(expectOffers(
                env,
                alice,
                1,
                {{Amounts{
                    XRPAmount(30201749),
                    STAmount(USD, UINT64_C(2990272233787818), -14)}}}));
            // Initial 30,000 + 100
            BEAST_EXPECT(expectLine(env, carol, STAmount{USD, 30100}));
            // Initial 1,000 - 30082730(AMM pool) - 70798251(offer) - 10(tx fee)
            BEAST_EXPECT(expectLedgerEntryRoot(
                env,
                bob,
                XRP(1000) - XRPAmount{30082730} - XRPAmount{70798251} -
                    txfee(env, 1)));
        }

        // Default path (with AMM) has a better quality than a non-default path.
        // The max possible liquidity is taken out of default
        // path ~49XRP/49USD. The rest is taken from the offer.
        testAMM([&](AMM& ammAlice, Env& env) {
            env.fund(XRP(1000), bob);
            env.close();
            env.trust(EUR(2000), alice);
            env.close();
            env(pay(gw, alice, EUR(1000)));
            env(offer(alice, XRP(101), EUR(100)), txflags(tfPassive));
            env.close();
            env(offer(alice, EUR(100), USD(100)), txflags(tfPassive));
            env.close();
            env(pay(bob, carol, USD(100)),
                path(~EUR, ~USD),
                sendmax(XRP(102)),
                txflags(tfPartialPayment));
            env.close();
            BEAST_EXPECT(ammAlice.expectBalances(
                XRPAmount(10050238637),
                STAmount(USD, UINT64_C(995001249687578), -11),
                ammAlice.tokens()));
            BEAST_EXPECT(expectOffers(
                env,
                alice,
                2,
                {{Amounts{
                      XRPAmount(50487378),
                      STAmount(EUR, UINT64_C(4998750312422), -11)},
                  Amounts{
                      STAmount(EUR, UINT64_C(4998750312422), -11),
                      STAmount(USD, UINT64_C(4998750312422), -11)}}}));
            // Initial 30,000 + 99.99999999999
            BEAST_EXPECT(expectLine(
                env, carol, STAmount{USD, UINT64_C(3009999999999999), -11}));
            // Initial 1,000 - 50238637(AMM pool) - 50512622(offer) - 10(tx
            // fee)
            BEAST_EXPECT(expectLedgerEntryRoot(
                env,
                bob,
                XRP(1000) - XRPAmount{50238637} - XRPAmount{50512622} -
                    txfee(env, 1)));
        });

        // Default path with AMM and Order Book offer. AMM is consumed first,
        // remaining amount is consumed by the offer.
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                fund(env, gw, {bob}, {USD(100)}, Fund::Acct);
                env.close();
                env(offer(bob, XRP(100), USD(100)), txflags(tfPassive));
                env.close();
                env(pay(alice, carol, USD(200)),
                    sendmax(XRP(200)),
                    txflags(tfPartialPayment));
                env.close();
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(10100), USD(10000), ammAlice.tokens()));
                // Initial 30,000 + 200
                BEAST_EXPECT(expectLine(env, carol, USD(30200)));
                // Initial 30,000 - 10000(AMM pool LP) - 100(AMM offer) -
                // - 100(offer) - 10(tx fee) - one reserve
                BEAST_EXPECT(expectLedgerEntryRoot(
                    env,
                    alice,
                    XRP(30000) - XRP(10000) - XRP(100) - XRP(100) -
                        ammCrtFee(env) - txfee(env, 1)));
                BEAST_EXPECT(expectOffers(env, bob, 0));
            },
            {{XRP(10000), USD(10100)}});

        // Default path with AMM and Order Book offer.
        // Order Book offer is consumed first.
        // Remaining amount is consumed by AMM.
        {
            Env env(*this);
            fund(env, gw, {alice, bob, carol}, XRP(20000), {USD(2000)});
            env(offer(bob, XRP(50), USD(150)), txflags(tfPassive));
            AMM ammAlice(env, alice, XRP(1000), USD(1050));
            env(pay(alice, carol, USD(200)),
                sendmax(XRP(200)),
                txflags(tfPartialPayment));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(1050), USD(1000), ammAlice.tokens()));
            BEAST_EXPECT(expectLine(env, carol, USD(2200)));
            BEAST_EXPECT(expectOffers(env, bob, 0));
        }

        // Offer crossing XRP/IOU
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                fund(env, gw, {bob}, {USD(1000)}, Fund::Acct);
                env.close();
                env(offer(bob, USD(100), XRP(100)));
                env.close();
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(10100), USD(10000), ammAlice.tokens()));
                // Initial 1,000 + 100
                BEAST_EXPECT(expectLine(env, bob, USD(1100)));
                // Initial 30,000 - 100(offer) - 10(tx fee)
                BEAST_EXPECT(expectLedgerEntryRoot(
                    env, bob, XRP(30000) - XRP(100) - txfee(env, 1)));
                BEAST_EXPECT(expectOffers(env, bob, 0));
            },
            {{XRP(10000), USD(10100)}});

        // Offer crossing IOU/IOU and transfer rate
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                env(rate(gw, 1.25));
                env.close();
                env(offer(carol, EUR(100), GBP(100)));
                env.close();
                // No transfer fee
                BEAST_EXPECT(ammAlice.expectBalances(
                    GBP(1100), EUR(1000), ammAlice.tokens()));
                // Initial 30,000 - 100(offer) - 25% transfer fee
                BEAST_EXPECT(expectLine(env, carol, GBP(29875)));
                // Initial 30,000 + 100(offer)
                BEAST_EXPECT(expectLine(env, carol, EUR(30100)));
                BEAST_EXPECT(expectOffers(env, bob, 0));
            },
            {{GBP(1000), EUR(1100)}});

        // Payment and transfer fee
        // Scenario:
        // Bob sends 125GBP to pay 100EUR to Carol
        // Payment execution:
        // bob's 125GBP/1.25 = 100GBP
        // 100GBP/100EUR AMM offer
        // 100EUR/1 (no AMM tr fee) = 100EUR paid to carol
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                fund(env, gw, {bob}, {GBP(200), EUR(200)}, Fund::Acct);
                env(rate(gw, 1.25));
                env.close();
                env(pay(bob, carol, EUR(100)),
                    path(~EUR),
                    sendmax(GBP(125)),
                    txflags(tfPartialPayment));
                env.close();
            },
            {{GBP(1000), EUR(1100)}});

        // Payment and transfer fee, multiple steps
        // Scenario:
        // Dan's offer 200CAN/200GBP
        // AMM 1000GBP/10125EUR
        // Ed's offer 200EUR/200USD
        // Bob sends 195.3125CAN to pay 100USD to Carol
        // Payment execution:
        // bob's 195.3125CAN/1.25 = 156.25CAN -> dan's offer
        // 156.25CAN/156.25GBP 156.25GBP/1.25 = 125GBP -> AMM's offer
        // 125GBP/125EUR 125EUR/1 (no AMM tr fee) = 125EUR -> ed's offer
        // 125EUR/125USD 125USD/1.25 = 100USD paid to carol
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                Account const dan("dan");
                Account const ed("ed");
                auto const CAN = gw["CAN"];
                fund(env, gw, {dan}, {CAN(200), GBP(200)}, Fund::Acct);
                fund(env, gw, {ed}, {EUR(200), USD(200)}, Fund::Acct);
                fund(env, gw, {bob}, {CAN(195.3125)}, Fund::Acct);
                env(trust(carol, USD(100)));
                env(rate(gw, 1.25));
                env.close();
                env(offer(dan, CAN(200), GBP(200)));
                env(offer(ed, EUR(200), USD(200)));
                env.close();
                env(pay(bob, carol, USD(100)),
                    path(~GBP, ~EUR, ~USD),
                    sendmax(CAN(195.3125)),
                    txflags(tfPartialPayment));
                env.close();
                BEAST_EXPECT(expectLine(env, bob, CAN(0)));
                BEAST_EXPECT(expectLine(env, dan, CAN(356.25), GBP(43.75)));
                BEAST_EXPECT(ammAlice.expectBalances(
                    GBP(10125), EUR(10000), ammAlice.tokens()));
                BEAST_EXPECT(expectLine(env, ed, EUR(325), USD(75)));
                BEAST_EXPECT(expectLine(env, carol, USD(100)));
            },
            {{GBP(10000), EUR(10125)}});

        // Pay amounts close to one side of the pool
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                env(pay(alice, carol, USD(99.99)),
                    path(~USD),
                    sendmax(XRP(1)),
                    txflags(tfPartialPayment),
                    ter(tesSUCCESS));
                env(pay(alice, carol, USD(100)),
                    path(~USD),
                    sendmax(XRP(1)),
                    txflags(tfPartialPayment),
                    ter(tesSUCCESS));
                env(pay(alice, carol, XRP(100)),
                    path(~XRP),
                    sendmax(USD(1)),
                    txflags(tfPartialPayment),
                    ter(tesSUCCESS));
                env(pay(alice, carol, STAmount{xrpIssue(), 99999900}),
                    path(~XRP),
                    sendmax(USD(1)),
                    txflags(tfPartialPayment),
                    ter(tesSUCCESS));
            },
            {{XRP(100), USD(100)}});

        // Multiple paths/steps
        {
            Env env(*this);
            auto const ETH = gw["ETH"];
            fund(
                env,
                gw,
                {alice},
                XRP(100000),
                {EUR(50000), BTC(50000), ETH(50000), USD(50000)});
            fund(env, gw, {carol, bob}, XRP(1000), {USD(200)}, Fund::Acct);
            AMM xrp_eur(env, alice, XRP(10100), EUR(10000));
            AMM eur_btc(env, alice, EUR(10000), BTC(10200));
            AMM btc_usd(env, alice, BTC(10100), USD(10000));
            AMM xrp_usd(env, alice, XRP(10150), USD(10200));
            AMM xrp_eth(env, alice, XRP(10000), ETH(10100));
            AMM eth_eur(env, alice, ETH(10900), EUR(11000));
            AMM eur_usd(env, alice, EUR(10100), USD(10000));
            env(pay(bob, carol, USD(100)),
                path(~EUR, ~BTC, ~USD),
                path(~USD),
                path(~ETH, ~EUR, ~USD),
                sendmax(XRP(200)));
            // XRP-ETH-EUR-USD
            // This path provides ~26.06USD/26.2XRP
            BEAST_EXPECT(xrp_eth.expectBalances(
                XRPAmount(10026208900),
                STAmount{ETH, UINT64_C(1007365779244494), -11},
                xrp_eth.tokens()));
            BEAST_EXPECT(eth_eur.expectBalances(
                STAmount{ETH, UINT64_C(1092634220755506), -11},
                STAmount{EUR, UINT64_C(1097354232078752), -11},
                eth_eur.tokens()));
            BEAST_EXPECT(eur_usd.expectBalances(
                STAmount{EUR, UINT64_C(1012645767921248), -11},
                STAmount{USD, UINT64_C(997393151712086), -11},
                eur_usd.tokens()));

            // XRP-USD path
            // This path provides ~73.9USD/74.1XRP
            BEAST_EXPECT(xrp_usd.expectBalances(
                XRPAmount(10224106246),
                STAmount{USD, UINT64_C(1012606848287914), -11},
                xrp_usd.tokens()));

            // XRP-EUR-BTC-USD
            // This path doesn't provide any liquidity due to how
            // offers are generated in multi-path. Analytical solution
            // shows a different distribution:
            // XRP-EUR-BTC-USD 11.6USD/11.64XRP, XRP-USD 60.7USD/60.8XRP,
            // XRP-ETH-EUR-USD 27.6USD/27.6XRP
            BEAST_EXPECT(xrp_eur.expectBalances(
                XRP(10100), EUR(10000), xrp_eur.tokens()));
            BEAST_EXPECT(eur_btc.expectBalances(
                EUR(10000), BTC(10200), eur_btc.tokens()));
            BEAST_EXPECT(btc_usd.expectBalances(
                BTC(10100), USD(10000), btc_usd.tokens()));

            BEAST_EXPECT(expectLine(env, carol, USD(300)));
        }

        // Dependent AMM
        {
            Env env(*this);
            auto const ETH = gw["ETH"];
            fund(
                env,
                gw,
                {alice},
                XRP(40000),
                {EUR(50000), BTC(50000), ETH(50000), USD(50000)});
            fund(env, gw, {carol, bob}, XRP(1000), {USD(200)}, Fund::Acct);
            AMM xrp_eur(env, alice, XRP(10100), EUR(10000));
            AMM eur_btc(env, alice, EUR(10000), BTC(10200));
            AMM btc_usd(env, alice, BTC(10100), USD(10000));
            AMM xrp_eth(env, alice, XRP(10000), ETH(10100));
            AMM eth_eur(env, alice, ETH(10900), EUR(11000));
            env(pay(bob, carol, USD(100)),
                path(~EUR, ~BTC, ~USD),
                path(~ETH, ~EUR, ~BTC, ~USD),
                sendmax(XRP(200)));
            // XRP-EUR-BTC-USD path provides ~17.8USD/~18.7XRP
            // XRP-ETH-EUR-BTC-USD path provides ~82.2USD/82.4XRP
            BEAST_EXPECT(xrp_eur.expectBalances(
                XRPAmount(10118738472),
                STAmount{EUR, UINT64_C(9981544436337968), -12},
                xrp_eur.tokens()));
            BEAST_EXPECT(eur_btc.expectBalances(
                STAmount{EUR, UINT64_C(1010116096785173), -11},
                STAmount{BTC, UINT64_C(1009791426968066), -11},
                eur_btc.tokens()));
            BEAST_EXPECT(btc_usd.expectBalances(
                STAmount{BTC, UINT64_C(1020208573031934), -11},
                USD(9900),
                btc_usd.tokens()));
            BEAST_EXPECT(xrp_eth.expectBalances(
                XRPAmount(10082446396),
                STAmount{ETH, UINT64_C(1001741072778012), -11},
                xrp_eth.tokens()));
            BEAST_EXPECT(eth_eur.expectBalances(
                STAmount{ETH, UINT64_C(1098258927221988), -11},
                STAmount{EUR, UINT64_C(109172945958103), -10},
                eth_eur.tokens()));
            BEAST_EXPECT(expectLine(env, carol, USD(300)));
        }

        // AMM offers limit
        // Consuming 30 CLOB offers, results in hitting 30 AMM offers limit.
        testAMM([&](AMM& ammAlice, Env& env) {
            env.fund(XRP(1000), bob);
            fund(env, gw, {bob}, {EUR(400)}, Fund::IOUOnly);
            env(trust(alice, EUR(200)));
            for (int i = 0; i < 30; ++i)
                env(offer(alice, EUR(1.0 + 0.01 * i), XRP(1)));
            // This is worse quality offer than 30 offers above.
            // It will not be consumed because of AMM offers limit.
            env(offer(alice, EUR(140), XRP(100)));
            env(pay(bob, carol, USD(100)),
                path(~XRP, ~USD),
                sendmax(EUR(400)),
                txflags(tfPartialPayment | tfNoRippleDirect));
            // Carol gets ~29.91USD because of the AMM offers limit
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10030),
                STAmount{USD, UINT64_C(9970089730807577), -12},
                ammAlice.tokens()));
            BEAST_EXPECT(expectLine(
                env, carol, STAmount{USD, UINT64_C(3002991026919241), -11}));
            BEAST_EXPECT(expectOffers(env, alice, 1, {{{EUR(140), XRP(100)}}}));
        });
        // This payment is fulfilled
        testAMM([&](AMM& ammAlice, Env& env) {
            env.fund(XRP(1000), bob);
            fund(env, gw, {bob}, {EUR(400)}, Fund::IOUOnly);
            env(trust(alice, EUR(200)));
            for (int i = 0; i < 29; ++i)
                env(offer(alice, EUR(1.0 + 0.01 * i), XRP(1)));
            // This is worse quality offer than 30 offers above.
            // It will not be consumed because of AMM offers limit.
            env(offer(alice, EUR(140), XRP(100)));
            env(pay(bob, carol, USD(100)),
                path(~XRP, ~USD),
                sendmax(EUR(400)),
                txflags(tfPartialPayment | tfNoRippleDirect));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRPAmount{10101010102}, USD(9900), ammAlice.tokens()));
            // Carol gets ~100USD
            BEAST_EXPECT(expectLine(
                env, carol, STAmount{USD, UINT64_C(3009999999999999), -11}));
            BEAST_EXPECT(expectOffers(
                env,
                alice,
                1,
                {{{STAmount{EUR, 391858572, -7}, XRPAmount{27989898}}}}));
        });

        // Offer crossing with AMM and another offer. AMM has a better
        // quality and is consumed first.
        {
            Env env(*this);
            fund(env, gw, {alice, carol, bob}, XRP(30000), {USD(30000)});
            env(offer(bob, XRP(100), USD(100.001)));
            AMM ammAlice(env, alice, XRP(10000), USD(10100));
            env(offer(carol, USD(100), XRP(100)));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRPAmount{10049825373},
                STAmount{USD, UINT64_C(1004992586949302), -11},
                ammAlice.tokens()));
            BEAST_EXPECT(expectOffers(
                env,
                bob,
                1,
                {{{XRPAmount{50074629},
                   STAmount{USD, UINT64_C(5007513050698), -11}}}}));
            BEAST_EXPECT(expectLine(env, carol, USD(30100)));
        }

        // Individually frozen account
        testAMM([&](AMM& ammAlice, Env& env) {
            env(trust(gw, carol["USD"](0), tfSetFreeze));
            env(trust(gw, alice["USD"](0), tfSetFreeze));
            env.close();
            env(pay(alice, carol, USD(1)),
                path(~USD),
                sendmax(XRP(10)),
                txflags(tfNoRippleDirect | tfPartialPayment),
                ter(tesSUCCESS));
        });
    }

    void
    testAMMTokens()
    {
        testcase("AMM Token Pool - AMM with token(s) from another AMM");
        using namespace jtx;

        // AMM with one LPToken from another AMM.
        testAMM([&](AMM& ammAlice, Env& env) {
            fund(env, gw, {alice}, {EUR(10000)}, Fund::IOUOnly);
            AMM ammAMMToken(
                env, alice, EUR(10000), STAmount{ammAlice.lptIssue(), 1000000});
            BEAST_EXPECT(ammAMMToken.expectBalances(
                EUR(10000),
                STAmount(ammAlice.lptIssue(), 1000000),
                ammAMMToken.tokens()));
        });

        // AMM with two LPTokens from other AMMs.
        // LP deposits/withdraws.
        testAMM([&](AMM& ammAlice, Env& env) {
            fund(env, gw, {alice}, {EUR(10000)}, Fund::IOUOnly);
            AMM ammAlice1(env, alice, XRP(10000), EUR(10000));
            auto const token1 = ammAlice.lptIssue();
            auto const token2 = ammAlice1.lptIssue();
            AMM ammAMMTokens(
                env,
                alice,
                STAmount{token1, 1000000},
                STAmount{token2, 1000000});
            BEAST_EXPECT(ammAMMTokens.expectBalances(
                STAmount(token1, 1000000),
                STAmount(token2, 1000000),
                ammAMMTokens.tokens()));
            ammAMMTokens.deposit(alice, 10000);
            ammAMMTokens.withdraw(alice, 10000);
            BEAST_EXPECT(ammAMMTokens.expectBalances(
                STAmount(token1, 1000000),
                STAmount(token2, 1000000),
                IOUAmount{1000000, 0}));
        });

        // Offer crossing with two AMM LPtokens.
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.deposit(carol, 1000000);
            fund(env, gw, {alice, carol}, {EUR(10000)}, Fund::IOUOnly);
            AMM ammAlice1(env, alice, XRP(10000), EUR(10000));
            ammAlice1.deposit(carol, 1000000);
            auto const token1 = ammAlice.lptIssue();
            auto const token2 = ammAlice1.lptIssue();
            env(offer(alice, STAmount{token1, 100}, STAmount{token2, 100}),
                txflags(tfPassive));
            env.close();
            BEAST_EXPECT(expectOffers(env, alice, 1));
            env(offer(carol, STAmount{token2, 100}, STAmount{token1, 100}));
            env.close();
            BEAST_EXPECT(
                expectLine(env, alice, STAmount{token1, 10000100}) &&
                expectLine(env, alice, STAmount{token2, 9999900}));
            BEAST_EXPECT(
                expectLine(env, carol, STAmount{token2, 1000100}) &&
                expectLine(env, carol, STAmount{token1, 999900}));
            BEAST_EXPECT(
                expectOffers(env, alice, 0) && expectOffers(env, carol, 0));
        });

        // Offer crossing with two AMM LPTokens via AMM.
        testAMM([&](AMM& ammAlice, Env& env) {
            ammAlice.deposit(carol, 1000000);
            fund(env, gw, {alice, carol}, {EUR(10000)}, Fund::IOUOnly);
            AMM ammAlice1(env, alice, XRP(10000), EUR(10000));
            ammAlice1.deposit(carol, 1000000);
            auto const token1 = ammAlice.lptIssue();
            auto const token2 = ammAlice1.lptIssue();
            AMM ammAMMTokens(
                env, alice, STAmount{token1, 10000}, STAmount{token2, 10100});
            env(offer(carol, STAmount{token2, 100}, STAmount{token1, 100}));
            env.close();
            BEAST_EXPECT(expectOffers(env, carol, 0));
            BEAST_EXPECT(ammAMMTokens.expectBalances(
                STAmount(token1, 10100),
                STAmount(token2, 10000),
                ammAMMTokens.tokens()));
            // Carol initial token1 1,000,000 - 100(offer)
            BEAST_EXPECT(expectLine(env, carol, STAmount{token1, 999900}));
            // Carol initial token2 1,000,000 + 100(offer)
            BEAST_EXPECT(expectLine(env, carol, STAmount{token2, 1000100}));
        });

        // LPs pay LPTokens directly. Must trust set because the trust line
        // is checked for the limit, which is 0 in the AMM auto-created
        // trust line.
        testAMM([&](AMM& ammAlice, Env& env) {
            auto const token1 = ammAlice.lptIssue();
            env.trust(STAmount{token1, 2000000}, carol);
            env.close();
            ammAlice.deposit(carol, 1000000);
            BEAST_EXPECT(
                ammAlice.expectLPTokens(alice, IOUAmount{10000000, 0}) &&
                ammAlice.expectLPTokens(carol, IOUAmount{1000000, 0}));
            // Pool balance doesn't change, only tokens moved from
            // one line to another.
            env(pay(alice, carol, STAmount{token1, 100}));
            env.close();
            BEAST_EXPECT(
                // Alice initial token1 10,000,000 - 100
                ammAlice.expectLPTokens(alice, IOUAmount{9999900, 0}) &&
                // Carol initial token1 1,000,000 + 100
                ammAlice.expectLPTokens(carol, IOUAmount{1000100, 0}));

            env.trust(STAmount{token1, 20000000}, alice);
            env.close();
            env(pay(carol, alice, STAmount{token1, 100}));
            env.close();
            // Back to the original balance
            BEAST_EXPECT(
                ammAlice.expectLPTokens(alice, IOUAmount{10000000, 0}) &&
                ammAlice.expectLPTokens(carol, IOUAmount{1000000, 0}));
        });

        // AMM with two tokens from another AMM.
        // LP pays LPTokens to non-LP via AMM.
        // Non-LP must trust set for LPTokens.
        testAMM([&](AMM& ammAlice, Env& env) {
            fund(env, gw, {alice}, {EUR(10000)}, Fund::IOUOnly);
            AMM ammAlice1(env, alice, XRP(10000), EUR(10000));
            auto const token1 = ammAlice.lptIssue();
            auto const token2 = ammAlice1.lptIssue();
            AMM ammAMMTokens(
                env,
                alice,
                STAmount{token1, 1000100},
                STAmount{token2, 1000000});
            BEAST_EXPECT(ammAMMTokens.expectBalances(
                STAmount(token1, 1000100),
                STAmount(token2, 1000000),
                ammAMMTokens.tokens()));
            env.trust(STAmount{token1, 1000}, carol);
            env.close();
            env(pay(alice, carol, STAmount{token1, 100}),
                path(BookSpec(token1.account, token1.currency)),
                sendmax(STAmount{token2, 100}),
                txflags(tfNoRippleDirect));
            env.close();
            BEAST_EXPECT(ammAMMTokens.expectBalances(
                STAmount(token1, 1000000),
                STAmount(token2, 1000100),
                ammAMMTokens.tokens()));
            // Alice's token1 balance doesn't change after the payment.
            // The payment comes out of AMM pool. Alice's token1 balance
            // is initial 10,000,000 - 1,000,100 deposited into ammAMMTokens
            // pool.
            BEAST_EXPECT(ammAlice.expectLPTokens(alice, IOUAmount{8999900}));
            // Carol got 100 token1 from ammAMMTokens pool. Alice swaps
            // in 100 token2 into ammAMMTokens pool.
            BEAST_EXPECT(ammAlice.expectLPTokens(carol, IOUAmount{100}));
            // Alice's token2 balance changes. Initial 10,000,000 - 1,000,000
            // deposited into ammAMMTokens pool - 100 payment.
            BEAST_EXPECT(ammAlice1.expectLPTokens(alice, IOUAmount{8999900}));
        });
    }

    void
    testAmendment()
    {
        testcase("Amendment");
        using namespace jtx;
        FeatureBitset const all{supported_amendments()};
        FeatureBitset const noAMM{all - featureAMM};
        FeatureBitset const noNumber{all - fixUniversalNumber};
        FeatureBitset const noAMMAndNumber{
            all - featureAMM - fixUniversalNumber};

        for (auto const& feature : {noAMM, noNumber, noAMMAndNumber})
        {
            Env env{*this, feature};
            fund(env, gw, {alice}, {USD(1000)}, Fund::All);
            AMM amm(env, alice, XRP(1000), USD(1000), ter(temDISABLED));
        }
    }

    void
    testFlags()
    {
        testcase("Flags");
        using namespace jtx;

        testAMM([&](AMM& ammAlice, Env& env) {
            auto const info = env.rpc(
                "json",
                "account_info",
                std::string(
                    "{\"account\": \"" + to_string(ammAlice.ammAccount()) +
                    "\"}"));
            auto const flags =
                info[jss::result][jss::account_data][jss::Flags].asUInt();
            BEAST_EXPECT(
                flags ==
                (lsfAMM | lsfDisableMaster | lsfDefaultRipple |
                 lsfDepositAuth));
        });
    }

    void
    testRippling()
    {
        testcase("Rippling");
        using namespace jtx;

        // Rippling via AMM fails because AMM trust line has 0 limit.
        // Set up two issuers, A and B. Have each issue a token called TST.
        // Have another account C hold TST from both issuers,
        //   and create an AMM for this pair.
        // Have a fourth account, D, create a trust line to the AMM for TST.
        // Send a payment delivering TST.AMM from C to D, using SendMax in
        //   TST.A (or B) and a path through the AMM account. By normal
        //   rippling rules, this would have caused the AMM's balances
        //   to shift at a 1:1 rate with no fee applied has it not been
        //   for 0 limit.
        {
            Env env(*this);
            auto const A = Account("A");
            auto const B = Account("B");
            auto const TSTA = A["TST"];
            auto const TSTB = B["TST"];
            auto const C = Account("C");
            auto const D = Account("D");

            env.fund(XRP(10000), A);
            env.fund(XRP(10000), B);
            env.fund(XRP(10000), C);
            env.fund(XRP(10000), D);

            env.trust(TSTA(10000), C);
            env.trust(TSTB(10000), C);
            env(pay(A, C, TSTA(10000)));
            env(pay(B, C, TSTB(10000)));
            AMM amm(env, C, TSTA(5000), TSTB(5000));
            auto const ammIss = Issue(TSTA.currency, amm.ammAccount());

            env.trust(STAmount{ammIss, 10000}, D);
            env.close();

            env(pay(C, D, STAmount{ammIss, 10}),
                sendmax(TSTA(100)),
                path(amm.ammAccount()),
                txflags(tfPartialPayment | tfNoRippleDirect),
                ter(tecPATH_DRY));
        }
    }

    void
    testAMMAndCLOB()
    {
        testcase("AMMAndCLOB, offer quality change");
        using namespace jtx;
        auto const gw = Account("gw");
        auto const TST = gw["TST"];
        auto const LP1 = Account("LP1");
        auto const LP2 = Account("LP2");

        auto prep = [&](auto const& offerCb, auto const& expectCb) {
            Env env(*this);
            env.fund(XRP(30'000'000'000), gw);
            env(offer(gw, XRP(11'500'000'000), TST(1'000'000'000)));

            env.fund(XRP(10'000), LP1);
            env.fund(XRP(10'000), LP2);
            env(offer(LP1, TST(25), XRPAmount(287'500'000)));

            // Either AMM or CLOB offer
            offerCb(env);

            env(offer(LP2, TST(25), XRPAmount(287'500'000)));

            expectCb(env);
        };

        // If we replace AMM with equivalent CLOB offer, which
        // AMM generates when it is consumed, then the
        // result must be identical.
        std::string lp2TSTBalance;
        std::string lp2TakerGets;
        std::string lp2TakerPays;
        // Execute with AMM first
        prep(
            [&](Env& env) { AMM amm(env, LP1, TST(25), XRP(250)); },
            [&](Env& env) {
                lp2TSTBalance =
                    getAccountLines(env, LP2, TST)["lines"][0u]["balance"]
                        .asString();
                auto const offer = getAccountOffers(env, LP2)["offers"][0u];
                lp2TakerGets = offer["taker_gets"].asString();
                lp2TakerPays = offer["taker_pays"]["value"].asString();
            });
        // Execute with CLOB offer
        prep(
            [&](Env& env) {
                env(offer(
                        LP1,
                        XRPAmount{18095133},
                        STAmount{TST, UINT64_C(168737984885388), -14}),
                    txflags(tfPassive));
            },
            [&](Env& env) {
                BEAST_EXPECT(
                    lp2TSTBalance ==
                    getAccountLines(env, LP2, TST)["lines"][0u]["balance"]
                        .asString());
                auto const offer = getAccountOffers(env, LP2)["offers"][0u];
                BEAST_EXPECT(lp2TakerGets == offer["taker_gets"].asString());
                BEAST_EXPECT(
                    lp2TakerPays == offer["taker_pays"]["value"].asString());
            });
    }

    void
    testTradingFee()
    {
        testcase("Trading Fee");
        using namespace jtx;

        // Single Deposit, 1% fee
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                // No fee
                ammAlice.deposit(carol, USD(3000));
                BEAST_EXPECT(ammAlice.expectLPTokens(carol, IOUAmount{1000}));
                ammAlice.withdrawAll(carol, USD(3000));
                BEAST_EXPECT(ammAlice.expectLPTokens(carol, IOUAmount{0}));
                BEAST_EXPECT(expectLine(env, carol, USD(30000)));
                // Set fee to 1%
                ammAlice.vote(alice, 1000);
                BEAST_EXPECT(ammAlice.expectTradingFee(1000));
                // Carol gets fewer LPToken ~994, because of the single deposit
                // fee
                ammAlice.deposit(carol, USD(3000));
                BEAST_EXPECT(ammAlice.expectLPTokens(
                    carol, IOUAmount{994981155689671, -12}));
                BEAST_EXPECT(expectLine(env, carol, USD(27000)));
                // Set fee to 0
                ammAlice.vote(alice, 0);
                ammAlice.withdrawAll(carol, USD(0));
                // Carol gets back less than the original deposit
                BEAST_EXPECT(expectLine(
                    env,
                    carol,
                    STAmount{USD, UINT64_C(2999496220068281), -11}));
            },
            {{USD(1000), EUR(1000)}});

        // Single deposit with EP not exceeding specified:
        // 100USD with EP not to exceed 0.1 (AssetIn/TokensOut). 1% fee.
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                auto const balance = env.balance(carol, USD);
                auto tokensFee = ammAlice.deposit(
                    carol, USD(1000), std::nullopt, STAmount{USD, 1, -1});
                auto const deposit = balance - env.balance(carol, USD);
                ammAlice.withdrawAll(carol, USD(0));
                ammAlice.vote(alice, 0);
                BEAST_EXPECT(ammAlice.expectTradingFee(0));
                auto const tokensNoFee = ammAlice.deposit(carol, deposit);
                // carol pays ~2008 LPTokens in fees or ~0.5% of the no-fee
                // LPTokens
                BEAST_EXPECT(tokensFee == IOUAmount(4856360611129, -7));
                BEAST_EXPECT(tokensNoFee == IOUAmount(48764485901109, -8));
            },
            std::nullopt,
            1000);

        // Single deposit with EP not exceeding specified:
        // 200USD with EP not to exceed 0.002020 (AssetIn/TokensOut). 1% fee
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                auto const balance = env.balance(carol, USD);
                auto const tokensFee = ammAlice.deposit(
                    carol, USD(200), std::nullopt, STAmount{USD, 2020, -6});
                auto const deposit = balance - env.balance(carol, USD);
                ammAlice.withdrawAll(carol, USD(0));
                ammAlice.vote(alice, 0);
                BEAST_EXPECT(ammAlice.expectTradingFee(0));
                auto const tokensNoFee = ammAlice.deposit(carol, deposit);
                // carol pays ~475 LPTokens in fees or ~0.5% of the no-fee
                // LPTokens
                BEAST_EXPECT(tokensFee == IOUAmount(9800000000002, -8));
                BEAST_EXPECT(tokensNoFee == IOUAmount(9847581871545, -8));
            },
            std::nullopt,
            1000);

        // Single Withdrawal, 1% fee
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                // No fee
                ammAlice.deposit(carol, USD(3000));

                BEAST_EXPECT(ammAlice.expectLPTokens(carol, IOUAmount{1000}));
                BEAST_EXPECT(expectLine(env, carol, USD(27000)));
                // Set fee to 1%
                ammAlice.vote(alice, 1000);
                BEAST_EXPECT(ammAlice.expectTradingFee(1000));
                // Single withdrawal. Carol gets ~5USD less than deposited.
                ammAlice.withdrawAll(carol, USD(0));
                BEAST_EXPECT(expectLine(
                    env,
                    carol,
                    STAmount{USD, UINT64_C(2999497487437186), -11}));
            },
            {{USD(1000), EUR(1000)}});

        // Withdraw with EPrice limit, 1% fee.
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                ammAlice.deposit(carol, 1000000);
                auto const tokensFee = ammAlice.withdraw(
                    carol, USD(100), std::nullopt, IOUAmount{520, 0});
                // carol withdraws ~1,443.44USD
                auto const balanceAfterWithdraw =
                    STAmount(USD, UINT64_C(3044343891402715), -11);
                BEAST_EXPECT(env.balance(carol, USD) == balanceAfterWithdraw);
                // Set to original pool size
                auto const deposit = balanceAfterWithdraw - USD(29000);
                ammAlice.deposit(carol, deposit);
                // fee 0%
                ammAlice.vote(alice, 0);
                BEAST_EXPECT(ammAlice.expectTradingFee(0));
                auto const tokensNoFee = ammAlice.withdraw(carol, deposit);
                BEAST_EXPECT(
                    env.balance(carol, USD) ==
                    STAmount(USD, UINT64_C(3044343891402717), -11));
                // carol pays ~4008 LPTokens in fees or ~0.5% of the no-fee
                // LPTokens
                BEAST_EXPECT(tokensNoFee == IOUAmount(74657980779913, -8));
                BEAST_EXPECT(tokensFee == IOUAmount(75058823529411, -8));
            },
            std::nullopt,
            1000);

        // Payment, 1% fee
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                fund(
                    env,
                    gw,
                    {bob},
                    XRP(1000),
                    {USD(1000), EUR(1000)},
                    Fund::Acct);
                // Alice contributed 1010EUR and 1000USD to the pool
                BEAST_EXPECT(expectLine(env, alice, EUR(28990)));
                BEAST_EXPECT(expectLine(env, alice, USD(29000)));
                BEAST_EXPECT(expectLine(env, carol, USD(30000)));
                // Carol pays to Alice with no fee
                env(pay(carol, alice, EUR(10)),
                    path(~EUR),
                    sendmax(USD(10)),
                    txflags(tfNoRippleDirect));
                env.close();
                // Alice has 10EUR more and Carol has 10USD less
                BEAST_EXPECT(expectLine(env, alice, EUR(29000)));
                BEAST_EXPECT(expectLine(env, alice, USD(29000)));
                BEAST_EXPECT(expectLine(env, carol, USD(29990)));

                // Set fee to 1%
                ammAlice.vote(alice, 1000);
                BEAST_EXPECT(ammAlice.expectTradingFee(1000));
                // Bob pays to Carol with 1% fee
                env(pay(bob, carol, USD(10)),
                    path(~USD),
                    sendmax(EUR(15)),
                    txflags(tfNoRippleDirect));
                env.close();
                // Bob sends 10.1~EUR to pay 10USD
                BEAST_EXPECT(expectLine(
                    env, bob, STAmount{EUR, UINT64_C(9898989898989899), -13}));
                // Carol got 10USD
                BEAST_EXPECT(expectLine(env, carol, USD(30000)));
                BEAST_EXPECT(ammAlice.expectBalances(
                    USD(1000),
                    STAmount{EUR, UINT64_C(101010101010101), -11},
                    ammAlice.tokens()));
            },
            {{USD(1000), EUR(1010)}});

        // Offer crossing, 0.5% fee
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                // No fee
                env(offer(carol, EUR(10), USD(10)));
                env.close();
                BEAST_EXPECT(expectLine(env, carol, USD(29990)));
                BEAST_EXPECT(expectLine(env, carol, EUR(30010)));
                // Change pool composition back
                env(offer(carol, USD(10), EUR(10)));
                env.close();
                // Set fee to 0.5%
                ammAlice.vote(alice, 500);
                BEAST_EXPECT(ammAlice.expectTradingFee(500));
                env(offer(carol, EUR(10), USD(10)));
                env.close();
                // Alice gets fewer ~4.97EUR for ~5.02USD, the difference goes
                // to the pool
                BEAST_EXPECT(expectLine(
                    env,
                    carol,
                    STAmount{USD, UINT64_C(2999502512562814), -11}));
                BEAST_EXPECT(expectLine(
                    env,
                    carol,
                    STAmount{EUR, UINT64_C(3000497487437186), -11}));
                BEAST_EXPECT(expectOffers(
                    env,
                    carol,
                    1,
                    {{Amounts{
                        STAmount{EUR, UINT64_C(5025125628140704), -15},
                        STAmount{USD, UINT64_C(5025125628140704), -15}}}}));
                BEAST_EXPECT(ammAlice.expectBalances(
                    STAmount{USD, UINT64_C(1004974874371859), -12},
                    STAmount{EUR, UINT64_C(1005025125628141), -12},
                    ammAlice.tokens()));
            },
            {{USD(1000), EUR(1010)}});

        // Payment with AMM and CLOB offer, 0 fee
        // AMM liquidity is consumed first up to CLOB offer quality
        // CLOB offer is fully consumed next
        // Remaining amount is consumed via AMM liquidity
        {
            Env env(*this);
            Account const ed("ed");
            fund(
                env,
                gw,
                {alice, bob, carol, ed},
                XRP(1000),
                {USD(2000), EUR(2000)});
            env(offer(carol, EUR(5), USD(5)));
            AMM ammAlice(env, alice, USD(1005), EUR(1000));
            env(pay(bob, ed, USD(10)),
                path(~USD),
                sendmax(EUR(15)),
                txflags(tfNoRippleDirect));
            BEAST_EXPECT(expectLine(env, ed, USD(2010)));
            BEAST_EXPECT(expectLine(env, bob, EUR(1990)));
            BEAST_EXPECT(ammAlice.expectBalances(
                USD(1000), EUR(1005), ammAlice.tokens()));
            BEAST_EXPECT(expectOffers(env, carol, 0));
        }

        // Payment with AMM and CLOB offer. Same as above but with 0.25% fee.
        {
            Env env(*this);
            Account const ed("ed");
            fund(
                env,
                gw,
                {alice, bob, carol, ed},
                XRP(1000),
                {USD(2000), EUR(2000)});
            env(offer(carol, EUR(5), USD(5)));
            // Set 0.25% fee
            AMM ammAlice(env, alice, USD(1005), EUR(1000), false, 250);
            env(pay(bob, ed, USD(10)),
                path(~USD),
                sendmax(EUR(15)),
                txflags(tfNoRippleDirect));
            BEAST_EXPECT(expectLine(env, ed, USD(2010)));
            BEAST_EXPECT(expectLine(
                env, bob, STAmount{EUR, UINT64_C(1989987453007618), -12}));
            BEAST_EXPECT(ammAlice.expectBalances(
                USD(1000),
                STAmount{EUR, UINT64_C(1005012546992382), -12},
                ammAlice.tokens()));
            BEAST_EXPECT(expectOffers(env, carol, 0));
        }

        // Payment with AMM and CLOB offer. AMM has a better
        // spot price quality, but 1% fee offsets that. As the result
        // the entire trade is executed via LOB.
        {
            Env env(*this);
            Account const ed("ed");
            fund(
                env,
                gw,
                {alice, bob, carol, ed},
                XRP(1000),
                {USD(2000), EUR(2000)});
            env(offer(carol, EUR(10), USD(10)));
            // Set 1% fee
            AMM ammAlice(env, alice, USD(1005), EUR(1000), false, 1000);
            env(pay(bob, ed, USD(10)),
                path(~USD),
                sendmax(EUR(15)),
                txflags(tfNoRippleDirect));
            BEAST_EXPECT(expectLine(env, ed, USD(2010)));
            BEAST_EXPECT(expectLine(env, bob, EUR(1990)));
            BEAST_EXPECT(ammAlice.expectBalances(
                USD(1005), EUR(1000), ammAlice.tokens()));
            BEAST_EXPECT(expectOffers(env, carol, 0));
        }

        // Payment with AMM and CLOB offer. AMM has a better
        // spot price quality, but 1% fee offsets that.
        // The CLOB offer is consumed first and the remaining
        // amount is consumed via AMM liquidity.
        {
            Env env(*this);
            Account const ed("ed");
            fund(
                env,
                gw,
                {alice, bob, carol, ed},
                XRP(1000),
                {USD(2000), EUR(2000)});
            env(offer(carol, EUR(9), USD(9)));
            // Set 1% fee
            AMM ammAlice(env, alice, USD(1005), EUR(1000), false, 1000);
            env(pay(bob, ed, USD(10)),
                path(~USD),
                sendmax(EUR(15)),
                txflags(tfNoRippleDirect));
            BEAST_EXPECT(expectLine(env, ed, USD(2010)));
            BEAST_EXPECT(expectLine(
                env, bob, STAmount{EUR, UINT64_C(1989993923296712), -12}));
            BEAST_EXPECT(ammAlice.expectBalances(
                USD(1004),
                STAmount{EUR, UINT64_C(1001006076703288), -12},
                ammAlice.tokens()));
            BEAST_EXPECT(expectOffers(env, carol, 0));
        }
    }

    void
    testAdjustedTokens()
    {
        testcase("Adjusted Deposit/Withdraw Tokens");

        using namespace jtx;

        // Deposit/Withdraw in USD
        testAMM([&](AMM& ammAlice, Env& env) {
            Account const bob("bob");
            Account const ed("ed");
            Account const paul("paul");
            Account const dan("dan");
            Account const chris("chris");
            Account const simon("simon");
            Account const ben("ben");
            Account const nataly("nataly");
            fund(
                env,
                gw,
                {bob, ed, paul, dan, chris, simon, ben, nataly},
                {USD(1500000)},
                Fund::Acct);
            for (int i = 0; i < 10; ++i)
            {
                ammAlice.deposit(ben, STAmount{USD, 1, -10});
                ammAlice.withdrawAll(ben, USD(0));
                ammAlice.deposit(simon, USD(0.1));
                ammAlice.withdrawAll(simon, USD(0));
                ammAlice.deposit(chris, USD(1));
                ammAlice.withdrawAll(chris, USD(0));
                ammAlice.deposit(dan, USD(10));
                ammAlice.withdrawAll(dan, USD(0));
                ammAlice.deposit(bob, USD(100));
                ammAlice.withdrawAll(bob, USD(0));
                ammAlice.deposit(carol, USD(1000));
                ammAlice.withdrawAll(carol, USD(0));
                ammAlice.deposit(ed, USD(10000));
                ammAlice.withdrawAll(ed, USD(0));
                ammAlice.deposit(paul, USD(100000));
                ammAlice.withdrawAll(paul, USD(0));
                ammAlice.deposit(nataly, USD(1000000));
                ammAlice.withdrawAll(nataly, USD(0));
            }
            // Due to round off some accounts have a tiny gain, while
            // other have a tiny loss. The last account to withdraw
            // gets everything in the pool.
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000),
                STAmount{USD, UINT64_C(100000000000013), -10},
                IOUAmount{10000000}));
            BEAST_EXPECT(expectLine(env, ben, USD(1500000)));
            BEAST_EXPECT(expectLine(env, simon, USD(1500000)));
            BEAST_EXPECT(expectLine(env, chris, USD(1500000)));
            BEAST_EXPECT(expectLine(env, dan, USD(1500000)));
            BEAST_EXPECT(expectLine(
                env, carol, STAmount{USD, UINT64_C(3000000000000001), -11}));
            BEAST_EXPECT(expectLine(env, ed, USD(1500000)));
            BEAST_EXPECT(expectLine(env, paul, USD(1500000)));
            BEAST_EXPECT(expectLine(
                env, nataly, STAmount{USD, UINT64_C(1500000000000002), -9}));
            ammAlice.withdrawAll(alice);
            BEAST_EXPECT(!ammAlice.ammExists());
            BEAST_EXPECT(expectLine(
                env, alice, STAmount{USD, UINT64_C(300000000000013), -10}));
            // alice XRP balance is 30,000initial - 50 ammcreate fee - 10drops
            // fee
            BEAST_EXPECT(accountBalance(env, alice) == "29949999990");
        });

        // Same as above but deposit/withdraw in XRP
        testAMM([&](AMM& ammAlice, Env& env) {
            Account const bob("bob");
            Account const ed("ed");
            Account const paul("paul");
            Account const dan("dan");
            Account const chris("chris");
            Account const simon("simon");
            Account const ben("ben");
            Account const nataly("nataly");
            fund(
                env,
                gw,
                {bob, ed, paul, dan, chris, simon, ben, nataly},
                XRP(2000000),
                {},
                Fund::Acct);
            for (int i = 0; i < 10; ++i)
            {
                ammAlice.deposit(ben, XRPAmount{1});
                ammAlice.withdrawAll(ben, XRP(0));
                ammAlice.deposit(simon, XRPAmount(1000));
                ammAlice.withdrawAll(simon, XRP(0));
                ammAlice.deposit(chris, XRP(1));
                ammAlice.withdrawAll(chris, XRP(0));
                ammAlice.deposit(dan, XRP(10));
                ammAlice.withdrawAll(dan, XRP(0));
                ammAlice.deposit(bob, XRP(100));
                ammAlice.withdrawAll(bob, XRP(0));
                ammAlice.deposit(carol, XRP(1000));
                ammAlice.withdrawAll(carol, XRP(0));
                ammAlice.deposit(ed, XRP(10000));
                ammAlice.withdrawAll(ed, XRP(0));
                ammAlice.deposit(paul, XRP(100000));
                ammAlice.withdrawAll(paul, XRP(0));
                ammAlice.deposit(nataly, XRP(1000000));
                ammAlice.withdrawAll(nataly, XRP(0));
            }
            // No round off with XRP in this test
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10000), USD(10000), IOUAmount{10000000}));
            ammAlice.withdrawAll(alice);
            BEAST_EXPECT(!ammAlice.ammExists());
            // 20,000 initial - (deposit+withdraw) * 10
            auto const xrpBalance = (XRP(2000000) - txfee(env, 20)).getText();
            BEAST_EXPECT(accountBalance(env, ben) == xrpBalance);
            BEAST_EXPECT(accountBalance(env, simon) == xrpBalance);
            BEAST_EXPECT(accountBalance(env, chris) == xrpBalance);
            BEAST_EXPECT(accountBalance(env, dan) == xrpBalance);
            // 30,000 initial - (deposit+withdraw) * 10
            BEAST_EXPECT(accountBalance(env, carol) == "29999999800");
            BEAST_EXPECT(accountBalance(env, ed) == xrpBalance);
            BEAST_EXPECT(accountBalance(env, paul) == xrpBalance);
            BEAST_EXPECT(accountBalance(env, nataly) == xrpBalance);
            // 30,000 initial - 50 ammcreate fee - 10drops withdraw fee
            BEAST_EXPECT(accountBalance(env, alice) == "29949999990");
        });
    }

    void
    testCore()
    {
        testInvalidInstance();
        testInstanceCreate();
        testInvalidDeposit();
        testDeposit();
        testInvalidWithdraw();
        testWithdraw();
        testInvalidFeeVote();
        testFeeVote();
        testInvalidBid();
        testBid();
        testInvalidAMMPayment();
        testBasicPaymentEngine();
        testAMMTokens();
        testAmendment();
        testFlags();
        testRippling();
        testAMMAndCLOB();
        testTradingFee();
        testAdjustedTokens();
    }

    void
    run() override
    {
        testCore();
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(AMM, app, ripple, 1);

}  // namespace test
}  // namespace ripple