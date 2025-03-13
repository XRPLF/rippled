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
#include <xrpld/app/tx/applySteps.h>
#include <xrpld/ledger/Dir.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

#include <algorithm>
#include <iterator>

namespace ripple {
namespace test {

struct EscrowToken_test : public beast::unit_test::suite
{
    static STAmount
    limitAmount(
        jtx::Env const& env,
        jtx::Account const& account,
        jtx::Account const& gw,
        jtx::IOU const& iou)
    {
        auto const aHigh = account.id() > gw.id();
        auto const sle = env.le(keylet::line(account, gw, iou.currency));
        if (sle && sle->isFieldPresent(aHigh ? sfLowLimit : sfHighLimit))
            return (*sle)[aHigh ? sfLowLimit : sfHighLimit];
        return STAmount(iou, 0);
    }

    static STAmount
    lineBalance(
        jtx::Env const& env,
        jtx::Account const& account,
        jtx::Account const& gw,
        jtx::IOU const& iou)
    {
        auto const sle = env.le(keylet::line(account, gw, iou.currency));
        if (sle && sle->isFieldPresent(sfBalance))
            return (*sle)[sfBalance];
        return STAmount(iou, 0);
    }

    static uint64_t
    mptBalance(
        jtx::Env const& env,
        jtx::Account const& account,
        jtx::MPT const& mpt)
    {
        auto const sle = env.le(keylet::mptoken(mpt.mpt(), account));
        if (sle && sle->isFieldPresent(sfMPTAmount))
            return (*sle)[sfMPTAmount];
        return 0;
    }

    static uint64_t
    mptEscrowed(
        jtx::Env const& env,
        jtx::Account const& account,
        jtx::MPT const& mpt)
    {
        auto const sle = env.le(keylet::mptoken(mpt.mpt(), account));
        if (sle && sle->isFieldPresent(sfEscrowedAmount))
            return (*sle)[sfEscrowedAmount];
        return 0;
    }

    static uint64_t
    issuerMPTEscrowed(jtx::Env const& env, jtx::MPT const& mpt)
    {
        auto const sle = env.le(keylet::mptIssuance(mpt.mpt()));
        if (sle && sle->isFieldPresent(sfEscrowedAmount))
            return (*sle)[sfEscrowedAmount];
        return 0;
    }

    static uint64_t
    issuerMPTOutstanding(jtx::Env const& env, jtx::MPT const& mpt)
    {
        auto const sle = env.le(keylet::mptIssuance(mpt.mpt()));
        if (sle && sle->isFieldPresent(sfOutstandingAmount))
            return (*sle)[sfOutstandingAmount];
        return 0;
    }

    void
    issuerIOUEscrowed(
        jtx::Env& env,
        jtx::Account const& account,
        Currency const& currency,
        int const& outstanding,
        int const& escrowed)
    {
        Json::Value params;
        params[jss::account] = account.human();
        auto jrr = env.rpc("json", "gateway_balances", to_string(params));
        std::cout << jrr << std::endl;
        auto const result = jrr[jss::result];
        auto const actualOutstanding =
            result[jss::obligations][to_string(currency)];
        BEAST_EXPECT(actualOutstanding == to_string(outstanding));
        if (escrowed != 0)
        {
            auto const actualEscrowed =
                result[jss::escrowed][to_string(currency)];
            BEAST_EXPECT(actualEscrowed == to_string(escrowed));
        }
    }

