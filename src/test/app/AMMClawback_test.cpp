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
#include <test/jtx/trust.h>
#include <xrpld/ledger/ApplyViewImpl.h>
#include <xrpl/basics/random.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/jss.h>
#include <initializer_list>
namespace ripple {
namespace test {
class AMMClawback_test : public jtx::AMMTest
{
    void
    testInvalidRequest(FeatureBitset features)
    {
        testcase("test invalid request");
        using namespace jtx;

        // Test if holder does not exist.
        {
            Env env(*this, features);
            Account gw{"gateway"};
            Account alice{"alice"};
            env.fund(XRP(100000), gw, alice);
            env.close();

            // gw sets asfAllowTrustLineClawback.
            env(fset(gw, asfAllowTrustLineClawback));
            env.close();
            env.require(flags(gw, asfAllowTrustLineClawback));

            env.trust(USD(10000), alice);
            env(pay(gw, alice, gw["USD"](100)));

            AMM amm(env, alice, XRP(100), USD(100));
            env.close();

            env(amm::ammClawback(
                    gw, Account("unknown"), USD, XRP, std::nullopt),
                ter(terNO_ACCOUNT));
        }

        // Test if asset pair provided does not exist. This should
        // return terNO_AMM error.
        {
            Env env(*this, features);
            Account gw{"gateway"};
            Account alice{"alice"};
            env.fund(XRP(100000), gw, alice);
            env.close();

            // gw sets asfAllowTrustLineClawback.
            env(fset(gw, asfAllowTrustLineClawback));
            env.close();
            env.require(flags(gw, asfAllowTrustLineClawback));

            // gw issues 100 USD to Alice.
            auto const USD = gw["USD"];
            env.trust(USD(10000), alice);
            env(pay(gw, alice, USD(100)));
            env.close();

            // Withdraw all the tokens from the AMMAccount.
            // The AMMAccount will be auto deleted.
            AMM amm(env, gw, XRP(100), USD(100));
            amm.withdrawAll(gw);
            BEAST_EXPECT(!amm.ammExists());
            env.close();

            // The AMM account does not exist at all now.
            // It should return terNO_AMM error.
            env(amm::ammClawback(gw, alice, USD, EUR, std::nullopt),
                ter(terNO_AMM));
        }

        // Test if the issuer field and holder field is the same. This should
        // return temMALFORMED error.
        {
            Env env(*this, features);
            Account gw{"gateway"};
            Account alice{"alice"};
            env.fund(XRP(10000), gw, alice);
            env.close();

            // gw sets asfAllowTrustLineClawback.
            env(fset(gw, asfAllowTrustLineClawback));
            env.close();
            env.require(flags(gw, asfAllowTrustLineClawback));

            // gw issues 100 USD to Alice.
            auto const USD = gw["USD"];
            env.trust(USD(1000), alice);
            env(pay(gw, alice, USD(100)));
            env.close();

            AMM amm(env, gw, XRP(100), USD(100), ter(tesSUCCESS));

            // Issuer can not clawback from himself.
            env(amm::ammClawback(gw, gw, USD, XRP, std::nullopt),
                ter(temMALFORMED));

            // Holder can not clawback from himself.
            env(amm::ammClawback(alice, alice, USD, XRP, std::nullopt),
                ter(temMALFORMED));
        }

        // Test if the Asset field matches the Account field.
        {
            Env env(*this, features);
            Account gw{"gateway"};
            Account alice{"alice"};
            env.fund(XRP(10000), gw, alice);
            env.close();

            // gw sets asfAllowTrustLineClawback.
            env(fset(gw, asfAllowTrustLineClawback));
            env.close();
            env.require(flags(gw, asfAllowTrustLineClawback));

            // gw issues 100 USD to Alice.
            auto const USD = gw["USD"];
            env.trust(USD(1000), alice);
            env(pay(gw, alice, USD(100)));
            env.close();

            AMM amm(env, gw, XRP(100), USD(100), ter(tesSUCCESS));

            // The Asset's issuer field is alice, while the Account field is gw.
            // This should return temMALFORMED because they do not match.
            env(amm::ammClawback(
                    gw,
                    alice,
                    Issue{gw["USD"].currency, alice.id()},
                    XRP,
                    std::nullopt),
                ter(temMALFORMED));
        }

        // Test if the Amount field matches the Asset field.
        {
            Env env(*this, features);
            Account gw{"gateway"};
            Account alice{"alice"};
            env.fund(XRP(10000), gw, alice);
            env.close();

            // gw sets asfAllowTrustLineClawback.
            env(fset(gw, asfAllowTrustLineClawback));
            env.close();
            env.require(flags(gw, asfAllowTrustLineClawback));

            // gw issues 100 USD to Alice.
            auto const USD = gw["USD"];
            env.trust(USD(1000), alice);
            env(pay(gw, alice, USD(100)));
            env.close();

            AMM amm(env, gw, XRP(100), USD(100), ter(tesSUCCESS));

            // The Asset's issuer subfield is gw account and Amount's issuer
            // subfield is alice account. Return temBAD_AMOUNT because
            // they do not match.
            env(amm::ammClawback(
                    gw,
                    alice,
                    USD,
                    XRP,
                    STAmount{Issue{gw["USD"].currency, alice.id()}, 1}),
                ter(temBAD_AMOUNT));
        }

        // Test if the Amount is invalid, which is less than zero.
        {
            Env env(*this, features);
            Account gw{"gateway"};
            Account alice{"alice"};
            env.fund(XRP(10000), gw, alice);
            env.close();

            // gw sets asfAllowTrustLineClawback.
            env(fset(gw, asfAllowTrustLineClawback));
            env.close();
            env.require(flags(gw, asfAllowTrustLineClawback));

            // gw issues 100 USD to Alice.
            auto const USD = gw["USD"];
            env.trust(USD(1000), alice);
            env(pay(gw, alice, USD(100)));
            env.close();

            AMM amm(env, gw, XRP(100), USD(100), ter(tesSUCCESS));

            // Return temBAD_AMOUNT if the Amount value is less than 0.
            env(amm::ammClawback(
                    gw,
                    alice,
                    USD,
                    XRP,
                    STAmount{Issue{gw["USD"].currency, gw.id()}, -1}),
                ter(temBAD_AMOUNT));

            // Return temBAD_AMOUNT if the Amount value is 0.
            env(amm::ammClawback(
                    gw,
                    alice,
                    USD,
                    XRP,
                    STAmount{Issue{gw["USD"].currency, gw.id()}, 0}),
                ter(temBAD_AMOUNT));
        }

        // Test if the issuer did not set asfAllowTrustLineClawback, AMMClawback
        // transaction is prohibited.
        {
            Env env(*this, features);
            Account gw{"gateway"};
            Account alice{"alice"};
            env.fund(XRP(10000), gw, alice);
            env.close();

            // gw issues 100 USD to Alice.
            auto const USD = gw["USD"];
            env.trust(USD(1000), alice);
            env(pay(gw, alice, USD(100)));
            env.close();
            env.require(balance(alice, gw["USD"](100)));
            env.require(balance(gw, alice["USD"](-100)));

            // gw creates AMM pool of XRP/USD.
            AMM amm(env, gw, XRP(100), USD(100), ter(tesSUCCESS));

            // If asfAllowTrustLineClawback is not set, the issuer is not
            // allowed to send the AMMClawback transaction.
            env(amm::ammClawback(gw, alice, USD, XRP, std::nullopt),
                ter(tecNO_PERMISSION));
        }

        // Test invalid flag.
        {
            Env env(*this, features);
            Account gw{"gateway"};
            Account alice{"alice"};
            env.fund(XRP(10000), gw, alice);
            env.close();

            // gw sets asfAllowTrustLineClawback.
            env(fset(gw, asfAllowTrustLineClawback));
            env.close();
            env.require(flags(gw, asfAllowTrustLineClawback));

            // gw issues 100 USD to Alice.
            auto const USD = gw["USD"];
            env.trust(USD(1000), alice);
            env(pay(gw, alice, USD(100)));
            env.close();

            AMM amm(env, gw, XRP(100), USD(100), ter(tesSUCCESS));

            // Return temINVALID_FLAG when providing invalid flag.
            env(amm::ammClawback(gw, alice, USD, XRP, std::nullopt),
                txflags(tfTwoAssetIfEmpty),
                ter(temINVALID_FLAG));
        }

        // Test if tfClawTwoAssets is set when the two assets in the AMM pool
        // are not issued by the same issuer.
        {
            Env env(*this, features);
            Account gw{"gateway"};
            Account alice{"alice"};
            env.fund(XRP(10000), gw, alice);
            env.close();

            // gw sets asfAllowTrustLineClawback.
            env(fset(gw, asfAllowTrustLineClawback));
            env.close();
            env.require(flags(gw, asfAllowTrustLineClawback));

            // gw issues 100 USD to Alice.
            auto const USD = gw["USD"];
            env.trust(USD(1000), alice);
            env(pay(gw, alice, USD(100)));
            env.close();

            // gw creates AMM pool of XRP/USD.
            AMM amm(env, gw, XRP(100), USD(100), ter(tesSUCCESS));

            // Return temINVALID_FLAG because the issuer set tfClawTwoAssets,
            // but the issuer only issues USD in the pool. The issuer is not
            // allowed to set tfClawTwoAssets flag if he did not issue both
            // assets in the pool.
            env(amm::ammClawback(gw, alice, USD, XRP, std::nullopt),
                txflags(tfClawTwoAssets),
                ter(temINVALID_FLAG));
        }

        // Test clawing back XRP is being prohibited.
        {
            Env env(*this, features);
            Account gw{"gateway"};
            Account alice{"alice"};
            env.fund(XRP(1000000), gw, alice);
            env.close();

            // gw sets asfAllowTrustLineClawback.
            env(fset(gw, asfAllowTrustLineClawback));
            env.close();
            env.require(flags(gw, asfAllowTrustLineClawback));

            // gw issues 3000 USD to Alice.
            auto const USD = gw["USD"];
            env.trust(USD(100000), alice);
            env(pay(gw, alice, USD(3000)));
            env.close();

            // Alice creates AMM pool of XRP/USD.
            AMM amm(env, alice, XRP(1000), USD(2000), ter(tesSUCCESS));
            env.close();

            // Clawback XRP is prohibited.
            env(amm::ammClawback(gw, alice, XRP, USD, std::nullopt),
                ter(temMALFORMED));
        }
    }

