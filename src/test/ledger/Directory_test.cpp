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

#include <ripple/basics/random.h>
#include <ripple/ledger/BookDirs.h>
#include <ripple/ledger/Directory.h>
#include <ripple/ledger/Sandbox.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/Protocol.h>
#include <ripple/protocol/jss.h>
#include <algorithm>
#include <test/jtx.h>

namespace ripple {
namespace test {

struct Directory_test : public beast::unit_test::suite
{
    // Map [0-15576] into a a unique 3 letter currency code
    std::string
    currcode(std::size_t i)
    {
        // There are only 17576 possible combinations
        BEAST_EXPECT(i < 17577);

        std::string code;

        for (int j = 0; j != 3; ++j)
        {
            code.push_back('A' + (i % 26));
            i /= 26;
        }

        return code;
    }

    // Insert n empty pages, numbered [0, ... n - 1], in the
    // specified directory:
    void
    makePages(Sandbox& sb, uint256 const& base, std::uint64_t n)
    {
        for (std::uint64_t i = 0; i < n; ++i)
        {
            auto p = std::make_shared<SLE>(keylet::page(base, i));

            p->setFieldV256(sfIndexes, STVector256{});

            if (i + 1 == n)
                p->setFieldU64(sfIndexNext, 0);
            else
                p->setFieldU64(sfIndexNext, i + 1);

            if (i == 0)
                p->setFieldU64(sfIndexPrevious, n - 1);
            else
                p->setFieldU64(sfIndexPrevious, i - 1);

            sb.insert(p);
        }
    }

    void
    testDirectoryOrdering()
    {
        using namespace jtx;

        auto gw = Account("gw");
        auto USD = gw["USD"];
        auto alice = Account("alice");
        auto bob = Account("bob");

        testcase("Directory Ordering (with 'SortedDirectories' amendment)");

        Env env(*this);
        env.fund(XRP(10000000), alice, gw);

        std::uint32_t const firstOfferSeq{env.seq(alice)};
        for (std::size_t i = 1; i <= 400; ++i)
            env(offer(alice, USD(i), XRP(i)));
        env.close();

        // Check Alice's directory: it should contain one
        // entry for each offer she added, and, within each
        // page the entries should be in sorted order.
        {
            auto const view = env.closed();

            std::uint64_t page = 0;

            do
            {
                auto p =
                    view->read(keylet::page(keylet::ownerDir(alice), page));

                // Ensure that the entries in the page are sorted
                auto const& v = p->getFieldV256(sfIndexes);
                BEAST_EXPECT(std::is_sorted(v.begin(), v.end()));

                // Ensure that the page contains the correct orders by
                // calculating which sequence numbers belong here.
                std::uint32_t const minSeq =
                    firstOfferSeq + (page * dirNodeMaxEntries);
                std::uint32_t const maxSeq = minSeq + dirNodeMaxEntries;

                for (auto const& e : v)
                {
                    auto c = view->read(keylet::child(e));
                    BEAST_EXPECT(c);
                    BEAST_EXPECT(c->getFieldU32(sfSequence) >= minSeq);
                    BEAST_EXPECT(c->getFieldU32(sfSequence) < maxSeq);
                }

                page = p->getFieldU64(sfIndexNext);
            } while (page != 0);
        }

        // Now check the orderbook: it should be in the order we placed
        // the offers.
        auto book = BookDirs(*env.current(), Book({xrpIssue(), USD.issue()}));
        int count = 1;

        for (auto const& offer : book)
        {
            count++;
            BEAST_EXPECT(offer->getFieldAmount(sfTakerPays) == USD(count));
            BEAST_EXPECT(offer->getFieldAmount(sfTakerGets) == XRP(count));
        }
    }

