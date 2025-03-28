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

#include <xrpld/app/ledger/PendingSaves.h>

#include <xrpl/beast/unit_test.h>

namespace ripple {
namespace test {

struct PendingSaves_test : public beast::unit_test::suite
{
    void
    testSaves()
    {
        PendingSaves ps;

        // Basic test
        BEAST_EXPECT(!ps.pending(0));
        BEAST_EXPECT(!ps.startWork(0));
        BEAST_EXPECT(ps.shouldWork(0, true));
        BEAST_EXPECT(ps.startWork(0));
        BEAST_EXPECT(ps.pending(0));
        BEAST_EXPECT(!ps.shouldWork(0, false));
        ps.finishWork(0);
        BEAST_EXPECT(!ps.pending(0));

        // Test work stealing
        BEAST_EXPECT(ps.shouldWork(0, false));
        BEAST_EXPECT(ps.pending(0));
        BEAST_EXPECT(ps.shouldWork(0, true));
        BEAST_EXPECT(ps.pending(0));
        BEAST_EXPECT(ps.startWork(0));
        BEAST_EXPECT(!ps.startWork(0));
        ps.finishWork(0);
        BEAST_EXPECT(!ps.pending(0));
    }

    void
    run() override
    {
        testSaves();
    }
};

BEAST_DEFINE_TESTSUITE(PendingSaves, ledger, ripple);

}  // namespace test
}  // namespace ripple
