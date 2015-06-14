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
#include <ripple/app/ledger/MetaView.h>
#include <ripple/ledger/ViewAPI.h>
#include <beast/cxx14/type_traits.h> // <type_traits>

namespace ripple {
namespace test {

class MetaView_test
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
    wipe (BasicView& v)
    {
        // HACK!
        boost::optional<uint256> next;
        next.emplace(0);
        for(;;)
        {
            next = v.succ(*next);
            if (! next)
                break;
            v.unchecked_erase(*next);
        }
    }

    // Test succ correctness
    void
    succ (BasicView const& v,
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

    // Exercise Ledger implementation of View
    void
    testLedger()
    {
        using namespace jtx;
        Env env(*this);
        wipe(*env.ledger);
        BasicView& v = *env.ledger;
        succ(v, 0, boost::none);
        v.unchecked_insert(sle(1, 1));
        expect(v.exists(k(1)));
        expect(seq(v.read(k(1))) == 1);
        succ(v, 0, 1);
        succ(v, 1, boost::none);
        v.unchecked_insert(sle(2, 2));
        expect(seq(v.read(k(2))) == 2);
        v.unchecked_insert(sle(3, 3));
        expect(seq(v.read(k(3))) == 3);
        auto s = copy(v.read(k(2)));
        seq(s, 4);
        v.unchecked_replace(std::move(s));
        expect(seq(v.read(k(2))) == 4);
        expect(v.unchecked_erase(k(2).key));
        expect(! v.exists(k(2)));
        expect(v.exists(k(1)));
        expect(v.exists(k(3)));
        expect(! v.unchecked_erase(k(5).key));
    }

    void
    testMeta()
    {
        using namespace jtx;
        Env env(*this);
        wipe(*env.ledger);
        MetaView v(*env.ledger, false);
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
        wipe(*env.ledger);
        BasicView& v0 = *env.ledger;

        v0.unchecked_insert(sle(1));
        v0.unchecked_insert(sle(2));
        v0.unchecked_insert(sle(4));
        v0.unchecked_insert(sle(7));
        {
            MetaView v1(v0, false);
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

            v1.apply();
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
        wipe(*env.ledger);
        BasicView& v0 = *env.ledger;
        v0.unchecked_insert(sle(1, 1));
        v0.unchecked_insert(sle(2, 2));
        v0.unchecked_insert(sle(4, 4));

        {
            MetaView v1(v0, true);
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
                MetaView v2(v1, true);
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
                MetaView v2(v1, true);
                auto s = v2.peek(k(3));
                seq(s, 6);
                v2.update(s);
                v2.erase(v2.peek(k(4)));
                expect(seq(v2.read(k(1))) == 1);
                expect(! v2.exists(k(2)));
                expect(seq(v2.read(k(3))) == 6);
                expect(! v2.exists(k(4)));
                v2.apply();
            }
            expect(seq(v1.read(k(1))) == 1);
            expect(! v1.exists(k(2)));
            expect(seq(v1.read(k(3))) == 6);
            expect(! v1.exists(k(4)));
            v1.apply();
        }
        expect(seq(v0.read(k(1))) == 1);
        expect(! v0.exists(k(2)));
        expect(seq(v0.read(k(3))) == 6);
        expect(! v0.exists(k(4)));
    }

    // Regression test:
    //  Create a ledger with 1 item, put a
    //  MetaView on that, then another MetaView,
    //  erase the item, apply.
    void
    testStackRegress()
    {
        using namespace jtx;
        Env env(*this);
        wipe(*env.ledger);
        BasicView& v0 = *env.ledger;
        v0.unchecked_insert(sle(1));
        MetaView v1(v0, true);
        {
            MetaView v2(v1, true);
            v2.erase(v2.peek(k(1)));
            v2.apply();
        }
        expect(! v1.exists(k(1)));
    }

    void run()
    {
        // This had better work, or else
        expect(k(0).key < k(1).key);

        testLedger();
        testMeta();
        testMetaSucc();
        testStacked();
        testStackRegress();
    }
};

BEAST_DEFINE_TESTSUITE(MetaView,app,ripple);

}  // test
}  // ripple
