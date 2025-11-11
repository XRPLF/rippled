#include <test/csf/Histogram.h>

#include <xrpl/beast/unit_test.h>

namespace ripple {
namespace test {

class Histogram_test : public beast::unit_test::suite
{
public:
    void
    run() override
    {
        using namespace csf;
        Histogram<int> hist;

        BEAST_EXPECT(hist.size() == 0);
        BEAST_EXPECT(hist.numBins() == 0);
        BEAST_EXPECT(hist.minValue() == 0);
        BEAST_EXPECT(hist.maxValue() == 0);
        BEAST_EXPECT(hist.avg() == 0);
        BEAST_EXPECT(hist.percentile(0.0f) == hist.minValue());
        BEAST_EXPECT(hist.percentile(0.5f) == 0);
        BEAST_EXPECT(hist.percentile(0.9f) == 0);
        BEAST_EXPECT(hist.percentile(1.0f) == hist.maxValue());

        hist.insert(1);

        BEAST_EXPECT(hist.size() == 1);
        BEAST_EXPECT(hist.numBins() == 1);
        BEAST_EXPECT(hist.minValue() == 1);
        BEAST_EXPECT(hist.maxValue() == 1);
        BEAST_EXPECT(hist.avg() == 1);
        BEAST_EXPECT(hist.percentile(0.0f) == hist.minValue());
        BEAST_EXPECT(hist.percentile(0.5f) == 1);
        BEAST_EXPECT(hist.percentile(0.9f) == 1);
        BEAST_EXPECT(hist.percentile(1.0f) == hist.maxValue());

        hist.insert(9);

        BEAST_EXPECT(hist.size() == 2);
        BEAST_EXPECT(hist.numBins() == 2);
        BEAST_EXPECT(hist.minValue() == 1);
        BEAST_EXPECT(hist.maxValue() == 9);
        BEAST_EXPECT(hist.avg() == 5);
        BEAST_EXPECT(hist.percentile(0.0f) == hist.minValue());
        BEAST_EXPECT(hist.percentile(0.5f) == 1);
        BEAST_EXPECT(hist.percentile(0.9f) == 9);
        BEAST_EXPECT(hist.percentile(1.0f) == hist.maxValue());

        hist.insert(1);

        BEAST_EXPECT(hist.size() == 3);
        BEAST_EXPECT(hist.numBins() == 2);
        BEAST_EXPECT(hist.minValue() == 1);
        BEAST_EXPECT(hist.maxValue() == 9);
        BEAST_EXPECT(hist.avg() == 11 / 3);
        BEAST_EXPECT(hist.percentile(0.0f) == hist.minValue());
        BEAST_EXPECT(hist.percentile(0.5f) == 1);
        BEAST_EXPECT(hist.percentile(0.9f) == 9);
        BEAST_EXPECT(hist.percentile(1.0f) == hist.maxValue());
    }
};

BEAST_DEFINE_TESTSUITE(Histogram, csf, ripple);

}  // namespace test
}  // namespace ripple