    void
    testFeatureDisabled(FeatureBitset features)
    {
        testcase("test featureAMMClawback is not enabled.");
        using namespace jtx;
        if (!features[featureAMMClawback])
        {
            Env env(*this, features);
            Account gw{"gateway"};
            Account alice{"alice"};
            env.fund(XRP(1000000), gw, alice);
            env.close();

            // gw sets asfAllowTrustLineClawback.
            env(fset(gw, asfAllowTrustLineClawback));
            env.close();
            env.require(flags(gw, asfAllowTrustLineClawback));

            // gw issues 3000 USD to Alice.
            auto const USD = gw["USD"];
            env.trust(USD(100000), alice);
            env(pay(gw, alice, USD(3000)));
            env.close();

            // When featureAMMClawback is not enabled, AMMClawback is disabled.
            // Because when featureAMMClawback is disabled, we can not create
            // amm account, call amm::ammClawback directly for testing purpose.
            env(amm::ammClawback(gw, alice, USD, XRP, std::nullopt),
                ter(temDISABLED));
        }
    }

    void
    testAMMClawbackSpecificAmount(FeatureBitset features)
    {
        testcase("test AMMClawback specific amount");
        using namespace jtx;

        // Test AMMClawback for USD/EUR pool. The assets are issued by different
        // issuer. Claw back USD, and EUR goes back to the holder.
        {
            Env env(*this, features);
            Account gw{"gateway"};
            Account gw2{"gateway2"};
            Account alice{"alice"};
            env.fund(XRP(1000000), gw, gw2, alice);
            env.close();

            // gw sets asfAllowTrustLineClawback.
            env(fset(gw, asfAllowTrustLineClawback));
            env.close();
            env.require(flags(gw, asfAllowTrustLineClawback));

            // gw issues 3000 USD to Alice.
            auto const USD = gw["USD"];
            env.trust(USD(100000), alice);
            env(pay(gw, alice, USD(3000)));
            env.close();
            env.require(balance(gw, alice["USD"](-3000)));
            env.require(balance(alice, gw["USD"](3000)));

            // gw2 issues 3000 EUR to Alice.
            auto const EUR = gw2["EUR"];
            env.trust(EUR(100000), alice);
            env(pay(gw2, alice, EUR(3000)));
            env.close();
            env.require(balance(gw2, alice["EUR"](-3000)));
            env.require(balance(alice, gw2["EUR"](3000)));

            // Alice creates AMM pool of EUR/USD.
            AMM amm(env, alice, EUR(1000), USD(2000), ter(tesSUCCESS));
            env.close();

            BEAST_EXPECT(amm.expectBalances(
                USD(2000), EUR(1000), IOUAmount{1414213562373095, -12}));

            // gw clawback 1000 USD from the AMM pool.
            env(amm::ammClawback(gw, alice, USD, EUR, USD(1000)),
                ter(tesSUCCESS));
            env.close();

            // Alice's initial balance for USD is 3000 USD. Alice deposited 2000
            // USD into the pool, then she has 1000 USD. And 1000 USD was clawed
            // back from the AMM pool, so she still has 1000 USD.
            env.require(balance(gw, alice["USD"](-1000)));
            env.require(balance(alice, gw["USD"](1000)));

            // Alice's initial balance for EUR is 3000 EUR. Alice deposited 1000
            // EUR into the pool, 500 EUR was withdrawn proportionally. So she
            // has 2500 EUR now.
            env.require(balance(gw2, alice["EUR"](-2500)));
            env.require(balance(alice, gw2["EUR"](2500)));

            // 1000 USD and 500 EUR was withdrawn from the AMM pool, so the
            // current balance is 1000 USD and 500 EUR.
            BEAST_EXPECT(amm.expectBalances(
                USD(1000), EUR(500), IOUAmount{7071067811865475, -13}));

            // Alice has half of its initial lptokens Left.
            BEAST_EXPECT(
                amm.expectLPTokens(alice, IOUAmount{7071067811865475, -13}));

            // gw clawback another 1000 USD from the AMM pool. The AMM pool will
            // be empty and get deleted.
            env(amm::ammClawback(gw, alice, USD, EUR, USD(1000)),
                ter(tesSUCCESS));
            env.close();

            // Alice should still has 1000 USD because gw clawed back from the
            // AMM pool.
            env.require(balance(gw, alice["USD"](-1000)));
            env.require(balance(alice, gw["USD"](1000)));

            // Alice should has 3000 EUR now because another 500 EUR was
            // withdrawn.
            env.require(balance(gw2, alice["EUR"](-3000)));
            env.require(balance(alice, gw2["EUR"](3000)));

            // amm is automatically deleted.
            BEAST_EXPECT(!amm.ammExists());
        }

        // Test AMMClawback for USD/XRP pool. Claw back USD, and XRP goes back
        // to the holder.
        {
            Env env(*this, features);
            Account gw{"gateway"};
            Account alice{"alice"};
            env.fund(XRP(1000000), gw, alice);
            env.close();

            // gw sets asfAllowTrustLineClawback.
            env(fset(gw, asfAllowTrustLineClawback));
            env.close();
            env.require(flags(gw, asfAllowTrustLineClawback));

            // gw issues 3000 USD to Alice.
            auto const USD = gw["USD"];
            env.trust(USD(100000), alice);
            env(pay(gw, alice, USD(3000)));
            env.close();
            env.require(balance(gw, alice["USD"](-3000)));
            env.require(balance(alice, gw["USD"](3000)));

            // Alice creates AMM pool of XRP/USD.
            AMM amm(env, alice, XRP(1000), USD(2000), ter(tesSUCCESS));
            env.close();

            BEAST_EXPECT(amm.expectBalances(
                USD(2000), XRP(1000), IOUAmount{1414213562373095, -9}));

            auto aliceXrpBalance = env.balance(alice, XRP);

            // gw clawback 1000 USD from the AMM pool.
            env(amm::ammClawback(gw, alice, USD, XRP, USD(1000)),
                ter(tesSUCCESS));
            env.close();

            // Alice's initial balance for USD is 3000 USD. Alice deposited 2000
            // USD into the pool, then she has 1000 USD. And 1000 USD was clawed
            // back from the AMM pool, so she still has 1000 USD.
            env.require(balance(gw, alice["USD"](-1000)));
            env.require(balance(alice, gw["USD"](1000)));

            // Alice will get 500 XRP back.
            BEAST_EXPECT(
                expectLedgerEntryRoot(env, alice, aliceXrpBalance + XRP(500)));

            // 1000 USD and 500 XRP was withdrawn from the AMM pool, so the
            // current balance is 1000 USD and 500 XRP.
            BEAST_EXPECT(amm.expectBalances(
                USD(1000), XRP(500), IOUAmount{7071067811865475, -10}));

            // Alice has half of its initial lptokens Left.
            BEAST_EXPECT(
                amm.expectLPTokens(alice, IOUAmount{7071067811865475, -10}));

            // gw clawback another 1000 USD from the AMM pool. The AMM pool will
            // be empty and get deleted.
            env(amm::ammClawback(gw, alice, USD, XRP, USD(1000)),
                ter(tesSUCCESS));
            env.close();

            // Alice should still has 1000 USD because gw clawed back from the
            // AMM pool.
            env.require(balance(gw, alice["USD"](-1000)));
            env.require(balance(alice, gw["USD"](1000)));

            // Alice will get another 1000 XRP back.
            BEAST_EXPECT(
                expectLedgerEntryRoot(env, alice, aliceXrpBalance + XRP(1000)));

            // amm is automatically deleted.
            BEAST_EXPECT(!amm.ammExists());
        }
    }

