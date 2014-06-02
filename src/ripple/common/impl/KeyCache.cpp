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

#include <ripple/common/KeyCache.h>

#include <beast/unit_test/suite.h>
#include <beast/chrono/manual_clock.h>

namespace ripple {

class KeyCache_test : public beast::unit_test::suite
{
public:
    void run ()
    {
        beast::manual_clock <std::chrono::seconds> clock;
        clock.set (0);

        typedef std::string Key;
        typedef KeyCache <Key> Cache;
        
        // Insert an item, retrieve it, and age it so it gets purged.
        {
            Cache c ("test", clock, 1, 2);

            expect (c.size () == 0);
            expect (c.insert ("one"));
            expect (! c.insert ("one"));
            expect (c.size () == 1);
            expect (c.exists ("one"));
            expect (c.touch_if_exists ("one"));
            ++clock;
            c.sweep ();
            expect (c.size () == 1);
            expect (c.exists ("one"));
            ++clock;
            c.sweep ();
            expect (c.size () == 0);
            expect (! c.exists ("one"));
            expect (! c.touch_if_exists ("one"));
        }

        // Insert two items, have one expire
        {
            Cache c ("test", clock, 2, 2);

            expect (c.insert ("one"));
            expect (c.size  () == 1);
            expect (c.insert ("two"));
            expect (c.size  () == 2);
            ++clock;
            c.sweep ();
            expect (c.size () == 2);
            expect (c.touch_if_exists ("two"));
            ++clock;
            c.sweep ();
            expect (c.size () == 1);
            expect (c.exists ("two"));
        }

        // Insert three items (1 over limit), sweep
        {
            Cache c ("test", clock, 2, 3);

            expect (c.insert ("one"));
            ++clock;
            expect (c.insert ("two"));
            ++clock;
            expect (c.insert ("three"));
            ++clock;
            expect (c.size () == 3);
            c.sweep ();
            expect (c.size () < 3);
        }
    }
};

BEAST_DEFINE_TESTSUITE(KeyCache,common,ripple);

}
