#include <csf/Histogram.h>

#include <gtest/gtest.h>

namespace xrpl::test {

TEST(HistogramTest, histogram)
{
    using namespace csf;
    Histogram<int> hist;

    EXPECT_TRUE(hist.size() == 0);
    EXPECT_TRUE(hist.numBins() == 0);
    EXPECT_TRUE(hist.minValue() == 0);
    EXPECT_TRUE(hist.maxValue() == 0);
    EXPECT_TRUE(hist.avg() == 0);
    EXPECT_TRUE(hist.percentile(0.0f) == hist.minValue());
    EXPECT_TRUE(hist.percentile(0.5f) == 0);
    EXPECT_TRUE(hist.percentile(0.9f) == 0);
    EXPECT_TRUE(hist.percentile(1.0f) == hist.maxValue());

    hist.insert(1);

    EXPECT_TRUE(hist.size() == 1);
    EXPECT_TRUE(hist.numBins() == 1);
    EXPECT_TRUE(hist.minValue() == 1);
    EXPECT_TRUE(hist.maxValue() == 1);
    EXPECT_TRUE(hist.avg() == 1);
    EXPECT_TRUE(hist.percentile(0.0f) == hist.minValue());
    EXPECT_TRUE(hist.percentile(0.5f) == 1);
    EXPECT_TRUE(hist.percentile(0.9f) == 1);
    EXPECT_TRUE(hist.percentile(1.0f) == hist.maxValue());

    hist.insert(9);

    EXPECT_TRUE(hist.size() == 2);
    EXPECT_TRUE(hist.numBins() == 2);
    EXPECT_TRUE(hist.minValue() == 1);
    EXPECT_TRUE(hist.maxValue() == 9);
    EXPECT_TRUE(hist.avg() == 5);
    EXPECT_TRUE(hist.percentile(0.0f) == hist.minValue());
    EXPECT_TRUE(hist.percentile(0.5f) == 1);
    EXPECT_TRUE(hist.percentile(0.9f) == 9);
    EXPECT_TRUE(hist.percentile(1.0f) == hist.maxValue());

    hist.insert(1);

    EXPECT_TRUE(hist.size() == 3);
    EXPECT_TRUE(hist.numBins() == 2);
    EXPECT_TRUE(hist.minValue() == 1);
    EXPECT_TRUE(hist.maxValue() == 9);
    EXPECT_TRUE(hist.avg() == 11 / 3);
    EXPECT_TRUE(hist.percentile(0.0f) == hist.minValue());
    EXPECT_TRUE(hist.percentile(0.5f) == 1);
    EXPECT_TRUE(hist.percentile(0.9f) == 9);
    EXPECT_TRUE(hist.percentile(1.0f) == hist.maxValue());
}

}  // namespace xrpl::test
