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
#include <ripple/test/jtx.h>
#include <ripple/app/ledger/Ledger.h>
#include <ripple/ledger/ApplyViewImpl.h>
#include <ripple/ledger/OpenView.h>
#include <ripple/ledger/PaymentSandbox.h>
#include <ripple/ledger/Sandbox.h>
#include <beast/cxx14/type_traits.h> // <type_traits>

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
            if (expect(next))
                expect(*next ==
                    k(*answer).key);
        }
        else
        {
            expect( ! next);
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
        Config const config;
        std::shared_ptr<Ledger const> const genesis =
            std::make_shared<Ledger>(
                create_genesis, config);
        auto const ledger =
            std::make_shared<Ledger>(
                open_ledger, *genesis);
        wipe(*ledger);
        ReadView& v = *ledger;
        succ(v, 0, boost::none);
        ledger->rawInsert(sle(1, 1));
        expect(v.exists(k(1)));
        expect(seq(v.read(k(1))) == 1);
        succ(v, 0, 1);
        succ(v, 1, boost::none);
        ledger->rawInsert(sle(2, 2));
        expect(seq(v.read(k(2))) == 2);
        ledger->rawInsert(sle(3, 3));
        expect(seq(v.read(k(3))) == 3);
        auto s = copy(v.read(k(2)));
        seq(s, 4);
        ledger->rawReplace(std::move(s));
        expect(seq(v.read(k(2))) == 4);
        ledger->rawErase(sle(2));
        expect(! v.exists(k(2)));
        expect(v.exists(k(1)));
        expect(v.exists(k(3)));
    }

    void
    testMeta()
    {
        using namespace jtx;
        Env env(*this);
        wipe(env.openLedger);
        auto const open = env.open();
        ApplyViewImpl v(&*open, tapNONE);
        succ(v, 0, boost::none);
        v.insert(sle(1));
        expect(v.exists(k(1)));
        expect(seq(v.read(k(1))) == 1);
        expect(seq(v.peek(k(1))) == 1);
        succ(v, 0, 1);
        succ(v, 1, boost::none);
        v.insert(sle(2, 2));
        expect(seq(v.read(k(2))) == 2);
        v.insert(sle(3, 3));
        auto s = v.peek(k(3));
        expect(seq(s) == 3);
        s = v.peek(k(2));
        seq(s, 4);
        v.update(s);
        expect(seq(v.read(k(2))) == 4);
        v.erase(s);
        expect(! v.exists(k(2)));
        expect(v.exists(k(1)));
        expect(v.exists(k(3)));
    }

    // Exercise all succ paths
    void
    testMetaSucc()
    {
        using namespace jtx;
        Env env(*this);
        wipe(env.openLedger);
        auto const open = env.open();
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
        wipe(env.openLedger);
        auto const open = env.open();
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
            expect(seq(v1.read(k(1))) == 1);
            expect(! v1.exists(k(2)));
            expect(seq(v1.read(k(3))) == 3);
            expect(seq(v1.read(k(4))) == 5);
            {
                Sandbox v2(&v1);
                auto s = v2.peek(k(3));
                seq(s, 6);
                v2.update(s);
                v2.erase(v2.peek(k(4)));
                expect(seq(v2.read(k(1))) == 1);
                expect(! v2.exists(k(2)));
                expect(seq(v2.read(k(3))) == 6);
                expect(! v2.exists(k(4)));
                // discard v2
            }
            expect(seq(v1.read(k(1))) == 1);
            expect(! v1.exists(k(2)));
            expect(seq(v1.read(k(3))) == 3);
            expect(seq(v1.read(k(4))) == 5);

            {
                Sandbox v2(&v1);
                auto s = v2.peek(k(3));
                seq(s, 6);
                v2.update(s);
                v2.erase(v2.peek(k(4)));
                expect(seq(v2.read(k(1))) == 1);
                expect(! v2.exists(k(2)));
                expect(seq(v2.read(k(3))) == 6);
                expect(! v2.exists(k(4)));
                v2.apply(v1);
            }
            expect(seq(v1.read(k(1))) == 1);
            expect(! v1.exists(k(2)));
            expect(seq(v1.read(k(3))) == 6);
            expect(! v1.exists(k(4)));
            v1.apply(v0);
        }
        expect(seq(v0.read(k(1))) == 1);
        expect(! v0.exists(k(2)));
        expect(seq(v0.read(k(3))) == 6);
        expect(! v0.exists(k(4)));
    }

    // Verify contextual information
    void
    testContext()
    {
        using namespace jtx;
        {
            Env env(*this);
            wipe(env.openLedger);
            auto const open = env.open();
            OpenView v0(open.get());
            expect(v0.seq() != 98);
            expect(v0.seq() == open->seq());
            expect(v0.parentCloseTime() != 99);
            expect(v0.parentCloseTime() ==
                open->parentCloseTime());
            {
                // shallow copy
                OpenView v1(v0);
                expect (v1.seq() == v0.seq());
                expect (v1.parentCloseTime() ==
                    v1.parentCloseTime());

                ApplyViewImpl v2(&v1, tapNO_CHECK_SIGN);
                expect(v2.parentCloseTime() ==
                    v1.parentCloseTime());
                expect(v2.seq() == v1.seq());
                expect(v2.flags() == tapNO_CHECK_SIGN);

                Sandbox v3(&v2);
                expect(v3.seq() == v2.seq());
                expect(v3.parentCloseTime() ==
                    v2.parentCloseTime());
                expect(v3.flags() == tapNO_CHECK_SIGN);
            }
            {
                ApplyViewImpl v1(&v0, tapNO_CHECK_SIGN);
                PaymentSandbox v2(&v1);
                expect(v2.seq() == v0.seq());
                expect(v2.parentCloseTime() ==
                    v0.parentCloseTime());
                expect(v2.flags() == tapNO_CHECK_SIGN);
                PaymentSandbox v3(&v2);
                expect(v3.seq() == v2.seq());
                expect(v3.parentCloseTime() ==
                    v2.parentCloseTime());
                expect(v3.flags() == v2.flags());
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
        Config const config;
        std::shared_ptr<Ledger const> const genesis =
            std::make_shared<Ledger> (
                create_genesis, config);
        auto const ledger =
            std::make_shared<Ledger> (
                open_ledger, *genesis);
        auto setup123 = [&ledger, this]()
        {
            // erase middle element
            wipe (*ledger);
            ledger->rawInsert (sle (1));
            ledger->rawInsert (sle (2));
            ledger->rawInsert (sle (3));
            expect (sles (*ledger) == list (1, 2, 3));
        };
        {
            setup123 ();
            OpenView view (ledger.get ());
            view.rawErase (sle (1));
            view.rawInsert (sle (4));
            view.rawInsert (sle (5));
            expect (sles (view) == list (2, 3, 4, 5));
            auto b = view.sles.begin();
            expect (view.sles.upper_bound(uint256(1)) == b); ++b;
            expect (view.sles.upper_bound(uint256(2)) == b); ++b;
            expect (view.sles.upper_bound(uint256(3)) == b); ++b;
            expect (view.sles.upper_bound(uint256(4)) == b); ++b;
            expect (view.sles.upper_bound(uint256(5)) == b);
        }
        {
            setup123 ();
            OpenView view (ledger.get ());
            view.rawErase (sle (1));
            view.rawErase (sle (2));
            view.rawInsert (sle (4));
            view.rawInsert (sle (5));
            expect (sles (view) == list (3, 4, 5));
            auto b = view.sles.begin();
            expect (view.sles.upper_bound(uint256(1)) == b);
            expect (view.sles.upper_bound(uint256(2)) == b); ++b;
            expect (view.sles.upper_bound(uint256(3)) == b); ++b;
            expect (view.sles.upper_bound(uint256(4)) == b); ++b;
            expect (view.sles.upper_bound(uint256(5)) == b);
        }
        {
            setup123 ();
            OpenView view (ledger.get ());
            view.rawErase (sle (1));
            view.rawErase (sle (2));
            view.rawErase (sle (3));
            view.rawInsert (sle (4));
            view.rawInsert (sle (5));
            expect (sles (view) == list (4, 5));
            auto b = view.sles.begin();
            expect (view.sles.upper_bound(uint256(1)) == b);
            expect (view.sles.upper_bound(uint256(2)) == b);
            expect (view.sles.upper_bound(uint256(3)) == b); ++b;
            expect (view.sles.upper_bound(uint256(4)) == b); ++b;
            expect (view.sles.upper_bound(uint256(5)) == b);
        }
        {
            setup123 ();
            OpenView view (ledger.get ());
            view.rawErase (sle (3));
            view.rawInsert (sle (4));
            view.rawInsert (sle (5));
            expect (sles (view) == list (1, 2, 4, 5));
            auto b = view.sles.begin();
            ++b;
            expect (view.sles.upper_bound(uint256(1)) == b); ++b;
            expect (view.sles.upper_bound(uint256(2)) == b);
            expect (view.sles.upper_bound(uint256(3)) == b); ++b;
            expect (view.sles.upper_bound(uint256(4)) == b); ++b;
            expect (view.sles.upper_bound(uint256(5)) == b);
        }
        {
            setup123 ();
            OpenView view (ledger.get ());
            view.rawReplace (sle (1, 10));
            view.rawReplace (sle (3, 30));
            expect (sles (view) == list (1, 2, 3));
            expect (seq (view.read(k (1))) == 10);
            expect (seq (view.read(k (2))) == 1);
            expect (seq (view.read(k (3))) == 30);

            view.rawErase (sle (3));
            expect (sles (view) == list (1, 2));
            auto b = view.sles.begin();
            ++b;
            expect (view.sles.upper_bound(uint256(1)) == b); ++b;
            expect (view.sles.upper_bound(uint256(2)) == b);
            expect (view.sles.upper_bound(uint256(3)) == b);
            expect (view.sles.upper_bound(uint256(4)) == b);
            expect (view.sles.upper_bound(uint256(5)) == b);

            view.rawInsert (sle (5));
            view.rawInsert (sle (4));
            view.rawInsert (sle (3));
            expect (sles (view) == list (1, 2, 3, 4, 5));
            b = view.sles.begin();
            ++b;
            expect (view.sles.upper_bound(uint256(1)) == b); ++b;
            expect (view.sles.upper_bound(uint256(2)) == b); ++b;
            expect (view.sles.upper_bound(uint256(3)) == b); ++b;
            expect (view.sles.upper_bound(uint256(4)) == b); ++b;
            expect (view.sles.upper_bound(uint256(5)) == b);
        }
    }

    void
    testRegressions()
    {
        using namespace jtx;

        // Create a ledger with 1 item, put a
        // ApplyView on that, then another ApplyView,
        // erase the item, apply.
        {
            Config const config;
            std::shared_ptr<Ledger const> const genesis =
                std::make_shared<Ledger>(
                    create_genesis, config);
            auto const ledger =
                std::make_shared<Ledger>(
                    open_ledger, *genesis);
            wipe(*ledger);
            ledger->rawInsert(sle(1));
            ReadView& v0 = *ledger;
            ApplyViewImpl v1(&v0, tapNONE);
            {
                Sandbox v2(&v1);
                v2.erase(v2.peek(k(1)));
                v2.apply(v1);
            }
            expect(! v1.exists(k(1)));
        }
    }

    void run()
    {
        // This had better work, or else
        expect(k(0).key < k(1).key);

        testLedger();
        testMeta();
        testMetaSucc();
        testStacked();
        testContext();
        testSles();
        testRegressions();
    }
};

BEAST_DEFINE_TESTSUITE(View,ledger,ripple);

}  // test
}  // ripple