    void
    testAMMClawbackExceedBalance(FeatureBitset features)
    {
        testcase(
            "test AMMClawback specific amount which exceeds the current "
            "balance");
        using namespace jtx;

        // Test AMMClawback for USD/EUR pool. The assets are issued by different
        // issuer. Claw back USD for multiple times, and EUR goes back to the
        // holder. The last AMMClawback transaction exceeds the holder's USD
        // balance in AMM pool.
        {
            Env env(*this, features);
            Account gw{"gateway"};
            Account gw2{"gateway2"};
            Account alice{"alice"};
            env.fund(XRP(1000000), gw, gw2, alice);
            env.close();

            // gw sets asfAllowTrustLineClawback.
            env(fset(gw, asfAllowTrustLineClawback));
            env.close();
            env.require(flags(gw, asfAllowTrustLineClawback));

            // gw issues 6000 USD to Alice.
            auto const USD = gw["USD"];
            env.trust(USD(100000), alice);
            env(pay(gw, alice, USD(6000)));
            env.close();
            env.require(balance(alice, gw["USD"](6000)));

            // gw2 issues 6000 EUR to Alice.
            auto const EUR = gw2["EUR"];
            env.trust(EUR(100000), alice);
            env(pay(gw2, alice, EUR(6000)));
            env.close();
            env.require(balance(alice, gw2["EUR"](6000)));

            // Alice creates AMM pool of EUR/USD
            AMM amm(env, alice, EUR(5000), USD(4000), ter(tesSUCCESS));
            env.close();

            BEAST_EXPECT(amm.expectBalances(
                USD(4000), EUR(5000), IOUAmount{4472135954999580, -12}));

            // gw clawback 1000 USD from the AMM pool
            env(amm::ammClawback(gw, alice, USD, EUR, USD(1000)),
                ter(tesSUCCESS));
            env.close();

            // Alice's initial balance for USD is 6000 USD. Alice deposited 4000
            // USD into the pool, then she has 2000 USD. And 1000 USD was clawed
            // back from the AMM pool, so she still has 2000 USD.
            env.require(balance(alice, gw["USD"](2000)));

            // Alice's initial balance for EUR is 6000 EUR. Alice deposited 5000
            // EUR into the pool, 1250 EUR was withdrawn proportionally. So she
            // has 2500 EUR now.
            env.require(balance(alice, gw2["EUR"](2250)));

            // 1000 USD and 1250 EUR was withdrawn from the AMM pool, so the
            // current balance is 3000 USD and 3750 EUR.
            BEAST_EXPECT(amm.expectBalances(
                USD(3000), EUR(3750), IOUAmount{3354101966249685, -12}));

            // Alice has 3/4 of its initial lptokens Left.
            BEAST_EXPECT(
                amm.expectLPTokens(alice, IOUAmount{3354101966249685, -12}));

            // gw clawback another 500 USD from the AMM pool.
            env(amm::ammClawback(gw, alice, USD, EUR, USD(500)),
                ter(tesSUCCESS));
            env.close();

            // Alice should still has 2000 USD because gw clawed back from the
            // AMM pool.
            env.require(balance(alice, gw["USD"](2000)));

            BEAST_EXPECT(amm.expectBalances(
                STAmount{USD, UINT64_C(2500000000000001), -12},
                STAmount{EUR, UINT64_C(3125000000000001), -12},
                IOUAmount{2795084971874738, -12}));

            BEAST_EXPECT(
                env.balance(alice, EUR) ==
                STAmount(EUR, UINT64_C(2874999999999999), -12));

            // gw clawback small amount, 1 USD.
            env(amm::ammClawback(gw, alice, USD, EUR, USD(1)), ter(tesSUCCESS));
            env.close();

            // Another 1 USD / 1.25 EUR was withdrawn.
            env.require(balance(alice, gw["USD"](2000)));

            BEAST_EXPECT(amm.expectBalances(
                STAmount{USD, UINT64_C(2499000000000002), -12},
                STAmount{EUR, UINT64_C(3123750000000002), -12},
                IOUAmount{2793966937885989, -12}));

            BEAST_EXPECT(
                env.balance(alice, EUR) ==
                STAmount(EUR, UINT64_C(2876249999999998), -12));

            // gw clawback 4000 USD, exceeding the current balance. We
            // will clawback all.
            env(amm::ammClawback(gw, alice, USD, EUR, USD(4000)),
                ter(tesSUCCESS));
            env.close();

            env.require(balance(alice, gw["USD"](2000)));

            // All alice's EUR in the pool goes back to alice.
            BEAST_EXPECT(
                env.balance(alice, EUR) ==
                STAmount(EUR, UINT64_C(6000000000000000), -12));

            // amm is automatically deleted.
            BEAST_EXPECT(!amm.ammExists());
        }

        // Test AMMClawback for USD/XRP pool. Claw back USD for multiple times,
        // and XRP goes back to the holder. The last AMMClawback transaction
        // exceeds the holder's USD balance in AMM pool. In this case, gw
        // creates the AMM pool USD/XRP, both alice and bob deposit into it. gw2
        // creates the AMM pool EUR/XRP.
        {
            Env env(*this, features);
            Account gw{"gateway"};
            Account gw2{"gateway2"};
            Account alice{"alice"};
            Account bob{"bob"};
            env.fund(XRP(1000000), gw, gw2, alice, bob);
            env.close();

            // gw sets asfAllowTrustLineClawback.
            env(fset(gw, asfAllowTrustLineClawback));
            env.close();
            env.require(flags(gw, asfAllowTrustLineClawback));

            // gw2 sets asfAllowTrustLineClawback.
            env(fset(gw2, asfAllowTrustLineClawback));
            env.close();
            env.require(flags(gw2, asfAllowTrustLineClawback));

            // gw issues 6000 USD to Alice and 5000 USD to Bob.
            auto const USD = gw["USD"];
            env.trust(USD(100000), alice);
            env(pay(gw, alice, USD(6000)));
            env.trust(USD(100000), bob);
            env(pay(gw, bob, USD(5000)));
            env.close();

            // gw2 issues 5000 EUR to Alice and 4000 EUR to Bob.
            auto const EUR = gw2["EUR"];
            env.trust(EUR(100000), alice);
            env(pay(gw2, alice, EUR(5000)));
            env.trust(EUR(100000), bob);
            env(pay(gw2, bob, EUR(4000)));
            env.close();

            // gw creates AMM pool of XRP/USD, alice and bob deposit XRP/USD.
            AMM amm(env, gw, XRP(2000), USD(1000), ter(tesSUCCESS));
            BEAST_EXPECT(amm.expectBalances(
                USD(1000), XRP(2000), IOUAmount{1414213562373095, -9}));
            amm.deposit(alice, USD(1000), XRP(2000));
            BEAST_EXPECT(amm.expectBalances(
                USD(2000), XRP(4000), IOUAmount{2828427124746190, -9}));
            amm.deposit(bob, USD(1000), XRP(2000));
            BEAST_EXPECT(amm.expectBalances(
                USD(3000), XRP(6000), IOUAmount{4242640687119285, -9}));
            env.close();

            // gw2 creates AMM pool of XRP/EUR, alice and bob deposit XRP/EUR.
            AMM amm2(env, gw2, XRP(3000), EUR(1000), ter(tesSUCCESS));
            BEAST_EXPECT(amm2.expectBalances(
                EUR(1000), XRP(3000), IOUAmount{1732050807568878, -9}));
            amm2.deposit(alice, EUR(1000), XRP(3000));
            BEAST_EXPECT(amm2.expectBalances(
                EUR(2000), XRP(6000), IOUAmount{3464101615137756, -9}));
            amm2.deposit(bob, EUR(1000), XRP(3000));
            BEAST_EXPECT(amm2.expectBalances(
                EUR(3000), XRP(9000), IOUAmount{5196152422706634, -9}));
            env.close();

            auto aliceXrpBalance = env.balance(alice, XRP);
            auto bobXrpBalance = env.balance(bob, XRP);

            // gw clawback 500 USD from alice in amm
            env(amm::ammClawback(gw, alice, USD, XRP, USD(500)),
                ter(tesSUCCESS));
            env.close();

            // Alice's initial balance for USD is 6000 USD. Alice deposited 1000
            // USD into the pool, then she has 5000 USD. And 500 USD was clawed
            // back from the AMM pool, so she still has 5000 USD.
            env.require(balance(alice, gw["USD"](5000)));

            // Bob's balance is not changed.
            env.require(balance(bob, gw["USD"](4000)));

            // Alice gets 1000 XRP back.
            BEAST_EXPECT(
                expectLedgerEntryRoot(env, alice, aliceXrpBalance + XRP(1000)));

            BEAST_EXPECT(amm.expectBalances(
                USD(2500), XRP(5000), IOUAmount{3535533905932738, -9}));
            BEAST_EXPECT(
                amm.expectLPTokens(alice, IOUAmount{7071067811865480, -10}));
            BEAST_EXPECT(
                amm.expectLPTokens(bob, IOUAmount{1414213562373095, -9}));

            // gw clawback 10 USD from bob in amm.
            env(amm::ammClawback(gw, bob, USD, XRP, USD(10)), ter(tesSUCCESS));
            env.close();

            env.require(balance(alice, gw["USD"](5000)));
            env.require(balance(bob, gw["USD"](4000)));

            // Bob gets 20 XRP back.
            BEAST_EXPECT(
                expectLedgerEntryRoot(env, bob, bobXrpBalance + XRP(20)));
            BEAST_EXPECT(amm.expectBalances(
                STAmount{USD, UINT64_C(2490000000000001), -12},
                XRP(4980),
                IOUAmount{3521391770309008, -9}));
            BEAST_EXPECT(
                amm.expectLPTokens(alice, IOUAmount{7071067811865480, -10}));
            BEAST_EXPECT(
                amm.expectLPTokens(bob, IOUAmount{1400071426749365, -9}));

            // gw2 clawback 200 EUR from amm2.
            env(amm::ammClawback(gw2, alice, EUR, XRP, EUR(200)),
                ter(tesSUCCESS));
            env.close();

            env.require(balance(alice, gw2["EUR"](4000)));
            env.require(balance(bob, gw2["EUR"](3000)));

            // Alice gets 600 XRP back.
            BEAST_EXPECT(expectLedgerEntryRoot(
                env, alice, aliceXrpBalance + XRP(1000) + XRP(600)));
            BEAST_EXPECT(amm2.expectBalances(
                EUR(2800), XRP(8400), IOUAmount{4849742261192859, -9}));
            BEAST_EXPECT(
                amm2.expectLPTokens(alice, IOUAmount{1385640646055103, -9}));
            BEAST_EXPECT(
                amm2.expectLPTokens(bob, IOUAmount{1732050807568878, -9}));

            // gw claw back 1000 USD from alice in amm, which exceeds alice's
            // balance. This will clawback all the remaining LP tokens of alice
            // (corresponding 500 USD / 1000 XRP).
            env(amm::ammClawback(gw, alice, USD, XRP, USD(1000)),
                ter(tesSUCCESS));
            env.close();

            env.require(balance(alice, gw["USD"](5000)));
            env.require(balance(bob, gw["USD"](4000)));

            // Alice gets 1000 XRP back.
            BEAST_EXPECT(expectLedgerEntryRoot(
                env,
                alice,
                aliceXrpBalance + XRP(1000) + XRP(600) + XRP(1000)));
            BEAST_EXPECT(amm.expectLPTokens(alice, IOUAmount(0)));
            BEAST_EXPECT(
                amm.expectLPTokens(bob, IOUAmount{1400071426749365, -9}));
            BEAST_EXPECT(amm.expectBalances(
                STAmount{USD, UINT64_C(1990000000000001), -12},
                XRP(3980),
                IOUAmount{2814284989122460, -9}));

            // gw clawback 1000 USD from bob in amm, which also exceeds bob's
            // balance in amm. All bob's lptoken in amm will be consumed, which
            // corresponds to 990 USD / 1980 XRP
            env(amm::ammClawback(gw, bob, USD, XRP, USD(1000)),
                ter(tesSUCCESS));
            env.close();

            env.require(balance(alice, gw["USD"](5000)));
            env.require(balance(bob, gw["USD"](4000)));

            BEAST_EXPECT(expectLedgerEntryRoot(
                env,
                alice,
                aliceXrpBalance + XRP(1000) + XRP(600) + XRP(1000)));
            BEAST_EXPECT(expectLedgerEntryRoot(
                env, bob, bobXrpBalance + XRP(20) + XRP(1980)));

            // Now neither alice nor bob has any lptoken in amm.
            BEAST_EXPECT(amm.expectLPTokens(alice, IOUAmount(0)));
            BEAST_EXPECT(amm.expectLPTokens(bob, IOUAmount(0)));

            // gw2 claw back 1000 EUR from alice in amm2, which exceeds alice's
            // balance. All alice's lptokens will be consumed, which corresponds
            // to 800EUR / 2400 XRP.
            env(amm::ammClawback(gw2, alice, EUR, XRP, EUR(1000)),
                ter(tesSUCCESS));
            env.close();

            env.require(balance(alice, gw2["EUR"](4000)));
            env.require(balance(bob, gw2["EUR"](3000)));

            // Alice gets another 2400 XRP back, bob's XRP balance remains the
            // same.
            BEAST_EXPECT(expectLedgerEntryRoot(
                env,
                alice,
                aliceXrpBalance + XRP(1000) + XRP(600) + XRP(1000) +
                    XRP(2400)));
            BEAST_EXPECT(expectLedgerEntryRoot(
                env, bob, bobXrpBalance + XRP(20) + XRP(1980)));

            // Alice now does not have any lptoken in amm2
            BEAST_EXPECT(amm2.expectLPTokens(alice, IOUAmount(0)));

            BEAST_EXPECT(amm2.expectBalances(
                EUR(2000), XRP(6000), IOUAmount{3464101615137756, -9}));

            // gw2 claw back 2000 EUR from bib in amm2, which exceeds bob's
            // balance. All bob's lptokens will be consumed, which corresponds
            // to 1000EUR / 3000 XRP.
            env(amm::ammClawback(gw2, bob, EUR, XRP, EUR(2000)),
                ter(tesSUCCESS));
            env.close();

            env.require(balance(alice, gw2["EUR"](4000)));
            env.require(balance(bob, gw2["EUR"](3000)));

            // Bob gets another 3000 XRP back. Alice's XRP balance remains the
            // same.
            BEAST_EXPECT(expectLedgerEntryRoot(
                env,
                alice,
                aliceXrpBalance + XRP(1000) + XRP(600) + XRP(1000) +
                    XRP(2400)));
            BEAST_EXPECT(expectLedgerEntryRoot(
                env, bob, bobXrpBalance + XRP(20) + XRP(1980) + XRP(3000)));

            // Neither alice nor bob has any lptoken in amm2
            BEAST_EXPECT(amm2.expectLPTokens(alice, IOUAmount(0)));
            BEAST_EXPECT(amm2.expectLPTokens(bob, IOUAmount(0)));

            BEAST_EXPECT(amm2.expectBalances(
                EUR(1000), XRP(3000), IOUAmount{1732050807568878, -9}));
        }
    }

