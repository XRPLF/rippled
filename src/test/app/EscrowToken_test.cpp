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
    // A PreimageSha256 fulfillments and its associated condition.
    std::array<std::uint8_t, 4> const fb1 = {{0xA0, 0x02, 0x80, 0x00}};

    std::array<std::uint8_t, 39> const cb1 = {
        {0xA0, 0x25, 0x80, 0x20, 0xE3, 0xB0, 0xC4, 0x42, 0x98, 0xFC,
         0x1C, 0x14, 0x9A, 0xFB, 0xF4, 0xC8, 0x99, 0x6F, 0xB9, 0x24,
         0x27, 0xAE, 0x41, 0xE4, 0x64, 0x9B, 0x93, 0x4C, 0xA4, 0x95,
         0x99, 0x1B, 0x78, 0x52, 0xB8, 0x55, 0x81, 0x01, 0x00}};

    // Another PreimageSha256 fulfillments and its associated condition.
    std::array<std::uint8_t, 7> const fb2 = {
        {0xA0, 0x05, 0x80, 0x03, 0x61, 0x61, 0x61}};

    std::array<std::uint8_t, 39> const cb2 = {
        {0xA0, 0x25, 0x80, 0x20, 0x98, 0x34, 0x87, 0x6D, 0xCF, 0xB0,
         0x5C, 0xB1, 0x67, 0xA5, 0xC2, 0x49, 0x53, 0xEB, 0xA5, 0x8C,
         0x4A, 0xC8, 0x9B, 0x1A, 0xDF, 0x57, 0xF2, 0x8F, 0x2F, 0x9D,
         0x09, 0xAF, 0x10, 0x7E, 0xE8, 0xF0, 0x81, 0x01, 0x03}};

    // Another PreimageSha256 fulfillment and its associated condition.
    std::array<std::uint8_t, 8> const fb3 = {
        {0xA0, 0x06, 0x80, 0x04, 0x6E, 0x69, 0x6B, 0x62}};

    std::array<std::uint8_t, 39> const cb3 = {
        {0xA0, 0x25, 0x80, 0x20, 0x6E, 0x4C, 0x71, 0x45, 0x30, 0xC0,
         0xA4, 0x26, 0x8B, 0x3F, 0xA6, 0x3B, 0x1B, 0x60, 0x6F, 0x2D,
         0x26, 0x4A, 0x2D, 0x85, 0x7B, 0xE8, 0xA0, 0x9C, 0x1D, 0xFD,
         0x57, 0x0D, 0x15, 0x85, 0x8B, 0xD4, 0x81, 0x01, 0x04}};

    /** Set the "FinishAfter" time tag on a JTx */
    struct finish_time
    {
    private:
        NetClock::time_point value_;

    public:
        explicit finish_time(NetClock::time_point const& value) : value_(value)
        {
        }

        void
        operator()(jtx::Env&, jtx::JTx& jt) const
        {
            jt.jv[sfFinishAfter.jsonName] = value_.time_since_epoch().count();
        }
    };

    /** Set the "CancelAfter" time tag on a JTx */
    struct cancel_time
    {
    private:
        NetClock::time_point value_;

    public:
        explicit cancel_time(NetClock::time_point const& value) : value_(value)
        {
        }

        void
        operator()(jtx::Env&, jtx::JTx& jt) const
        {
            jt.jv[sfCancelAfter.jsonName] = value_.time_since_epoch().count();
        }
    };

    struct condition
    {
    private:
        std::string value_;

    public:
        explicit condition(Slice cond) : value_(strHex(cond))
        {
        }

        template <size_t N>
        explicit condition(std::array<std::uint8_t, N> c)
            : condition(makeSlice(c))
        {
        }

        void
        operator()(jtx::Env&, jtx::JTx& jt) const
        {
            jt.jv[sfCondition.jsonName] = value_;
        }
    };

    struct fulfillment
    {
    private:
        std::string value_;

    public:
        explicit fulfillment(Slice condition) : value_(strHex(condition))
        {
        }

        template <size_t N>
        explicit fulfillment(std::array<std::uint8_t, N> f)
            : fulfillment(makeSlice(f))
        {
        }

        void
        operator()(jtx::Env&, jtx::JTx& jt) const
        {
            jt.jv[sfFulfillment.jsonName] = value_;
        }
    };

    static Json::Value
    escrow(
        jtx::Account const& account,
        jtx::Account const& to,
        STAmount const& amount)
    {
        using namespace jtx;
        Json::Value jv;
        jv[jss::TransactionType] = jss::EscrowCreate;
        jv[jss::Flags] = tfUniversal;
        jv[jss::Account] = account.human();
        jv[jss::Destination] = to.human();
        jv[jss::Amount] = amount.getJson(JsonOptions::none);
        return jv;
    }

    static Json::Value
    finish(
        jtx::Account const& account,
        jtx::Account const& from,
        std::uint32_t seq)
    {
        Json::Value jv;
        jv[jss::TransactionType] = jss::EscrowFinish;
        jv[jss::Flags] = tfUniversal;
        jv[jss::Account] = account.human();
        jv[sfOwner.jsonName] = from.human();
        jv[sfOfferSequence.jsonName] = seq;
        return jv;
    }

    static Json::Value
    cancel(
        jtx::Account const& account,
        jtx::Account const& from,
        std::uint32_t seq)
    {
        Json::Value jv;
        jv[jss::TransactionType] = jss::EscrowCancel;
        jv[jss::Flags] = tfUniversal;
        jv[jss::Account] = account.human();
        jv[sfOwner.jsonName] = from.human();
        jv[sfOfferSequence.jsonName] = seq;
        return jv;
    }

    static Rate
    escrowRate(
        jtx::Env const& env,
        jtx::Account const& account,
        uint32_t const& seq)
    {
        auto const sle = env.le(keylet::escrow(account.id(), seq));
        if (sle->isFieldPresent(sfTransferRate))
            return ripple::Rate((*sle)[sfTransferRate]);
        return Rate{0};
    }

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
            env.trust(USD(10000), alice, bob);
            env.close();
            env(pay(gw, alice, USD(5000)));
            env(pay(gw, bob, USD(5000)));
            env.close();

            auto const createResult =
                withTokenEscrow ? ter(tesSUCCESS) : ter(temDISABLED);
            auto const finishResult =
                withTokenEscrow ? ter(tesSUCCESS) : ter(tecNO_TARGET);
            env(escrow(alice, bob, USD(1000)),
                finish_time(env.now() + 1s),
                createResult);
            env.close();

            auto const seq1 = env.seq(alice);

            env(escrow(alice, bob, USD(1000)),
                condition(cb1),
                finish_time(env.now() + 1s),
                fee(1500),
                createResult);
            env.close();
            env(finish(bob, alice, seq1),
                condition(cb1),
                fulfillment(fb1),
                fee(1500),
                finishResult);

            auto const seq2 = env.seq(alice);

            env(escrow(alice, bob, USD(1000)),
                condition(cb2),
                finish_time(env.now() + 1s),
                cancel_time(env.now() + 2s),
                fee(1500),
                createResult);
            env.close();
            env(cancel(bob, alice, seq2), fee(1500), finishResult);
        }
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
            env.trust(USD(10000), alice, bob, carol);
            env.close();
            env(pay(gw, alice, USD(5000)));
            env(pay(gw, bob, USD(5000)));
            env(pay(gw, carol, USD(5000)));
            env.close();
            auto const aseq = env.seq(alice);
            auto const bseq = env.seq(bob);

            env(escrow(alice, alice, USD(1000)),
                finish_time(env.now() + 1s),
                cancel_time(env.now() + 500s));
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

            env(escrow(bob, bob, USD(1000)),
                finish_time(env.now() + 1s),
                cancel_time(env.now() + 2s));
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
            env(finish(alice, alice, aseq));
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
            env(cancel(bob, bob, bseq));
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
            env.trust(USD(10000), alice, bob, carol);
            env.close();
            env(pay(gw, alice, USD(5000)));
            env(pay(gw, bob, USD(5000)));
            env(pay(gw, carol, USD(5000)));
            env.close();
            auto const aseq = env.seq(alice);
            auto const bseq = env.seq(bob);

            env(escrow(alice, bob, USD(1000)), finish_time(env.now() + 1s));
            BEAST_EXPECT(
                (*env.meta())[sfTransactionResult] ==
                static_cast<std::uint8_t>(tesSUCCESS));
            env.close(5s);
            env(escrow(bob, carol, USD(1000)),
                finish_time(env.now() + 1s),
                cancel_time(env.now() + 2s));
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
            env(finish(alice, alice, aseq));
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
            env(cancel(bob, bob, bseq));
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
            env.trust(USD(10000), alice, carol);
            env.close();
            env(pay(gw, alice, USD(5000)));
            env(pay(gw, carol, USD(5000)));
            env.close();
            auto const aseq = env.seq(alice);
            auto const gseq = env.seq(gw);

            env(escrow(alice, gw, USD(1000)), finish_time(env.now() + 1s));

            BEAST_EXPECT(
                (*env.meta())[sfTransactionResult] ==
                static_cast<std::uint8_t>(tesSUCCESS));
            env.close(5s);
            env(escrow(gw, carol, USD(1000)),
                finish_time(env.now() + 1s),
                cancel_time(env.now() + 2s));
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
            env(finish(alice, alice, aseq));
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
            env(cancel(gw, gw, gseq));
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
            // // dst > src && dst > issuer && dst no trustline
            {Account("dan1"), Account("alice2"), Account{"gw0"}, false, true},
            // // dst < src && dst < issuer && dst no trustline
            {Account("bob0"), Account("carol0"), Account{"gw1"}, false, false},
            // // src > dst && src > issuer && dst has trustline
            {Account("alice2"), Account("bob0"), Account{"gw0"}, true, true},
            // // src < dst && src < issuer && dst has trustline
            {Account("carol0"), Account("dan1"), Account{"gw1"}, true, false},
            // // dst > src && dst > issuer && dst has trustline
            {Account("dan1"), Account("alice2"), Account{"gw0"}, true, true},
            // // dst < src && dst < issuer && dst has trustline
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
                env.trust(USD(100000), t.src, t.dst);
            else
                env.trust(USD(100000), t.src);
            env.close();

            env(pay(t.gw, t.src, USD(10000)));
            if (t.hasTrustline)
                env(pay(t.gw, t.dst, USD(10000)));
            env.close();

            // src can create escrow
            auto const seq1 = env.seq(t.src);
            auto const delta = USD(1000);
            env(escrow(t.src, t.dst, delta),
                condition(cb1),
                finish_time(env.now() + 1s),
                fee(1500));
            env.close();

            // dst can finish escrow
            auto const preSrc = lineBalance(env, t.src, t.gw, USD);
            auto const preDst = lineBalance(env, t.dst, t.gw, USD);

            env(finish(t.dst, t.src, seq1),
                condition(cb1),
                fulfillment(fb1),
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
            // // src < dst && src < issuer && dst no trustline
            {Account("gw1"), Account{"carol0"}, false, false},
            // // // // // dst > src && dst > issuer && dst no trustline
            {Account("gw0"), Account{"dan1"}, false, true},
            // // // // // dst < src && dst < issuer && dst no trustline
            {Account("gw1"), Account{"bob0"}, false, false},
            // // // // src > dst && src > issuer && dst has trustline
            {Account("gw0"), Account{"alice2"}, true, true},
            // // // // src < dst && src < issuer && dst has trustline
            {Account("gw1"), Account{"carol0"}, true, false},
            // // // // dst > src && dst > issuer && dst has trustline
            {Account("gw0"), Account{"dan1"}, true, true},
            // // // // dst < src && dst < issuer && dst has trustline
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
                env.trust(USD(100000), t.dst);

            env.close();

            if (t.hasTrustline)
                env(pay(t.src, t.dst, USD(10000)));

            env.close();

            // issuer can create escrow
            auto const seq1 = env.seq(t.src);
            auto const preDst = lineBalance(env, t.dst, t.src, USD);
            env(escrow(t.src, t.dst, USD(1000)),
                condition(cb1),
                finish_time(env.now() + 1s),
                fee(1500));
            env.close();

            // src can finish escrow, no dest trustline
            env(finish(t.dst, t.src, seq1),
                condition(cb1),
                fulfillment(fb1),
                fee(1500));
            env.close();
            auto const preAmount = t.hasTrustline ? 10000 : 0;
            BEAST_EXPECT(
                preDst == (t.negative ? -USD(preAmount) : USD(preAmount)));
            auto const postAmount = t.hasTrustline ? 11000 : 1000;
            BEAST_EXPECT(
                lineBalance(env, t.dst, t.src, USD) ==
                (t.negative ? -USD(postAmount) : USD(postAmount)));
            BEAST_EXPECT(lineBalance(env, t.src, t.src, USD) == USD(0));
        }

        std::array<TestAccountData, 4> gwDstTests = {{
            // // // // src > dst && src > issuer && dst has trustline
            {Account("alice2"), Account{"gw0"}, true, true},
            // // // // src < dst && src < issuer && dst has trustline
            {Account("carol0"), Account{"gw1"}, true, false},
            // // // // dst > src && dst > issuer && dst has trustline
            {Account("dan1"), Account{"gw0"}, true, true},
            // // // // dst < src && dst < issuer && dst has trustline
            {Account("bob0"), Account{"gw1"}, true, false},
        }};

        for (auto const& t : gwDstTests)
        {
            Env env{*this, features};
            auto const USD = t.dst["USD"];
            env.fund(XRP(5000), t.dst, t.src);
            env(fset(t.dst, asfAllowTokenLocking));
            env.close();

            env.trust(USD(100000), t.src);
            env.close();

            env(pay(t.dst, t.src, USD(10000)));
            env.close();

            // issuer can receive escrow
            auto const seq1 = env.seq(t.src);
            auto const preSrc = lineBalance(env, t.src, t.dst, USD);
            env(escrow(t.src, t.dst, USD(1000)),
                condition(cb1),
                finish_time(env.now() + 1s),
                fee(1500));
            env.close();

            // issuer can finish escrow, no dest trustline
            env(finish(t.dst, t.src, seq1),
                condition(cb1),
                fulfillment(fb1),
                fee(1500));
            env.close();
            auto const preAmount = 10000;
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
            env(escrow(gw, gw, USD(1000)),
                condition(cb1),
                finish_time(env.now() + 1s),
                fee(1500));
            env.close();

            // issuer can finish escrow
            env(finish(gw, gw, seq1),
                condition(cb1),
                fulfillment(fb1),
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
            env.fund(XRP(10000), alice, bob, gw);
            env(fset(gw, asfAllowTokenLocking));
            env(rate(gw, 1.25));
            env.close();
            env.trust(USD(100000), alice);
            env.trust(USD(100000), bob);
            env.close();
            env(pay(gw, alice, USD(10000)));
            env(pay(gw, bob, USD(10000)));
            env.close();

            // alice can create escrow w/ xfer rate
            auto const preAlice = env.balance(alice, USD);
            auto const seq1 = env.seq(alice);
            auto const delta = USD(125);
            env(escrow(alice, bob, delta),
                condition(cb1),
                finish_time(env.now() + 1s),
                fee(1500));
            env.close();
            auto const transferRate = escrowRate(env, alice, seq1);
            BEAST_EXPECT(
                transferRate.value == std::uint32_t(1000000000 * 1.25));

            // bob can finish escrow
            env(finish(bob, alice, seq1),
                condition(cb1),
                fulfillment(fb1),
                fee(1500));
            env.close();

            BEAST_EXPECT(env.balance(alice, USD) == preAlice - delta);
            BEAST_EXPECT(env.balance(bob, USD) == USD(10100));
        }
        // test rate change - higher
        {
            Env env{*this, features};
            env.fund(XRP(10000), alice, bob, gw);
            env(fset(gw, asfAllowTokenLocking));
            env(rate(gw, 1.25));
            env.close();
            env.trust(USD(100000), alice);
            env.trust(USD(100000), bob);
            env.close();
            env(pay(gw, alice, USD(10000)));
            env(pay(gw, bob, USD(10000)));
            env.close();

            // alice can create escrow w/ xfer rate
            auto const preAlice = env.balance(alice, USD);
            auto const seq1 = env.seq(alice);
            auto const delta = USD(125);
            env(escrow(alice, bob, delta),
                condition(cb1),
                finish_time(env.now() + 1s),
                fee(1500));
            env.close();
            auto transferRate = escrowRate(env, alice, seq1);
            BEAST_EXPECT(
                transferRate.value == std::uint32_t(1000000000 * 1.25));

            // issuer changes rate higher
            env(rate(gw, 1.26));
            env.close();

            // bob can finish escrow - rate unchanged
            env(finish(bob, alice, seq1),
                condition(cb1),
                fulfillment(fb1),
                fee(1500));
            env.close();

            BEAST_EXPECT(env.balance(alice, USD) == preAlice - delta);
            BEAST_EXPECT(env.balance(bob, USD) == USD(10100));
        }
        // test rate change - lower
        {
            Env env{*this, features};
            env.fund(XRP(10000), alice, bob, gw);
            env(fset(gw, asfAllowTokenLocking));
            env(rate(gw, 1.25));
            env.close();
            env.trust(USD(100000), alice);
            env.trust(USD(100000), bob);
            env.close();
            env(pay(gw, alice, USD(10000)));
            env(pay(gw, bob, USD(10000)));
            env.close();

            // alice can create escrow w/ xfer rate
            auto const preAlice = env.balance(alice, USD);
            auto const seq1 = env.seq(alice);
            auto const delta = USD(125);
            env(escrow(alice, bob, delta),
                condition(cb1),
                finish_time(env.now() + 1s),
                fee(1500));
            env.close();
            auto transferRate = escrowRate(env, alice, seq1);
            BEAST_EXPECT(
                transferRate.value == std::uint32_t(1000000000 * 1.25));

            // issuer changes rate higher
            env(rate(gw, 1.00));
            env.close();

            // bob can finish escrow - rate changed
            env(finish(bob, alice, seq1),
                condition(cb1),
                fulfillment(fb1),
                fee(1500));
            env.close();

            BEAST_EXPECT(env.balance(alice, USD) == preAlice - delta);
            BEAST_EXPECT(env.balance(bob, USD) == USD(10125));
        }
        // test issuer doesnt pay own rate
        {
            Env env{*this, features};
            env.fund(XRP(10000), alice, bob, gw);
            env(fset(gw, asfAllowTokenLocking));
            env(rate(gw, 1.25));
            env.close();
            env.trust(USD(100000), alice);
            env.trust(USD(100000), bob);
            env.close();
            env(pay(gw, alice, USD(10000)));
            env(pay(gw, bob, USD(10000)));
            env.close();

            // issuer with rate can create escrow
            auto const preAlice = env.balance(alice, USD);
            auto const seq1 = env.seq(gw);
            auto const delta = USD(125);
            env(escrow(gw, alice, delta),
                condition(cb1),
                finish_time(env.now() + 1s),
                fee(1500));
            env.close();

            auto transferRate = escrowRate(env, gw, seq1);
            BEAST_EXPECT(
                transferRate.value == std::uint32_t(1000000000 * 1.25));

            // alice can finish escrow - no rate charged
            env(finish(alice, gw, seq1),
                condition(cb1),
                fulfillment(fb1),
                fee(1500));
            env.close();

            BEAST_EXPECT(env.balance(alice, USD) == preAlice + delta);
            BEAST_EXPECT(env.balance(alice, USD) == USD(10125));
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
            env.fund(XRP(1000), alice, bob, gw);
            env(fset(gw, asfAllowTokenLocking));
            env.close();
            env.trust(USD(10000), alice, bob);
            env.close();
            env(pay(gw, alice, USD(1000)));
            env(pay(gw, bob, USD(1000)));
            env.close();

            // alice can create escrow
            auto seq1 = env.seq(alice);
            auto const delta = USD(125);
            env(escrow(alice, bob, delta),
                condition(cb1),
                finish_time(env.now() + 1s),
                fee(1500));
            env.close();

            // bob can finish
            auto const preBobLimit = limitAmount(env, bob, gw, USD);
            env(finish(bob, alice, seq1),
                condition(cb1),
                fulfillment(fb1),
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
            env.fund(XRP(1000), alice, bob, gw);
            env(fset(gw, asfAllowTokenLocking));
            env(fset(gw, asfRequireAuth));
            env.close();
            env(trust(gw, aliceUSD(10000)), txflags(tfSetfAuth));
            env(trust(alice, USD(10000)));
            env(trust(bob, USD(10000)));
            env.close();
            env(pay(gw, alice, USD(1000)));
            env.close();

            // alice cannot create escrow - fails without auth
            auto seq1 = env.seq(alice);
            auto const delta = USD(125);
            env(escrow(alice, bob, delta),
                condition(cb1),
                finish_time(env.now() + 1s),
                fee(1500),
                ter(tecNO_AUTH));
            env.close();

            // set auth on bob
            env(trust(gw, bobUSD(10000)), txflags(tfSetfAuth));
            env(trust(bob, USD(10000)));
            env.close();
            env(pay(gw, bob, USD(1000)));
            env.close();

            // alice can create escrow - bob has auth
            seq1 = env.seq(alice);
            env(escrow(alice, bob, delta),
                condition(cb1),
                finish_time(env.now() + 1s),
                fee(1500));
            env.close();

            // bob can finish
            env(finish(bob, alice, seq1),
                condition(cb1),
                fulfillment(fb1),
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
            env.fund(XRP(10000), alice, bob, gw);
            env(fset(gw, asfAllowTokenLocking));
            env.close();
            env.trust(USD(100000), alice);
            env.trust(USD(100000), bob);
            env.close();
            env(pay(gw, alice, USD(10000)));
            env(pay(gw, bob, USD(10000)));
            env.close();
            env(fset(gw, asfGlobalFreeze));
            env.close();

            // setup transaction
            auto seq1 = env.seq(alice);
            auto const delta = USD(125);

            // create escrow fails - frozen trustline
            env(escrow(alice, bob, delta),
                condition(cb1),
                finish_time(env.now() + 1s),
                fee(1500),
                ter(tecFROZEN));
            env.close();

            // clear global freeze
            env(fclear(gw, asfGlobalFreeze));
            env.close();

            // create escrow success
            seq1 = env.seq(alice);
            env(escrow(alice, bob, delta),
                condition(cb1),
                finish_time(env.now() + 1s),
                fee(1500));
            env.close();

            // set global freeze
            env(fset(gw, asfGlobalFreeze));
            env.close();

            // bob finish escrow success regardless of frozen assets
            env(finish(bob, alice, seq1),
                condition(cb1),
                fulfillment(fb1),
                fee(1500));
            env.close();
        }
        // test Individual Freeze
        {
            // Env Setup
            Env env{*this, features};
            env.fund(XRP(10000), alice, bob, gw);
            env(fset(gw, asfAllowTokenLocking));
            env.close();
            env(trust(alice, USD(100000)));
            env(trust(bob, USD(100000)));
            env.close();
            env(pay(gw, alice, USD(10000)));
            env(pay(gw, bob, USD(10000)));
            env.close();

            // set freeze on alice trustline
            env(trust(gw, USD(10000), alice, tfSetFreeze));
            env.close();

            // setup transaction
            auto seq1 = env.seq(alice);
            auto const delta = USD(125);

            // create escrow fails - frozen trustline
            env(escrow(alice, bob, delta),
                condition(cb1),
                finish_time(env.now() + 1s),
                fee(1500),
                ter(tecFROZEN));
            env.close();

            // clear freeze on alice trustline
            env(trust(gw, USD(10000), alice, tfClearFreeze));
            env.close();

            // create escrow success
            seq1 = env.seq(alice);
            env(escrow(alice, bob, delta),
                condition(cb1),
                finish_time(env.now() + 1s),
                fee(1500));
            env.close();

            // set freeze on bob trustline
            env(trust(gw, USD(10000), bob, tfSetFreeze));
            env.close();

            // bob finish escrow success regardless of frozen assets
            env(finish(bob, alice, seq1),
                condition(cb1),
                fulfillment(fb1),
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
            // ie. has 10000, escrow 1000 then try to pay 10000
            Env env{*this, features};
            env.fund(XRP(10000), alice, bob, gw);
            env(fset(gw, asfAllowTokenLocking));
            env.close();
            env.trust(USD(100000), alice);
            env.trust(USD(100000), bob);
            env.close();
            env(pay(gw, alice, USD(10000)));
            env(pay(gw, bob, USD(10000)));
            env.close();

            // create escrow success
            auto const delta = USD(1000);
            env(escrow(alice, bob, delta),
                condition(cb1),
                finish_time(env.now() + 1s),
                fee(1500));
            env.close();
            env(pay(alice, gw, USD(10000)), ter(tecPATH_PARTIAL));
        }
        {
            // test tecINSUFFICIENT_FUNDS
            // ie. has 10000 escrow 1000 then try to escrow 10000
            Env env{*this, features};
            env.fund(XRP(10000), alice, bob, gw);
            env(fset(gw, asfAllowTokenLocking));
            env.close();
            env.trust(USD(100000), alice);
            env.trust(USD(100000), bob);
            env.close();
            env(pay(gw, alice, USD(10000)));
            env(pay(gw, bob, USD(10000)));
            env.close();

            auto const delta = USD(1000);
            env(escrow(alice, bob, delta),
                condition(cb1),
                finish_time(env.now() + 1s),
                fee(1500));
            env.close();

            env(escrow(alice, bob, USD(10000)),
                condition(cb1),
                finish_time(env.now() + 1s),
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
            env.fund(XRP(10000), alice, bob, gw);
            env(fset(gw, asfAllowTokenLocking));
            env.close();
            env.trust(USD(100000000000000000), alice);
            env.trust(USD(100000000000000000), bob);
            env.close();
            env(pay(gw, alice, USD(10000000000000000)));
            env(pay(gw, bob, USD(1)));
            env.close();

            // alice cannot create escrow for 1/10 iou - precision loss
            env(escrow(alice, bob, USD(1)),
                condition(cb1),
                finish_time(env.now() + 1s),
                fee(1500),
                ter(tecPRECISION_LOSS));
            env.close();

            auto const seq1 = env.seq(alice);
            // alice can create escrow for 1000 iou
            env(escrow(alice, bob, USD(1000)),
                condition(cb1),
                finish_time(env.now() + 1s),
                fee(1500));
            env.close();

            // bob finish escrow success
            env(finish(bob, alice, seq1),
                condition(cb1),
                fulfillment(fb1),
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
            env(escrow(alice, bob, MPT(1000)),
                finish_time(env.now() + 1s),
                createResult);
            env.close();

            auto const seq1 = env.seq(alice);

            env(escrow(alice, bob, MPT(1000)),
                condition(cb1),
                finish_time(env.now() + 1s),
                fee(1500),
                createResult);
            env.close();
            env(finish(bob, alice, seq1),
                condition(cb1),
                fulfillment(fb1),
                fee(1500),
                finishResult);

            auto const seq2 = env.seq(alice);

            env(escrow(alice, bob, MPT(1000)),
                condition(cb2),
                finish_time(env.now() + 1s),
                cancel_time(env.now() + 2s),
                fee(1500),
                createResult);
            env.close();
            env(cancel(bob, alice, seq2), fee(1500), finishResult);
        }
    }

    void
    testIOUWithFeats(FeatureBitset features)
    {
        testIOUEnablement(features);
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
        // testMPTGateway(features);
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