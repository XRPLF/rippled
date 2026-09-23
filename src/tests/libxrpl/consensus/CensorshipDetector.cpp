#include <xrpl/consensus/CensorshipDetector.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <utility>
#include <vector>

namespace xrpl::test {

namespace {

void
runRound(
    CensorshipDetector<int, int>& cdet,
    int round,
    std::vector<int> proposed,
    std::vector<int> accepted,
    std::vector<int> remain,
    std::vector<int> remove)
{
    // Begin tracking what we're proposing this round
    CensorshipDetector<int, int>::TxIDSeqVec proposal;
    for (auto const& i : proposed)
        proposal.emplace_back(i, round);
    cdet.propose(std::move(proposal));

    // Finalize the round, by processing what we accepted; then
    // remove anything that needs to be removed and ensure that
    // what remains is correct.
    cdet.check(std::move(accepted), [&remove, &remain](auto id, auto seq) {
        // If the item is supposed to be removed from the censorship
        // detector internal tracker manually, do it now:
        if (std::ranges::find(remove, id) != remove.end())
            return true;

        // If the item is supposed to still remain in the censorship
        // detector internal tracker; remove it from the vector.
        auto it = std::ranges::find(remain, id);
        if (it != remain.end())
            remain.erase(it);
        return false;
    });

    // On entry, this set contained all the elements that should be tracked
    // by the detector after we process this round. We removed all the items
    // that actually were in the tracker, so this should now be empty:
    EXPECT_TRUE(remain.empty());
}

}  // namespace

TEST(CensorshipDetectorTest, censorship_detector)
{
    SCOPED_TRACE("Censorship Detector");

    CensorshipDetector<int, int> cdet;
    int round = 0;
    // proposed            accepted    remain          remove
    runRound(cdet, ++round, {}, {}, {}, {});
    runRound(cdet, ++round, {10, 11, 12, 13}, {11, 2}, {10, 13}, {});
    runRound(cdet, ++round, {10, 13, 14, 15}, {14}, {10, 13, 15}, {});
    runRound(cdet, ++round, {10, 13, 15, 16}, {15, 16}, {10, 13}, {});
    runRound(cdet, ++round, {10, 13}, {17, 18}, {10, 13}, {});
    runRound(cdet, ++round, {10, 19}, {}, {10, 19}, {});
    runRound(cdet, ++round, {10, 19, 20}, {20}, {10}, {19});
    runRound(cdet, ++round, {21}, {21}, {}, {});
    runRound(cdet, ++round, {}, {22}, {}, {});
    runRound(cdet, ++round, {23, 24, 25, 26}, {25, 27}, {23, 26}, {24});
    runRound(cdet, ++round, {23, 26, 28}, {26, 28}, {23}, {});

    for (auto i = 0uz; i != 10; ++i)
        runRound(cdet, ++round, {23}, {}, {23}, {});

    runRound(cdet, ++round, {23, 29}, {29}, {23}, {});
    runRound(cdet, ++round, {30, 31}, {31}, {30}, {});
    runRound(cdet, ++round, {30}, {30}, {}, {});
    runRound(cdet, ++round, {}, {}, {}, {});
}

}  // namespace xrpl::test