    void
    testAMMClawbackAll(FeatureBitset features)
    {
        testcase("test AMMClawback all the tokens in the AMM pool");
        using namespace jtx;

        // Test AMMClawback for USD/EUR pool. The assets are issued by different
        // issuer. Claw back all the USD for different users.
        {
            Env env(*this, features);
            Account gw{"gateway"};
            Account gw2{"gateway2"};
            Account alice{"alice"};
            Account bob{"bob"};
            Account carol{"carol"};
            env.fund(XRP(1000000), gw, gw2, alice, bob, carol);
            env.close();

            // gw sets asfAllowTrustLineClawback.
            env(fset(gw, asfAllowTrustLineClawback));
            env.close();
            env.require(flags(gw, asfAllowTrustLineClawback));

            // gw2 sets asfAllowTrustLineClawback.
            env(fset(gw2, asfAllowTrustLineClawback));
            env.close();
            env.require(flags(gw2, asfAllowTrustLineClawback));

            // gw issues 6000 USD to Alice, 5000 USD to Bob, and 4000 USD
            // to Carol.
            auto const USD = gw["USD"];
            env.trust(USD(100000), alice);
            env(pay(gw, alice, USD(6000)));
            env.trust(USD(100000), bob);
            env(pay(gw, bob, USD(5000)));
            env.trust(USD(100000), carol);
            env(pay(gw, carol, USD(4000)));
            env.close();

            // gw2 issues 6000 EUR to Alice and 5000 EUR to Bob and 4000
            // EUR to Carol.
            auto const EUR = gw2["EUR"];
            env.trust(EUR(100000), alice);
            env(pay(gw2, alice, EUR(6000)));
            env.trust(EUR(100000), bob);
            env(pay(gw2, bob, EUR(5000)));
            env.trust(EUR(100000), carol);
            env(pay(gw2, carol, EUR(4000)));
            env.close();

            // Alice creates AMM pool of EUR/USD
            AMM amm(env, alice, EUR(5000), USD(4000), ter(tesSUCCESS));
            env.close();

            BEAST_EXPECT(amm.expectBalances(
                USD(4000), EUR(5000), IOUAmount{4472135954999580, -12}));
            amm.deposit(bob, USD(2000), EUR(2500));
            BEAST_EXPECT(amm.expectBalances(
                USD(6000), EUR(7500), IOUAmount{6708203932499370, -12}));
            amm.deposit(carol, USD(1000), EUR(1250));
            BEAST_EXPECT(amm.expectBalances(
                USD(7000), EUR(8750), IOUAmount{7826237921249265, -12}));

            BEAST_EXPECT(
                amm.expectLPTokens(alice, IOUAmount{4472135954999580, -12}));
            BEAST_EXPECT(
                amm.expectLPTokens(bob, IOUAmount{2236067977499790, -12}));
            BEAST_EXPECT(
                amm.expectLPTokens(carol, IOUAmount{1118033988749895, -12}));

            env.require(balance(alice, gw["USD"](2000)));
            env.require(balance(alice, gw2["EUR"](1000)));
            env.require(balance(bob, gw["USD"](3000)));
            env.require(balance(bob, gw2["EUR"](2500)));
            env.require(balance(carol, gw["USD"](3000)));
            env.require(balance(carol, gw2["EUR"](2750)));

            // gw clawback all the bob's USD in amm. (2000 USD / 2500 EUR)
            env(amm::ammClawback(gw, bob, USD, EUR, std::nullopt),
                ter(tesSUCCESS));
            env.close();

            BEAST_EXPECT(amm.expectBalances(
                STAmount{USD, UINT64_C(4999999999999999), -12},
                STAmount{EUR, UINT64_C(6249999999999999), -12},
                IOUAmount{5590169943749475, -12}));

            BEAST_EXPECT(
                amm.expectLPTokens(alice, IOUAmount{4472135954999580, -12}));
            BEAST_EXPECT(amm.expectLPTokens(bob, IOUAmount(0)));
            BEAST_EXPECT(
                amm.expectLPTokens(carol, IOUAmount{1118033988749895, -12}));

            // Bob will get 2500 EUR back.
            env.require(balance(alice, gw["USD"](2000)));
            env.require(balance(alice, gw2["EUR"](1000)));
            BEAST_EXPECT(
                env.balance(bob, USD) ==
                STAmount(USD, UINT64_C(3000000000000000), -12));

            BEAST_EXPECT(
                env.balance(bob, EUR) ==
                STAmount(EUR, UINT64_C(5000000000000001), -12));
            env.require(balance(carol, gw["USD"](3000)));
            env.require(balance(carol, gw2["EUR"](2750)));

            // gw2 clawback all carol's EUR in amm. (1000 USD / 1250 EUR)
            env(amm::ammClawback(gw2, carol, EUR, USD, std::nullopt),
                ter(tesSUCCESS));
            env.close();
            BEAST_EXPECT(amm.expectBalances(
                STAmount{USD, UINT64_C(3999999999999999), -12},
                STAmount{EUR, UINT64_C(4999999999999999), -12},
                IOUAmount{4472135954999580, -12}));

            BEAST_EXPECT(
                amm.expectLPTokens(alice, IOUAmount{4472135954999580, -12}));
            BEAST_EXPECT(amm.expectLPTokens(bob, IOUAmount(0)));
            BEAST_EXPECT(amm.expectLPTokens(carol, IOUAmount(0)));

            // gw2 clawback all alice's EUR in amm. (4000 USD / 5000 EUR)
            env(amm::ammClawback(gw2, alice, EUR, USD, std::nullopt),
                ter(tesSUCCESS));
            env.close();

            env.require(balance(carol, gw2["EUR"](2750)));
            env.require(balance(carol, gw["USD"](4000)));
            BEAST_EXPECT(!amm.ammExists());
        }

        // Test AMMClawback for USD/XRP pool. Claw back all the USD for
        // different users.
        {
            Env env(*this, features);
            Account gw{"gateway"};
            Account alice{"alice"};
            Account bob{"bob"};
            env.fund(XRP(1000000), gw, alice, bob);
            env.close();

            // gw sets asfAllowTrustLineClawback
            env(fset(gw, asfAllowTrustLineClawback));
            env.close();
            env.require(flags(gw, asfAllowTrustLineClawback));

            // gw issues 600000 USD to Alice and 500000 USD to Bob.
            auto const USD = gw["USD"];
            env.trust(USD(1000000), alice);
            env(pay(gw, alice, USD(600000)));
            env.trust(USD(1000000), bob);
            env(pay(gw, bob, USD(500000)));
            env.close();

            // gw creates AMM pool of XRP/USD, alice and bob deposit XRP/USD.
            AMM amm(env, gw, XRP(2000), USD(10000), ter(tesSUCCESS));
            BEAST_EXPECT(amm.expectBalances(
                USD(10000), XRP(2000), IOUAmount{4472135954999580, -9}));
            amm.deposit(alice, USD(1000), XRP(200));
            BEAST_EXPECT(amm.expectBalances(
                USD(11000), XRP(2200), IOUAmount{4919349550499538, -9}));
            amm.deposit(bob, USD(2000), XRP(400));
            BEAST_EXPECT(amm.expectBalances(
                USD(13000), XRP(2600), IOUAmount{5813776741499453, -9}));
            env.close();

            auto aliceXrpBalance = env.balance(alice, XRP);
            auto bobXrpBalance = env.balance(bob, XRP);

            // gw clawback all alice's USD in amm. (1000 USD / 200 XRP)
            env(amm::ammClawback(gw, alice, USD, XRP, std::nullopt),
                ter(tesSUCCESS));
            env.close();
            BEAST_EXPECT(amm.expectBalances(
                USD(12000), XRP(2400), IOUAmount{5366563145999495, -9}));
            BEAST_EXPECT(
                expectLedgerEntryRoot(env, alice, aliceXrpBalance + XRP(200)));
            BEAST_EXPECT(amm.expectLPTokens(alice, IOUAmount(0)));

            // gw clawback all bob's USD in amm. (2000 USD / 400 XRP)
            env(amm::ammClawback(gw, bob, USD, XRP, std::nullopt),
                ter(tesSUCCESS));
            env.close();
            BEAST_EXPECT(amm.expectBalances(
                USD(10000), XRP(2000), IOUAmount{4472135954999580, -9}));
            BEAST_EXPECT(
                expectLedgerEntryRoot(env, bob, bobXrpBalance + XRP(400)));
            BEAST_EXPECT(amm.expectLPTokens(alice, IOUAmount(0)));
            BEAST_EXPECT(amm.expectLPTokens(bob, IOUAmount(0)));
        }
    }

