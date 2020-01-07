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

#include <test/jtx.h>
#include <ripple/ledger/BookDirs.h>
#include <ripple/protocol/Feature.h>

namespace ripple {
namespace test {

struct BookDirs_test : public beast::unit_test::suite
{
    void test_bookdir(FeatureBitset features)
    {
        using namespace jtx;
        Env env(*this, features);
        auto gw = Account("gw");
        auto USD = gw["USD"];
        env.fund(XRP(1000000), "alice", "bob", "gw");

        {
            Book book(xrpIssue(), USD.issue());
            {
                auto d = BookDirs(*env.current(), book);
                BEAST_EXPECT(std::begin(d) == std::end(d));
                BEAST_EXPECT(std::distance(d.begin(), d.end()) == 0);
            }
            {
                auto d = BookDirs(*env.current(), reversed(book));
                BEAST_EXPECT(std::distance(d.begin(), d.end()) == 0);
            }
        }

        {
            env(offer("alice", Account("alice")["USD"](50), XRP(10)));
            auto d = BookDirs(*env.current(),
                Book(Account("alice")["USD"].issue(), xrpIssue()));
            BEAST_EXPECT(std::distance(d.begin(), d.end()) == 1);
        }

        {
            env(offer("alice", gw["CNY"](50), XRP(10)));
            auto d = BookDirs(*env.current(),
                Book(gw["CNY"].issue(), xrpIssue()));
            BEAST_EXPECT(std::distance(d.begin(), d.end()) == 1);
        }

        {
            env.trust(Account("bob")["CNY"](10), "alice");
            env(pay("bob", "alice", Account("bob")["CNY"](10)));
            env(offer("alice", USD(50), Account("bob")["CNY"](10)));
            auto d = BookDirs(*env.current(),
                Book(USD.issue(), Account("bob")["CNY"].issue()));
            BEAST_EXPECT(std::distance(d.begin(), d.end()) == 1);
        }

        {
            auto AUD = gw["AUD"];
            for (auto i = 1, j = 3; i <= 3; ++i, --j)
                for (auto k = 0; k < 80; ++k)
                    env(offer("alice", AUD(i), XRP(j)));

            auto d = BookDirs(*env.current(),
                Book(AUD.issue(), xrpIssue()));
            BEAST_EXPECT(std::distance(d.begin(), d.end()) == 240);
            auto i = 1, j = 3, k = 0;
            for (auto const& e : d)
            {
                BEAST_EXPECT(e->getFieldAmount(sfTakerPays) == AUD(i));
                BEAST_EXPECT(e->getFieldAmount(sfTakerGets) == XRP(j));
                if (++k % 80 == 0)
                {
                    ++i;
                    --j;
                }
            }
        }
    }

    void run() override
    {
        using namespace jtx;
        auto const sa = supported_amendments();
        test_bookdir(sa - fix1373 - featureFlowCross);
        test_bookdir(sa           - featureFlowCross);
        test_bookdir(sa);
    }
};

BEAST_DEFINE_TESTSUITE(BookDirs,ledger,ripple);

} // test
} // ripple
