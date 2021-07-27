//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2020 Ripple Labs Inc.

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
#include <ripple/overlay/impl/Handshake.h>

namespace ripple {

namespace test {

class handshake_test : public beast::unit_test::suite
{
public:
    handshake_test() = default;

    void
    testHandshake()
    {
        testcase("X-Protocol-Ctl");
        boost::beast::http::fields headers;
        headers.insert(
            "X-Protocol-Ctl",
            "feature1=v1,v2,v3; feature2=v4; feature3=10; feature4=1; "
            "feature5=v6");
        BEAST_EXPECT(!featureEnabled(headers, "feature1"));
        BEAST_EXPECT(!isFeatureValue(headers, "feature1", "2"));
        BEAST_EXPECT(isFeatureValue(headers, "feature1", "v1"));
        BEAST_EXPECT(isFeatureValue(headers, "feature1", "v2"));
        BEAST_EXPECT(isFeatureValue(headers, "feature1", "v3"));
        BEAST_EXPECT(isFeatureValue(headers, "feature2", "v4"));
        BEAST_EXPECT(!isFeatureValue(headers, "feature3", "1"));
        BEAST_EXPECT(isFeatureValue(headers, "feature3", "10"));
        BEAST_EXPECT(!isFeatureValue(headers, "feature4", "10"));
        BEAST_EXPECT(isFeatureValue(headers, "feature4", "1"));
        BEAST_EXPECT(!featureEnabled(headers, "v6"));
    }

    void
    run() override
    {
        testHandshake();
    }
};

BEAST_DEFINE_TESTSUITE(handshake, overlay, ripple);

}  // namespace test
}  // namespace ripple