    void
    testDirIsEmpty()
    {
        testcase("dirIsEmpty");

        using namespace jtx;
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const charlie = Account("charlie");
        auto const gw = Account("gw");

        Env env(*this);

        env.fund(XRP(1000000), alice, charlie, gw);
        env.close();

        // alice should have an empty directory.
        BEAST_EXPECT(dirIsEmpty(*env.closed(), keylet::ownerDir(alice)));

        // Give alice a signer list, then there will be stuff in the directory.
        env(signers(alice, 1, {{bob, 1}}));
        env.close();
        BEAST_EXPECT(!dirIsEmpty(*env.closed(), keylet::ownerDir(alice)));

        env(signers(alice, jtx::none));
        env.close();
        BEAST_EXPECT(dirIsEmpty(*env.closed(), keylet::ownerDir(alice)));

        std::vector<IOU> const currencies = [this, &gw]() {
            std::vector<IOU> c;

            c.reserve((2 * dirNodeMaxEntries) + 3);

            while (c.size() != c.capacity())
                c.push_back(gw[currcode(c.size())]);

            return c;
        }();

        // First, Alices creates a lot of trustlines, and then
        // deletes them in a different order:
        {
            auto cl = currencies;

            for (auto const& c : cl)
            {
                env(trust(alice, c(50)));
                env.close();
            }

            BEAST_EXPECT(!dirIsEmpty(*env.closed(), keylet::ownerDir(alice)));

            std::shuffle(cl.begin(), cl.end(), default_prng());

            for (auto const& c : cl)
            {
                env(trust(alice, c(0)));
                env.close();
            }

            BEAST_EXPECT(dirIsEmpty(*env.closed(), keylet::ownerDir(alice)));
        }

        // Now, Alice creates offers to buy currency, creating
        // implicit trust lines.
        {
            auto cl = currencies;

            BEAST_EXPECT(dirIsEmpty(*env.closed(), keylet::ownerDir(alice)));

            for (auto c : currencies)
            {
                env(trust(charlie, c(50)));
                env.close();
                env(pay(gw, charlie, c(50)));
                env.close();
                env(offer(alice, c(50), XRP(50)));
                env.close();
            }

            BEAST_EXPECT(!dirIsEmpty(*env.closed(), keylet::ownerDir(alice)));

            // Now fill the offers in a random order. Offer
            // entries will drop, and be replaced by trust
            // lines that are implicitly created.
            std::shuffle(cl.begin(), cl.end(), default_prng());

            for (auto const& c : cl)
            {
                env(offer(charlie, XRP(50), c(50)));
                env.close();
            }
            BEAST_EXPECT(!dirIsEmpty(*env.closed(), keylet::ownerDir(alice)));
            // Finally, Alice now sends the funds back to
            // Charlie. The implicitly created trust lines
            // should drop away:
            std::shuffle(cl.begin(), cl.end(), default_prng());

            for (auto const& c : cl)
            {
                env(pay(alice, charlie, c(50)));
                env.close();
            }

            BEAST_EXPECT(dirIsEmpty(*env.closed(), keylet::ownerDir(alice)));
        }
    }

    void
    testRipd1353()
    {
        testcase("RIPD-1353 Empty Offer Directories");

        using namespace jtx;
        Env env(*this);

        auto const gw = Account{"gateway"};
        auto const alice = Account{"alice"};
        auto const USD = gw["USD"];

        env.fund(XRP(10000), alice, gw);
        env.close();
        env.trust(USD(1000), alice);
        env(pay(gw, alice, USD(1000)));

        auto const firstOfferSeq = env.seq(alice);

        // Fill up three pages of offers
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < dirNodeMaxEntries; ++j)
                env(offer(alice, XRP(1), USD(1)));
        env.close();

        // remove all the offers. Remove the middle page last
        for (auto page : {0, 2, 1})
        {
            for (int i = 0; i < dirNodeMaxEntries; ++i)
            {
                env(offer_cancel(
                    alice, firstOfferSeq + page * dirNodeMaxEntries + i));
                env.close();
            }
        }

