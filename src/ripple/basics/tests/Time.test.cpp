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
#include <ripple/basics/Time.h>
#include <ripple/basics/TestSuite.h>

namespace ripple {

class Time_test : public TestSuite
{
public:
    void test_timestamp() {
        expectEquals (timestamp (nullptr, 1428444687),
                      "Tue, 07 Apr 2015 22:11:27 +0000");

        expectEquals (timestamp ("%A %B %C %G %p", 1428444687),
                      "Tuesday April 20 2015 PM");

        // Check we calculate capacity properly.
        expectEquals (timestamp ("%r", 11428444687),
                      "03:58:07 PM");

        expectEquals (timestamp ("%r %r", 11428444687),
                      "03:58:07 PM 03:58:07 PM");
    }

    void run ()
    {
        test_timestamp();
    }
};

BEAST_DEFINE_TESTSUITE(Time, basics, ripple);

} // ripple