    void
    testIOUEnablement(FeatureBitset features)
    {
        testcase("IOU Enablement");

        using namespace jtx;
        using namespace std::chrono;

        for (bool const withTokenEscrow : {false, true})
        {
            auto const amend =
                withTokenEscrow ? features : features - featureTokenEscrow;
            Env env{*this, amend};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];
            env.fund(XRP(5000), alice, bob, gw);
            env(fset(gw, asfAllowTokenLocking));
            env.close();
            env.trust(USD(10'000), alice, bob);
            env.close();
            env(pay(gw, alice, USD(5000)));
            env(pay(gw, bob, USD(5000)));
            env.close();

            auto const createResult =
                withTokenEscrow ? ter(tesSUCCESS) : ter(temDISABLED);
            auto const finishResult =
                withTokenEscrow ? ter(tesSUCCESS) : ter(tecNO_TARGET);

            auto const seq1 = env.seq(alice);
            env(escrow::create(alice, bob, USD(1'000)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(1500),
                createResult);
            env.close();
            env(escrow::finish(bob, alice, seq1),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(1500),
                finishResult);
            env.close();

            auto const seq2 = env.seq(alice);
            env(escrow::create(alice, bob, USD(1'000)),
                escrow::condition(escrow::cb2),
                escrow::finish_time(env.now() + 1s),
                escrow::cancel_time(env.now() + 2s),
                fee(1500),
                createResult);
            env.close();
            env(escrow::cancel(bob, alice, seq2), fee(1500), finishResult);
            env.close();
        }
    }

    void
    testIOUBalances(FeatureBitset features)
    {
        testcase("IOU Balances");

        using namespace jtx;
        using namespace std::chrono;

        Env env{*this, features};
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];
        env.fund(XRP(5000), alice, bob, gw);
        env(fset(gw, asfAllowTokenLocking));
        env.close();
        env.trust(USD(10'000), alice, bob);
        env.close();
        env(pay(gw, alice, USD(5000)));
        env(pay(gw, bob, USD(5000)));
        env.close();

        auto const seq1 = env.seq(alice);
        env(escrow::create(alice, bob, USD(1'000)),
            escrow::condition(escrow::cb1),
            escrow::finish_time(env.now() + 1s),
            fee(1500),
            ter(tesSUCCESS));
        env.close();
        env(escrow::finish(bob, alice, seq1),
            escrow::condition(escrow::cb1),
            escrow::fulfillment(escrow::fb1),
            fee(1500),
            ter(tesSUCCESS));
        env.close();

        auto const seq2 = env.seq(alice);
        env(escrow::create(alice, bob, USD(1'000)),
            escrow::condition(escrow::cb2),
            escrow::finish_time(env.now() + 1s),
            escrow::cancel_time(env.now() + 2s),
            fee(1500),
            ter(tesSUCCESS));
        env.close();
        env(escrow::cancel(bob, alice, seq2), fee(1500), ter(tesSUCCESS));
        env.close();
    }

    void
    testIOUMetaAndOwnership(FeatureBitset features)
    {
        using namespace jtx;
        using namespace std::chrono;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];
        {
            testcase("IOU Metadata to self");

            Env env{*this, features};
            env.fund(XRP(5000), alice, bob, carol, gw);
            env(fset(gw, asfAllowTokenLocking));
            env.close();
            env.trust(USD(10'000), alice, bob, carol);
            env.close();
            env(pay(gw, alice, USD(5000)));
            env(pay(gw, bob, USD(5000)));
            env(pay(gw, carol, USD(5000)));
            env.close();
            auto const aseq = env.seq(alice);
            auto const bseq = env.seq(bob);

            env(escrow::create(alice, alice, USD(1'000)),
                escrow::finish_time(env.now() + 1s),
                escrow::cancel_time(env.now() + 500s));
            BEAST_EXPECT(
                (*env.meta())[sfTransactionResult] ==
                static_cast<std::uint8_t>(tesSUCCESS));
            env.close(5s);
            auto const aa = env.le(keylet::escrow(alice.id(), aseq));
            BEAST_EXPECT(aa);
            {
                ripple::Dir aod(*env.current(), keylet::ownerDir(alice.id()));
                BEAST_EXPECT(std::distance(aod.begin(), aod.end()) == 2);
                BEAST_EXPECT(
                    std::find(aod.begin(), aod.end(), aa) != aod.end());
            }

            {
                ripple::Dir iod(*env.current(), keylet::ownerDir(gw.id()));
                BEAST_EXPECT(std::distance(iod.begin(), iod.end()) == 4);
                BEAST_EXPECT(
                    std::find(iod.begin(), iod.end(), aa) != iod.end());
            }

            env(escrow::create(bob, bob, USD(1'000)),
                escrow::finish_time(env.now() + 1s),
                escrow::cancel_time(env.now() + 2s));
            BEAST_EXPECT(
                (*env.meta())[sfTransactionResult] ==
                static_cast<std::uint8_t>(tesSUCCESS));
            env.close(5s);
            auto const bb = env.le(keylet::escrow(bob.id(), bseq));
            BEAST_EXPECT(bb);

            {
                ripple::Dir bod(*env.current(), keylet::ownerDir(bob.id()));
                BEAST_EXPECT(std::distance(bod.begin(), bod.end()) == 2);
                BEAST_EXPECT(
                    std::find(bod.begin(), bod.end(), bb) != bod.end());
            }

            {
                ripple::Dir iod(*env.current(), keylet::ownerDir(gw.id()));
                BEAST_EXPECT(std::distance(iod.begin(), iod.end()) == 5);
                BEAST_EXPECT(
                    std::find(iod.begin(), iod.end(), bb) != iod.end());
            }

            env.close(5s);
            env(escrow::finish(alice, alice, aseq));
            {
                BEAST_EXPECT(!env.le(keylet::escrow(alice.id(), aseq)));
                BEAST_EXPECT(
                    (*env.meta())[sfTransactionResult] ==
                    static_cast<std::uint8_t>(tesSUCCESS));

                ripple::Dir aod(*env.current(), keylet::ownerDir(alice.id()));
                BEAST_EXPECT(std::distance(aod.begin(), aod.end()) == 1);
                BEAST_EXPECT(
                    std::find(aod.begin(), aod.end(), aa) == aod.end());

                ripple::Dir bod(*env.current(), keylet::ownerDir(bob.id()));
                BEAST_EXPECT(std::distance(bod.begin(), bod.end()) == 2);
                BEAST_EXPECT(
                    std::find(bod.begin(), bod.end(), bb) != bod.end());

                ripple::Dir iod(*env.current(), keylet::ownerDir(gw.id()));
                BEAST_EXPECT(std::distance(iod.begin(), iod.end()) == 4);
                BEAST_EXPECT(
                    std::find(iod.begin(), iod.end(), bb) != iod.end());
            }

            env.close(5s);
            env(escrow::cancel(bob, bob, bseq));
            {
                BEAST_EXPECT(!env.le(keylet::escrow(bob.id(), bseq)));
                BEAST_EXPECT(
                    (*env.meta())[sfTransactionResult] ==
                    static_cast<std::uint8_t>(tesSUCCESS));

                ripple::Dir bod(*env.current(), keylet::ownerDir(bob.id()));
                BEAST_EXPECT(std::distance(bod.begin(), bod.end()) == 1);
                BEAST_EXPECT(
                    std::find(bod.begin(), bod.end(), bb) == bod.end());

                ripple::Dir iod(*env.current(), keylet::ownerDir(gw.id()));
                BEAST_EXPECT(std::distance(iod.begin(), iod.end()) == 3);
                BEAST_EXPECT(
                    std::find(iod.begin(), iod.end(), bb) == iod.end());
            }
        }
        {
            testcase("IOU Metadata to other");

            Env env{*this, features};
            env.fund(XRP(5000), alice, bob, carol, gw);
            env(fset(gw, asfAllowTokenLocking));
            env.close();
            env.trust(USD(10'000), alice, bob, carol);
            env.close();
            env(pay(gw, alice, USD(5000)));
            env(pay(gw, bob, USD(5000)));
            env(pay(gw, carol, USD(5000)));
            env.close();
            auto const aseq = env.seq(alice);
            auto const bseq = env.seq(bob);

            env(escrow::create(alice, bob, USD(1'000)),
                escrow::finish_time(env.now() + 1s));
            BEAST_EXPECT(
                (*env.meta())[sfTransactionResult] ==
                static_cast<std::uint8_t>(tesSUCCESS));
            env.close(5s);
            env(escrow::create(bob, carol, USD(1'000)),
                escrow::finish_time(env.now() + 1s),
                escrow::cancel_time(env.now() + 2s));
            BEAST_EXPECT(
                (*env.meta())[sfTransactionResult] ==
                static_cast<std::uint8_t>(tesSUCCESS));
            env.close(5s);

            auto const ab = env.le(keylet::escrow(alice.id(), aseq));
            BEAST_EXPECT(ab);

            auto const bc = env.le(keylet::escrow(bob.id(), bseq));
            BEAST_EXPECT(bc);

            {
                ripple::Dir aod(*env.current(), keylet::ownerDir(alice.id()));
                BEAST_EXPECT(std::distance(aod.begin(), aod.end()) == 2);
                BEAST_EXPECT(
                    std::find(aod.begin(), aod.end(), ab) != aod.end());

                ripple::Dir bod(*env.current(), keylet::ownerDir(bob.id()));
                BEAST_EXPECT(std::distance(bod.begin(), bod.end()) == 3);
                BEAST_EXPECT(
                    std::find(bod.begin(), bod.end(), ab) != bod.end());
                BEAST_EXPECT(
                    std::find(bod.begin(), bod.end(), bc) != bod.end());

                ripple::Dir cod(*env.current(), keylet::ownerDir(carol.id()));
                BEAST_EXPECT(std::distance(cod.begin(), cod.end()) == 2);
                BEAST_EXPECT(
                    std::find(cod.begin(), cod.end(), bc) != cod.end());

                ripple::Dir iod(*env.current(), keylet::ownerDir(gw.id()));
                BEAST_EXPECT(std::distance(iod.begin(), iod.end()) == 5);
                BEAST_EXPECT(
                    std::find(iod.begin(), iod.end(), ab) != iod.end());
                BEAST_EXPECT(
                    std::find(iod.begin(), iod.end(), bc) != iod.end());
            }

            env.close(5s);
            env(escrow::finish(alice, alice, aseq));
            {
                BEAST_EXPECT(!env.le(keylet::escrow(alice.id(), aseq)));
                BEAST_EXPECT(env.le(keylet::escrow(bob.id(), bseq)));

                ripple::Dir aod(*env.current(), keylet::ownerDir(alice.id()));
                BEAST_EXPECT(std::distance(aod.begin(), aod.end()) == 1);
                BEAST_EXPECT(
                    std::find(aod.begin(), aod.end(), ab) == aod.end());

                ripple::Dir bod(*env.current(), keylet::ownerDir(bob.id()));
                BEAST_EXPECT(std::distance(bod.begin(), bod.end()) == 2);
                BEAST_EXPECT(
                    std::find(bod.begin(), bod.end(), ab) == bod.end());
                BEAST_EXPECT(
                    std::find(bod.begin(), bod.end(), bc) != bod.end());

                ripple::Dir cod(*env.current(), keylet::ownerDir(carol.id()));
                BEAST_EXPECT(std::distance(cod.begin(), cod.end()) == 2);

                ripple::Dir iod(*env.current(), keylet::ownerDir(gw.id()));
                BEAST_EXPECT(std::distance(iod.begin(), iod.end()) == 4);
                BEAST_EXPECT(
                    std::find(iod.begin(), iod.end(), ab) == iod.end());
                BEAST_EXPECT(
                    std::find(iod.begin(), iod.end(), bc) != iod.end());
            }

            env.close(5s);
            env(escrow::cancel(bob, bob, bseq));
            {
                BEAST_EXPECT(!env.le(keylet::escrow(alice.id(), aseq)));
                BEAST_EXPECT(!env.le(keylet::escrow(bob.id(), bseq)));

                ripple::Dir aod(*env.current(), keylet::ownerDir(alice.id()));
                BEAST_EXPECT(std::distance(aod.begin(), aod.end()) == 1);
                BEAST_EXPECT(
                    std::find(aod.begin(), aod.end(), ab) == aod.end());

                ripple::Dir bod(*env.current(), keylet::ownerDir(bob.id()));
                BEAST_EXPECT(std::distance(bod.begin(), bod.end()) == 1);
                BEAST_EXPECT(
                    std::find(bod.begin(), bod.end(), ab) == bod.end());
                BEAST_EXPECT(
                    std::find(bod.begin(), bod.end(), bc) == bod.end());

                ripple::Dir cod(*env.current(), keylet::ownerDir(carol.id()));
                BEAST_EXPECT(std::distance(cod.begin(), cod.end()) == 1);

                ripple::Dir iod(*env.current(), keylet::ownerDir(gw.id()));
                BEAST_EXPECT(std::distance(iod.begin(), iod.end()) == 3);
                BEAST_EXPECT(
                    std::find(iod.begin(), iod.end(), ab) == iod.end());
                BEAST_EXPECT(
                    std::find(iod.begin(), iod.end(), bc) == iod.end());
            }
        }

        {
            testcase("IOU Metadata to issuer");

            Env env{*this, features};
            env.fund(XRP(5000), alice, carol, gw);
            env(fset(gw, asfAllowTokenLocking));
            env.close();
            env.trust(USD(10'000), alice, carol);
            env.close();
            env(pay(gw, alice, USD(5000)));
            env(pay(gw, carol, USD(5000)));
            env.close();
            auto const aseq = env.seq(alice);
            auto const gseq = env.seq(gw);

            env(escrow::create(alice, gw, USD(1'000)),
                escrow::finish_time(env.now() + 1s));

            BEAST_EXPECT(
                (*env.meta())[sfTransactionResult] ==
                static_cast<std::uint8_t>(tesSUCCESS));
            env.close(5s);
            env(escrow::create(gw, carol, USD(1'000)),
                escrow::finish_time(env.now() + 1s),
                escrow::cancel_time(env.now() + 2s));
            BEAST_EXPECT(
                (*env.meta())[sfTransactionResult] ==
                static_cast<std::uint8_t>(tesSUCCESS));
            env.close(5s);

            auto const ag = env.le(keylet::escrow(alice.id(), aseq));
            BEAST_EXPECT(ag);

            auto const gc = env.le(keylet::escrow(gw.id(), gseq));
            BEAST_EXPECT(gc);

            {
                ripple::Dir aod(*env.current(), keylet::ownerDir(alice.id()));
                BEAST_EXPECT(std::distance(aod.begin(), aod.end()) == 2);
                BEAST_EXPECT(
                    std::find(aod.begin(), aod.end(), ag) != aod.end());

                ripple::Dir cod(*env.current(), keylet::ownerDir(carol.id()));
                BEAST_EXPECT(std::distance(cod.begin(), cod.end()) == 2);
                BEAST_EXPECT(
                    std::find(cod.begin(), cod.end(), gc) != cod.end());

                ripple::Dir iod(*env.current(), keylet::ownerDir(gw.id()));
                BEAST_EXPECT(std::distance(iod.begin(), iod.end()) == 4);
                BEAST_EXPECT(
                    std::find(iod.begin(), iod.end(), ag) != iod.end());
                BEAST_EXPECT(
                    std::find(iod.begin(), iod.end(), gc) != iod.end());
            }

            env.close(5s);
            env(escrow::finish(alice, alice, aseq));
            {
                BEAST_EXPECT(!env.le(keylet::escrow(alice.id(), aseq)));
                BEAST_EXPECT(env.le(keylet::escrow(gw.id(), gseq)));

                ripple::Dir aod(*env.current(), keylet::ownerDir(alice.id()));
                BEAST_EXPECT(std::distance(aod.begin(), aod.end()) == 1);
                BEAST_EXPECT(
                    std::find(aod.begin(), aod.end(), ag) == aod.end());

                ripple::Dir cod(*env.current(), keylet::ownerDir(carol.id()));
                BEAST_EXPECT(std::distance(cod.begin(), cod.end()) == 2);

                ripple::Dir iod(*env.current(), keylet::ownerDir(gw.id()));
                BEAST_EXPECT(std::distance(iod.begin(), iod.end()) == 3);
                BEAST_EXPECT(
                    std::find(iod.begin(), iod.end(), ag) == iod.end());
                BEAST_EXPECT(
                    std::find(iod.begin(), iod.end(), gc) != iod.end());
            }

            env.close(5s);
            env(escrow::cancel(gw, gw, gseq));
            {
                BEAST_EXPECT(!env.le(keylet::escrow(alice.id(), aseq)));
                BEAST_EXPECT(!env.le(keylet::escrow(gw.id(), gseq)));

                ripple::Dir aod(*env.current(), keylet::ownerDir(alice.id()));
                BEAST_EXPECT(std::distance(aod.begin(), aod.end()) == 1);
                BEAST_EXPECT(
                    std::find(aod.begin(), aod.end(), ag) == aod.end());

                ripple::Dir cod(*env.current(), keylet::ownerDir(carol.id()));
                BEAST_EXPECT(std::distance(cod.begin(), cod.end()) == 1);

                ripple::Dir iod(*env.current(), keylet::ownerDir(gw.id()));
                BEAST_EXPECT(std::distance(iod.begin(), iod.end()) == 2);
                BEAST_EXPECT(
                    std::find(iod.begin(), iod.end(), ag) == iod.end());
                BEAST_EXPECT(
                    std::find(iod.begin(), iod.end(), gc) == iod.end());
            }
        }
    }

    void
    testIOURippleState(FeatureBitset features)
    {
        testcase("IOU RippleState");
        using namespace test::jtx;
        using namespace std::literals;

        struct TestAccountData
        {
            Account src;
            Account dst;
            Account gw;
            bool hasTrustline;
            bool negative;
        };

        std::array<TestAccountData, 8> tests = {{
            // src > dst && src > issuer && dst no trustline
            {Account("alice2"), Account("bob0"), Account{"gw0"}, false, true},
            // src < dst && src < issuer && dst no trustline
            {Account("carol0"), Account("dan1"), Account{"gw1"}, false, false},
            // dst > src && dst > issuer && dst no trustline
            {Account("dan1"), Account("alice2"), Account{"gw0"}, false, true},
            // dst < src && dst < issuer && dst no trustline
            {Account("bob0"), Account("carol0"), Account{"gw1"}, false, false},
            // src > dst && src > issuer && dst has trustline
            {Account("alice2"), Account("bob0"), Account{"gw0"}, true, true},
            // src < dst && src < issuer && dst has trustline
            {Account("carol0"), Account("dan1"), Account{"gw1"}, true, false},
            // dst > src && dst > issuer && dst has trustline
            {Account("dan1"), Account("alice2"), Account{"gw0"}, true, true},
            // dst < src && dst < issuer && dst has trustline
            {Account("bob0"), Account("carol0"), Account{"gw1"}, true, false},
        }};

        for (auto const& t : tests)
        {
            Env env{*this, features};
            auto const USD = t.gw["USD"];
            env.fund(XRP(5000), t.src, t.dst, t.gw);
            env(fset(t.gw, asfAllowTokenLocking));
            env.close();

            if (t.hasTrustline)
                env.trust(USD(100'000), t.src, t.dst);
            else
                env.trust(USD(100'000), t.src);
            env.close();

            env(pay(t.gw, t.src, USD(10'000)));
            if (t.hasTrustline)
                env(pay(t.gw, t.dst, USD(10'000)));
            env.close();

            // src can create escrow
            auto const seq1 = env.seq(t.src);
            auto const delta = USD(1'000);
            env(escrow::create(t.src, t.dst, delta),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(1500));
            env.close();

            // dst can finish escrow
            auto const preSrc = lineBalance(env, t.src, t.gw, USD);
            auto const preDst = lineBalance(env, t.dst, t.gw, USD);

            env(escrow::finish(t.dst, t.src, seq1),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(1500));
            env.close();

            BEAST_EXPECT(lineBalance(env, t.src, t.gw, USD) == preSrc);
            BEAST_EXPECT(
                lineBalance(env, t.dst, t.gw, USD) ==
                (t.negative ? (preDst - delta) : (preDst + delta)));
        }
    }

    void
    testIOUGateway(FeatureBitset features)
    {
        testcase("IOU Gateway");
        using namespace test::jtx;
        using namespace std::literals;

        struct TestAccountData
        {
            Account src;
            Account dst;
            bool hasTrustline;
            bool negative;
        };

        std::array<TestAccountData, 8> gwSrcTests = {{
            // src > dst && src > issuer && dst no trustline
            {Account("gw0"), Account{"alice2"}, false, true},
            // src < dst && src < issuer && dst no trustline
            {Account("gw1"), Account{"carol0"}, false, false},
            // dst > src && dst > issuer && dst no trustline
            {Account("gw0"), Account{"dan1"}, false, true},
            // dst < src && dst < issuer && dst no trustline
            {Account("gw1"), Account{"bob0"}, false, false},
            // src > dst && src > issuer && dst has trustline
            {Account("gw0"), Account{"alice2"}, true, true},
            // src < dst && src < issuer && dst has trustline
            {Account("gw1"), Account{"carol0"}, true, false},
            // dst > src && dst > issuer && dst has trustline
            {Account("gw0"), Account{"dan1"}, true, true},
            // dst < src && dst < issuer && dst has trustline
            {Account("gw1"), Account{"bob0"}, true, false},
        }};

        for (auto const& t : gwSrcTests)
        {
            Env env{*this, features};
            auto const USD = t.src["USD"];
            env.fund(XRP(5000), t.dst, t.src);
            env(fset(t.src, asfAllowTokenLocking));
            env.close();

            if (t.hasTrustline)
                env.trust(USD(100'000), t.dst);

            env.close();

            if (t.hasTrustline)
                env(pay(t.src, t.dst, USD(10'000)));

            env.close();

            // issuer can create escrow
            auto const seq1 = env.seq(t.src);
            auto const preDst = lineBalance(env, t.dst, t.src, USD);
            env(escrow::create(t.src, t.dst, USD(1'000)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(1500));
            env.close();

            // src can finish escrow, no dest trustline
            env(escrow::finish(t.dst, t.src, seq1),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(1500));
            env.close();
            auto const preAmount = t.hasTrustline ? 10'000 : 0;
            BEAST_EXPECT(
                preDst == (t.negative ? -USD(preAmount) : USD(preAmount)));
            auto const postAmount = t.hasTrustline ? 11000 : 1'000;
            BEAST_EXPECT(
                lineBalance(env, t.dst, t.src, USD) ==
                (t.negative ? -USD(postAmount) : USD(postAmount)));
            BEAST_EXPECT(lineBalance(env, t.src, t.src, USD) == USD(0));
        }

        std::array<TestAccountData, 4> gwDstTests = {{
            // src > dst && src > issuer && dst has trustline
            {Account("alice2"), Account{"gw0"}, true, true},
            // src < dst && src < issuer && dst has trustline
            {Account("carol0"), Account{"gw1"}, true, false},
            // dst > src && dst > issuer && dst has trustline
            {Account("dan1"), Account{"gw0"}, true, true},
            // dst < src && dst < issuer && dst has trustline
            {Account("bob0"), Account{"gw1"}, true, false},
        }};

        for (auto const& t : gwDstTests)
        {
            Env env{*this, features};
            auto const USD = t.dst["USD"];
            env.fund(XRP(5000), t.dst, t.src);
            env(fset(t.dst, asfAllowTokenLocking));
            env.close();

            env.trust(USD(100'000), t.src);
            env.close();

            env(pay(t.dst, t.src, USD(10'000)));
            env.close();

            // issuer can receive escrow
            auto const seq1 = env.seq(t.src);
            auto const preSrc = lineBalance(env, t.src, t.dst, USD);
            env(escrow::create(t.src, t.dst, USD(1'000)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(1500));
            env.close();

            // issuer can finish escrow, no dest trustline
            env(escrow::finish(t.dst, t.src, seq1),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(1500));
            env.close();
            auto const preAmount = 10'000;
            BEAST_EXPECT(
                preSrc == (t.negative ? -USD(preAmount) : USD(preAmount)));
            auto const postAmount = 9000;
            BEAST_EXPECT(
                lineBalance(env, t.src, t.dst, USD) ==
                (t.negative ? -USD(postAmount) : USD(postAmount)));
            BEAST_EXPECT(lineBalance(env, t.dst, t.dst, USD) == USD(0));
        }

        // issuer is source and destination
        {
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];
            Env env{*this, features};
            env.fund(XRP(5000), gw);
            env(fset(gw, asfAllowTokenLocking));
            env.close();

            // issuer can receive escrow
            auto const seq1 = env.seq(gw);
            env(escrow::create(gw, gw, USD(1'000)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(1500));
            env.close();

            // issuer can finish escrow
            env(escrow::finish(gw, gw, seq1),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(1500));
            env.close();
        }
    }

    void
    testIOULockedRate(FeatureBitset features)
    {
        testcase("IOU Locked Rate");
        using namespace test::jtx;
        using namespace std::literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];

        // test locked rate
        {
            Env env{*this, features};
            env.fund(XRP(10'000), alice, bob, gw);
            env(fset(gw, asfAllowTokenLocking));
            env(rate(gw, 1.25));
            env.close();
            env.trust(USD(100'000), alice);
            env.trust(USD(100'000), bob);
            env.close();
            env(pay(gw, alice, USD(10'000)));
            env(pay(gw, bob, USD(10'000)));
            env.close();

            // alice can create escrow w/ xfer rate
            auto const preAlice = env.balance(alice, USD);
            auto const seq1 = env.seq(alice);
            auto const delta = USD(125);
            env(escrow::create(alice, bob, delta),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(1500));
            env.close();
            auto const transferRate = escrow::rate(env, alice, seq1);
            BEAST_EXPECT(
                transferRate.value == std::uint32_t(1'000'000'000 * 1.25));

            // bob can finish escrow
            env(escrow::finish(bob, alice, seq1),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(1500));
            env.close();

            BEAST_EXPECT(env.balance(alice, USD) == preAlice - delta);
            BEAST_EXPECT(env.balance(bob, USD) == USD(10'100));
        }
        // test rate change - higher
        {
            Env env{*this, features};
            env.fund(XRP(10'000), alice, bob, gw);
            env(fset(gw, asfAllowTokenLocking));
            env(rate(gw, 1.25));
            env.close();
            env.trust(USD(100'000), alice);
            env.trust(USD(100'000), bob);
            env.close();
            env(pay(gw, alice, USD(10'000)));
            env(pay(gw, bob, USD(10'000)));
            env.close();

            // alice can create escrow w/ xfer rate
            auto const preAlice = env.balance(alice, USD);
            auto const seq1 = env.seq(alice);
            auto const delta = USD(125);
            env(escrow::create(alice, bob, delta),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(1500));
            env.close();
            auto transferRate = escrow::rate(env, alice, seq1);
            BEAST_EXPECT(
                transferRate.value == std::uint32_t(1'000'000'000 * 1.25));

            // issuer changes rate higher
            env(rate(gw, 1.26));
            env.close();

            // bob can finish escrow - rate unchanged
            env(escrow::finish(bob, alice, seq1),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(1500));
            env.close();

            BEAST_EXPECT(env.balance(alice, USD) == preAlice - delta);
            BEAST_EXPECT(env.balance(bob, USD) == USD(10'100));
        }
        // test rate change - lower
        {
            Env env{*this, features};
            env.fund(XRP(10'000), alice, bob, gw);
            env(fset(gw, asfAllowTokenLocking));
            env(rate(gw, 1.25));
            env.close();
            env.trust(USD(100'000), alice);
            env.trust(USD(100'000), bob);
            env.close();
            env(pay(gw, alice, USD(10'000)));
            env(pay(gw, bob, USD(10'000)));
            env.close();

            // alice can create escrow w/ xfer rate
            auto const preAlice = env.balance(alice, USD);
            auto const seq1 = env.seq(alice);
            auto const delta = USD(125);
            env(escrow::create(alice, bob, delta),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(1500));
            env.close();
            auto transferRate = escrow::rate(env, alice, seq1);
            BEAST_EXPECT(
                transferRate.value == std::uint32_t(1'000'000'000 * 1.25));

            // issuer changes rate lower
            env(rate(gw, 1.00));
            env.close();

            // bob can finish escrow - rate changed
            env(escrow::finish(bob, alice, seq1),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(1500));
            env.close();

            BEAST_EXPECT(env.balance(alice, USD) == preAlice - delta);
            BEAST_EXPECT(env.balance(bob, USD) == USD(10125));
        }
        // test issuer doesnt pay own rate
        {
            Env env{*this, features};
            env.fund(XRP(10'000), alice, bob, gw);
            env(fset(gw, asfAllowTokenLocking));
            env(rate(gw, 1.25));
            env.close();
            env.trust(USD(100'000), alice);
            env.trust(USD(100'000), bob);
            env.close();
            env(pay(gw, alice, USD(10'000)));
            env(pay(gw, bob, USD(10'000)));
            env.close();

            // issuer with rate can create escrow
            auto const preAlice = env.balance(alice, USD);
            auto const seq1 = env.seq(gw);
            auto const delta = USD(125);
            env(escrow::create(gw, alice, delta),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(1500));
            env.close();

            auto transferRate = escrow::rate(env, gw, seq1);
            BEAST_EXPECT(
                transferRate.value == std::uint32_t(1'000'000'000 * 1.25));

            // alice can finish escrow - no rate charged
            env(escrow::finish(alice, gw, seq1),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(1500));
            env.close();

            BEAST_EXPECT(env.balance(alice, USD) == preAlice + delta);
            BEAST_EXPECT(env.balance(alice, USD) == USD(10'125));
        }
    }

    void
    testIOULimitAmount(FeatureBitset features)
    {
        testcase("IOU Trustline Limit");
        using namespace test::jtx;
        using namespace std::literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];

        // test LimitAmount
        {
            Env env{*this, features};
            env.fund(XRP(1'000), alice, bob, gw);
            env(fset(gw, asfAllowTokenLocking));
            env.close();
            env.trust(USD(10'000), alice, bob);
            env.close();
            env(pay(gw, alice, USD(1'000)));
            env(pay(gw, bob, USD(1'000)));
            env.close();

            // alice can create escrow
            auto seq1 = env.seq(alice);
            auto const delta = USD(125);
            env(escrow::create(alice, bob, delta),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(1500));
            env.close();

            // bob can finish
            auto const preBobLimit = limitAmount(env, bob, gw, USD);
            env(escrow::finish(bob, alice, seq1),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(1500));
            env.close();
            auto const postBobLimit = limitAmount(env, bob, gw, USD);
            // bobs limit is NOT changed
            BEAST_EXPECT(postBobLimit == preBobLimit);
        }
    }

    void
    testIOURequireAuth(FeatureBitset features)
    {
        testcase("IOU Trustline Require Auth");
        using namespace test::jtx;
        using namespace std::literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];

        auto const aliceUSD = alice["USD"];
        auto const bobUSD = bob["USD"];

        // test asfRequireAuth
        {
            Env env{*this, features};
            env.fund(XRP(1'000), alice, bob, gw);
            env(fset(gw, asfAllowTokenLocking));
            env(fset(gw, asfRequireAuth));
            env.close();
            env(trust(gw, aliceUSD(10'000)), txflags(tfSetfAuth));
            env(trust(alice, USD(10'000)));
            env(trust(bob, USD(10'000)));
            env.close();
            env(pay(gw, alice, USD(1'000)));
            env.close();

            // alice cannot create escrow - fails without auth
            auto seq1 = env.seq(alice);
            auto const delta = USD(125);
            env(escrow::create(alice, bob, delta),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(1500),
                ter(tecNO_AUTH));
            env.close();

            // set auth on bob
            env(trust(gw, bobUSD(10'000)), txflags(tfSetfAuth));
            env(trust(bob, USD(10'000)));
            env.close();
            env(pay(gw, bob, USD(1'000)));
            env.close();

            // alice can create escrow - bob has auth
            seq1 = env.seq(alice);
            env(escrow::create(alice, bob, delta),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(1500));
            env.close();

            // bob can finish
            env(escrow::finish(bob, alice, seq1),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(1500));
            env.close();
        }
    }

    void
    testIOUFreeze(FeatureBitset features)
    {
        testcase("IOU Trustline Freeze");
        using namespace test::jtx;
        using namespace std::literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];

        // test Global Freeze
        {
            Env env{*this, features};
            env.fund(XRP(10'000), alice, bob, gw);
            env(fset(gw, asfAllowTokenLocking));
            env.close();
            env.trust(USD(100'000), alice);
            env.trust(USD(100'000), bob);
            env.close();
            env(pay(gw, alice, USD(10'000)));
            env(pay(gw, bob, USD(10'000)));
            env.close();
            env(fset(gw, asfGlobalFreeze));
            env.close();

            // setup transaction
            auto seq1 = env.seq(alice);
            auto const delta = USD(125);

            // create escrow fails - frozen trustline
            env(escrow::create(alice, bob, delta),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(1500),
                ter(tecFROZEN));
            env.close();

            // clear global freeze
            env(fclear(gw, asfGlobalFreeze));
            env.close();

            // create escrow success
            seq1 = env.seq(alice);
            env(escrow::create(alice, bob, delta),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(1500));
            env.close();

            // set global freeze
            env(fset(gw, asfGlobalFreeze));
            env.close();

            // bob finish escrow success regardless of frozen assets
            env(escrow::finish(bob, alice, seq1),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(1500));
            env.close();
        }
        // test Individual Freeze
        {
            // Env Setup
            Env env{*this, features};
            env.fund(XRP(10'000), alice, bob, gw);
            env(fset(gw, asfAllowTokenLocking));
            env.close();
            env(trust(alice, USD(100'000)));
            env(trust(bob, USD(100'000)));
            env.close();
            env(pay(gw, alice, USD(10'000)));
            env(pay(gw, bob, USD(10'000)));
            env.close();

            // set freeze on alice trustline
            env(trust(gw, USD(10'000), alice, tfSetFreeze));
            env.close();

            // setup transaction
            auto seq1 = env.seq(alice);
            auto const delta = USD(125);

            // create escrow fails - frozen trustline
            env(escrow::create(alice, bob, delta),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(1500),
                ter(tecFROZEN));
            env.close();

            // clear freeze on alice trustline
            env(trust(gw, USD(10'000), alice, tfClearFreeze));
            env.close();

            // create escrow success
            seq1 = env.seq(alice);
            env(escrow::create(alice, bob, delta),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(1500));
            env.close();

            // set freeze on bob trustline
            env(trust(gw, USD(10'000), bob, tfSetFreeze));
            env.close();

            // bob finish escrow success regardless of frozen assets
            env(escrow::finish(bob, alice, seq1),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(1500));
            env.close();
        }
        // TODO: Deep Freeze
    }
    void
    testIOUINSF(FeatureBitset features)
    {
        testcase("IOU Trustline Insuficient Funds");
        using namespace test::jtx;
        using namespace std::literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];
        {
            // test tecPATH_PARTIAL
            // ie. has 10'000, escrow 1'000 then try to pay 10'000
            Env env{*this, features};
            env.fund(XRP(10'000), alice, bob, gw);
            env(fset(gw, asfAllowTokenLocking));
            env.close();
            env.trust(USD(100'000), alice);
            env.trust(USD(100'000), bob);
            env.close();
            env(pay(gw, alice, USD(10'000)));
            env(pay(gw, bob, USD(10'000)));
            env.close();

            // create escrow success
            auto const delta = USD(1'000);
            env(escrow::create(alice, bob, delta),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(1500));
            env.close();
            env(pay(alice, gw, USD(10'000)), ter(tecPATH_PARTIAL));
        }
        {
            // test tecINSUFFICIENT_FUNDS
            // ie. has 10'000 escrow 1'000 then try to escrow 10'000
            Env env{*this, features};
            env.fund(XRP(10'000), alice, bob, gw);
            env(fset(gw, asfAllowTokenLocking));
            env.close();
            env.trust(USD(100'000), alice);
            env.trust(USD(100'000), bob);
            env.close();
            env(pay(gw, alice, USD(10'000)));
            env(pay(gw, bob, USD(10'000)));
            env.close();

            auto const delta = USD(1'000);
            env(escrow::create(alice, bob, delta),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(1500));
            env.close();

            env(escrow::create(alice, bob, USD(10'000)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(1500),
                ter(tecINSUFFICIENT_FUNDS));
            env.close();
        }
    }

    void
    testIOUPrecisionLoss(FeatureBitset features)
    {
        testcase("IOU Precision Loss");
        using namespace test::jtx;
        using namespace std::literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];

        // test min create precision loss
        {
            Env env(*this, features);
            env.fund(XRP(10'000), alice, bob, gw);
            env(fset(gw, asfAllowTokenLocking));
            env.close();
            env.trust(USD(100000000000000000), alice);
            env.trust(USD(100000000000000000), bob);
            env.close();
            env(pay(gw, alice, USD(10000000000000000)));
            env(pay(gw, bob, USD(1)));
            env.close();

            // alice cannot create escrow for 1/10 iou - precision loss
            env(escrow::create(alice, bob, USD(1)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(1500),
                ter(tecPRECISION_LOSS));
            env.close();

            auto const seq1 = env.seq(alice);
            // alice can create escrow for 1'000 iou
            env(escrow::create(alice, bob, USD(1'000)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(1500));
            env.close();

            // bob finish escrow success
            env(escrow::finish(bob, alice, seq1),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(1500));
            env.close();
        }
    }

    void
    testMPTEnablement(FeatureBitset features)
    {
        testcase("MPT Enablement");

        using namespace jtx;
        using namespace std::chrono;

        for (bool const withTokenEscrow : {false, true})
        {
            auto const amend =
                withTokenEscrow ? features : features - featureTokenEscrow;
            Env env{*this, amend};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");
            env.fund(XRP(5000), bob);

            MPTTester mptGw(env, gw, {.holders = {alice}});
            mptGw.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanEscrow | tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10'000)));
            env.close();

            auto const createResult =
                withTokenEscrow ? ter(tesSUCCESS) : ter(temDISABLED);
            auto const finishResult =
                withTokenEscrow ? ter(tesSUCCESS) : ter(tecNO_TARGET);

            auto const seq1 = env.seq(alice);
            env(escrow::create(alice, bob, MPT(1'000)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(1500),
                createResult);
            env.close();
            env(escrow::finish(bob, alice, seq1),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(1500),
                finishResult);
            env.close();
            auto const seq2 = env.seq(alice);
            env(escrow::create(alice, bob, MPT(1'000)),
                escrow::condition(escrow::cb2),
                escrow::finish_time(env.now() + 1s),
                escrow::cancel_time(env.now() + 2s),
                fee(1500),
                createResult);
            env.close();
            env(escrow::cancel(bob, alice, seq2), fee(1500), finishResult);
            env.close();
        }
    }

    void
    testMPTBalances(FeatureBitset features)
    {
        testcase("MPT Balances");

        using namespace jtx;
        using namespace std::chrono;

        Env env{*this, features};
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account("gw");
        env.fund(XRP(5000), bob);

        MPTTester mptGw(env, gw, {.holders = {alice, carol}});
        mptGw.create(
            {.ownerCount = 1,
             .holderCount = 0,
             .flags = tfMPTCanEscrow | tfMPTCanTransfer});
        mptGw.authorize({.account = alice});
        mptGw.authorize({.account = carol});
        auto const MPT = mptGw["MPT"];
        env(pay(gw, alice, MPT(10'000)));
        env(pay(gw, carol, MPT(10'000)));
        env.close();

        auto outstandingMPT = issuerMPTOutstanding(env, MPT);

        // Create & Finish Escrow
        auto const seq1 = env.seq(alice);
        {
            auto const preAliceMPT = mptBalance(env, alice, MPT);
            auto const preBobMPT = mptBalance(env, bob, MPT);
            env(escrow::create(alice, bob, MPT(1'000)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(1500),
                ter(tesSUCCESS));
            env.close();

            BEAST_EXPECT(mptBalance(env, alice, MPT) == preAliceMPT - 1'000);
            BEAST_EXPECT(mptEscrowed(env, alice, MPT) == 1'000);
            BEAST_EXPECT(mptBalance(env, bob, MPT) == preBobMPT);
            BEAST_EXPECT(mptEscrowed(env, bob, MPT) == 0);
            BEAST_EXPECT(issuerMPTOutstanding(env, MPT) == outstandingMPT);
            BEAST_EXPECT(issuerMPTEscrowed(env, MPT) == 1'000);
        }
        {
            auto const preAliceMPT = mptBalance(env, alice, MPT);
            auto const preBobMPT = mptBalance(env, bob, MPT);
            env(escrow::finish(bob, alice, seq1),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(1500),
                ter(tesSUCCESS));
            env.close();

            BEAST_EXPECT(mptBalance(env, alice, MPT) == preAliceMPT);
            BEAST_EXPECT(mptEscrowed(env, alice, MPT) == 0);
            BEAST_EXPECT(mptBalance(env, bob, MPT) == preBobMPT + 1'000);
            BEAST_EXPECT(mptEscrowed(env, bob, MPT) == 0);
            BEAST_EXPECT(issuerMPTOutstanding(env, MPT) == outstandingMPT);
            BEAST_EXPECT(issuerMPTEscrowed(env, MPT) == 0);
        }

        // Create & Cancel Escrow
        auto const seq2 = env.seq(alice);
        {
            auto const preAliceMPT = mptBalance(env, alice, MPT);
            auto const preBobMPT = mptBalance(env, bob, MPT);
            env(escrow::create(alice, bob, MPT(1'000)),
                escrow::condition(escrow::cb2),
                escrow::finish_time(env.now() + 1s),
                escrow::cancel_time(env.now() + 2s),
                fee(1500),
                ter(tesSUCCESS));
            env.close();

            BEAST_EXPECT(mptBalance(env, alice, MPT) == preAliceMPT - 1'000);
            BEAST_EXPECT(mptEscrowed(env, alice, MPT) == 1'000);
            BEAST_EXPECT(mptBalance(env, bob, MPT) == preBobMPT);
            BEAST_EXPECT(mptEscrowed(env, bob, MPT) == 0);
            BEAST_EXPECT(issuerMPTOutstanding(env, MPT) == outstandingMPT);
            BEAST_EXPECT(issuerMPTEscrowed(env, MPT) == 1'000);
        }
        {
            auto const preAliceMPT = mptBalance(env, alice, MPT);
            auto const preBobMPT = mptBalance(env, bob, MPT);
            env(escrow::cancel(bob, alice, seq2), fee(1500), ter(tesSUCCESS));
            env.close();

            BEAST_EXPECT(mptBalance(env, alice, MPT) == preAliceMPT + 1'000);
            BEAST_EXPECT(mptEscrowed(env, alice, MPT) == 0);
            BEAST_EXPECT(mptBalance(env, bob, MPT) == preBobMPT);
            BEAST_EXPECT(mptEscrowed(env, bob, MPT) == 0);
            BEAST_EXPECT(issuerMPTOutstanding(env, MPT) == outstandingMPT);
            BEAST_EXPECT(issuerMPTEscrowed(env, MPT) == 0);
        }

        // Multiple Escrows
        {
            auto const preAliceMPT = mptBalance(env, alice, MPT);
            auto const preBobMPT = mptBalance(env, bob, MPT);
            auto const preCarolMPT = mptBalance(env, carol, MPT);
            env(escrow::create(alice, bob, MPT(1'000)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(1500),
                ter(tesSUCCESS));
            env.close();

            env(escrow::create(carol, bob, MPT(1'000)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(1500),
                ter(tesSUCCESS));
            env.close();

            BEAST_EXPECT(mptBalance(env, alice, MPT) == preAliceMPT - 1'000);
            BEAST_EXPECT(mptEscrowed(env, alice, MPT) == 1'000);
            BEAST_EXPECT(mptBalance(env, bob, MPT) == preBobMPT);
            BEAST_EXPECT(mptEscrowed(env, bob, MPT) == 0);
            BEAST_EXPECT(mptBalance(env, carol, MPT) == preCarolMPT - 1'000);
            BEAST_EXPECT(mptEscrowed(env, carol, MPT) == 1'000);
            BEAST_EXPECT(issuerMPTOutstanding(env, MPT) == outstandingMPT);
            BEAST_EXPECT(issuerMPTEscrowed(env, MPT) == 2'000);
        }
    }

    void
    testMPTGateway(FeatureBitset features)
    {
        testcase("MPT Gateway");
        using namespace test::jtx;
        using namespace std::literals;

        // issuer is source; alice w/ authorization
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice}});
            mptGw.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanEscrow | tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10'000)));
            env.close();

            // issuer can create escrow
            auto const seq1 = env.seq(gw);
            auto const preAliceMPT = mptBalance(env, alice, MPT);
            auto const preOutstanding = issuerMPTOutstanding(env, MPT);
            auto const preEscrowed = issuerMPTEscrowed(env, MPT);
            BEAST_EXPECT(preOutstanding == 10'000);
            BEAST_EXPECT(preEscrowed == 0);

            env(escrow::create(gw, alice, MPT(1'000)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(1500));
            env.close();

            BEAST_EXPECT(mptBalance(env, alice, MPT) == preAliceMPT);
            BEAST_EXPECT(mptEscrowed(env, alice, MPT) == 0);
            BEAST_EXPECT(issuerMPTOutstanding(env, MPT) == preOutstanding);
            BEAST_EXPECT(issuerMPTEscrowed(env, MPT) == preEscrowed + 1'000);

            // alice (dest) can finish escrow
            env(escrow::finish(alice, gw, seq1),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(1500));
            env.close();

            BEAST_EXPECT(mptBalance(env, alice, MPT) == preAliceMPT + 1'000);
            BEAST_EXPECT(mptEscrowed(env, alice, MPT) == 0);
            BEAST_EXPECT(
                issuerMPTOutstanding(env, MPT) == preOutstanding + 1'000);
            BEAST_EXPECT(issuerMPTEscrowed(env, MPT) == preEscrowed);
        }

        // TODO
        // issuer is source; alice w/out authorization

        // issuer is dest; alice w/ authorization
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice}});
            mptGw.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanEscrow | tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10'000)));
            env.close();

            // issuer can create escrow
            auto const seq1 = env.seq(alice);
            auto const preAliceMPT = mptBalance(env, alice, MPT);
            auto const preOutstanding = issuerMPTOutstanding(env, MPT);
            auto const preEscrowed = issuerMPTEscrowed(env, MPT);
            BEAST_EXPECT(preOutstanding == 10'000);
            BEAST_EXPECT(preEscrowed == 0);

            env(escrow::create(alice, gw, MPT(1'000)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(1500));
            env.close();

            BEAST_EXPECT(mptBalance(env, alice, MPT) == preAliceMPT - 1'000);
            BEAST_EXPECT(mptEscrowed(env, alice, MPT) == 1'000);
            BEAST_EXPECT(issuerMPTOutstanding(env, MPT) == preOutstanding);
            BEAST_EXPECT(issuerMPTEscrowed(env, MPT) == preEscrowed + 1'000);

            // issuer (dest) can finish escrow
            env(escrow::finish(gw, alice, seq1),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(1500));
            env.close();

            BEAST_EXPECT(mptBalance(env, alice, MPT) == preAliceMPT - 1'000);
            BEAST_EXPECT(mptEscrowed(env, alice, MPT) == 0);
            BEAST_EXPECT(
                issuerMPTOutstanding(env, MPT) == preOutstanding - 1'000);
            BEAST_EXPECT(issuerMPTEscrowed(env, MPT) == preEscrowed);
        }
        // TODO
        // issuer is dest; alice w/out authorization

        // issuer is source and destination
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice}});
            mptGw.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanEscrow | tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10'000)));
            env.close();

            // issuer can create escrow
            auto const seq1 = env.seq(gw);
            auto const preOutstanding = issuerMPTOutstanding(env, MPT);
            auto const preEscrowed = issuerMPTEscrowed(env, MPT);
            BEAST_EXPECT(preOutstanding == 10'000);
            BEAST_EXPECT(preEscrowed == 0);

            env(escrow::create(gw, gw, MPT(1'000)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(1500));
            env.close();

            BEAST_EXPECT(issuerMPTOutstanding(env, MPT) == preOutstanding);
            BEAST_EXPECT(issuerMPTEscrowed(env, MPT) == preEscrowed + 1'000);

            // issuer (dest) can finish escrow
            env(escrow::finish(gw, gw, seq1),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(1500));
            env.close();

            BEAST_EXPECT(issuerMPTOutstanding(env, MPT) == preOutstanding);
            BEAST_EXPECT(issuerMPTEscrowed(env, MPT) == preEscrowed);
        }
    }

    void
    testIOUWithFeats(FeatureBitset features)
    {
        testIOUEnablement(features);
        // testIOUBalances(features);
        testIOUMetaAndOwnership(features);
        testIOURippleState(features);
        testIOUGateway(features);
        testIOULockedRate(features);
        testIOULimitAmount(features);
        testIOURequireAuth(features);
        testIOUFreeze(features);
        testIOUINSF(features);
        testIOUPrecisionLoss(features);
    }

    void
    testMPTWithFeats(FeatureBitset features)
    {
        testMPTEnablement(features);
        testMPTBalances(features);
        testMPTGateway(features);
        // testMPTLockedRate(features);
        // testMPTTLLimitAmount(features);
        // testMPTTLRequireAuth(features);
        // testMPTTLFreeze(features);
        // testMPTTLINSF(features);
        // testMPTPrecisionLoss(features);
    }

public:
    void
    run() override
    {
        using namespace test::jtx;
        FeatureBitset const all{supported_amendments()};
        testIOUWithFeats(all);
        testMPTWithFeats(all);
    }
};

BEAST_DEFINE_TESTSUITE(EscrowToken, app, ripple);

}  // namespace test
}  // namespace ripple