    void
    testAMMClawbackSameIssuerAssets(FeatureBitset features)
    {
        testcase(
            "test AMMClawback from AMM pool with assets having the same "
            "issuer");
        using namespace jtx;

        // Test AMMClawback for USD/EUR pool. The assets are issued by different
        // issuer. Claw back all the USD for different users.
        Env env(*this, features);
        Account gw{"gateway"};
        Account alice{"alice"};
        Account bob{"bob"};
        Account carol{"carol"};
        env.fund(XRP(1000000), gw, alice, bob, carol);
        env.close();

        // gw sets asfAllowTrustLineClawback.
        env(fset(gw, asfAllowTrustLineClawback));
        env.close();
        env.require(flags(gw, asfAllowTrustLineClawback));

        auto const USD = gw["USD"];
        env.trust(USD(100000), alice);
        env(pay(gw, alice, USD(10000)));
        env.trust(USD(100000), bob);
        env(pay(gw, bob, USD(9000)));
        env.trust(USD(100000), carol);
        env(pay(gw, carol, USD(8000)));
        env.close();

        auto const EUR = gw["EUR"];
        env.trust(EUR(100000), alice);
        env(pay(gw, alice, EUR(10000)));
        env.trust(EUR(100000), bob);
        env(pay(gw, bob, EUR(9000)));
        env.trust(EUR(100000), carol);
        env(pay(gw, carol, EUR(8000)));
        env.close();

        AMM amm(env, alice, EUR(2000), USD(8000), ter(tesSUCCESS));
        env.close();

        BEAST_EXPECT(amm.expectBalances(USD(8000), EUR(2000), IOUAmount(4000)));
        amm.deposit(bob, USD(4000), EUR(1000));
        BEAST_EXPECT(
            amm.expectBalances(USD(12000), EUR(3000), IOUAmount(6000)));
        amm.deposit(carol, USD(2000), EUR(500));
        BEAST_EXPECT(
            amm.expectBalances(USD(14000), EUR(3500), IOUAmount(7000)));

        // gw clawback 1000 USD from carol.
        env(amm::ammClawback(gw, carol, USD, EUR, USD(1000)), ter(tesSUCCESS));
        env.close();
        BEAST_EXPECT(
            amm.expectBalances(USD(13000), EUR(3250), IOUAmount(6500)));

        BEAST_EXPECT(amm.expectLPTokens(alice, IOUAmount(4000)));
        BEAST_EXPECT(amm.expectLPTokens(bob, IOUAmount(2000)));
        BEAST_EXPECT(amm.expectLPTokens(carol, IOUAmount(500)));
        BEAST_EXPECT(env.balance(alice, USD) == USD(2000));
        BEAST_EXPECT(env.balance(alice, EUR) == EUR(8000));
        BEAST_EXPECT(env.balance(bob, USD) == USD(5000));
        BEAST_EXPECT(env.balance(bob, EUR) == EUR(8000));
        BEAST_EXPECT(env.balance(carol, USD) == USD(6000));
        // 250 EUR goes back to carol.
        BEAST_EXPECT(env.balance(carol, EUR) == EUR(7750));

        // gw clawback 1000 USD from bob with tfClawTwoAssets flag.
        // then the corresponding EUR will also be clawed back
        // by gw.
        env(amm::ammClawback(gw, bob, USD, EUR, USD(1000)),
            txflags(tfClawTwoAssets),
            ter(tesSUCCESS));
        env.close();
        BEAST_EXPECT(
            amm.expectBalances(USD(12000), EUR(3000), IOUAmount(6000)));

        BEAST_EXPECT(amm.expectLPTokens(alice, IOUAmount(4000)));
        BEAST_EXPECT(amm.expectLPTokens(bob, IOUAmount(1500)));
        BEAST_EXPECT(amm.expectLPTokens(carol, IOUAmount(500)));
        BEAST_EXPECT(env.balance(alice, USD) == USD(2000));
        BEAST_EXPECT(env.balance(alice, EUR) == EUR(8000));
        BEAST_EXPECT(env.balance(bob, USD) == USD(5000));
        // 250 EUR did not go back to bob because tfClawTwoAssets is set.
        BEAST_EXPECT(env.balance(bob, EUR) == EUR(8000));
        BEAST_EXPECT(env.balance(carol, USD) == USD(6000));
        BEAST_EXPECT(env.balance(carol, EUR) == EUR(7750));

        // gw clawback all USD from alice and set tfClawTwoAssets.
        env(amm::ammClawback(gw, alice, USD, EUR, std::nullopt),
            txflags(tfClawTwoAssets),
            ter(tesSUCCESS));
        env.close();
        BEAST_EXPECT(amm.expectBalances(USD(4000), EUR(1000), IOUAmount(2000)));

        BEAST_EXPECT(amm.expectLPTokens(alice, IOUAmount(0)));
        BEAST_EXPECT(amm.expectLPTokens(bob, IOUAmount(1500)));
        BEAST_EXPECT(amm.expectLPTokens(carol, IOUAmount(500)));
        BEAST_EXPECT(env.balance(alice, USD) == USD(2000));
        BEAST_EXPECT(env.balance(alice, EUR) == EUR(8000));
        BEAST_EXPECT(env.balance(bob, USD) == USD(5000));
        BEAST_EXPECT(env.balance(bob, EUR) == EUR(8000));
        BEAST_EXPECT(env.balance(carol, USD) == USD(6000));
        BEAST_EXPECT(env.balance(carol, EUR) == EUR(7750));
    }

