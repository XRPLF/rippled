//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2017 Ripple Labs Inc

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
#include <test/csf/Histogram.h>

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

BEAST_DEFINE_TESTSUITE(Histogram, test, ripple);

}  // namespace test
}  // namespace ripple
