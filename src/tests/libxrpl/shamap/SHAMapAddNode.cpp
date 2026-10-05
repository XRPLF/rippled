#include <xrpl/shamap/SHAMapAddNode.h>

#include <gtest/gtest.h>

namespace xrpl::tests {

// get() is a log format, so its wording is pinned here, once. Every other site reads the same tally
// by value, through the count and verdict accessors.
TEST(SHAMapAddNode, get_names_every_non_empty_count)
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

// The three counts and the verdicts derived from them.
TEST(SHAMapAddNode, counts_and_verdicts_agree)
{
    SHAMapAddNode san;
    EXPECT_EQ(san.getGood(), 0);
    EXPECT_EQ(san.getBad(), 0);
    EXPECT_EQ(san.getDuplicate(), 0);
    EXPECT_FALSE(san.isInvalid());
    EXPECT_FALSE(san.isUseful());

    // Good counts what produced a good result, and useful is that count being non-zero.
    san.incUseful();
    EXPECT_EQ(san.getGood(), 1);
    EXPECT_TRUE(san.isUseful());
    EXPECT_TRUE(san.isGood());

    // A duplicate counts toward good. isUseful() here reflects the incUseful() above.
    san.incDuplicate();
    EXPECT_EQ(san.getDuplicate(), 1);
    EXPECT_FALSE(san.isInvalid());
    EXPECT_TRUE(san.isGood());

    // Bad is a count, so a batch that carries on past a rejected node reports one per node, which
    // distinguishes "stopped on the first" from "rejected several".
    san.incInvalid();
    EXPECT_EQ(san.getBad(), 1);
    EXPECT_TRUE(san.isInvalid());
    EXPECT_TRUE(san.isGood()) << "one bad node among two accepted ones is still a good batch";

    san.incInvalid();
    san.incInvalid();
    EXPECT_EQ(san.getGood(), 1);
    EXPECT_EQ(san.getBad(), 3);
    EXPECT_EQ(san.getDuplicate(), 1);
    EXPECT_FALSE(san.isGood()) << "more bad nodes than accepted ones is not a good batch";

    // Adding one verdict to another sums every count.
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