    void
    testAMMClawbackSameCurrency(FeatureBitset features)
    {
        testcase(
            "test AMMClawback from AMM pool with assets having the same "
            "currency, but from different issuer");
        using namespace jtx;

        // Test AMMClawback for USD/EUR pool. The assets are issued by different
        // issuer. Claw back all the USD for different users.
        Env env(*this, features);
        Account gw{"gateway"};
        Account gw2{"gateway2"};
        Account alice{"alice"};
        Account bob{"bob"};
        env.fund(XRP(1000000), gw, gw2, alice, bob);
        env.close();

        // gw sets asfAllowTrustLineClawback.
        env(fset(gw, asfAllowTrustLineClawback));
        env.close();
        env.require(flags(gw, asfAllowTrustLineClawback));

        // gw2 sets asfAllowTrustLineClawback.
        env(fset(gw2, asfAllowTrustLineClawback));
        env.close();
        env.require(flags(gw2, asfAllowTrustLineClawback));

        env.trust(gw["USD"](100000), alice);
        env(pay(gw, alice, gw["USD"](8000)));
        env.trust(gw["USD"](100000), bob);
        env(pay(gw, bob, gw["USD"](7000)));

        env.trust(gw2["USD"](100000), alice);
        env(pay(gw2, alice, gw2["USD"](6000)));
        env.trust(gw2["USD"](100000), bob);
        env(pay(gw2, bob, gw2["USD"](5000)));
        env.close();

        AMM amm(env, alice, gw["USD"](1000), gw2["USD"](1500), ter(tesSUCCESS));
        env.close();

        BEAST_EXPECT(amm.expectBalances(
            gw["USD"](1000),
            gw2["USD"](1500),
            IOUAmount{1224744871391589, -12}));
        amm.deposit(bob, gw["USD"](2000), gw2["USD"](3000));
        BEAST_EXPECT(amm.expectBalances(
            gw["USD"](3000),
            gw2["USD"](4500),
            IOUAmount{3674234614174767, -12}));

        // Issuer does not match with asset.
        env(amm::ammClawback(
                gw,
                alice,
                gw2["USD"],
                gw["USD"],
                STAmount{Issue{gw2["USD"].currency, gw2.id()}, 500}),
            ter(temMALFORMED));

        // gw2 clawback 500 gw2[USD] from alice.
        env(amm::ammClawback(
                gw2,
                alice,
                gw2["USD"],
                gw["USD"],
                STAmount{Issue{gw2["USD"].currency, gw2.id()}, 500}),
            ter(tesSUCCESS));
        env.close();
        BEAST_EXPECT(amm.expectBalances(
            STAmount{gw["USD"], UINT64_C(2666666666666667), -12},
            gw2["USD"](4000),
            IOUAmount{3265986323710904, -12}));

        BEAST_EXPECT(
            amm.expectLPTokens(alice, IOUAmount{8164965809277260, -13}));
        BEAST_EXPECT(amm.expectLPTokens(bob, IOUAmount{2449489742783178, -12}));
        BEAST_EXPECT(
            env.balance(alice, gw["USD"]) ==
            STAmount(gw["USD"], UINT64_C(7333333333333333), -12));
        BEAST_EXPECT(env.balance(alice, gw2["USD"]) == gw2["USD"](4500));
        BEAST_EXPECT(env.balance(bob, gw["USD"]) == gw["USD"](5000));
        BEAST_EXPECT(env.balance(bob, gw2["USD"]) == gw2["USD"](2000));

        // gw clawback all gw["USD"] from bob.
        env(amm::ammClawback(gw, bob, gw["USD"], gw2["USD"], std::nullopt),
            ter(tesSUCCESS));
        env.close();
        BEAST_EXPECT(amm.expectBalances(
            STAmount{gw["USD"], UINT64_C(6666666666666670), -13},
            gw2["USD"](1000),
            IOUAmount{8164965809277260, -13}));

        BEAST_EXPECT(
            amm.expectLPTokens(alice, IOUAmount{8164965809277260, -13}));
        BEAST_EXPECT(amm.expectLPTokens(bob, IOUAmount(0)));
        BEAST_EXPECT(
            env.balance(alice, gw["USD"]) ==
            STAmount(gw["USD"], UINT64_C(7333333333333333), -12));
        BEAST_EXPECT(env.balance(alice, gw2["USD"]) == gw2["USD"](4500));
        BEAST_EXPECT(env.balance(bob, gw["USD"]) == gw["USD"](5000));
        // Bob gets 3000 gw2["USD"] back and now his balance is 5000.
        BEAST_EXPECT(env.balance(bob, gw2["USD"]) == gw2["USD"](5000));
    }

