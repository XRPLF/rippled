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
#include <ripple/ledger/Directory.h>

namespace ripple {
namespace test {

struct Directory_test : public beast::unit_test::suite
{
    void testDirectory()
    {
        using namespace jtx;
        Env env(*this);
        auto gw = Account("gw");
        auto USD = gw["USD"];

        {
            auto dir = Dir(*env.current(),
                keylet::ownerDir(Account("alice")));
            BEAST_EXPECT(std::begin(dir) == std::end(dir));
            BEAST_EXPECT(std::end(dir) == dir.find(uint256(), uint256()));
        }

        env.fund(XRP(10000), "alice", "bob", gw);

        auto i = 10;
        for (; i <= 400; i += 10)
            env(offer("alice", USD(i), XRP(10)));
        env(offer("bob", USD(500), XRP(10)));

        {
            auto dir = Dir(*env.current(),
                keylet::ownerDir(Account("bob")));
            BEAST_EXPECT(std::begin(dir)->get()->
                getFieldAmount(sfTakerPays) == USD(500));
        }

        auto dir = Dir(*env.current(),
            keylet::ownerDir(Account("alice")));
        i = 0;
        for (auto const& e : dir)
            BEAST_EXPECT(e->getFieldAmount(sfTakerPays) == USD(i += 10));

        BEAST_EXPECT(std::begin(dir) != std::end(dir));
        BEAST_EXPECT(std::end(dir) ==
            dir.find(std::begin(dir).page().key,
                uint256()));
        BEAST_EXPECT(std::begin(dir) ==
            dir.find(std::begin(dir).page().key,
                std::begin(dir).index()));
        auto entry = std::next(std::begin(dir), 32);
        auto it = dir.find(entry.page().key, entry.index());
        BEAST_EXPECT(it != std::end(dir));
        BEAST_EXPECT((*it)->getFieldAmount(sfTakerPays) == USD(330));
    }

    void run() override
    {
        testDirectory();
    }
};

BEAST_DEFINE_TESTSUITE(Directory,ledger,ripple);

} // test
} // ripple
