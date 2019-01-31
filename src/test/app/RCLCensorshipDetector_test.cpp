//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2018 Ripple Labs Inc.

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

#include <ripple/beast/unit_test.h>
#include <ripple/app/consensus/RCLCensorshipDetector.h>
#include <algorithm>
#include <vector>

namespace ripple {
namespace test {

class RCLCensorshipDetector_test : public beast::unit_test::suite
{
    void test(
        RCLCensorshipDetector<int, int>& cdet, int round,
        std::vector<int> proposed, std::vector<int> accepted,
        std::vector<int> remain, std::vector<int> remove)
    {
        // Begin tracking what we're proposing this round
        RCLCensorshipDetector<int, int>::TxIDSeqVec proposal;
        for (auto const& i : proposed)
            proposal.emplace_back(i, round);
        cdet.propose(std::move(proposal));

        // Finalize the round, by processing what we accepted; then
        // remove anything that needs to be removed and ensure that
        // what remains is correct.
        cdet.check(std::move(accepted),
            [&remove, &remain](auto id, auto seq)
            {
                // If the item is supposed to be removed from the censorship
                // detector internal tracker manually, do it now:
                if (std::find(remove.begin(), remove.end(), id) != remove.end())
                    return true;

                // If the item is supposed to still remain in the censorship
                // detector internal tracker; remove it from the vector.
                auto it = std::find(remain.begin(), remain.end(), id);
                if (it != remain.end())
                    remain.erase(it);
                return false;
            });

        // On entry, this set contained all the elements that should be tracked
        // by the detector after we process this round. We removed all the items
        // that actually were in the tracker, so this should now be empty:
        BEAST_EXPECT(remain.empty());
    }

public:
    void
    run() override
    {
        testcase ("Censorship Detector");

        RCLCensorshipDetector<int, int> cdet;
        int round = 0;
                             // proposed            accepted    remain          remove
        test(cdet, ++round,     { },                { },        { },            { });
        test(cdet, ++round,     { 10, 11, 12, 13 }, { 11, 2 },  { 10, 13 },     { });
        test(cdet, ++round,     { 10, 13, 14, 15 }, { 14 },     { 10, 13, 15 }, { });
        test(cdet, ++round,     { 10, 13, 15, 16 }, { 15, 16 }, { 10, 13 },     { });
        test(cdet, ++round,     { 10, 13 },         { 17, 18 }, { 10, 13 },     { });
        test(cdet, ++round,     { 10, 19 },         { },        { 10, 19 },     { });
        test(cdet, ++round,     { 10, 19, 20 },     { 20 },     { 10 },         { 19 });
        test(cdet, ++round,     { 21 },             { 21 },     { },            { });
        test(cdet, ++round,     { },                { 22 },     { },            { });
        test(cdet, ++round,     { 23, 24, 25, 26 }, { 25, 27 }, { 23, 26 },     { 24 });
        test(cdet, ++round,     { 23, 26, 28 },     { 26, 28 }, { 23 },         { });

        for (int i = 0; i != 10; ++i)
            test(cdet, ++round, { 23 },             { },        { 23 },         { });

        test(cdet, ++round,     { 23, 29 },         { 29 },     { 23 },         { });
        test(cdet, ++round,     { 30, 31 },         { 31 },     { 30 },         { });
        test(cdet, ++round,     { 30 },             { 30 },     { },            { });
        test(cdet, ++round,     { },                { },        { },            { });
    }
};

BEAST_DEFINE_TESTSUITE(RCLCensorshipDetector, app, ripple);
}  // namespace test
}  // namespace ripple
