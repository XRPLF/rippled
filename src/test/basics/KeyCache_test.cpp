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

#include <test/unit_test/SuiteJournal.h>

#include <xrpl/basics/TaggedCache.h>
#include <xrpl/basics/TaggedCache.ipp>
#include <xrpl/basics/chrono.h>
#include <xrpl/protocol/Protocol.h>

namespace ripple {

class KeyCache_test : public beast::unit_test::suite
{
public:
    void
    run() override
    {
        using namespace std::chrono_literals;
        TestStopwatch clock;
        clock.set(0);

        using Key = std::string;
        using Cache = TaggedCache<Key, int, true>;

        test::SuiteJournal j("KeyCacheTest", *this);

        // Insert an item, retrieve it, and age it so it gets purged.
        {
            Cache c("test", LedgerIndex(1), 2s, clock, j);

            BEAST_EXPECT(c.size() == 0);
            BEAST_EXPECT(c.insert("one"));
            BEAST_EXPECT(!c.insert("one"));
            BEAST_EXPECT(c.size() == 1);
            BEAST_EXPECT(c.touch_if_exists("one"));
            ++clock;
            c.sweep();
            BEAST_EXPECT(c.size() == 1);
            ++clock;
            c.sweep();
            BEAST_EXPECT(c.size() == 0);
            BEAST_EXPECT(!c.touch_if_exists("one"));
        }

        // Insert two items, have one expire
        {
            Cache c("test", LedgerIndex(2), 2s, clock, j);

            BEAST_EXPECT(c.insert("one"));
            BEAST_EXPECT(c.size() == 1);
            BEAST_EXPECT(c.insert("two"));
            BEAST_EXPECT(c.size() == 2);
            ++clock;
            c.sweep();
            BEAST_EXPECT(c.size() == 2);
            BEAST_EXPECT(c.touch_if_exists("two"));
            ++clock;
            c.sweep();
            BEAST_EXPECT(c.size() == 1);
        }

        // Insert three items (1 over limit), sweep
        {
            Cache c("test", LedgerIndex(2), 3s, clock, j);

            BEAST_EXPECT(c.insert("one"));
            ++clock;
            BEAST_EXPECT(c.insert("two"));
            ++clock;
            BEAST_EXPECT(c.insert("three"));
            ++clock;
            BEAST_EXPECT(c.size() == 3);
            c.sweep();
            BEAST_EXPECT(c.size() < 3);
        }
    }
};

BEAST_DEFINE_TESTSUITE(KeyCache, common, ripple);

}  // namespace ripple
