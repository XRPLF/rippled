
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

#include <test/conditions/ConditionsTestBase.h>

namespace ripple {
namespace cryptoconditions {

class Conditions_preim_test : public ConditionsTestBase
{
    void
    testPreim0()
    {
        testcase("Preim0");

        using namespace std::string_literals;
        using namespace ripple::cryptoconditions;

        // Fulfillment structure
        // * preim0

        auto const preim0Preimage = "I am root"s;
        auto const preim0Msg = "abcdefghijklmnopqrstuvwxyz"s;

        auto preim0 =
            std::make_unique<PreimageSha256>(makeSlice(preim0Preimage));
        {
            auto preim0EncodedFulfillment =
                "\xa0\x0b\x80\x09\x49\x20\x61\x6d\x20\x72\x6f\x6f\x74"s;
            auto const preim0EncodedCondition =
                "\xa0\x25\x80\x20\x5d\xa0\x30\xef\xfd\xe1\x75\x11\x51\xe8\x5f"
                "\x5e\x54\x2d\x6a\x5b\xd1\x5c\xc9\x33\x21\x2c\xe2\x68\xfc\xfd"
                "\x53\xee\x93\x58\xeb\x4e\x81\x01\x09"s;
            auto const preim0EncodedFingerprint =
                "\x49\x20\x61\x6d\x20\x72\x6f\x6f\x74"s;
            check(
                std::move(preim0),
                preim0Msg,
                std::move(preim0EncodedFulfillment),
                preim0EncodedCondition,
                preim0EncodedFingerprint);
        }
    }

    void
    run()
    {
        testPreim0();
    }
};

BEAST_DEFINE_TESTSUITE(Conditions_preim, conditions, ripple);
}  // cryptoconditions
}  // ripple
