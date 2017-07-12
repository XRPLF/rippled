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
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_TEST_CONDITIONS_CONDITIONSTESTBASE_H_INCLUDED
#define RIPPLE_TEST_CONDITIONS_CONDITIONSTESTBASE_H_INCLUDED

#include <ripple/basics/Slice.h>
#include <ripple/beast/unit_test.h>
#include <ripple/conditions/Condition.h>
#include <ripple/conditions/Fulfillment.h>
#include <ripple/conditions/impl/Ed25519.h>
#include <ripple/conditions/impl/PrefixSha256.h>
#include <ripple/conditions/impl/PreimageSha256.h>
#include <ripple/conditions/impl/RsaSha256.h>
#include <ripple/conditions/impl/ThresholdSha256.h>

#include <boost/math/special_functions/sign.hpp>

#include <memory>
#include <string>

namespace ripple {
namespace cryptoconditions {

class ConditionsTestBase : public beast::unit_test::suite
{
protected:
    void
    check(
        std::unique_ptr<Fulfillment> expectedF,
        std::string const& msg,
        std::string encodedFulfillment,
        std::string const& encodedCondition,
        std::string const& encodedFingerprint)
    {
        using namespace cryptoconditions::der;

        std::string const badMsg = msg + " bad";
        std::error_code ec;
        auto f = Fulfillment::deserialize(makeSlice(encodedFulfillment), ec);
        BEAST_EXPECT(f && !ec && f->checkEqualForTesting(*expectedF));
        if (!f)
            return;
        BEAST_EXPECT(f->validate(makeSlice(msg)));

        if (f->validationDependsOnMessage())
            BEAST_EXPECT(!f->validate(makeSlice(badMsg)));

        auto c1 = Condition::deserialize(makeSlice(encodedCondition), ec);
        BEAST_EXPECT(!ec);
        BEAST_EXPECT(f->condition(ec) == c1 && !ec);
        BEAST_EXPECT(expectedF->condition(ec) == c1 && !ec);

        {
            // check fulfillment encodings match
            Encoder s{TagMode::automatic};
            s << f << eos;
            std::error_code ecSB;
            auto const& encoded = s.serializationBuffer(ecSB);
            BEAST_EXPECT(!ecSB && makeSlice(encoded) == makeSlice(encodedFulfillment));
        }
        if (f->type() != cryptoconditions::Type::preimageSha256)
        {
            // check condition fingerprint encodings
            Encoder s{TagMode::automatic};
            f->encodeFingerprint(s);
            s << eos;
            std::error_code ecSB;
            auto const& encoded = s.serializationBuffer(ecSB);
            BEAST_EXPECT(!ecSB && makeSlice(encoded) == makeSlice(encodedFingerprint));
        }
        {
            // check condition encoding match
            Encoder s{TagMode::automatic};
            s << f->condition(ec) << eos;
            BEAST_EXPECT(!ec);
            std::error_code ecSB;
            auto const& encoded = s.serializationBuffer(ecSB);
            BEAST_EXPECT(!ecSB && makeSlice(encoded) == makeSlice(encodedCondition));
        }

        using Traits = cryptoconditions::der::DerCoderTraits<std::unique_ptr<Fulfillment>>;

        static std::vector<std::pair<std::unique_ptr<Fulfillment>, std::string>>
            prevTests;

        {
            cryptoconditions::der::TraitsCache dummy;
            BEAST_EXPECT(Traits::compare(expectedF, expectedF, dummy) == 0);
        }

        for(auto const& i : prevTests)
        {
            cryptoconditions::der::TraitsCache dummy;
            BEAST_EXPECT(
                boost::math::sign(
                    Traits::compare(expectedF, i.first, dummy)) ==
                boost::math::sign(encodedFulfillment.compare(i.second)));

        }
        prevTests.emplace_back(std::move(expectedF), std::move(encodedFulfillment));
    }
};

}
}

#endif
