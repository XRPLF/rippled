#include <xrpl/shamap/SHAMapAddNode.h>

#include <gtest/gtest.h>

namespace xrpl::tests {

// get() is a log format rather than an API, so it is pinned here, once, instead of at every site
// that has a verdict to check. Those check the tally through getGood()/getBad()/getDuplicate()
// (see tallyIs() in AcquireTestHelpers.h and SHAMapSync.cpp) or through
// isGood()/isUseful()/isInvalid(), which say the same thing without depending on the wording.
TEST(SHAMapAddNode, getNamesEveryNonEmptyCount)
{
    EXPECT_EQ(SHAMapAddNode{}.get(), "no nodes processed");
    EXPECT_EQ(SHAMapAddNode::useful().get(), "good:1");
    EXPECT_EQ(SHAMapAddNode::invalid().get(), "bad:1");
    EXPECT_EQ(SHAMapAddNode::duplicate().get(), "dupe:1");

    // Several of a kind are counted, and the counts are joined in a fixed order with a single
    // space, whichever order they were recorded in.
    SHAMapAddNode san;
    san.incInvalid();
    san.incUseful();
    san.incUseful();
    san.incDuplicate();
    EXPECT_EQ(san.get(), "good:2 bad:1 dupe:1");

    san.reset();
    EXPECT_EQ(san.get(), "no nodes processed");
}

// The three counts the tests assert on, and the verdicts derived from them, so a tally check and
// the log line cannot drift apart.
TEST(SHAMapAddNode, countsAndVerdictsAgree)
{
    SHAMapAddNode san;
    EXPECT_EQ(san.getGood(), 0);
    EXPECT_EQ(san.getBad(), 0);
    EXPECT_EQ(san.getDuplicate(), 0);
    EXPECT_FALSE(san.isInvalid());
    EXPECT_FALSE(san.isUseful());

    // Good counts what was hooked in, and useful is that count being non-zero.
    san.incUseful();
    EXPECT_EQ(san.getGood(), 1);
    EXPECT_TRUE(san.isUseful());
    EXPECT_TRUE(san.isGood());

    // A duplicate counts towards good without needing to be useful itself: isUseful() here still
    // reflects the incUseful() above, not this increment.
    san.incDuplicate();
    EXPECT_EQ(san.getDuplicate(), 1);
    EXPECT_FALSE(san.isInvalid());
    EXPECT_TRUE(san.isGood());

    // Bad is counted, not merely flagged: a batch that carries on past a rejected node reports one
    // per node, so a test can tell "stopped on the first" from "rejected several".
    san.incInvalid();
    EXPECT_EQ(san.getBad(), 1);
    EXPECT_TRUE(san.isInvalid());
    EXPECT_TRUE(san.isGood()) << "one bad node among two accepted ones is still a good batch";

    san.incInvalid();
    san.incInvalid();
    EXPECT_EQ(san.getGood(), 1);
    EXPECT_EQ(san.getBad(), 3);
    EXPECT_EQ(san.getDuplicate(), 1);
    EXPECT_FALSE(san.isGood()) << "more bad nodes than accepted ones is not";

    // Adding one verdict to another sums every count, which is how a batch's verdict is built up
    // one node at a time.
    SHAMapAddNode total;
    total += SHAMapAddNode::useful();
    total += SHAMapAddNode::invalid();
    total += SHAMapAddNode::invalid();
    total += SHAMapAddNode::duplicate();
    EXPECT_EQ(total.getGood(), 1);
    EXPECT_EQ(total.getBad(), 2);
    EXPECT_EQ(total.getDuplicate(), 1);
}

}  // namespace xrpl::tests
