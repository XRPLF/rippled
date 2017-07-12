
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

class Conditions_rsa_test : public ConditionsTestBase
{
    void
    testRsa0()
    {
        testcase("Rsa0");

        using namespace std::string_literals;
        using namespace ripple::cryptoconditions;

        // Fulfillment structure
        // * rsa0

        auto const rsa0Msg = "abcdefghijklmnopqrstuvwxyz"s;
        std::array<std::uint8_t, 256> const rsa0PublicKey{
            {0xa7, 0x6c, 0x6f, 0x92, 0x15, 0x7c, 0x56, 0xe8, 0xad, 0x03, 0x9a,
             0x2b, 0x04, 0x6c, 0xb6, 0x41, 0x02, 0x4a, 0x73, 0x12, 0x87, 0x86,
             0xc2, 0x58, 0x3c, 0xd0, 0x04, 0xa4, 0x8c, 0x98, 0xa7, 0x7b, 0x79,
             0x21, 0xba, 0xde, 0x38, 0xca, 0x67, 0xef, 0xdd, 0xb4, 0x9e, 0x37,
             0x8b, 0x65, 0xf5, 0x5a, 0xce, 0xfe, 0x16, 0xfd, 0xad, 0x62, 0x7b,
             0xa7, 0xba, 0x5e, 0x2c, 0x10, 0x99, 0x40, 0x30, 0xf6, 0xee, 0x26,
             0x49, 0x27, 0x3b, 0xc5, 0xb8, 0x21, 0x30, 0x5c, 0x3e, 0x46, 0xb6,
             0x02, 0x15, 0xc4, 0x0f, 0x18, 0x24, 0xf2, 0xcb, 0x38, 0x8b, 0x47,
             0x77, 0x31, 0x63, 0x71, 0x48, 0xee, 0x76, 0xb3, 0xfd, 0xb6, 0xa2,
             0xa0, 0x8e, 0x5a, 0x01, 0x73, 0x32, 0x35, 0xbf, 0x98, 0x39, 0x3e,
             0x1a, 0xab, 0x87, 0xda, 0xbf, 0x82, 0x0c, 0x71, 0x2e, 0xb5, 0x8a,
             0x54, 0xcc, 0x7f, 0xc2, 0xbf, 0xb4, 0x7f, 0xe2, 0xe5, 0xb9, 0x0f,
             0x6e, 0xee, 0x4a, 0x2f, 0x8b, 0x20, 0xb1, 0xea, 0xaa, 0xdc, 0x89,
             0x59, 0x34, 0x7c, 0xba, 0x9b, 0x3a, 0xa5, 0xe1, 0x57, 0x70, 0x64,
             0x68, 0xf1, 0xbb, 0xae, 0x40, 0xf6, 0xf8, 0x90, 0x63, 0x55, 0x75,
             0x3a, 0x3a, 0x9e, 0x71, 0x6a, 0x93, 0x50, 0x8d, 0x62, 0xfd, 0xaf,
             0x89, 0x8a, 0x1f, 0x27, 0x57, 0xdd, 0x30, 0xd3, 0xca, 0x7d, 0x8f,
             0xa5, 0x59, 0xca, 0x4e, 0xe4, 0xec, 0x9d, 0x25, 0x20, 0xa7, 0x81,
             0x6a, 0x06, 0xa6, 0xbb, 0xb2, 0xf1, 0x75, 0xf9, 0x6b, 0x29, 0xc7,
             0x75, 0xc0, 0x9f, 0xcc, 0x87, 0x42, 0x19, 0x9f, 0x8b, 0xb5, 0x2b,
             0xa6, 0x9a, 0xc3, 0x61, 0x88, 0x74, 0x82, 0x91, 0x76, 0xf5, 0x46,
             0x8c, 0xe7, 0x81, 0xf5, 0x2c, 0x25, 0x6d, 0x5d, 0x50, 0x18, 0x03,
             0xd6, 0x1d, 0xed, 0x8d, 0x74, 0x7f, 0x05, 0xae, 0xbb, 0x35, 0xb9,
             0xe7, 0x6f, 0xe1}};
        std::array<std::uint8_t, 256> const rsa0Sig{
            {0x73, 0x78, 0x37, 0xfc, 0x66, 0x28, 0x75, 0x62, 0xd9, 0xd1, 0x7e,
             0x23, 0x82, 0x65, 0xe7, 0xef, 0x73, 0x31, 0xd8, 0x6a, 0xdc, 0xbb,
             0xeb, 0xa8, 0x81, 0x84, 0xa8, 0x2d, 0x48, 0x77, 0xd0, 0xa6, 0xfd,
             0x60, 0x36, 0x05, 0x8a, 0x9a, 0xc0, 0xf9, 0xb0, 0x62, 0x2c, 0x9a,
             0xab, 0xb0, 0x50, 0x57, 0x37, 0x23, 0x42, 0x43, 0x92, 0x84, 0xa8,
             0x75, 0x88, 0x11, 0x05, 0xc3, 0x24, 0x65, 0xa7, 0x65, 0x6d, 0xf1,
             0x30, 0x5e, 0x03, 0x44, 0x7d, 0x62, 0x7f, 0xb4, 0xfd, 0x4e, 0x52,
             0x6e, 0xfe, 0xf5, 0x66, 0x50, 0x91, 0x42, 0xd8, 0x9c, 0x40, 0x23,
             0x8f, 0x4c, 0xc4, 0x88, 0xca, 0xdd, 0xb5, 0x47, 0xca, 0x0b, 0x07,
             0x6d, 0x09, 0x7d, 0xa4, 0x4e, 0xeb, 0x30, 0x15, 0x18, 0x00, 0x1c,
             0x12, 0xc8, 0x06, 0x96, 0xbc, 0xdb, 0xc9, 0x64, 0xa0, 0x16, 0xa2,
             0xfb, 0xe5, 0xe2, 0x0f, 0x3c, 0x79, 0x25, 0xa0, 0x55, 0x05, 0xdf,
             0xfb, 0x11, 0x54, 0xdd, 0x14, 0x2e, 0x3b, 0x2b, 0x3c, 0x3b, 0x0e,
             0x57, 0x36, 0xe0, 0x37, 0xa9, 0x85, 0xfc, 0xc4, 0xbc, 0x39, 0xcd,
             0xf8, 0xa6, 0xb3, 0xa5, 0xf3, 0x97, 0xb8, 0x73, 0x6c, 0xef, 0xc9,
             0x58, 0x1a, 0xec, 0xa1, 0x69, 0x2d, 0xfc, 0x8f, 0x94, 0x13, 0xfa,
             0x61, 0xf9, 0x8a, 0x38, 0x92, 0x0c, 0x2f, 0x4d, 0x6e, 0x10, 0x0d,
             0xe3, 0x6d, 0x6d, 0xc3, 0xc1, 0x23, 0x5b, 0x52, 0x50, 0x3e, 0x8c,
             0xff, 0xaa, 0xe6, 0xbd, 0x0d, 0x52, 0xcb, 0x2c, 0xcb, 0x54, 0xfa,
             0x25, 0x85, 0x57, 0x4c, 0x89, 0xa6, 0x37, 0xc2, 0x28, 0xaf, 0xf2,
             0x90, 0xe5, 0x88, 0x2c, 0xc8, 0xa8, 0xb4, 0xa3, 0xbc, 0x9b, 0x0d,
             0x83, 0x56, 0xea, 0xf8, 0x44, 0x77, 0x12, 0x1c, 0x05, 0x86, 0x1a,
             0x45, 0xc5, 0x32, 0x2a, 0x36, 0xe9, 0x1b, 0x25, 0xb6, 0xfe, 0xf2,
             0xfa, 0x6a, 0x93}};

        auto rsa0 = std::make_unique<RsaSha256>(
            makeSlice(rsa0PublicKey), makeSlice(rsa0Sig));
        {
            auto rsa0EncodedFulfillment =
                "\xa3\x82\x02\x08\x80\x82\x01\x00\xa7\x6c\x6f\x92\x15\x7c\x56"
                "\xe8\xad\x03\x9a\x2b\x04\x6c\xb6\x41\x02\x4a\x73\x12\x87\x86"
                "\xc2\x58\x3c\xd0\x04\xa4\x8c\x98\xa7\x7b\x79\x21\xba\xde\x38"
                "\xca\x67\xef\xdd\xb4\x9e\x37\x8b\x65\xf5\x5a\xce\xfe\x16\xfd"
                "\xad\x62\x7b\xa7\xba\x5e\x2c\x10\x99\x40\x30\xf6\xee\x26\x49"
                "\x27\x3b\xc5\xb8\x21\x30\x5c\x3e\x46\xb6\x02\x15\xc4\x0f\x18"
                "\x24\xf2\xcb\x38\x8b\x47\x77\x31\x63\x71\x48\xee\x76\xb3\xfd"
                "\xb6\xa2\xa0\x8e\x5a\x01\x73\x32\x35\xbf\x98\x39\x3e\x1a\xab"
                "\x87\xda\xbf\x82\x0c\x71\x2e\xb5\x8a\x54\xcc\x7f\xc2\xbf\xb4"
                "\x7f\xe2\xe5\xb9\x0f\x6e\xee\x4a\x2f\x8b\x20\xb1\xea\xaa\xdc"
                "\x89\x59\x34\x7c\xba\x9b\x3a\xa5\xe1\x57\x70\x64\x68\xf1\xbb"
                "\xae\x40\xf6\xf8\x90\x63\x55\x75\x3a\x3a\x9e\x71\x6a\x93\x50"
                "\x8d\x62\xfd\xaf\x89\x8a\x1f\x27\x57\xdd\x30\xd3\xca\x7d\x8f"
                "\xa5\x59\xca\x4e\xe4\xec\x9d\x25\x20\xa7\x81\x6a\x06\xa6\xbb"
                "\xb2\xf1\x75\xf9\x6b\x29\xc7\x75\xc0\x9f\xcc\x87\x42\x19\x9f"
                "\x8b\xb5\x2b\xa6\x9a\xc3\x61\x88\x74\x82\x91\x76\xf5\x46\x8c"
                "\xe7\x81\xf5\x2c\x25\x6d\x5d\x50\x18\x03\xd6\x1d\xed\x8d\x74"
                "\x7f\x05\xae\xbb\x35\xb9\xe7\x6f\xe1\x81\x82\x01\x00\x73\x78"
                "\x37\xfc\x66\x28\x75\x62\xd9\xd1\x7e\x23\x82\x65\xe7\xef\x73"
                "\x31\xd8\x6a\xdc\xbb\xeb\xa8\x81\x84\xa8\x2d\x48\x77\xd0\xa6"
                "\xfd\x60\x36\x05\x8a\x9a\xc0\xf9\xb0\x62\x2c\x9a\xab\xb0\x50"
                "\x57\x37\x23\x42\x43\x92\x84\xa8\x75\x88\x11\x05\xc3\x24\x65"
                "\xa7\x65\x6d\xf1\x30\x5e\x03\x44\x7d\x62\x7f\xb4\xfd\x4e\x52"
                "\x6e\xfe\xf5\x66\x50\x91\x42\xd8\x9c\x40\x23\x8f\x4c\xc4\x88"
                "\xca\xdd\xb5\x47\xca\x0b\x07\x6d\x09\x7d\xa4\x4e\xeb\x30\x15"
                "\x18\x00\x1c\x12\xc8\x06\x96\xbc\xdb\xc9\x64\xa0\x16\xa2\xfb"
                "\xe5\xe2\x0f\x3c\x79\x25\xa0\x55\x05\xdf\xfb\x11\x54\xdd\x14"
                "\x2e\x3b\x2b\x3c\x3b\x0e\x57\x36\xe0\x37\xa9\x85\xfc\xc4\xbc"
                "\x39\xcd\xf8\xa6\xb3\xa5\xf3\x97\xb8\x73\x6c\xef\xc9\x58\x1a"
                "\xec\xa1\x69\x2d\xfc\x8f\x94\x13\xfa\x61\xf9\x8a\x38\x92\x0c"
                "\x2f\x4d\x6e\x10\x0d\xe3\x6d\x6d\xc3\xc1\x23\x5b\x52\x50\x3e"
                "\x8c\xff\xaa\xe6\xbd\x0d\x52\xcb\x2c\xcb\x54\xfa\x25\x85\x57"
                "\x4c\x89\xa6\x37\xc2\x28\xaf\xf2\x90\xe5\x88\x2c\xc8\xa8\xb4"
                "\xa3\xbc\x9b\x0d\x83\x56\xea\xf8\x44\x77\x12\x1c\x05\x86\x1a"
                "\x45\xc5\x32\x2a\x36\xe9\x1b\x25\xb6\xfe\xf2\xfa\x6a\x93"s;
            auto const rsa0EncodedCondition =
                "\xa3\x27\x80\x20\x07\x62\xb2\xf0\x74\x9d\x30\xf9\xaa\xf6\x56"
                "\x29\x05\x9d\xb1\x00\x4b\xd6\x8f\x1e\x1d\xa7\x38\xb0\x34\x9c"
                "\x27\xaa\x3a\xfa\x12\x09\x81\x03\x01\x00\x00"s;
            auto const rsa0EncodedFingerprint =
                "\x30\x82\x01\x04\x80\x82\x01\x00\xa7\x6c\x6f\x92\x15\x7c\x56"
                "\xe8\xad\x03\x9a\x2b\x04\x6c\xb6\x41\x02\x4a\x73\x12\x87\x86"
                "\xc2\x58\x3c\xd0\x04\xa4\x8c\x98\xa7\x7b\x79\x21\xba\xde\x38"
                "\xca\x67\xef\xdd\xb4\x9e\x37\x8b\x65\xf5\x5a\xce\xfe\x16\xfd"
                "\xad\x62\x7b\xa7\xba\x5e\x2c\x10\x99\x40\x30\xf6\xee\x26\x49"
                "\x27\x3b\xc5\xb8\x21\x30\x5c\x3e\x46\xb6\x02\x15\xc4\x0f\x18"
                "\x24\xf2\xcb\x38\x8b\x47\x77\x31\x63\x71\x48\xee\x76\xb3\xfd"
                "\xb6\xa2\xa0\x8e\x5a\x01\x73\x32\x35\xbf\x98\x39\x3e\x1a\xab"
                "\x87\xda\xbf\x82\x0c\x71\x2e\xb5\x8a\x54\xcc\x7f\xc2\xbf\xb4"
                "\x7f\xe2\xe5\xb9\x0f\x6e\xee\x4a\x2f\x8b\x20\xb1\xea\xaa\xdc"
                "\x89\x59\x34\x7c\xba\x9b\x3a\xa5\xe1\x57\x70\x64\x68\xf1\xbb"
                "\xae\x40\xf6\xf8\x90\x63\x55\x75\x3a\x3a\x9e\x71\x6a\x93\x50"
                "\x8d\x62\xfd\xaf\x89\x8a\x1f\x27\x57\xdd\x30\xd3\xca\x7d\x8f"
                "\xa5\x59\xca\x4e\xe4\xec\x9d\x25\x20\xa7\x81\x6a\x06\xa6\xbb"
                "\xb2\xf1\x75\xf9\x6b\x29\xc7\x75\xc0\x9f\xcc\x87\x42\x19\x9f"
                "\x8b\xb5\x2b\xa6\x9a\xc3\x61\x88\x74\x82\x91\x76\xf5\x46\x8c"
                "\xe7\x81\xf5\x2c\x25\x6d\x5d\x50\x18\x03\xd6\x1d\xed\x8d\x74"
                "\x7f\x05\xae\xbb\x35\xb9\xe7\x6f\xe1"s;
            check(
                std::move(rsa0),
                rsa0Msg,
                std::move(rsa0EncodedFulfillment),
                rsa0EncodedCondition,
                rsa0EncodedFingerprint);
        }
    }

    void
    run()
    {
        testRsa0();
    }
};

BEAST_DEFINE_TESTSUITE(Conditions_rsa, conditions, ripple);
}  // cryptoconditions
}  // ripple
