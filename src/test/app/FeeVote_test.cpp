//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2019 Ripple Labs Inc.

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

#include <test/jtx.h>

#include <xrpl/basics/BasicConfig.h>

namespace ripple {
namespace test {

class FeeVote_test : public beast::unit_test::suite
{
    void
    testSetup()
    {
        FeeSetup const defaultSetup;
        {
            // defaults
            Section config;
            auto setup = setup_FeeVote(config);
            BEAST_EXPECT(setup.reference_fee == defaultSetup.reference_fee);
            BEAST_EXPECT(setup.account_reserve == defaultSetup.account_reserve);
            BEAST_EXPECT(setup.owner_reserve == defaultSetup.owner_reserve);
        }
        {
            Section config;
            config.append(
                {"reference_fee = 50",
                 "account_reserve = 1234567",
                 "owner_reserve = 1234"});
            auto setup = setup_FeeVote(config);
            BEAST_EXPECT(setup.reference_fee == 50);
            BEAST_EXPECT(setup.account_reserve == 1234567);
            BEAST_EXPECT(setup.owner_reserve == 1234);
        }
        {
            Section config;
            config.append(
                {"reference_fee = blah",
                 "account_reserve = yada",
                 "owner_reserve = foo"});
            // Illegal values are ignored, and the defaults left unchanged
            auto setup = setup_FeeVote(config);
            BEAST_EXPECT(setup.reference_fee == defaultSetup.reference_fee);
            BEAST_EXPECT(setup.account_reserve == defaultSetup.account_reserve);
            BEAST_EXPECT(setup.owner_reserve == defaultSetup.owner_reserve);
        }
        {
            Section config;
            config.append(
                {"reference_fee = -50",
                 "account_reserve = -1234567",
                 "owner_reserve = -1234"});
            // Illegal values are ignored, and the defaults left unchanged
            auto setup = setup_FeeVote(config);
            BEAST_EXPECT(setup.reference_fee == defaultSetup.reference_fee);
            BEAST_EXPECT(
                setup.account_reserve == static_cast<std::uint32_t>(-1234567));
            BEAST_EXPECT(
                setup.owner_reserve == static_cast<std::uint32_t>(-1234));
        }
        {
            auto const big64 = std::to_string(
                static_cast<std::uint64_t>(
                    std::numeric_limits<XRPAmount::value_type>::max()) +
                1);
            Section config;
            config.append(
                {"reference_fee = " + big64,
                 "account_reserve = " + big64,
                 "owner_reserve = " + big64});
            // Illegal values are ignored, and the defaults left unchanged
            auto setup = setup_FeeVote(config);
            BEAST_EXPECT(setup.reference_fee == defaultSetup.reference_fee);
            BEAST_EXPECT(setup.account_reserve == defaultSetup.account_reserve);
            BEAST_EXPECT(setup.owner_reserve == defaultSetup.owner_reserve);
        }
    }

    void
    run() override
    {
        testSetup();
    }
};

BEAST_DEFINE_TESTSUITE(FeeVote, server, ripple);

}  // namespace test
}  // namespace ripple
