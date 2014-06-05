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

#include <ripple/common/TaggedCache.h>

#include <beast/unit_test/suite.h>
#include <beast/chrono/manual_clock.h>

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
    void run ()
    {
        beast::Journal const j;

        beast::manual_clock <std::chrono::seconds> clock;
        clock.set (0);

        typedef int Key;
        typedef std::string Value;
        typedef TaggedCache <Key, Value> Cache;
        
        Cache c ("test", 1, 1, clock, j);

        // Insert an item, retrieve it, and age it so it gets purged.
        {
            expect (c.getCacheSize() == 0);
            expect (c.getTrackSize() == 0);
            expect (! c.insert (1, "one"));
            expect (c.getCacheSize() == 1);
            expect (c.getTrackSize() == 1);

            {
                std::string s;
                expect (c.retrieve (1, s));
                expect (s == "one");
            }

            ++clock;
            c.sweep ();
            expect (c.getCacheSize () == 0);
            expect (c.getTrackSize () == 0);
        }

        // Insert an item, maintain a strong pointer, age it, and
        // verify that the entry still exists.
        {
            expect (! c.insert (2, "two"));
            expect (c.getCacheSize() == 1);
            expect (c.getTrackSize() == 1);

            {
                Cache::mapped_ptr p (c.fetch (2));
                expect (p != nullptr);
                ++clock;
                c.sweep ();
                expect (c.getCacheSize() == 0);
                expect (c.getTrackSize() == 1);
            }

            // Make sure its gone now that our reference is gone
            ++clock;
            c.sweep ();
            expect (c.getCacheSize() == 0);
            expect (c.getTrackSize() == 0);
        }

        // Insert the same key/value pair and make sure we get the same result
        {
            expect (! c.insert (3, "three"));

            {
                Cache::mapped_ptr const p1 (c.fetch (3));
                Cache::mapped_ptr p2 (std::make_shared <Value> ("three"));
                c.canonicalize (3, p2);
                expect (p1.get() == p2.get());
            }
            ++clock;
            c.sweep ();
            expect (c.getCacheSize() == 0);
            expect (c.getTrackSize() == 0);
        }

        // Put an object in but keep a strong pointer to it, advance the clock a lot,
        // then canonicalize a new object with the same key, make sure you get the
        // original object.
        {
            // Put an object in
            expect (! c.insert (4, "four"));
            expect (c.getCacheSize() == 1);
            expect (c.getTrackSize() == 1);

            {
                // Keep a strong pointer to it
                Cache::mapped_ptr p1 (c.fetch (4));
                expect (p1 != nullptr);
                expect (c.getCacheSize() == 1);
                expect (c.getTrackSize() == 1);
                // Advance the clock a lot
                ++clock;
                c.sweep ();
                expect (c.getCacheSize() == 0);
                expect (c.getTrackSize() == 1);
                // Canonicalize a new object with the same key
                Cache::mapped_ptr p2 (std::make_shared <std::string> ("four"));
                expect (c.canonicalize (4, p2, false));
                expect (c.getCacheSize() == 1);
                expect (c.getTrackSize() == 1);
                // Make sure we get the original object
                expect (p1.get() == p2.get());
            }

            ++clock;
            c.sweep ();
            expect (c.getCacheSize() == 0);
            expect (c.getTrackSize() == 0);
        }
    }
};

BEAST_DEFINE_TESTSUITE(TaggedCache,common,ripple);

}
