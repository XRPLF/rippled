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

            env(escrow::create(alice, gw, USD(1'000)),
                escrow::finish_time(env.now() + 1s));

            BEAST_EXPECT(
                (*env.meta())[sfTransactionResult] ==
                static_cast<std::uint8_t>(tesSUCCESS));
            env.close(5s);
            env(escrow::create(gw, carol, USD(1'000)),
                escrow::finish_time(env.now() + 1s),
                escrow::cancel_time(env.now() + 2s),
                ter(tecNO_PERMISSION));
            env.close(5s);

            auto const ag = env.le(keylet::escrow(alice.id(), aseq));
            BEAST_EXPECT(ag);

            {
                ripple::Dir aod(*env.current(), keylet::ownerDir(alice.id()));
                BEAST_EXPECT(std::distance(aod.begin(), aod.end()) == 2);
                BEAST_EXPECT(
                    std::find(aod.begin(), aod.end(), ag) != aod.end());

                ripple::Dir cod(*env.current(), keylet::ownerDir(carol.id()));
                BEAST_EXPECT(std::distance(cod.begin(), cod.end()) == 1);

                ripple::Dir iod(*env.current(), keylet::ownerDir(gw.id()));
                BEAST_EXPECT(std::distance(iod.begin(), iod.end()) == 3);
                BEAST_EXPECT(
                    std::find(iod.begin(), iod.end(), ag) != iod.end());
            }

            env.close(5s);
            env(escrow::finish(alice, alice, aseq));
            {
                BEAST_EXPECT(!env.le(keylet::escrow(alice.id(), aseq)));

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

        // issuer is source
        {
            auto const gw = Account{"gateway"};
            auto const alice = Account{"alice"};
            Env env{*this, features};
            auto const USD = gw["USD"];
            env.fund(XRP(5000), alice, gw);
            env(fset(gw, asfAllowTokenLocking));
            env.close();
            env.trust(USD(100'000), alice);
            env.close();

            env(pay(gw, alice, USD(10'000)));
            env.close();

            // issuer cannot create escrow
            env(escrow::create(gw, alice, USD(1'000)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(1500),
                ter(tecNO_PERMISSION));
            env.close();
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

        // issuer is destination
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

            // issuer cannot receive escrow
            env(escrow::create(gw, gw, USD(1'000)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(1500),
                ter(tecNO_PERMISSION));
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
    }

    void
    testIOULimitAmount(FeatureBitset features)
    {
        testcase("IOU Limit");
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
        testcase("IOU Require Auth");
        using namespace test::jtx;
        using namespace std::literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];

        auto const aliceUSD = alice["USD"];
        auto const bobUSD = bob["USD"];

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

    void
    testIOUFreeze(FeatureBitset features)
    {
        testcase("IOU Freeze");
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
        testcase("IOU Insuficient Funds");
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
    testMPTCreatePreflight(FeatureBitset features)
    {
        testcase("MPT Create Preflight");
        using namespace test::jtx;
        using namespace std::literals;

        for (bool const withMPT : {true, false})
        {
            auto const amend =
                withMPT ? features : features - featureMPTokensV1;
            Env env{*this, amend};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");
            env.fund(XRP(1'000), alice, bob, gw);

            Json::Value jv = escrow::create(alice, bob, XRP(1));
            jv.removeMember(jss::Amount);
            jv[jss::Amount][jss::mpt_issuance_id] =
                "00000004A407AF5856CCF3C42619DAA925813FC955C72983";
            jv[jss::Amount][jss::value] = "-1";

            auto const result = withMPT ? ter(temBAD_AMOUNT) : ter(temDISABLED);
            env(jv,
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(1500),
                result);
            env.close();
        }

        // temBAD_AMOUNT: maxMPTokenAmount
        // {
        //     Env env{*this, features};
        //     auto const alice = Account("alice");
        //     auto const bob = Account("bob");
        //     auto const gw = Account("gw");

        //     MPTTester mptGw(env, gw, {.holders = {alice, bob}});
        //     mptGw.create(
        //         {.ownerCount = 1,
        //             .holderCount = 0,
        //             .flags = tfMPTCanEscrow | tfMPTCanTransfer});
        //     mptGw.authorize({.account = alice});
        //     mptGw.authorize({.account = bob});
        //     auto const MPT = mptGw["MPT"];
        //     env(pay(gw, alice, MPT(10'000)));
        //     env(pay(gw, bob, MPT(10'000)));
        //     env.close();

        //     Json::Value jv = escrow::create(alice, bob, MPT(1));
        //     jv[jss::Amount][jss::value] = "9223372036854775807";
        //     env(jv,
        //         escrow::condition(escrow::cb1),
        //         escrow::finish_time(env.now() + 1s),
        //         fee(1500),
        //         ter(temBAD_AMOUNT));
        //     env.close();
        // }

        // temBAD_AMOUNT: amount < 0
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice, bob}});
            mptGw.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanEscrow | tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = bob});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10'000)));
            env(pay(gw, bob, MPT(10'000)));
            env.close();

            env(escrow::create(alice, bob, MPT(-1)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(1500),
                ter(temBAD_AMOUNT));
            env.close();
        }
    }

    void
    testMPTCreatePreclaim(FeatureBitset features)
    {
        testcase("MPT Create Preclaim");
        using namespace test::jtx;
        using namespace std::literals;

        // tecNO_PERMISSION: issuer is the same as the account
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

            env(escrow::create(gw, alice, MPT(1)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(1500),
                ter(tecNO_PERMISSION));
            env.close();
        }

        // tecOBJECT_NOT_FOUND: mpt does not exist
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");
            env.fund(XRP(10'000), alice, bob, gw);
            env.close();

            auto const mpt = ripple::test::jtx::MPT(
                alice.name(), makeMptID(env.seq(alice), alice));
            Json::Value jv = escrow::create(alice, bob, mpt(2));
            jv[jss::Amount][jss::mpt_issuance_id] =
                "00000004A407AF5856CCF3C42619DAA925813FC955C72983";
            env(jv,
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(1500),
                ter(tecOBJECT_NOT_FOUND));
            env.close();
        }

        // tecNO_PERMISSION: tfMPTCanEscrow is not enabled
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice, bob}});
            mptGw.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = bob});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10'000)));
            env(pay(gw, bob, MPT(10'000)));
            env.close();

            env(escrow::create(alice, bob, MPT(3)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(1500),
                ter(tecNO_PERMISSION));
            env.close();
        }

        // tecOBJECT_NOT_FOUND: account does not have the mpt
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice, bob}});
            mptGw.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanEscrow | tfMPTCanTransfer});
            auto const MPT = mptGw["MPT"];

            env(escrow::create(alice, bob, MPT(4)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(1500),
                ter(tecOBJECT_NOT_FOUND));
            env.close();
        }

        // tecNO_AUTH: requireAuthIfNeeded set: account not authorized
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice, bob}});
            mptGw.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags =
                     tfMPTCanEscrow | tfMPTCanTransfer | tfMPTRequireAuth});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = gw, .holder = alice});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10'000)));
            env.close();

            // unauthorize account
            mptGw.authorize(
                {.account = gw, .holder = alice, .flags = tfMPTUnauthorize});

            env(escrow::create(alice, bob, MPT(5)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(1500),
                ter(tecNO_AUTH));
            env.close();
        }

        // tecNO_AUTH: requireAuthIfNeeded set: dest not authorized
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice, bob}});
            mptGw.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags =
                     tfMPTCanEscrow | tfMPTCanTransfer | tfMPTRequireAuth});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = gw, .holder = alice});
            mptGw.authorize({.account = bob});
            mptGw.authorize({.account = gw, .holder = bob});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10'000)));
            env(pay(gw, bob, MPT(10'000)));
            env.close();

            // unauthorize dest
            mptGw.authorize(
                {.account = gw, .holder = bob, .flags = tfMPTUnauthorize});

            env(escrow::create(alice, bob, MPT(6)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(1500),
                ter(tecNO_AUTH));
            env.close();
        }

        // tecFROZEN: issuer has frozen the account
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice, bob}});
            mptGw.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanEscrow | tfMPTCanTransfer | tfMPTCanLock});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = bob});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10'000)));
            env(pay(gw, bob, MPT(10'000)));
            env.close();

            // lock/freeze account
            mptGw.set({.account = gw, .holder = alice, .flags = tfMPTLock});

            env(escrow::create(alice, bob, MPT(7)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(1500),
                ter(tecFROZEN));
            env.close();
        }

        // tecFROZEN: issuer has frozen the dest
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice, bob}});
            mptGw.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanEscrow | tfMPTCanTransfer | tfMPTCanLock});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = bob});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10'000)));
            env(pay(gw, bob, MPT(10'000)));
            env.close();

            // lock/freeze dest
            mptGw.set({.account = gw, .holder = bob, .flags = tfMPTLock});

            env(escrow::create(alice, bob, MPT(8)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(1500),
                ter(tecFROZEN));
            env.close();
        }

        // tecNO_AUTH: mpt cannot be transferred
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice, bob}});
            mptGw.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanEscrow});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = bob});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10'000)));
            env(pay(gw, bob, MPT(10'000)));
            env.close();

            env(escrow::create(alice, bob, MPT(9)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(1500),
                ter(tecNO_AUTH));
            env.close();
        }

        // tecINSUFFICIENT_FUNDS: spendable amount is less than the amount
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice, bob}});
            mptGw.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanEscrow | tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = bob});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10)));
            env(pay(gw, bob, MPT(10)));
            env.close();

            env(escrow::create(alice, bob, MPT(11)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(1500),
                ter(tecINSUFFICIENT_FUNDS));
            env.close();
        }
    }

    void
    testMPTFinishPreclaim(FeatureBitset features)
    {
        testcase("MPT Finish Preclaim");
        using namespace test::jtx;
        using namespace std::literals;

        // tecNO_AUTH: requireAuthIfNeeded set: dest not authorized
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice, bob}});
            mptGw.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags =
                     tfMPTCanEscrow | tfMPTCanTransfer | tfMPTRequireAuth});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = gw, .holder = alice});
            mptGw.authorize({.account = bob});
            mptGw.authorize({.account = gw, .holder = bob});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10'000)));
            env(pay(gw, bob, MPT(10'000)));
            env.close();

            auto const seq1 = env.seq(alice);
            env(escrow::create(alice, bob, MPT(10)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(1500),
                ter(tesSUCCESS));
            env.close();

            // unauthorize dest
            mptGw.authorize(
                {.account = gw, .holder = bob, .flags = tfMPTUnauthorize});

            env(escrow::finish(bob, alice, seq1),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(1500),
                ter(tecNO_AUTH));
            env.close();
        }

        // tecFROZEN: issuer has frozen the dest
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice, bob}});
            mptGw.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanEscrow | tfMPTCanTransfer | tfMPTCanLock});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = bob});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10'000)));
            env(pay(gw, bob, MPT(10'000)));
            env.close();

            auto const seq1 = env.seq(alice);
            env(escrow::create(alice, bob, MPT(8)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(1500),
                ter(tesSUCCESS));
            env.close();

            // lock/freeze dest
            mptGw.set({.account = gw, .holder = bob, .flags = tfMPTLock});

            env(escrow::finish(bob, alice, seq1),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(1500),
                ter(tecFROZEN));
            env.close();
        }
    }

    void
    testMPTFinishCreateAsset(FeatureBitset features)
    {
        testcase("MPT Finish Create Asset");
        using namespace test::jtx;
        using namespace std::literals;

        // tecINSUFFICIENT_RESERVE: insufficient reserve to create MPT
        {
            Env env{*this, features};
            auto const acctReserve = env.current()->fees().accountReserve(0);
            auto const incReserve = env.current()->fees().increment;

            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");
            env.fund(acctReserve + (incReserve - 1), bob);
            env.close();

            MPTTester mptGw(env, gw, {.holders = {alice}});
            mptGw.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanEscrow | tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10'000)));
            env.close();

            auto const seq1 = env.seq(alice);
            env(escrow::create(alice, bob, MPT(10)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(1500),
                ter(tesSUCCESS));
            env.close();

            env(escrow::finish(bob, alice, seq1),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(1500),
                ter(tecINSUFFICIENT_RESERVE));
            env.close();
        }

        // tesSUCCESS: bob submits; finish MPT created
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");
            env.fund(XRP(10'000), bob);
            env.close();

            MPTTester mptGw(env, gw, {.holders = {alice}});
            mptGw.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanEscrow | tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10'000)));
            env.close();

            auto const seq1 = env.seq(alice);
            env(escrow::create(alice, bob, MPT(10)),
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
        }

        // tecNO_PERMISSION: not bob submits; finish MPT not created
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const carol = Account("carol");
            auto const gw = Account("gw");
            env.fund(XRP(10'000), bob, carol);
            env.close();

            MPTTester mptGw(env, gw, {.holders = {alice}});
            mptGw.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanEscrow | tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10'000)));
            env.close();

            auto const seq1 = env.seq(alice);
            env(escrow::create(alice, bob, MPT(10)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(1500),
                ter(tesSUCCESS));
            env.close();

            env(escrow::finish(carol, alice, seq1),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(1500),
                ter(tecNO_PERMISSION));
            env.close();
        }
    }

    void
    testMPTCancelPreclaim(FeatureBitset features)
    {
        testcase("MPT Cancel Preclaim");
        using namespace test::jtx;
        using namespace std::literals;

        // tecNO_AUTH: requireAuthIfNeeded set: account not authorized
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice, bob}});
            mptGw.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags =
                     tfMPTCanEscrow | tfMPTCanTransfer | tfMPTRequireAuth});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = gw, .holder = alice});
            mptGw.authorize({.account = bob});
            mptGw.authorize({.account = gw, .holder = bob});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10'000)));
            env(pay(gw, bob, MPT(10'000)));
            env.close();

            auto const seq1 = env.seq(alice);
            env(escrow::create(alice, bob, MPT(10)),
                escrow::cancel_time(env.now() + 2s),
                escrow::condition(escrow::cb1),
                fee(1500),
                ter(tesSUCCESS));
            env.close();

            // unauthorize account
            mptGw.authorize(
                {.account = gw, .holder = alice, .flags = tfMPTUnauthorize});

            env(escrow::cancel(bob, alice, seq1), ter(tecNO_AUTH));
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
    testMPTMetaAndOwnership(FeatureBitset features)
    {
        using namespace jtx;
        using namespace std::chrono;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account{"gateway"};
        {
            testcase("MPT Metadata to self");

            Env env{*this, features};
            MPTTester mptGw(env, gw, {.holders = {alice, bob}});
            mptGw.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanEscrow | tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = bob});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10'000)));
            env(pay(gw, bob, MPT(10'000)));
            env.close();
            auto const aseq = env.seq(alice);
            auto const bseq = env.seq(bob);

            env(escrow::create(alice, alice, MPT(1'000)),
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
                BEAST_EXPECT(std::distance(iod.begin(), iod.end()) == 2);
                BEAST_EXPECT(
                    std::find(iod.begin(), iod.end(), aa) != iod.end());
            }

            env(escrow::create(bob, bob, MPT(1'000)),
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
                BEAST_EXPECT(std::distance(iod.begin(), iod.end()) == 3);
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
                BEAST_EXPECT(std::distance(iod.begin(), iod.end()) == 2);
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
                BEAST_EXPECT(std::distance(iod.begin(), iod.end()) == 1);
                BEAST_EXPECT(
                    std::find(iod.begin(), iod.end(), bb) == iod.end());
            }
        }

        {
            testcase("MPT Metadata to other");

            Env env{*this, features};
            MPTTester mptGw(env, gw, {.holders = {alice, bob, carol}});
            mptGw.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanEscrow | tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = bob});
            mptGw.authorize({.account = carol});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10'000)));
            env(pay(gw, bob, MPT(10'000)));
            env(pay(gw, carol, MPT(10'000)));
            env.close();
            auto const aseq = env.seq(alice);
            auto const bseq = env.seq(bob);

            env(escrow::create(alice, bob, MPT(1'000)),
                escrow::finish_time(env.now() + 1s));
            BEAST_EXPECT(
                (*env.meta())[sfTransactionResult] ==
                static_cast<std::uint8_t>(tesSUCCESS));
            env.close(5s);
            env(escrow::create(bob, carol, MPT(1'000)),
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
                BEAST_EXPECT(std::distance(iod.begin(), iod.end()) == 3);
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
                BEAST_EXPECT(std::distance(iod.begin(), iod.end()) == 2);
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
                BEAST_EXPECT(std::distance(iod.begin(), iod.end()) == 1);
                BEAST_EXPECT(
                    std::find(iod.begin(), iod.end(), ab) == iod.end());
                BEAST_EXPECT(
                    std::find(iod.begin(), iod.end(), bc) == iod.end());
            }
        }

        {
            testcase("MPT Metadata to issuer");

            Env env{*this, features};
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
            auto const aseq = env.seq(alice);

            env(escrow::create(alice, gw, MPT(1'000)),
                escrow::finish_time(env.now() + 1s));

            BEAST_EXPECT(
                (*env.meta())[sfTransactionResult] ==
                static_cast<std::uint8_t>(tesSUCCESS));
            env.close(5s);
            env(escrow::create(gw, carol, MPT(1'000)),
                escrow::finish_time(env.now() + 1s),
                escrow::cancel_time(env.now() + 2s),
                ter(tecNO_PERMISSION));
            env.close(5s);

            auto const ag = env.le(keylet::escrow(alice.id(), aseq));
            BEAST_EXPECT(ag);

            {
                ripple::Dir aod(*env.current(), keylet::ownerDir(alice.id()));
                BEAST_EXPECT(std::distance(aod.begin(), aod.end()) == 2);
                BEAST_EXPECT(
                    std::find(aod.begin(), aod.end(), ag) != aod.end());

                ripple::Dir cod(*env.current(), keylet::ownerDir(carol.id()));
                BEAST_EXPECT(std::distance(cod.begin(), cod.end()) == 1);

                ripple::Dir iod(*env.current(), keylet::ownerDir(gw.id()));
                BEAST_EXPECT(std::distance(iod.begin(), iod.end()) == 2);
                BEAST_EXPECT(
                    std::find(iod.begin(), iod.end(), ag) != iod.end());
            }

            env.close(5s);
            env(escrow::finish(alice, alice, aseq));
            {
                BEAST_EXPECT(!env.le(keylet::escrow(alice.id(), aseq)));

                ripple::Dir aod(*env.current(), keylet::ownerDir(alice.id()));
                BEAST_EXPECT(std::distance(aod.begin(), aod.end()) == 1);
                BEAST_EXPECT(
                    std::find(aod.begin(), aod.end(), ag) == aod.end());

                ripple::Dir cod(*env.current(), keylet::ownerDir(carol.id()));
                BEAST_EXPECT(std::distance(cod.begin(), cod.end()) == 1);

                ripple::Dir iod(*env.current(), keylet::ownerDir(gw.id()));
                BEAST_EXPECT(std::distance(iod.begin(), iod.end()) == 1);
                BEAST_EXPECT(
                    std::find(iod.begin(), iod.end(), ag) == iod.end());
            }
        }
    }

    void
    testMPTGateway(FeatureBitset features)
    {
        testcase("MPT Gateway Balances");
        using namespace test::jtx;
        using namespace std::literals;

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

            // issuer can be destination
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
    }

    void
    testMPTLockedRate(FeatureBitset features)
    {
        testcase("MPT Locked Rate");
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
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice, bob}});
            mptGw.create(
                {.ownerCount = 1,
                 .transferFee = 25000,
                 .holderCount = 0,
                 .flags = tfMPTCanEscrow | tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = bob});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10'000)));
            env(pay(gw, bob, MPT(10'000)));
            env.close();

            // alice can create escrow w/ xfer rate
            auto const preAlice = mptBalance(env, alice, MPT);
            auto const seq1 = env.seq(alice);
            auto const delta = MPT(125);
            env(escrow::create(alice, bob, MPT(125)),
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

            BEAST_EXPECT(
                mptBalance(env, alice, MPT) ==
                preAlice - delta.value().value());
            BEAST_EXPECT(mptBalance(env, bob, MPT) == 10'100);
        }
    }

    void
    testMPTRequireAuth(FeatureBitset features)
    {
        testcase("MPT Require Auth");
        using namespace test::jtx;
        using namespace std::literals;

        Env env{*this, features};
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gw");

        MPTTester mptGw(env, gw, {.holders = {alice, bob}});
        mptGw.create(
            {.ownerCount = 1,
             .holderCount = 0,
             .flags = tfMPTCanEscrow | tfMPTCanTransfer | tfMPTRequireAuth});
        mptGw.authorize({.account = alice});
        mptGw.authorize({.account = gw, .holder = alice});
        mptGw.authorize({.account = bob});
        mptGw.authorize({.account = gw, .holder = bob});
        auto const MPT = mptGw["MPT"];
        env(pay(gw, alice, MPT(10'000)));
        env.close();

        // alice cannot create escrow - fails without auth
        auto seq1 = env.seq(alice);
        auto const delta = MPT(125);
        env(escrow::create(alice, bob, MPT(100)),
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

    void
    testMPTFreeze(FeatureBitset features)
    {
        testcase("MPT Freeze");
        using namespace test::jtx;
        using namespace std::literals;

        Env env{*this, features};
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gw");

        MPTTester mptGw(env, gw, {.holders = {alice, bob}});
        mptGw.create(
            {.ownerCount = 1,
             .holderCount = 0,
             .flags = tfMPTCanEscrow | tfMPTCanTransfer | tfMPTCanLock});
        mptGw.authorize({.account = alice});
        mptGw.authorize({.account = bob});
        auto const MPT = mptGw["MPT"];
        env(pay(gw, alice, MPT(10'000)));
        env(pay(gw, bob, MPT(10'000)));
        env.close();

        // alice create escrow
        auto seq1 = env.seq(alice);
        env(escrow::create(alice, bob, MPT(100)),
            escrow::condition(escrow::cb1),
            escrow::finish_time(env.now() + 1s),
            escrow::cancel_time(env.now() + 2s),
            fee(1500));
        env.close();

        // lock/freeze account & dest
        mptGw.set({.account = gw, .holder = alice, .flags = tfMPTLock});
        mptGw.set({.account = gw, .holder = bob, .flags = tfMPTLock});

        // bob cannot finish
        env(escrow::finish(bob, alice, seq1),
            escrow::condition(escrow::cb1),
            escrow::fulfillment(escrow::fb1),
            fee(1500),
            ter(tecFROZEN));
        env.close();

        // bob can cancel
        env(escrow::cancel(bob, alice, seq1));
        env.close();
    }

    void
    testMPTDestroy(FeatureBitset features)
    {
        testcase("MPT Destroy");
        using namespace test::jtx;
        using namespace std::literals;

        // tecHAS_OBLIGATIONS: issuer cannot destroy issuance
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice, bob}});
            mptGw.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanEscrow | tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = bob});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10'000)));
            env(pay(gw, bob, MPT(10'000)));
            env.close();

            auto const seq1 = env.seq(alice);
            env(escrow::create(alice, bob, MPT(10)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(1500));
            env.close();

            env(pay(alice, gw, MPT(9'990)));
            env(pay(bob, gw, MPT(10'000)));
            BEAST_EXPECT(mptBalance(env, alice, MPT) == 0);
            BEAST_EXPECT(mptEscrowed(env, alice, MPT) == 10);
            BEAST_EXPECT(mptBalance(env, bob, MPT) == 0);
            BEAST_EXPECT(mptEscrowed(env, bob, MPT) == 0);
            mptGw.authorize({.account = bob, .flags = tfMPTUnauthorize});
            mptGw.destroy(
                {.id = mptGw.issuanceID(),
                 .ownerCount = 1,
                 .err = tecHAS_OBLIGATIONS});

            env(escrow::finish(bob, alice, seq1),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(1500),
                ter(tesSUCCESS));
            env.close();

            env(pay(bob, gw, MPT(10)));
            mptGw.destroy({.id = mptGw.issuanceID(), .ownerCount = 0});
        }

        // tecHAS_OBLIGATIONS: holder cannot destroy mptoken
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");
            env.fund(XRP(10'000), bob);
            env.close();

            MPTTester mptGw(env, gw, {.holders = {alice}});
            mptGw.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanEscrow | tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10'000)));
            env.close();

            auto const seq1 = env.seq(alice);
            env(escrow::create(alice, bob, MPT(10)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(1500),
                ter(tesSUCCESS));
            env.close();

            env(pay(alice, gw, MPT(9'990)));
            env.close();

            BEAST_EXPECT(mptBalance(env, alice, MPT) == 0);
            BEAST_EXPECT(mptEscrowed(env, alice, MPT) == 10);
            mptGw.authorize(
                {.account = alice,
                 .flags = tfMPTUnauthorize,
                 .err = tecHAS_OBLIGATIONS});

            env(escrow::finish(bob, alice, seq1),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(1500),
                ter(tesSUCCESS));
            env.close();

            BEAST_EXPECT(mptBalance(env, alice, MPT) == 0);
            BEAST_EXPECT(mptEscrowed(env, alice, MPT) == 0);
            mptGw.authorize({.account = alice, .flags = tfMPTUnauthorize});
            BEAST_EXPECT(!env.le(keylet::mptoken(MPT.mpt(), alice)));
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
        testMPTCreatePreflight(features);
        testMPTCreatePreclaim(features);
        testMPTFinishPreclaim(features);
        testMPTFinishCreateAsset(features);
        testMPTCancelPreclaim(features);
        testMPTBalances(features);
        testMPTMetaAndOwnership(features);
        testMPTGateway(features);
        testMPTLockedRate(features);
        testMPTRequireAuth(features);
        testMPTFreeze(features);
        testMPTDestroy(features);
    }

public:
    void
    run() override
    {
        using namespace test::jtx;
        FeatureBitset const all{supported_amendments()};
        // testIOUWithFeats(all);
        testMPTWithFeats(all);
    }
};

BEAST_DEFINE_TESTSUITE(EscrowToken, app, ripple);

}  // namespace test
}  // namespace ripple