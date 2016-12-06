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

#include <ripple/beast/xor_shift_engine.h>
#include <ripple/ledger/Directory.h>
#include <ripple/ledger/Sandbox.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/protocol/Protocol.h>
#include <ripple/test/jtx.h>
#include <algorithm>

namespace ripple {
namespace test {

struct Directory_test : public beast::unit_test::suite
{
    // Map [0-15576] into a a unique 3 letter currency code
    std::string
    currcode (std::size_t i)
    {
        // There are only 17576 possible combinations
        BEAST_EXPECT (i < 17577);

        std::string code;

        for (int j = 0; j != 3; ++j)
        {
            code.push_back ('A' + (i % 26));
            i /= 26;
        }

        return code;
    }

    void testDirectoryWithoutPageOrdering()
    {
        testcase ("Directory Insertion Without Page Ordering");

        using namespace jtx;
        Env env(*this);
        auto gw = Account("gw");
        auto USD = gw["USD"];

        auto alice = Account("alice");
        auto bob = Account("bob");

        {
            auto dir = Dir(*env.current(),
                keylet::ownerDir(alice));
            BEAST_EXPECT(std::begin(dir) == std::end(dir));
            BEAST_EXPECT(std::end(dir) == dir.find(uint256(), uint256()));
        }

        env.fund(XRP(10000000), alice, bob, gw);

        // Insert 400 offers from Alice, then one from Bob:
        for (std::size_t i = 1; i <= 400; ++i)
            env(offer(alice, USD(10), XRP(10)));
        env(offer(bob, USD(100), XRP(100)));

        // Check Bob's directory: it should contain exactly
        // one entry, listing the offer he added.
        {
            auto const dir = Dir(*env.current(),
                keylet::ownerDir(bob));

            auto iter = std::begin(dir);

            BEAST_EXPECT(iter->get()->getFieldAmount(sfTakerPays) == USD(100));
            BEAST_EXPECT(iter->get()->getFieldAmount(sfTakerGets) == XRP(100));
            BEAST_EXPECT(std::next(iter) == std::end(dir));
        }

        // Check Alice's directory: it should contain one
        // entry for each offer she added, and, within each
        // page, the entries should be in sorted order.
        {
            auto dir = Dir(*env.current(),
                keylet::ownerDir(alice));

            std::uint32_t lastSeq = 1;

            // Check that the orders are sequential by checking
            // that their sequence numbers are:
            for (auto iter = dir.begin(); iter != std::end(dir); ++iter)
                BEAST_EXPECT(++lastSeq == (*iter)->getFieldU32(sfSequence));

            BEAST_EXPECT(lastSeq != 1);
        }
    }

    void
    testDirIsEmpty()
    {
        testcase ("dirIsEmpty");

        using namespace jtx;
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const charlie = Account ("charlie");
        auto const gw = Account ("gw");

        beast::xor_shift_engine eng;

        Env env(*this, features(featureMultiSign));

        env.fund(XRP(1000000), alice, charlie, gw);
        env.close();

        // alice should have an empty directory.
        BEAST_EXPECT(dirIsEmpty (*env.closed(), keylet::ownerDir(alice)));

        // Give alice a signer list, then there will be stuff in the directory.
        env(signers(alice, 1, { { bob, 1} }));
        env.close();
        BEAST_EXPECT(! dirIsEmpty (*env.closed(), keylet::ownerDir(alice)));

        env(signers(alice, jtx::none));
        env.close();
        BEAST_EXPECT(dirIsEmpty (*env.closed(), keylet::ownerDir(alice)));

        std::vector<IOU> const currencies = [this,&eng,&gw]()
        {
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

            BEAST_EXPECT(! dirIsEmpty (*env.closed(), keylet::ownerDir(alice)));

            std::shuffle (cl.begin(), cl.end(), eng);

            for (auto const& c : cl)
            {
                env(trust(alice, c(0)));
                env.close();
            }

            BEAST_EXPECT(dirIsEmpty (*env.closed(), keylet::ownerDir(alice)));
        }

        // Now, Alice creates offers to buy currency, creating
        // implicit trust lines.
        {
            auto cl = currencies;

            BEAST_EXPECT(dirIsEmpty (*env.closed(), keylet::ownerDir(alice)));

            for (auto c : currencies)
            {
                env(trust(charlie, c(50)));
                env.close();
                env(pay(gw, charlie, c(50)));
                env.close();
                env(offer(alice, c(50), XRP(50)));
                env.close();
            }

            BEAST_EXPECT(! dirIsEmpty (*env.closed(), keylet::ownerDir(alice)));

            // Now fill the offers in a random order. Offer
            // entries will drop, and be replaced by trust
            // lines that are implicitly created.
            std::shuffle (cl.begin(), cl.end(), eng);

            for (auto const& c : cl)
            {
                env(offer(charlie, XRP(50), c(50)));
                env.close();
            }
            BEAST_EXPECT(! dirIsEmpty (*env.closed(), keylet::ownerDir(alice)));
            // Finally, Alice now sends the funds back to
            // Charlie. The implicitly created trust lines
            // should drop away:
            std::shuffle (cl.begin(), cl.end(), eng);

            for (auto const& c : cl)
            {
                env(pay(alice, charlie, c(50)));
                env.close();
            }

            BEAST_EXPECT(dirIsEmpty (*env.closed(), keylet::ownerDir(alice)));
        }
    }

    void
    testRipd1353()
    {
        testcase("RIPD-1353 Empty Offer Directories");

        using namespace jtx;
        Env env{*this};
        auto const gw = Account{"gateway"};
        auto const alice = Account{"alice"};
        auto const USD = gw["USD"];

        env.fund(XRP(10000), alice, gw);
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
                Json::Value cancelOffer;
                cancelOffer[jss::Account] = alice.human();
                cancelOffer[jss::OfferSequence] =
                    Json::UInt(firstOfferSeq + page * dirNodeMaxEntries + i);
                cancelOffer[jss::TransactionType] = "OfferCancel";
                env(cancelOffer);
                env.close();
            }
        }

        // All the offers have been cancelled, so the book
        // should have no entries and be empty:
        {
            Sandbox sb(env.closed().get(), tapNONE);
            uint256 const bookBase = getBookBase({xrpIssue(), USD.issue()});

            BEAST_EXPECT(dirIsEmpty (sb, keylet::page(bookBase)));
            BEAST_EXPECT (!sb.succ(bookBase, getQualityNext(bookBase)));
        }

        // Alice returns the USD she has to the gateway
        // and removes her trust line. Her owner directory
        // should now be empty:
        {
            env.trust(USD(0), alice);
            env(pay(alice, gw, alice["USD"](1000)));
            env.close();
            BEAST_EXPECT(dirIsEmpty (*env.closed(), keylet::ownerDir(alice)));
        }
    }

    void run() override
    {
        testDirectoryWithoutPageOrdering();
        testDirIsEmpty();
        testRipd1353();
    }
};

}
}
