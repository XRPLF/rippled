
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

class Conditions_ed_test : public ConditionsTestBase
{
    void
    testEd0()
    {
        testcase("Ed0");

        using namespace std::string_literals;
        using namespace ripple::cryptoconditions;

        // Fulfillment structure
        // * ed0

        auto const ed0Msg = "abcdefghijklmnopqrstuvwxyz"s;
        std::array<std::uint8_t, 32> const ed0PublicKey{
            {0xa2, 0x0e, 0x9f, 0x32, 0x9a, 0x21, 0x6d, 0xac, 0x88, 0xbf, 0x51,
             0xc7, 0x60, 0xe1, 0x4c, 0xa6, 0xb6, 0x4b, 0x2d, 0x24, 0x87, 0xf6,
             0x29, 0xae, 0xd4, 0xf1, 0xe1, 0x2d, 0x15, 0x84, 0x0b, 0xb9}};
        std::array<std::uint8_t, 64> const ed0Sig{
            {0x6d, 0xea, 0x44, 0xb0, 0x26, 0x5e, 0x51, 0x57, 0x57, 0x6a, 0x49,
             0xd8, 0x03, 0x81, 0x14, 0xb8, 0x77, 0x78, 0x9f, 0xb8, 0x4f, 0xab,
             0xce, 0x19, 0x07, 0x4c, 0xf0, 0xa6, 0x3a, 0x17, 0x06, 0x10, 0x6d,
             0x80, 0xda, 0xc6, 0x8f, 0x06, 0xad, 0x53, 0xb0, 0x9c, 0x36, 0x43,
             0xd9, 0x56, 0xfd, 0x5f, 0xbd, 0x4a, 0xf0, 0xff, 0xcb, 0x20, 0x96,
             0x7f, 0x5d, 0xca, 0xcf, 0x23, 0x52, 0x62, 0x91, 0x08}};
        std::array<std::uint8_t, 32> const ed0SigningKey{
            {0x1a, 0xee, 0xd3, 0x72, 0x55, 0x41, 0x83, 0xf2, 0xa7, 0xe2, 0x5e,
             0x24, 0x0e, 0x22, 0x28, 0x9e, 0x81, 0x61, 0x5f, 0x9d, 0xd2, 0x11,
             0xdd, 0xcd, 0xc3, 0x94, 0x2d, 0x02, 0x26, 0xcf, 0x9e, 0x1c}};
        (void)ed0SigningKey;

        auto ed0 = std::make_unique<Ed25519>(ed0PublicKey, ed0Sig);
        {
            auto ed0EncodedFulfillment =
                "\xa4\x64\x80\x20\xa2\x0e\x9f\x32\x9a\x21\x6d\xac\x88\xbf\x51"
                "\xc7\x60\xe1\x4c\xa6\xb6\x4b\x2d\x24\x87\xf6\x29\xae\xd4\xf1"
                "\xe1\x2d\x15\x84\x0b\xb9\x81\x40\x6d\xea\x44\xb0\x26\x5e\x51"
                "\x57\x57\x6a\x49\xd8\x03\x81\x14\xb8\x77\x78\x9f\xb8\x4f\xab"
                "\xce\x19\x07\x4c\xf0\xa6\x3a\x17\x06\x10\x6d\x80\xda\xc6\x8f"
                "\x06\xad\x53\xb0\x9c\x36\x43\xd9\x56\xfd\x5f\xbd\x4a\xf0\xff"
                "\xcb\x20\x96\x7f\x5d\xca\xcf\x23\x52\x62\x91\x08"s;
            auto const ed0EncodedCondition =
                "\xa4\x27\x80\x20\xa3\xbe\x3a\xd0\x8e\x60\x1c\xd7\x95\xb0\xd9"
                "\x00\x49\x56\xdb\xdb\xb5\xd2\x6e\x41\x03\x6a\x6e\x01\x3f\x58"
                "\x43\x14\x5a\xd2\x58\xc5\x81\x03\x02\x00\x00"s;
            auto const ed0EncodedFingerprint =
                "\x30\x22\x80\x20\xa2\x0e\x9f\x32\x9a\x21\x6d\xac\x88\xbf\x51"
                "\xc7\x60\xe1\x4c\xa6\xb6\x4b\x2d\x24\x87\xf6\x29\xae\xd4\xf1"
                "\xe1\x2d\x15\x84\x0b\xb9"s;
            check(
                std::move(ed0),
                ed0Msg,
                std::move(ed0EncodedFulfillment),
                ed0EncodedCondition,
                ed0EncodedFingerprint);
        }
    }

    void
    run()
    {
        testEd0();
    }
};

BEAST_DEFINE_TESTSUITE(Conditions_ed, conditions, ripple);
}  // cryptoconditions
}  // ripple