    void
    testAMMClawbackIssuesEachOther(FeatureBitset features)
    {
        testcase("test AMMClawback when issuing token for each other");
        using namespace jtx;

        // gw and gw2 issues token for each other. Test AMMClawback from
        // each other.
        Env env(*this, features);
        Account gw{"gateway"};
        Account gw2{"gateway2"};
        Account alice{"alice"};
        env.fund(XRP(1000000), gw, gw2, alice);
        env.close();

        // gw sets asfAllowTrustLineClawback.
        env(fset(gw, asfAllowTrustLineClawback));
        env.close();
        env.require(flags(gw, asfAllowTrustLineClawback));

        // gw2 sets asfAllowTrustLineClawback.
        env(fset(gw2, asfAllowTrustLineClawback));
        env.close();
        env.require(flags(gw2, asfAllowTrustLineClawback));

        auto const USD = gw["USD"];
        env.trust(USD(100000), gw2);
        env(pay(gw, gw2, USD(5000)));
        env.trust(USD(100000), alice);
        env(pay(gw, alice, USD(5000)));

        auto const EUR = gw2["EUR"];
        env.trust(EUR(100000), gw);
        env(pay(gw2, gw, EUR(6000)));
        env.trust(EUR(100000), alice);
        env(pay(gw2, alice, EUR(6000)));
        env.close();

        AMM amm(env, gw, USD(1000), EUR(2000), ter(tesSUCCESS));
        env.close();
        BEAST_EXPECT(amm.expectBalances(
            USD(1000), EUR(2000), IOUAmount{1414213562373095, -12}));

        amm.deposit(gw2, USD(2000), EUR(4000));
        BEAST_EXPECT(amm.expectBalances(
            USD(3000), EUR(6000), IOUAmount{4242640687119285, -12}));

        amm.deposit(alice, USD(3000), EUR(6000));
        BEAST_EXPECT(amm.expectBalances(
            USD(6000), EUR(12000), IOUAmount{8485281374238570, -12}));

        BEAST_EXPECT(amm.expectLPTokens(gw, IOUAmount{1414213562373095, -12}));
        BEAST_EXPECT(amm.expectLPTokens(gw2, IOUAmount{2828427124746190, -12}));
        BEAST_EXPECT(
            amm.expectLPTokens(alice, IOUAmount{4242640687119285, -12}));

        // gw claws back 1000 USD from gw2.
        env(amm::ammClawback(gw, gw2, USD, EUR, USD(1000)), ter(tesSUCCESS));
        env.close();
        BEAST_EXPECT(amm.expectBalances(
            USD(5000), EUR(10000), IOUAmount{7071067811865475, -12}));

        BEAST_EXPECT(amm.expectLPTokens(gw, IOUAmount{1414213562373095, -12}));
        BEAST_EXPECT(amm.expectLPTokens(gw2, IOUAmount{1414213562373095, -12}));
        BEAST_EXPECT(
            amm.expectLPTokens(alice, IOUAmount{4242640687119285, -12}));

        BEAST_EXPECT(env.balance(alice, USD) == USD(2000));
        BEAST_EXPECT(env.balance(alice, EUR) == EUR(0));
        BEAST_EXPECT(env.balance(gw, EUR) == EUR(4000));
        BEAST_EXPECT(env.balance(gw2, USD) == USD(3000));

        // gw2 claws back 1000 EUR from gw.
        env(amm::ammClawback(gw2, gw, EUR, USD, EUR(1000)), ter(tesSUCCESS));
        env.close();
        BEAST_EXPECT(amm.expectBalances(
            USD(4500),
            STAmount(EUR, UINT64_C(9000000000000001), -12),
            IOUAmount{6363961030678928, -12}));

        BEAST_EXPECT(amm.expectLPTokens(gw, IOUAmount{7071067811865480, -13}));
        BEAST_EXPECT(amm.expectLPTokens(gw2, IOUAmount{1414213562373095, -12}));
        BEAST_EXPECT(
            amm.expectLPTokens(alice, IOUAmount{4242640687119285, -12}));

        BEAST_EXPECT(env.balance(alice, USD) == USD(2000));
        BEAST_EXPECT(env.balance(alice, EUR) == EUR(0));
        BEAST_EXPECT(env.balance(gw, EUR) == EUR(4000));
        BEAST_EXPECT(env.balance(gw2, USD) == USD(3000));

        // gw2 claws back 4000 EUR from alice.
        env(amm::ammClawback(gw2, alice, EUR, USD, EUR(4000)), ter(tesSUCCESS));
        env.close();
        BEAST_EXPECT(amm.expectBalances(
            USD(2500),
            STAmount(EUR, UINT64_C(5000000000000001), -12),
            IOUAmount{3535533905932738, -12}));

        BEAST_EXPECT(amm.expectLPTokens(gw, IOUAmount{7071067811865480, -13}));
        BEAST_EXPECT(amm.expectLPTokens(gw2, IOUAmount{1414213562373095, -12}));
        BEAST_EXPECT(
            amm.expectLPTokens(alice, IOUAmount{1414213562373095, -12}));

        BEAST_EXPECT(env.balance(alice, USD) == USD(4000));
        BEAST_EXPECT(env.balance(alice, EUR) == EUR(0));
        BEAST_EXPECT(env.balance(gw, EUR) == EUR(4000));
        BEAST_EXPECT(env.balance(gw2, USD) == USD(3000));
    }

    void
    testNotHoldingLptoken(FeatureBitset features)
    {
        testcase(
            "test AMMClawback from account which does not own any lptoken in "
            "the pool");
        using namespace jtx;

        Env env(*this, features);
        Account gw{"gateway"};
        Account alice{"alice"};
        env.fund(XRP(1000000), gw, alice);
        env.close();

        // gw sets asfAllowTrustLineClawback.
        env(fset(gw, asfAllowTrustLineClawback));
        env.close();
        env.require(flags(gw, asfAllowTrustLineClawback));

        auto const USD = gw["USD"];
        env.trust(USD(100000), alice);
        env(pay(gw, alice, USD(5000)));

        AMM amm(env, gw, USD(1000), XRP(2000), ter(tesSUCCESS));
        env.close();

        // Alice did not deposit in the amm pool. So AMMClawback from Alice
        // will fail.
        env(amm::ammClawback(gw, alice, USD, XRP, USD(1000)),
            ter(tecAMM_BALANCE));
    }

