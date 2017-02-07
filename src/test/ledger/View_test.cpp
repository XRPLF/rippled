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

#include <BeastConfig.h>
#include <test/jtx.h>
#include <ripple/app/ledger/Ledger.h>
#include <ripple/ledger/ApplyViewImpl.h>
#include <ripple/ledger/OpenView.h>
#include <ripple/ledger/PaymentSandbox.h>
#include <ripple/ledger/Sandbox.h>
#include <ripple/core/ConfigSections.h>
#include <ripple/protocol/Feature.h>
#include <type_traits>

namespace ripple {
namespace test {

class View_test
    : public beast::unit_test::suite
{
    // Convert a small integer to a key
    static
    Keylet
    k (std::uint64_t id)
    {
        return Keylet{
            ltACCOUNT_ROOT, uint256(id)};
    }

    // Create SLE with key and payload
    static
    std::shared_ptr<SLE>
    sle (std::uint64_t id,
        std::uint32_t seq = 1)
    {
        auto const le =
            std::make_shared<SLE>(k(id));
        le->setFieldU32(sfSequence, seq);
        return le;
    }

    // Return payload for SLE
    template <class T>
    static
    std::uint32_t
    seq (std::shared_ptr<T> const& le)
    {
        return le->getFieldU32(sfSequence);
    }

    // Set payload on SLE
    static
    void
    seq (std::shared_ptr<SLE> const& le,
        std::uint32_t seq)
    {
        le->setFieldU32(sfSequence, seq);
    }

    // Erase all state items
    static
    void
    wipe (OpenLedger& openLedger)
    {
        openLedger.modify(
            [](OpenView& view, beast::Journal)
        {
            // HACK!
            boost::optional<uint256> next;
            next.emplace(0);
            for(;;)
            {
                next = view.succ(*next);
                if (! next)
                    break;
                view.rawErase(std::make_shared<SLE>(
                    *view.read(keylet::unchecked(*next))));
            }
            return true;
        });
    }

    static
    void
    wipe (Ledger& ledger)
    {
        // HACK!
        boost::optional<uint256> next;
        next.emplace(0);
        for(;;)
        {
            next = ledger.succ(*next);
            if (! next)
                break;
            ledger.rawErase(std::make_shared<SLE>(
                *ledger.read(keylet::unchecked(*next))));
        }
    }

    // Test succ correctness
    void
    succ (ReadView const& v,
        std::uint32_t id,
            boost::optional<
                std::uint32_t> answer)
    {
        auto const next =
            v.succ(k(id).key);
        if (answer)
        {
            if (BEAST_EXPECT(next))
                BEAST_EXPECT(*next ==
                    k(*answer).key);
        }
        else
        {
            BEAST_EXPECT( ! next);
        }
    }

    template <class T>
    static
    std::shared_ptr<
        std::remove_const_t<T>>
    copy (std::shared_ptr<T> const& sp)
    {
        return std::make_shared<
            std::remove_const_t<T>>(*sp);
    }

    // Exercise Ledger implementation of ApplyView
    void
    testLedger()
    {
        using namespace jtx;
        Env env(*this);
        Config config;
        std::shared_ptr<Ledger const> const genesis =
            std::make_shared<Ledger>(
                create_genesis, config,
                std::vector<uint256>{}, env.app().family());
        auto const ledger =
            std::make_shared<Ledger>(
                *genesis,
                env.app().timeKeeper().closeTime());
        wipe(*ledger);
        ReadView& v = *ledger;
        succ(v, 0, boost::none);
        ledger->rawInsert(sle(1, 1));
        BEAST_EXPECT(v.exists(k(1)));
        BEAST_EXPECT(seq(v.read(k(1))) == 1);
        succ(v, 0, 1);
        succ(v, 1, boost::none);
        ledger->rawInsert(sle(2, 2));
        BEAST_EXPECT(seq(v.read(k(2))) == 2);
        ledger->rawInsert(sle(3, 3));
        BEAST_EXPECT(seq(v.read(k(3))) == 3);
        auto s = copy(v.read(k(2)));
        seq(s, 4);
        ledger->rawReplace(std::move(s));
        BEAST_EXPECT(seq(v.read(k(2))) == 4);
        ledger->rawErase(sle(2));
        BEAST_EXPECT(! v.exists(k(2)));
        BEAST_EXPECT(v.exists(k(1)));
        BEAST_EXPECT(v.exists(k(3)));
    }

    void
    testMeta()
    {
        using namespace jtx;
        Env env(*this);
        wipe(env.app().openLedger());
        auto const open = env.current();
        ApplyViewImpl v(&*open, tapNONE);
        succ(v, 0, boost::none);
        v.insert(sle(1));
        BEAST_EXPECT(v.exists(k(1)));
        BEAST_EXPECT(seq(v.read(k(1))) == 1);
        BEAST_EXPECT(seq(v.peek(k(1))) == 1);
        succ(v, 0, 1);
        succ(v, 1, boost::none);
        v.insert(sle(2, 2));
        BEAST_EXPECT(seq(v.read(k(2))) == 2);
        v.insert(sle(3, 3));
        auto s = v.peek(k(3));
        BEAST_EXPECT(seq(s) == 3);
        s = v.peek(k(2));
        seq(s, 4);
        v.update(s);
        BEAST_EXPECT(seq(v.read(k(2))) == 4);
        v.erase(s);
        BEAST_EXPECT(! v.exists(k(2)));
        BEAST_EXPECT(v.exists(k(1)));
        BEAST_EXPECT(v.exists(k(3)));
    }

    // Exercise all succ paths
    void
    testMetaSucc()
    {
        using namespace jtx;
        Env env(*this);
        wipe(env.app().openLedger());
        auto const open = env.current();
        ApplyViewImpl v0(&*open, tapNONE);
        v0.insert(sle(1));
        v0.insert(sle(2));
        v0.insert(sle(4));
        v0.insert(sle(7));
        {
            Sandbox v1(&v0);
            v1.insert(sle(3));
            v1.insert(sle(5));
            v1.insert(sle(6));

            // v0: 12-4--7
            // v1: --3-56-

            succ(v0, 0, 1);
            succ(v0, 1, 2);
            succ(v0, 2, 4);
            succ(v0, 3, 4);
            succ(v0, 4, 7);
            succ(v0, 5, 7);
            succ(v0, 6, 7);
            succ(v0, 7, boost::none);

            succ(v1, 0, 1);
            succ(v1, 1, 2);
            succ(v1, 2, 3);
            succ(v1, 3, 4);
            succ(v1, 4, 5);
            succ(v1, 5, 6);
            succ(v1, 6, 7);
            succ(v1, 7, boost::none);

            v1.erase(v1.peek(k(4)));
            succ(v1, 3, 5);

            v1.erase(v1.peek(k(6)));
            succ(v1, 5, 7);
            succ(v1, 6, 7);

            // v0: 12----7
            // v1: --3-5--

            v1.apply(v0);
        }

        // v0: 123-5-7

        succ(v0, 0, 1);
        succ(v0, 1, 2);
        succ(v0, 2, 3);
        succ(v0, 3, 5);
        succ(v0, 4, 5);
        succ(v0, 5, 7);
        succ(v0, 6, 7);
        succ(v0, 7, boost::none);
    }

    void
    testStacked()
    {
        using namespace jtx;
        Env env(*this);
        wipe(env.app().openLedger());
        auto const open = env.current();
        ApplyViewImpl v0 (&*open, tapNONE);
        v0.rawInsert(sle(1, 1));
        v0.rawInsert(sle(2, 2));
        v0.rawInsert(sle(4, 4));

        {
            Sandbox v1(&v0);
            v1.erase(v1.peek(k(2)));
            v1.insert(sle(3, 3));
            auto s = v1.peek(k(4));
            seq(s, 5);
            v1.update(s);
            BEAST_EXPECT(seq(v1.read(k(1))) == 1);
            BEAST_EXPECT(! v1.exists(k(2)));
            BEAST_EXPECT(seq(v1.read(k(3))) == 3);
            BEAST_EXPECT(seq(v1.read(k(4))) == 5);
            {
                Sandbox v2(&v1);
                auto s = v2.peek(k(3));
                seq(s, 6);
                v2.update(s);
                v2.erase(v2.peek(k(4)));
                BEAST_EXPECT(seq(v2.read(k(1))) == 1);
                BEAST_EXPECT(! v2.exists(k(2)));
                BEAST_EXPECT(seq(v2.read(k(3))) == 6);
                BEAST_EXPECT(! v2.exists(k(4)));
                // discard v2
            }
            BEAST_EXPECT(seq(v1.read(k(1))) == 1);
            BEAST_EXPECT(! v1.exists(k(2)));
            BEAST_EXPECT(seq(v1.read(k(3))) == 3);
            BEAST_EXPECT(seq(v1.read(k(4))) == 5);

            {
                Sandbox v2(&v1);
                auto s = v2.peek(k(3));
                seq(s, 6);
                v2.update(s);
                v2.erase(v2.peek(k(4)));
                BEAST_EXPECT(seq(v2.read(k(1))) == 1);
                BEAST_EXPECT(! v2.exists(k(2)));
                BEAST_EXPECT(seq(v2.read(k(3))) == 6);
                BEAST_EXPECT(! v2.exists(k(4)));
                v2.apply(v1);
            }
            BEAST_EXPECT(seq(v1.read(k(1))) == 1);
            BEAST_EXPECT(! v1.exists(k(2)));
            BEAST_EXPECT(seq(v1.read(k(3))) == 6);
            BEAST_EXPECT(! v1.exists(k(4)));
            v1.apply(v0);
        }
        BEAST_EXPECT(seq(v0.read(k(1))) == 1);
        BEAST_EXPECT(! v0.exists(k(2)));
        BEAST_EXPECT(seq(v0.read(k(3))) == 6);
        BEAST_EXPECT(! v0.exists(k(4)));
    }

    // Verify contextual information
    void
    testContext()
    {
        using namespace jtx;
        using namespace std::chrono;
        {
            Env env(*this);
            wipe(env.app().openLedger());
            auto const open = env.current();
            OpenView v0(open.get());
            BEAST_EXPECT(v0.seq() != 98);
            BEAST_EXPECT(v0.seq() == open->seq());
            BEAST_EXPECT(v0.parentCloseTime() != NetClock::time_point{99s});
            BEAST_EXPECT(v0.parentCloseTime() ==
                open->parentCloseTime());
            {
                // shallow copy
                OpenView v1(v0);
                BEAST_EXPECT(v1.seq() == v0.seq());
                BEAST_EXPECT(v1.parentCloseTime() ==
                    v1.parentCloseTime());

                ApplyViewImpl v2(&v1, tapNO_CHECK_SIGN);
                BEAST_EXPECT(v2.parentCloseTime() ==
                    v1.parentCloseTime());
                BEAST_EXPECT(v2.seq() == v1.seq());
                BEAST_EXPECT(v2.flags() == tapNO_CHECK_SIGN);

                Sandbox v3(&v2);
                BEAST_EXPECT(v3.seq() == v2.seq());
                BEAST_EXPECT(v3.parentCloseTime() ==
                    v2.parentCloseTime());
                BEAST_EXPECT(v3.flags() == tapNO_CHECK_SIGN);
            }
            {
                ApplyViewImpl v1(&v0, tapNO_CHECK_SIGN);
                PaymentSandbox v2(&v1);
                BEAST_EXPECT(v2.seq() == v0.seq());
                BEAST_EXPECT(v2.parentCloseTime() ==
                    v0.parentCloseTime());
                BEAST_EXPECT(v2.flags() == tapNO_CHECK_SIGN);
                PaymentSandbox v3(&v2);
                BEAST_EXPECT(v3.seq() == v2.seq());
                BEAST_EXPECT(v3.parentCloseTime() ==
                    v2.parentCloseTime());
                BEAST_EXPECT(v3.flags() == v2.flags());
            }
        }
    }

    // Return a list of keys found via sles
    static
    std::vector<uint256>
    sles (ReadView const& ledger)
    {
        std::vector<uint256> v;
        v.reserve (32);
        for(auto const& sle : ledger.sles)
            v.push_back(sle->key());
        return v;
    }

    template <class... Args>
    static
    std::vector<uint256>
    list (Args... args)
    {
        return std::vector<uint256> ({uint256(args)...});
    }

    void
    testSles()
    {
        using namespace jtx;
        Env env(*this);
        Config config;
        std::shared_ptr<Ledger const> const genesis =
            std::make_shared<Ledger> (
                create_genesis, config,
                std::vector<uint256>{}, env.app().family());
        auto const ledger = std::make_shared<Ledger>(
            *genesis,
            env.app().timeKeeper().closeTime());
        auto setup123 = [&ledger, this]()
        {
            // erase middle element
            wipe (*ledger);
            ledger->rawInsert (sle (1));
            ledger->rawInsert (sle (2));
            ledger->rawInsert (sle (3));
            BEAST_EXPECT(sles (*ledger) == list (1, 2, 3));
        };
        {
            setup123 ();
            OpenView view (ledger.get ());
            view.rawErase (sle (1));
            view.rawInsert (sle (4));
            view.rawInsert (sle (5));
            BEAST_EXPECT(sles (view) == list (2, 3, 4, 5));
            auto b = view.sles.begin();
            BEAST_EXPECT(view.sles.upper_bound(uint256(1)) == b); ++b;
            BEAST_EXPECT(view.sles.upper_bound(uint256(2)) == b); ++b;
            BEAST_EXPECT(view.sles.upper_bound(uint256(3)) == b); ++b;
            BEAST_EXPECT(view.sles.upper_bound(uint256(4)) == b); ++b;
            BEAST_EXPECT(view.sles.upper_bound(uint256(5)) == b);
        }
        {
            setup123 ();
            OpenView view (ledger.get ());
            view.rawErase (sle (1));
            view.rawErase (sle (2));
            view.rawInsert (sle (4));
            view.rawInsert (sle (5));
            BEAST_EXPECT(sles (view) == list (3, 4, 5));
            auto b = view.sles.begin();
            BEAST_EXPECT(view.sles.upper_bound(uint256(1)) == b);
            BEAST_EXPECT(view.sles.upper_bound(uint256(2)) == b); ++b;
            BEAST_EXPECT(view.sles.upper_bound(uint256(3)) == b); ++b;
            BEAST_EXPECT(view.sles.upper_bound(uint256(4)) == b); ++b;
            BEAST_EXPECT(view.sles.upper_bound(uint256(5)) == b);
        }
        {
            setup123 ();
            OpenView view (ledger.get ());
            view.rawErase (sle (1));
            view.rawErase (sle (2));
            view.rawErase (sle (3));
            view.rawInsert (sle (4));
            view.rawInsert (sle (5));
            BEAST_EXPECT(sles (view) == list (4, 5));
            auto b = view.sles.begin();
            BEAST_EXPECT(view.sles.upper_bound(uint256(1)) == b);
            BEAST_EXPECT(view.sles.upper_bound(uint256(2)) == b);
            BEAST_EXPECT(view.sles.upper_bound(uint256(3)) == b); ++b;
            BEAST_EXPECT(view.sles.upper_bound(uint256(4)) == b); ++b;
            BEAST_EXPECT(view.sles.upper_bound(uint256(5)) == b);
        }
        {
            setup123 ();
            OpenView view (ledger.get ());
            view.rawErase (sle (3));
            view.rawInsert (sle (4));
            view.rawInsert (sle (5));
            BEAST_EXPECT(sles (view) == list (1, 2, 4, 5));
            auto b = view.sles.begin();
            ++b;
            BEAST_EXPECT(view.sles.upper_bound(uint256(1)) == b); ++b;
            BEAST_EXPECT(view.sles.upper_bound(uint256(2)) == b);
            BEAST_EXPECT(view.sles.upper_bound(uint256(3)) == b); ++b;
            BEAST_EXPECT(view.sles.upper_bound(uint256(4)) == b); ++b;
            BEAST_EXPECT(view.sles.upper_bound(uint256(5)) == b);
        }
        {
            setup123 ();
            OpenView view (ledger.get ());
            view.rawReplace (sle (1, 10));
            view.rawReplace (sle (3, 30));
            BEAST_EXPECT(sles (view) == list (1, 2, 3));
            BEAST_EXPECT(seq (view.read(k (1))) == 10);
            BEAST_EXPECT(seq (view.read(k (2))) == 1);
            BEAST_EXPECT(seq (view.read(k (3))) == 30);

            view.rawErase (sle (3));
            BEAST_EXPECT(sles (view) == list (1, 2));
            auto b = view.sles.begin();
            ++b;
            BEAST_EXPECT(view.sles.upper_bound(uint256(1)) == b); ++b;
            BEAST_EXPECT(view.sles.upper_bound(uint256(2)) == b);
            BEAST_EXPECT(view.sles.upper_bound(uint256(3)) == b);
            BEAST_EXPECT(view.sles.upper_bound(uint256(4)) == b);
            BEAST_EXPECT(view.sles.upper_bound(uint256(5)) == b);

            view.rawInsert (sle (5));
            view.rawInsert (sle (4));
            view.rawInsert (sle (3));
            BEAST_EXPECT(sles (view) == list (1, 2, 3, 4, 5));
            b = view.sles.begin();
            ++b;
            BEAST_EXPECT(view.sles.upper_bound(uint256(1)) == b); ++b;
            BEAST_EXPECT(view.sles.upper_bound(uint256(2)) == b); ++b;
            BEAST_EXPECT(view.sles.upper_bound(uint256(3)) == b); ++b;
            BEAST_EXPECT(view.sles.upper_bound(uint256(4)) == b); ++b;
            BEAST_EXPECT(view.sles.upper_bound(uint256(5)) == b);
        }
    }

    void
    testFlags()
    {
        using namespace jtx;
        Env env(*this);

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account("gateway");
        auto const USD = gw["USD"];
        auto const EUR = gw["EUR"];

        env.fund(XRP(10000), alice, bob, carol, gw);
        env.trust(USD(100), alice, bob, carol);
        {
            // Global freezing.
            env(pay(gw, alice, USD(50)));
            env(offer(alice, XRP(5), USD(5)));

            // Now freeze gw.
            env(fset (gw, asfGlobalFreeze));
            env.close();
            env(offer(alice, XRP(4), USD(5)), ter(tecFROZEN));
            env.close();

            // Alice's USD balance should be zero if frozen.
            BEAST_EXPECT(USD(0) == accountHolds (*env.closed(),
                alice, USD.currency, gw, fhZERO_IF_FROZEN, env.journal));

            // Thaw gw and try again.
            env(fclear (gw, asfGlobalFreeze));
            env.close();
            env(offer("alice", XRP(4), USD(5)));
        }
        {
            // Local freezing.
            env(pay(gw, bob, USD(50)));
            env.close();

            // Now gw freezes bob's USD trust line.
            env(trust(gw, USD(100), bob, tfSetFreeze));
            env.close();

            // Bob's balance should be zero if frozen.
            BEAST_EXPECT(USD(0) == accountHolds (*env.closed(),
                bob, USD.currency, gw, fhZERO_IF_FROZEN, env.journal));

            // gw thaws bob's trust line.  bob gets his money back.
            env(trust(gw, USD(100), bob, tfClearFreeze));
            env.close();
            BEAST_EXPECT(USD(50) == accountHolds (*env.closed(),
                bob, USD.currency, gw, fhZERO_IF_FROZEN, env.journal));
        }
        {
            // accountHolds().
            env(pay(gw, carol, USD(50)));
            env.close();

            // carol has no EUR.
            BEAST_EXPECT(EUR(0) == accountHolds (*env.closed(),
                carol, EUR.currency, gw, fhZERO_IF_FROZEN, env.journal));

            // But carol does have USD.
            BEAST_EXPECT(USD(50) == accountHolds (*env.closed(),
                carol, USD.currency, gw, fhZERO_IF_FROZEN, env.journal));

            // carol's XRP balance should be her holdings minus her reserve.
            auto const carolsXRP = accountHolds (*env.closed(), carol,
                xrpCurrency(), xrpAccount(), fhZERO_IF_FROZEN, env.journal);
            // carol's XRP balance:              10000
            // base reserve:                      -200
            // 1 trust line times its reserve: 1 * -50
            //                                 -------
            // carol's available balance:         9750
            BEAST_EXPECT(carolsXRP == XRP(9750));

            // carol should be able to spend *more* than her XRP balance on
            // a fee by eating into her reserve.
            env(noop(carol), fee(carolsXRP + XRP(10)));
            env.close();

            // carol's XRP balance should now show as zero.
            BEAST_EXPECT(XRP(0) == accountHolds (*env.closed(),
                carol, xrpCurrency(), gw, fhZERO_IF_FROZEN, env.journal));
        }
        {
            // accountFunds().
            // Gateways have whatever funds they claim to have.
            auto const gwUSD = accountFunds(
                *env.closed(), gw, USD(314159), fhZERO_IF_FROZEN, env.journal);
            BEAST_EXPECT(gwUSD == USD(314159));

            // carol has funds from the gateway.
            auto carolsUSD = accountFunds(
                *env.closed(), carol, USD(0), fhZERO_IF_FROZEN, env.journal);
            BEAST_EXPECT(carolsUSD == USD(50));

            // If carol's funds are frozen she has no funds...
            env(fset (gw, asfGlobalFreeze));
            env.close();
            carolsUSD = accountFunds(
                *env.closed(), carol, USD(0), fhZERO_IF_FROZEN, env.journal);
            BEAST_EXPECT(carolsUSD == USD(0));

            // ... unless the query ignores the FROZEN state.
            carolsUSD = accountFunds(
                *env.closed(), carol, USD(0), fhIGNORE_FREEZE, env.journal);
            BEAST_EXPECT(carolsUSD == USD(50));

            // Just to be tidy, thaw gw.
            env(fclear (gw, asfGlobalFreeze));
            env.close();
        }
    }

    void
    testTransferRate()
    {
        using namespace jtx;
        Env env(*this);

        auto const gw1 = Account("gw1");

        env.fund(XRP(10000), gw1);
        env.close();

        auto rdView = env.closed();
        // Test with no rate set on gw1.
        BEAST_EXPECT(transferRate (*rdView, gw1) == parityRate);

        env(rate(gw1, 1.02));
        env.close();

        rdView = env.closed();
        BEAST_EXPECT(transferRate (*rdView, gw1) == Rate{ 1020000000 });
    }

    void
    testAreCompatible()
    {
        // This test requires incompatible ledgers.  The good news we can
        // construct and manage two different Env instances at the same
        // time.  So we can use two Env instances to produce mutually
        // incompatible ledgers.
        using namespace jtx;
        auto const alice = Account("alice");
        auto const bob = Account("bob");

        // The first Env.
        Env eA(*this);

        eA.fund(XRP(10000), alice);
        eA.close();
        auto const rdViewA3 = eA.closed();

        eA.fund(XRP(10000), bob);
        eA.close();
        auto const rdViewA4 = eA.closed();

        // The two Env's can't share the same ports, so edit the config
        // of the second Env.
        auto getConfigWithNewPorts = [this] ()
        {
            auto cfg = std::make_unique<Config>();
            setupConfigForUnitTests(*cfg);

            for (auto const sectionName : {"port_peer", "port_rpc", "port_ws"})
            {
                Section& s = (*cfg)[sectionName];
                auto const port = s.get<std::int32_t>("port");
                BEAST_EXPECT(port);
                if (port)
                {
                    constexpr int portIncr = 5;
                    s.set ("port", std::to_string(*port + portIncr));
                }
            }
            return cfg;
        };
        Env eB(*this, getConfigWithNewPorts());

        // Make ledgers that are incompatible with the first ledgers.  Note
        // that bob is funded before alice.
        eB.fund(XRP(10000), bob);
        eB.close();
        auto const rdViewB3 = eB.closed();

        eB.fund(XRP(10000), alice);
        eB.close();
        auto const rdViewB4 = eB.closed();

        // Check for compatibility.
        auto jStream = eA.journal.error();
        BEAST_EXPECT(  areCompatible (*rdViewA3, *rdViewA4, jStream, ""));
        BEAST_EXPECT(  areCompatible (*rdViewA4, *rdViewA3, jStream, ""));
        BEAST_EXPECT(  areCompatible (*rdViewA4, *rdViewA4, jStream, ""));
        BEAST_EXPECT(! areCompatible (*rdViewA3, *rdViewB4, jStream, ""));
        BEAST_EXPECT(! areCompatible (*rdViewA4, *rdViewB3, jStream, ""));
        BEAST_EXPECT(! areCompatible (*rdViewA4, *rdViewB4, jStream, ""));

        // Try the other interface.
        // Note that the different interface has different outcomes.
        auto const& iA3 = rdViewA3->info();
        auto const& iA4 = rdViewA4->info();

        BEAST_EXPECT(  areCompatible (iA3.hash, iA3.seq, *rdViewA4, jStream, ""));
        BEAST_EXPECT(  areCompatible (iA4.hash, iA4.seq, *rdViewA3, jStream, ""));
        BEAST_EXPECT(  areCompatible (iA4.hash, iA4.seq, *rdViewA4, jStream, ""));
        BEAST_EXPECT(! areCompatible (iA3.hash, iA3.seq, *rdViewB4, jStream, ""));
        BEAST_EXPECT(  areCompatible (iA4.hash, iA4.seq, *rdViewB3, jStream, ""));
        BEAST_EXPECT(! areCompatible (iA4.hash, iA4.seq, *rdViewB4, jStream, ""));
    }

    void
    testRegressions()
    {
        using namespace jtx;

        // Create a ledger with 1 item, put a
        // ApplyView on that, then another ApplyView,
        // erase the item, apply.
        {
            Env env(*this);
            Config config;
            std::shared_ptr<Ledger const> const genesis =
                std::make_shared<Ledger>(
                    create_genesis, config,
                    std::vector<uint256>{}, env.app().family());
            auto const ledger =
                std::make_shared<Ledger>(
                    *genesis,
                    env.app().timeKeeper().closeTime());
            wipe(*ledger);
            ledger->rawInsert(sle(1));
            ReadView& v0 = *ledger;
            ApplyViewImpl v1(&v0, tapNONE);
            {
                Sandbox v2(&v1);
                v2.erase(v2.peek(k(1)));
                v2.apply(v1);
            }
            BEAST_EXPECT(! v1.exists(k(1)));
        }

        // Make sure OpenLedger::empty works
        {
            Env env(*this);
            BEAST_EXPECT(env.app().openLedger().empty());
            env.fund(XRP(10000), Account("test"));
            BEAST_EXPECT(! env.app().openLedger().empty());
        }
    }

    void run()
    {
        // This had better work, or else
        BEAST_EXPECT(k(0).key < k(1).key);

        testLedger();
        testMeta();
        testMetaSucc();
        testStacked();
        testContext();
        testSles();
        testFlags();
        testTransferRate();
        testAreCompatible();
        testRegressions();
    }
};

class GetAmendments_test
    : public beast::unit_test::suite
{
    static
    std::unique_ptr<Config>
    makeValidatorConfig()
    {
        auto p = std::make_unique<Config>();
        setupConfigForUnitTests(*p);

        // If the config has valid validation keys then we run as a validator.
        p->section(SECTION_VALIDATION_SEED).append(
            std::vector<std::string>{"shUwVw52ofnCUX5m7kPTKzJdr4HEH"});

        return p;
    }

    void
    testGetAmendments()
    {
        using namespace jtx;
        Env env(*this, makeValidatorConfig());

        // Start out with no amendments.
        auto majorities = getMajorityAmendments (*env.closed());
        BEAST_EXPECT(majorities.empty());

        // Now close ledgers until the amendments show up.
        int i = 0;
        for (i = 0; i <= 256; ++i)
        {
            env.close();
            majorities = getMajorityAmendments (*env.closed());
            if (! majorities.empty())
                break;
        }

        // There should be at least 5 amendments.  Don't do exact comparison
        // to avoid maintenance as more amendments are added in the future.
        BEAST_EXPECT(i == 254);
        BEAST_EXPECT(majorities.size() >= 5);

        // None of the amendments should be enabled yet.
        auto enableds = getEnabledAmendments(*env.closed());
        BEAST_EXPECT(enableds.empty());

        // Now wait 2 weeks modulo 256 ledgers for the amendments to be
        // enabled.  Speed the process by closing ledgers every 80 minutes,
        // which should get us to just past 2 weeks after 256 ledgers.
        for (i = 0; i <= 256; ++i)
        {
            using namespace std::chrono_literals;
            env.close(80min);
            enableds = getEnabledAmendments(*env.closed());
            if (! enableds.empty())
                break;
        }
        BEAST_EXPECT(i == 255);
        BEAST_EXPECT(enableds.size() >= 5);
    }

    void run() override
    {
        testGetAmendments();
    }
};

class DirIsEmpty_test
    : public beast::unit_test::suite
{
    void
    testDirIsEmpty()
    {
        using namespace jtx;
        auto const alice = Account("alice");
        auto const bogie = Account("bogie");

        Env env(*this, features(featureMultiSign));

        env.fund(XRP(10000), alice);
        env.close();

        // alice should have an empty directory.
        BEAST_EXPECT(dirIsEmpty (*env.closed(), keylet::ownerDir(alice)));

        // Give alice a signer list, then there will be stuff in the directory.
        env(signers(alice, 1, { { bogie, 1} }));
        env.close();
        BEAST_EXPECT(! dirIsEmpty (*env.closed(), keylet::ownerDir(alice)));

        env(signers(alice, jtx::none));
        env.close();
        BEAST_EXPECT(dirIsEmpty (*env.closed(), keylet::ownerDir(alice)));

        // The next test is a bit awkward.  It tests the case where alice
        // uses 3 directory pages and then deletes all entries from the
        // first 2 pages.  dirIsEmpty() should still return false in this
        // circumstance.
        //
        // Fill alice's directory with implicit trust lines (produced by
        // taking offers) and then remove all but the last one.
        auto const becky = Account ("becky");
        auto const gw = Account ("gw");
        env.fund(XRP(10000), becky, gw);
        env.close();

        // The DIR_NODE_MAX constant is hidden in View.cpp (Feb 2016).  But,
        // ideally, we'd verify we're doing a good test with the following:
//      static_assert (64 >= (2 * DIR_NODE_MAX), "");

        // Generate 64 currencies named AAA -> AAP and ADA -> ADP.
        std::vector<IOU> currencies;
        currencies.reserve(64);
        for (char b = 'A'; b <= 'D'; ++b)
        {
            for (char c = 'A'; c <= 'P'; ++c)
            {
                currencies.push_back(gw[std::string("A") + b + c]);
                IOU const& currency = currencies.back();

                // Establish trust lines.
                env(trust(becky, currency(50)));
                env.close();
                env(pay(gw, becky, currency(50)));
                env.close();
                env(offer(alice, currency(50), XRP(10)));
                env(offer(becky, XRP(10), currency(50)));
                env.close();
            }
        }

        // Set up one more currency that alice will hold onto.  We expect
        // this one to go in the third directory page.
        IOU const lastCurrency = gw["ZZZ"];
        env(trust(becky, lastCurrency(50)));
        env.close();
        env(pay(gw, becky, lastCurrency(50)));
        env.close();
        env(offer(alice, lastCurrency(50), XRP(10)));
        env(offer(becky, XRP(10), lastCurrency(50)));
        env.close();

        BEAST_EXPECT(! dirIsEmpty (*env.closed(), keylet::ownerDir(alice)));

        // Now alice gives all the currencies except the last one back to becky.
        for (auto currency : currencies)
        {
            env(pay(alice, becky, currency(50)));
            env.close();
        }

        // This is the crux of the test.
        BEAST_EXPECT(! dirIsEmpty (*env.closed(), keylet::ownerDir(alice)));

        // Give the last currency to becky.  Now alice's directory is empty.
        env(pay(alice, becky, lastCurrency(50)));
        env.close();

        BEAST_EXPECT(dirIsEmpty (*env.closed(), keylet::ownerDir(alice)));
    }

    void run() override
    {
        testDirIsEmpty();
    }
};

BEAST_DEFINE_TESTSUITE(View,ledger,ripple);
BEAST_DEFINE_TESTSUITE(GetAmendments,ledger,ripple);
BEAST_DEFINE_TESTSUITE(DirIsEmpty, ledger,ripple);

}  // test
}  // ripple
