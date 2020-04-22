//-----------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2015 Ripple Labs Inc.

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

#include <ripple/app/ledger/Ledger.h>
#include <ripple/basics/Log.h>
#include <ripple/beast/unit_test.h>
#include <ripple/ledger/View.h>
#include <test/jtx.h>

namespace ripple {
namespace test {

class SkipList_test : public beast::unit_test::suite
{
    void
    testSkipList()
    {
        jtx::Env env(*this);
        std::vector<std::shared_ptr<Ledger>> history;
        {
            Config config;
            auto prev = std::make_shared<Ledger>(
                create_genesis,
                config,
                std::vector<uint256>{},
                env.app().family());
            history.push_back(prev);
            for (auto i = 0; i < 1023; ++i)
            {
                auto next = std::make_shared<Ledger>(
                    *prev, env.app().timeKeeper().closeTime());
                next->updateSkipList();
                history.push_back(next);
                prev = next;
            }
        }

        {
            auto l = *(std::next(std::begin(history)));
            BEAST_EXPECT((*std::begin(history))->info().seq < l->info().seq);
            BEAST_EXPECT(
                hashOfSeq(*l, l->info().seq + 1, env.journal) == boost::none);
            BEAST_EXPECT(
                hashOfSeq(*l, l->info().seq, env.journal) == l->info().hash);
            BEAST_EXPECT(
                hashOfSeq(*l, l->info().seq - 1, env.journal) ==
                l->info().parentHash);
            BEAST_EXPECT(
                hashOfSeq(*history.back(), l->info().seq, env.journal) ==
                boost::none);
        }

        // ledger skip lists store up to the previous 256 hashes
        for (auto i = history.crbegin(); i != history.crend(); i += 256)
        {
            for (auto n = i;
                 n != std::next(i, (*i)->info().seq - 256 > 1 ? 257 : 256);
                 ++n)
            {
                BEAST_EXPECT(
                    hashOfSeq(**i, (*n)->info().seq, env.journal) ==
                    (*n)->info().hash);
            }

            // edge case accessing beyond 256
            BEAST_EXPECT(
                hashOfSeq(**i, (*i)->info().seq - 258, env.journal) ==
                boost::none);
        }

        // every 256th hash beyond the first 256 is stored
        for (auto i = history.crbegin(); i != std::next(history.crend(), -512);
             i += 256)
        {
            for (auto n = std::next(i, 512); n != history.crend(); n += 256)
            {
                BEAST_EXPECT(
                    hashOfSeq(**i, (*n)->info().seq, env.journal) ==
                    (*n)->info().hash);
            }
        }
    }

    void
    run() override
    {
        testSkipList();
    }
};

BEAST_DEFINE_TESTSUITE(SkipList, ledger, ripple);

}  // namespace test
}  // namespace ripple