    void
    testAssetFrozen(FeatureBitset features)
    {
        testcase("test assets frozen");
        using namespace jtx;

        // test individually frozen trustline.
        {
            Env env(*this, features);
            Account gw{"gateway"};
            Account gw2{"gateway2"};
            Account alice{"alice"};
            env.fund(XRP(1000000), gw, gw2, alice);
            env.close();

            // gw sets asfAllowTrustLineClawback.
            env(fset(gw, asfAllowTrustLineClawback));
            env.close();
            env.require(flags(gw, asfAllowTrustLineClawback));

            // gw issues 3000 USD to Alice.
            auto const USD = gw["USD"];
            env.trust(USD(100000), alice);
            env(pay(gw, alice, USD(3000)));
            env.close();
            env.require(balance(alice, gw["USD"](3000)));

            // gw2 issues 3000 EUR to Alice.
            auto const EUR = gw2["EUR"];
            env.trust(EUR(100000), alice);
            env(pay(gw2, alice, EUR(3000)));
            env.close();
            env.require(balance(alice, gw2["EUR"](3000)));

            // Alice creates AMM pool of EUR/USD.
            AMM amm(env, alice, EUR(1000), USD(2000), ter(tesSUCCESS));
            env.close();

            BEAST_EXPECT(amm.expectBalances(
                USD(2000), EUR(1000), IOUAmount{1414213562373095, -12}));

            // freeze trustline
            env(trust(gw, alice["USD"](0), tfSetFreeze));
            env.close();

            // gw clawback 1000 USD from the AMM pool.
            env(amm::ammClawback(gw, alice, USD, EUR, USD(1000)),
                ter(tesSUCCESS));
            env.close();

            env.require(balance(alice, gw["USD"](1000)));
            env.require(balance(alice, gw2["EUR"](2500)));
            BEAST_EXPECT(amm.expectBalances(
                USD(1000), EUR(500), IOUAmount{7071067811865475, -13}));

            // Alice has half of its initial lptokens Left.
            BEAST_EXPECT(
                amm.expectLPTokens(alice, IOUAmount{7071067811865475, -13}));

            // gw clawback another 1000 USD from the AMM pool. The AMM pool will
            // be empty and get deleted.
            env(amm::ammClawback(gw, alice, USD, EUR, USD(1000)),
                ter(tesSUCCESS));
            env.close();

            // Alice should still has 1000 USD because gw clawed back from the
            // AMM pool.
            env.require(balance(alice, gw["USD"](1000)));
            env.require(balance(alice, gw2["EUR"](3000)));

            // amm is automatically deleted.
            BEAST_EXPECT(!amm.ammExists());
        }

        // test individually frozen trustline of both USD and EUR currency.
        {
            Env env(*this, features);
            Account gw{"gateway"};
            Account gw2{"gateway2"};
            Account alice{"alice"};
            env.fund(XRP(1000000), gw, gw2, alice);
            env.close();

            // gw sets asfAllowTrustLineClawback.
            env(fset(gw, asfAllowTrustLineClawback));
            env.close();
            env.require(flags(gw, asfAllowTrustLineClawback));

            // gw issues 3000 USD to Alice.
            auto const USD = gw["USD"];
            env.trust(USD(100000), alice);
            env(pay(gw, alice, USD(3000)));
            env.close();
            env.require(balance(alice, gw["USD"](3000)));

            // gw2 issues 3000 EUR to Alice.
            auto const EUR = gw2["EUR"];
            env.trust(EUR(100000), alice);
            env(pay(gw2, alice, EUR(3000)));
            env.close();
            env.require(balance(alice, gw2["EUR"](3000)));

            // Alice creates AMM pool of EUR/USD.
            AMM amm(env, alice, EUR(1000), USD(2000), ter(tesSUCCESS));
            env.close();

            BEAST_EXPECT(amm.expectBalances(
                USD(2000), EUR(1000), IOUAmount{1414213562373095, -12}));

            // freeze trustlines
            env(trust(gw, alice["USD"](0), tfSetFreeze));
            env(trust(gw2, alice["EUR"](0), tfSetFreeze));
            env.close();

            // gw clawback 1000 USD from the AMM pool.
            env(amm::ammClawback(gw, alice, USD, EUR, USD(1000)),
                ter(tesSUCCESS));
            env.close();

            env.require(balance(alice, gw["USD"](1000)));
            env.require(balance(alice, gw2["EUR"](2500)));
            BEAST_EXPECT(amm.expectBalances(
                USD(1000), EUR(500), IOUAmount{7071067811865475, -13}));
            BEAST_EXPECT(
                amm.expectLPTokens(alice, IOUAmount{7071067811865475, -13}));
        }

        // test gw global freeze.
        {
            Env env(*this, features);
            Account gw{"gateway"};
            Account gw2{"gateway2"};
            Account alice{"alice"};
            env.fund(XRP(1000000), gw, gw2, alice);
            env.close();

            // gw sets asfAllowTrustLineClawback.
            env(fset(gw, asfAllowTrustLineClawback));
            env.close();
            env.require(flags(gw, asfAllowTrustLineClawback));

            // gw issues 3000 USD to Alice.
            auto const USD = gw["USD"];
            env.trust(USD(100000), alice);
            env(pay(gw, alice, USD(3000)));
            env.close();
            env.require(balance(alice, gw["USD"](3000)));

            // gw2 issues 3000 EUR to Alice.
            auto const EUR = gw2["EUR"];
            env.trust(EUR(100000), alice);
            env(pay(gw2, alice, EUR(3000)));
            env.close();
            env.require(balance(alice, gw2["EUR"](3000)));

            // Alice creates AMM pool of EUR/USD.
            AMM amm(env, alice, EUR(1000), USD(2000), ter(tesSUCCESS));
            env.close();

            BEAST_EXPECT(amm.expectBalances(
                USD(2000), EUR(1000), IOUAmount{1414213562373095, -12}));

            // global freeze
            env(fset(gw, asfGlobalFreeze));
            env.close();

            // gw clawback 1000 USD from the AMM pool.
            env(amm::ammClawback(gw, alice, USD, EUR, USD(1000)),
                ter(tesSUCCESS));
            env.close();

            env.require(balance(alice, gw["USD"](1000)));
            env.require(balance(alice, gw2["EUR"](2500)));
            BEAST_EXPECT(amm.expectBalances(
                USD(1000), EUR(500), IOUAmount{7071067811865475, -13}));
            BEAST_EXPECT(
                amm.expectLPTokens(alice, IOUAmount{7071067811865475, -13}));
        }

        // Test both assets are issued by the same issuer. And issuer sets
        // global freeze.
        {
            Env env(*this, features);
            Account gw{"gateway"};
            Account alice{"alice"};
            Account bob{"bob"};
            Account carol{"carol"};
            env.fund(XRP(1000000), gw, alice, bob, carol);
            env.close();

            // gw sets asfAllowTrustLineClawback.
            env(fset(gw, asfAllowTrustLineClawback));
            env.close();
            env.require(flags(gw, asfAllowTrustLineClawback));

            auto const USD = gw["USD"];
            env.trust(USD(100000), alice);
            env(pay(gw, alice, USD(10000)));
            env.trust(USD(100000), bob);
            env(pay(gw, bob, USD(9000)));
            env.trust(USD(100000), carol);
            env(pay(gw, carol, USD(8000)));
            env.close();

            auto const EUR = gw["EUR"];
            env.trust(EUR(100000), alice);
            env(pay(gw, alice, EUR(10000)));
            env.trust(EUR(100000), bob);
            env(pay(gw, bob, EUR(9000)));
            env.trust(EUR(100000), carol);
            env(pay(gw, carol, EUR(8000)));
            env.close();

            AMM amm(env, alice, EUR(2000), USD(8000), ter(tesSUCCESS));
            env.close();

            BEAST_EXPECT(
                amm.expectBalances(USD(8000), EUR(2000), IOUAmount(4000)));
            amm.deposit(bob, USD(4000), EUR(1000));
            BEAST_EXPECT(
                amm.expectBalances(USD(12000), EUR(3000), IOUAmount(6000)));
            amm.deposit(carol, USD(2000), EUR(500));
            BEAST_EXPECT(
                amm.expectBalances(USD(14000), EUR(3500), IOUAmount(7000)));

            // global freeze
            env(fset(gw, asfGlobalFreeze));
            env.close();

            // gw clawback 1000 USD from carol.
            env(amm::ammClawback(gw, carol, USD, EUR, USD(1000)),
                ter(tesSUCCESS));
            env.close();
            BEAST_EXPECT(
                amm.expectBalances(USD(13000), EUR(3250), IOUAmount(6500)));

            BEAST_EXPECT(amm.expectLPTokens(alice, IOUAmount(4000)));
            BEAST_EXPECT(amm.expectLPTokens(bob, IOUAmount(2000)));
            BEAST_EXPECT(amm.expectLPTokens(carol, IOUAmount(500)));
            BEAST_EXPECT(env.balance(alice, USD) == USD(2000));
            BEAST_EXPECT(env.balance(alice, EUR) == EUR(8000));
            BEAST_EXPECT(env.balance(bob, USD) == USD(5000));
            BEAST_EXPECT(env.balance(bob, EUR) == EUR(8000));
            BEAST_EXPECT(env.balance(carol, USD) == USD(6000));
            // 250 EUR goes back to carol.
            BEAST_EXPECT(env.balance(carol, EUR) == EUR(7750));

            // gw clawback 1000 USD from bob with tfClawTwoAssets flag.
            // then the corresponding EUR will also be clawed back
            // by gw.
            env(amm::ammClawback(gw, bob, USD, EUR, USD(1000)),
                txflags(tfClawTwoAssets),
                ter(tesSUCCESS));
            env.close();
            BEAST_EXPECT(
                amm.expectBalances(USD(12000), EUR(3000), IOUAmount(6000)));

            BEAST_EXPECT(amm.expectLPTokens(alice, IOUAmount(4000)));
            BEAST_EXPECT(amm.expectLPTokens(bob, IOUAmount(1500)));
            BEAST_EXPECT(amm.expectLPTokens(carol, IOUAmount(500)));
            BEAST_EXPECT(env.balance(alice, USD) == USD(2000));
            BEAST_EXPECT(env.balance(alice, EUR) == EUR(8000));
            BEAST_EXPECT(env.balance(bob, USD) == USD(5000));
            // 250 EUR did not go back to bob because tfClawTwoAssets is set.
            BEAST_EXPECT(env.balance(bob, EUR) == EUR(8000));
            BEAST_EXPECT(env.balance(carol, USD) == USD(6000));
            BEAST_EXPECT(env.balance(carol, EUR) == EUR(7750));

            // gw clawback all USD from alice and set tfClawTwoAssets.
            env(amm::ammClawback(gw, alice, USD, EUR, std::nullopt),
                txflags(tfClawTwoAssets),
                ter(tesSUCCESS));
            env.close();
            BEAST_EXPECT(
                amm.expectBalances(USD(4000), EUR(1000), IOUAmount(2000)));

            BEAST_EXPECT(amm.expectLPTokens(alice, IOUAmount(0)));
            BEAST_EXPECT(amm.expectLPTokens(bob, IOUAmount(1500)));
            BEAST_EXPECT(amm.expectLPTokens(carol, IOUAmount(500)));
            BEAST_EXPECT(env.balance(alice, USD) == USD(2000));
            BEAST_EXPECT(env.balance(alice, EUR) == EUR(8000));
            BEAST_EXPECT(env.balance(bob, USD) == USD(5000));
            BEAST_EXPECT(env.balance(bob, EUR) == EUR(8000));
            BEAST_EXPECT(env.balance(carol, USD) == USD(6000));
            BEAST_EXPECT(env.balance(carol, EUR) == EUR(7750));
        }
    }

    void
    testSingleDepositAndClawback(FeatureBitset features)
    {
        testcase("test single depoit and clawback");
        using namespace jtx;

        // Test AMMClawback for USD/XRP pool. Claw back USD, and XRP goes back
        // to the holder.
        Env env(*this, features);
        Account gw{"gateway"};
        Account alice{"alice"};
        env.fund(XRP(1000000000), gw, alice);
        env.close();

        // gw sets asfAllowTrustLineClawback.
        env(fset(gw, asfAllowTrustLineClawback));
        env.close();
        env.require(flags(gw, asfAllowTrustLineClawback));

        // gw issues 1000 USD to Alice.
        auto const USD = gw["USD"];
        env.trust(USD(100000), alice);
        env(pay(gw, alice, USD(1000)));
        env.close();
        env.require(balance(alice, gw["USD"](1000)));

        // gw creates AMM pool of XRP/USD.
        AMM amm(env, gw, XRP(100), USD(400), ter(tesSUCCESS));
        env.close();

        BEAST_EXPECT(amm.expectBalances(USD(400), XRP(100), IOUAmount(200000)));

        amm.deposit(alice, USD(400));
        env.close();

        BEAST_EXPECT(amm.expectBalances(
            USD(800), XRP(100), IOUAmount{2828427124746190, -10}));

        auto aliceXrpBalance = env.balance(alice, XRP);

        env(amm::ammClawback(gw, alice, USD, XRP, USD(400)), ter(tesSUCCESS));
        env.close();

        BEAST_EXPECT(amm.expectBalances(
            STAmount(USD, UINT64_C(5656854249492380), -13),
            XRP(70.710678),
            IOUAmount(200000)));
        BEAST_EXPECT(amm.expectLPTokens(alice, IOUAmount(0)));
        BEAST_EXPECT(expectLedgerEntryRoot(
            env, alice, aliceXrpBalance + XRP(29.289322)));
    }

    void
    run() override
    {
        FeatureBitset const all{jtx::supported_amendments()};
        testInvalidRequest(all);
        testFeatureDisabled(all - featureAMMClawback);
        testAMMClawbackSpecificAmount(all);
        testAMMClawbackExceedBalance(all);
        testAMMClawbackAll(all);
        testAMMClawbackSameIssuerAssets(all);
        testAMMClawbackSameCurrency(all);
        testAMMClawbackIssuesEachOther(all);
        testNotHoldingLptoken(all);
        testAssetFrozen(all);
        testSingleDepositAndClawback(all);
    }
};
BEAST_DEFINE_TESTSUITE(AMMClawback, app, ripple);
}  // namespace test
}  // namespace ripple
