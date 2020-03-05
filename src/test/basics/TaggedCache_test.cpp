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

#include <ripple/basics/chrono.h>
#include <ripple/basics/TaggedCache.h>
#include <ripple/beast/unit_test.h>
#include <ripple/beast/clock/manual_clock.h>
#include <test/unit_test/SuiteJournal.h>

namespace ripple {

/*
I guess you can put some items in, make sure they're still there. Let some
time pass, make sure they're gone. Keep a strong pointer to one of them, make
sure you can still find it even after time passes. Create two objects with
the same key, canonicalize them both and make sure you get the same object.
Put an object in but keep a strong pointer to it, advance the clock a lot,
then canonicalize a new object with the same key, make sure you get the
original object.
*/

class TaggedCache_test : public beast::unit_test::suite
{
public:
    void run () override
    {
        using namespace std::chrono_literals;
        using namespace beast::severities;
        test::SuiteJournal journal ("TaggedCache_test", *this);

        TestStopwatch clock;
        clock.set (0);

        using Key = int;
        using Value = std::string;
        using Cache = TaggedCache <Key, Value>;

        Cache c ("test", 1, 1s, clock, journal);

        // Insert an item, retrieve it, and age it so it gets purged.
        {
            BEAST_EXPECT(c.getCacheSize() == 0);
            BEAST_EXPECT(c.getTrackSize() == 0);
            BEAST_EXPECT(! c.insert (1, "one"));
            BEAST_EXPECT(c.getCacheSize() == 1);
            BEAST_EXPECT(c.getTrackSize() == 1);

            {
                std::string s;
                BEAST_EXPECT(c.retrieve (1, s));
                BEAST_EXPECT(s == "one");
            }

            ++clock;
            c.sweep ();
            BEAST_EXPECT(c.getCacheSize () == 0);
            BEAST_EXPECT(c.getTrackSize () == 0);
        }

        // Insert an item, maintain a strong pointer, age it, and
        // verify that the entry still exists.
        {
            BEAST_EXPECT(! c.insert (2, "two"));
            BEAST_EXPECT(c.getCacheSize() == 1);
            BEAST_EXPECT(c.getTrackSize() == 1);

            {
                Cache::mapped_ptr p (c.fetch (2));
                BEAST_EXPECT(p != nullptr);
                ++clock;
                c.sweep ();
                BEAST_EXPECT(c.getCacheSize() == 0);
                BEAST_EXPECT(c.getTrackSize() == 1);
            }

            // Make sure its gone now that our reference is gone
            ++clock;
            c.sweep ();
            BEAST_EXPECT(c.getCacheSize() == 0);
            BEAST_EXPECT(c.getTrackSize() == 0);
        }

        // Insert the same key/value pair and make sure we get the same result
        {
            BEAST_EXPECT(! c.insert (3, "three"));

            {
                Cache::mapped_ptr const p1 (c.fetch (3));
                Cache::mapped_ptr p2 (std::make_shared <Value> ("three"));
                c.canonicalize_replace_client(3, p2);
                BEAST_EXPECT(p1.get() == p2.get());
            }
            ++clock;
            c.sweep ();
            BEAST_EXPECT(c.getCacheSize() == 0);
            BEAST_EXPECT(c.getTrackSize() == 0);
        }

        // Put an object in but keep a strong pointer to it, advance the clock a lot,
        // then canonicalize a new object with the same key, make sure you get the
        // original object.
        {
            // Put an object in
            BEAST_EXPECT(! c.insert (4, "four"));
            BEAST_EXPECT(c.getCacheSize() == 1);
            BEAST_EXPECT(c.getTrackSize() == 1);

            {
                // Keep a strong pointer to it
                Cache::mapped_ptr p1 (c.fetch (4));
                BEAST_EXPECT(p1 != nullptr);
                BEAST_EXPECT(c.getCacheSize() == 1);
                BEAST_EXPECT(c.getTrackSize() == 1);
                // Advance the clock a lot
                ++clock;
                c.sweep ();
                BEAST_EXPECT(c.getCacheSize() == 0);
                BEAST_EXPECT(c.getTrackSize() == 1);
                // Canonicalize a new object with the same key
                Cache::mapped_ptr p2 (std::make_shared <std::string> ("four"));
                BEAST_EXPECT(c.canonicalize_replace_client(4, p2));
                BEAST_EXPECT(c.getCacheSize() == 1);
                BEAST_EXPECT(c.getTrackSize() == 1);
                // Make sure we get the original object
                BEAST_EXPECT(p1.get() == p2.get());
            }

            ++clock;
            c.sweep ();
            BEAST_EXPECT(c.getCacheSize() == 0);
            BEAST_EXPECT(c.getTrackSize() == 0);
        }
    }
};

BEAST_DEFINE_TESTSUITE(TaggedCache,common,ripple);

}
