//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2016 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  xING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <ripple/basics/Buffer.h>
#include <ripple/basics/strHex.h>
#include <ripple/basics/Slice.h>
#include <ripple/beast/unit_test.h>
#include <ripple/conditions/Condition.h>
#include <ripple/conditions/Fulfillment.h>
#include <ripple/conditions/impl/PreimageSha256.h>
#include <algorithm>
#include <numeric>
#include <vector>
#include <random>

namespace ripple {
namespace cryptoconditions {

class PreimageSha256_test : public beast::unit_test::suite
{
    inline
    Buffer
    hexblob(std::string const& s)
    {
        std::vector<std::uint8_t> x;
        x.reserve(s.size() / 2);

        auto iter = s.cbegin();

        while (iter != s.cend())
        {
            int cHigh = charUnHex(*iter++);

            if (cHigh < 0)
                return {};

            int cLow = charUnHex(*iter++);

            if (cLow < 0)
                return {};

            x.push_back(
                static_cast<std::uint8_t>(cHigh << 4) |
                static_cast<std::uint8_t>(cLow));
        }

        return { x.data(), x.size() };
    }

    void
    testKnownVectors()
    {
        testcase("Known Vectors");

        std::pair<std::string, std::string> known[] =
        {
            { "A0028000",
                "A0258020E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855810100" },
            { "A0058003616161",
                "A02580209834876DCFB05CB167A5C24953EBA58C4AC89B1ADF57F28F2F9D09AF107EE8F0810103" },
        };

        std::error_code ec;

        auto f1 = Fulfillment::deserialize (hexblob(known[0].first), ec);
        BEAST_EXPECT (f1);
        BEAST_EXPECT (!ec);

        auto c1 = Condition::deserialize (hexblob(known[0].second), ec);
        BEAST_EXPECT (c1);
        BEAST_EXPECT (!ec);

        auto f2 = Fulfillment::deserialize(hexblob(known[1].first), ec);
        BEAST_EXPECT(f2);
        BEAST_EXPECT(!ec);

        auto c2 = Condition::deserialize(hexblob(known[1].second), ec);
        BEAST_EXPECT(c2);
        BEAST_EXPECT(!ec);
        
        // Check equality and inequality
        BEAST_EXPECT (f1->condition() == *c1);
        BEAST_EXPECT (f1->condition() != *c2);
        BEAST_EXPECT (f2->condition() == *c2);
        BEAST_EXPECT (f2->condition() != *c1);
        BEAST_EXPECT (*c1 != *c2);
        BEAST_EXPECT (*c1 == *c1);
        BEAST_EXPECT (f1->condition() == f1->condition());

        // Should validate with the empty string
        BEAST_EXPECT (validate (*f1, *c1));
        BEAST_EXPECT (validate(*f2, *c2));

        // And with any string - the message doesn't matter for PrefixSha256
        BEAST_EXPECT (validate (*f1, *c1, makeSlice(known[0].first)));
        BEAST_EXPECT (validate(*f1, *c1, makeSlice(known[0].second)));
        BEAST_EXPECT (validate(*f2, *c2, makeSlice(known[0].first)));
        BEAST_EXPECT (validate(*f2, *c2, makeSlice(known[0].second)));

        // Shouldn't validate if the fulfillment & condition don't match
        // regardless of the message.
        BEAST_EXPECT (! validate(*f2, *c1));
        BEAST_EXPECT (! validate(*f2, *c1, makeSlice(known[0].first)));
        BEAST_EXPECT (! validate(*f2, *c1, makeSlice(known[0].second)));
        BEAST_EXPECT (! validate(*f1, *c2));
        BEAST_EXPECT (! validate(*f1, *c2, makeSlice(known[0].first)));
        BEAST_EXPECT (! validate(*f1, *c2, makeSlice(known[0].second)));
    }

    void run ()
    {
        testKnownVectors();
    }
};

BEAST_DEFINE_TESTSUITE (PreimageSha256, conditions, ripple);

}

}