        // All the offers have been cancelled, so the book
        // should have no entries and be empty:
        {
            Sandbox sb(env.closed().get(), tapNONE);
            uint256 const bookBase = getBookBase({xrpIssue(), USD.issue()});

            BEAST_EXPECT(dirIsEmpty(sb, keylet::page(bookBase)));
            BEAST_EXPECT(!sb.succ(bookBase, getQualityNext(bookBase)));
        }

        // Alice returns the USD she has to the gateway
        // and removes her trust line. Her owner directory
        // should now be empty:
        {
            env.trust(USD(0), alice);
            env(pay(alice, gw, alice["USD"](1000)));
            env.close();
            BEAST_EXPECT(dirIsEmpty(*env.closed(), keylet::ownerDir(alice)));
        }
    }

    void
    testEmptyChain()
    {
        testcase("Empty Chain on Delete");

        using namespace jtx;
        Env env(*this);

        auto const gw = Account{"gateway"};
        auto const alice = Account{"alice"};
        auto const USD = gw["USD"];

        env.fund(XRP(10000), alice);
        env.close();

        constexpr uint256 base(
            "fb71c9aa3310141da4b01d6c744a98286af2d72ab5448d5adc0910ca0c910880");

        constexpr uint256 item(
            "bad0f021aa3b2f6754a8fe82a5779730aa0bbbab82f17201ef24900efc2c7312");

        {
            // Create a chain of three pages:
            Sandbox sb(env.closed().get(), tapNONE);
            makePages(sb, base, 3);

            // Insert an item in the middle page:
            {
                auto p = sb.peek(keylet::page(base, 1));
                BEAST_EXPECT(p);

                STVector256 v;
                v.push_back(item);
                p->setFieldV256(sfIndexes, v);
                sb.update(p);
            }

            // Now, try to delete the item from the middle
            // page. This should cause all pages to be deleted:
            BEAST_EXPECT(sb.dirRemove(
                keylet::page(base, 0), 1, keylet::unchecked(item), false));
            BEAST_EXPECT(!sb.peek(keylet::page(base, 2)));
            BEAST_EXPECT(!sb.peek(keylet::page(base, 1)));
            BEAST_EXPECT(!sb.peek(keylet::page(base, 0)));
        }

        {
            // Create a chain of four pages:
            Sandbox sb(env.closed().get(), tapNONE);
            makePages(sb, base, 4);

            // Now add items on pages 1 and 2:
            {
                auto p1 = sb.peek(keylet::page(base, 1));
                BEAST_EXPECT(p1);

                STVector256 v1;
                v1.push_back(~item);
                p1->setFieldV256(sfIndexes, v1);
                sb.update(p1);

                auto p2 = sb.peek(keylet::page(base, 2));
                BEAST_EXPECT(p2);

                STVector256 v2;
                v2.push_back(item);
                p2->setFieldV256(sfIndexes, v2);
                sb.update(p2);
            }

            // Now, try to delete the item from page 2.
            // This should cause pages 2 and 3 to be
            // deleted:
            BEAST_EXPECT(sb.dirRemove(
                keylet::page(base, 0), 2, keylet::unchecked(item), false));
            BEAST_EXPECT(!sb.peek(keylet::page(base, 3)));
            BEAST_EXPECT(!sb.peek(keylet::page(base, 2)));

            auto p1 = sb.peek(keylet::page(base, 1));
            BEAST_EXPECT(p1);
            BEAST_EXPECT(p1->getFieldU64(sfIndexNext) == 0);
            BEAST_EXPECT(p1->getFieldU64(sfIndexPrevious) == 0);

            auto p0 = sb.peek(keylet::page(base, 0));
            BEAST_EXPECT(p0);
            BEAST_EXPECT(p0->getFieldU64(sfIndexNext) == 1);
            BEAST_EXPECT(p0->getFieldU64(sfIndexPrevious) == 1);
        }
    }

    void
    run() override
    {
        testDirectoryOrdering();
        testDirIsEmpty();
        testRipd1353();
        testEmptyChain();
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(Directory, ledger, ripple, 1);

}  // namespace test
}  // namespace ripple
