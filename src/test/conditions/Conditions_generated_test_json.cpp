
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

class Conditions_json_test : public ConditionsTestBase
{
    void
    testPreim0()
    {
        testcase("Preim0");

        using namespace std::string_literals;
        using namespace ripple::cryptoconditions;

        // Fulfillment structure
        // * preim0

        auto const preim0Preimage = ""s;
        auto const preim0Msg = ""s;

        auto preim0 =
            std::make_unique<PreimageSha256>(makeSlice(preim0Preimage));
        {
            auto preim0EncodedFulfillment = "\xa0\x02\x80\x00"s;
            auto const preim0EncodedCondition =
                "\xa0\x25\x80\x20\xe3\xb0\xc4\x42\x98\xfc\x1c\x14\x9a\xfb\xf4"
                "\xc8\x99\x6f\xb9\x24\x27\xae\x41\xe4\x64\x9b\x93\x4c\xa4\x95"
                "\x99\x1b\x78\x52\xb8\x55\x81\x01\x00"s;
            auto const preim0EncodedFingerprint = ""s;
            check(
                std::move(preim0),
                preim0Msg,
                std::move(preim0EncodedFulfillment),
                preim0EncodedCondition,
                preim0EncodedFingerprint);
        }
    }

    void
    testPrefix0()
    {
        testcase("Prefix0");

        using namespace std::string_literals;
        using namespace ripple::cryptoconditions;

        // Fulfillment structure
        // * prefix0
        // ** preim1

        auto const preim1Preimage = ""s;
        auto const preim1Msg = ""s;
        auto const prefix0Prefix = ""s;
        auto const prefix0Msg = ""s;
        auto const prefix0MaxMsgLength = 0;

        auto preim1 =
            std::make_unique<PreimageSha256>(makeSlice(preim1Preimage));
        auto prefix0 = std::make_unique<PrefixSha256>(
            makeSlice(prefix0Prefix), prefix0MaxMsgLength, std::move(preim1));
        {
            auto prefix0EncodedFulfillment =
                "\xa1\x0b\x80\x00\x81\x01\x00\xa2\x04\xa0\x02\x80\x00"s;
            auto const prefix0EncodedCondition =
                "\xa1\x2a\x80\x20\xbb\x1a\xc5\x26\x0c\x01\x41\xb7\xe5\x4b\x26"
                "\xec\x23\x30\x63\x7c\x55\x97\xbf\x81\x19\x51\xac\x09\xe7\x44"
                "\xad\x20\xff\x77\xe2\x87\x81\x02\x04\x00\x82\x02\x07\x80"s;
            auto const prefix0EncodedFingerprint =
                "\x30\x2e\x80\x00\x81\x01\x00\xa2\x27\xa0\x25\x80\x20\xe3\xb0"
                "\xc4\x42\x98\xfc\x1c\x14\x9a\xfb\xf4\xc8\x99\x6f\xb9\x24\x27"
                "\xae\x41\xe4\x64\x9b\x93\x4c\xa4\x95\x99\x1b\x78\x52\xb8\x55"
                "\x81\x01\x00"s;
            check(
                std::move(prefix0),
                prefix0Msg,
                std::move(prefix0EncodedFulfillment),
                prefix0EncodedCondition,
                prefix0EncodedFingerprint);
        }
    }

    void
    testThresh0()
    {
        testcase("Thresh0");

        using namespace std::string_literals;
        using namespace ripple::cryptoconditions;

        // Fulfillment structure
        // * thresh0
        // ** preim1

        auto const preim1Preimage = ""s;
        auto const preim1Msg = ""s;
        auto const thresh0Msg = ""s;

        auto preim1 =
            std::make_unique<PreimageSha256>(makeSlice(preim1Preimage));
        std::vector<std::unique_ptr<Fulfillment>> thresh0Subfulfillments;
        thresh0Subfulfillments.emplace_back(std::move(preim1));
        std::vector<Condition> thresh0Subconditions{};
        auto thresh0 = std::make_unique<ThresholdSha256>(
            std::move(thresh0Subfulfillments), std::move(thresh0Subconditions));
        {
            auto thresh0EncodedFulfillment =
                "\xa2\x08\xa0\x04\xa0\x02\x80\x00\xa1\x00"s;
            auto const thresh0EncodedCondition =
                "\xa2\x2a\x80\x20\xb4\xb8\x41\x36\xdf\x48\xa7\x1d\x73\xf4\x98"
                "\x5c\x04\xc6\x76\x7a\x77\x8e\xcb\x65\xba\x70\x23\xb4\x50\x68"
                "\x23\xbe\xee\x76\x31\xb9\x81\x02\x04\x00\x82\x02\x07\x80"s;
            auto const thresh0EncodedFingerprint =
                "\x30\x2c\x80\x01\x01\xa1\x27\xa0\x25\x80\x20\xe3\xb0\xc4\x42"
                "\x98\xfc\x1c\x14\x9a\xfb\xf4\xc8\x99\x6f\xb9\x24\x27\xae\x41"
                "\xe4\x64\x9b\x93\x4c\xa4\x95\x99\x1b\x78\x52\xb8\x55\x81\x01"
                "\x00"s;
            check(
                std::move(thresh0),
                thresh0Msg,
                std::move(thresh0EncodedFulfillment),
                thresh0EncodedCondition,
                thresh0EncodedFingerprint);
        }
    }

    void
    testRsa0()
    {
        testcase("Rsa0");

        using namespace std::string_literals;
        using namespace ripple::cryptoconditions;

        // Fulfillment structure
        // * rsa0

        auto const rsa0Msg = ""s;
        std::array<std::uint8_t, 256> const rsa0PublicKey{
            {0xe1, 0xef, 0x8b, 0x24, 0xd6, 0xf7, 0x6b, 0x09, 0xc8, 0x1e, 0xd7,
             0x75, 0x2a, 0xa2, 0x62, 0xf0, 0x44, 0xf0, 0x4a, 0x87, 0x4d, 0x43,
             0x80, 0x9d, 0x31, 0xce, 0xa6, 0x12, 0xf9, 0x9b, 0x0c, 0x97, 0xa8,
             0xb4, 0x37, 0x41, 0x53, 0xe3, 0xee, 0xf3, 0xd6, 0x66, 0x16, 0x84,
             0x3e, 0x0e, 0x41, 0xc2, 0x93, 0x26, 0x4b, 0x71, 0xb6, 0x17, 0x3d,
             0xb1, 0xcf, 0x0d, 0x6c, 0xd5, 0x58, 0xc5, 0x86, 0x57, 0x70, 0x6f,
             0xcf, 0x09, 0x7f, 0x70, 0x4c, 0x48, 0x3e, 0x59, 0xcb, 0xfd, 0xfd,
             0x5b, 0x3e, 0xe7, 0xbc, 0x80, 0xd7, 0x40, 0xc5, 0xe0, 0xf0, 0x47,
             0xf3, 0xe8, 0x5f, 0xc0, 0xd7, 0x58, 0x15, 0x77, 0x6a, 0x6f, 0x3f,
             0x23, 0xc5, 0xdc, 0x5e, 0x79, 0x71, 0x39, 0xa6, 0x88, 0x2e, 0x38,
             0x33, 0x6a, 0x4a, 0x5f, 0xb3, 0x61, 0x37, 0x62, 0x0f, 0xf3, 0x66,
             0x3d, 0xba, 0xe3, 0x28, 0x47, 0x28, 0x01, 0x86, 0x2f, 0x72, 0xf2,
             0xf8, 0x7b, 0x20, 0x2b, 0x9c, 0x89, 0xad, 0xd7, 0xcd, 0x5b, 0x0a,
             0x07, 0x6f, 0x7c, 0x53, 0xe3, 0x50, 0x39, 0xf6, 0x7e, 0xd1, 0x7e,
             0xc8, 0x15, 0xe5, 0xb4, 0x30, 0x5c, 0xc6, 0x31, 0x97, 0x06, 0x8d,
             0x5e, 0x6e, 0x57, 0x9b, 0xa6, 0xde, 0x5f, 0x4e, 0x3e, 0x57, 0xdf,
             0x5e, 0x4e, 0x07, 0x2f, 0xf2, 0xce, 0x4c, 0x66, 0xeb, 0x45, 0x23,
             0x39, 0x73, 0x87, 0x52, 0x75, 0x96, 0x39, 0xf0, 0x25, 0x7b, 0xf5,
             0x7d, 0xbd, 0x5c, 0x44, 0x3f, 0xb5, 0x15, 0x8c, 0xce, 0x0a, 0x3d,
             0x36, 0xad, 0xc7, 0xba, 0x01, 0xf3, 0x3a, 0x0b, 0xb6, 0xdb, 0xb2,
             0xbf, 0x98, 0x9d, 0x60, 0x71, 0x12, 0xf2, 0x34, 0x4d, 0x99, 0x3e,
             0x77, 0xe5, 0x63, 0xc1, 0xd3, 0x61, 0xde, 0xdf, 0x57, 0xda, 0x96,
             0xef, 0x2c, 0xfc, 0x68, 0x5f, 0x00, 0x2b, 0x63, 0x82, 0x46, 0xa5,
             0xb3, 0x09, 0xb9}};
        std::array<std::uint8_t, 256> const rsa0Sig{
            {0xbd, 0x42, 0xd6, 0x56, 0x9f, 0x65, 0x99, 0xae, 0xd4, 0x55, 0xf9,
             0x6b, 0xc0, 0xed, 0x08, 0xed, 0x14, 0x80, 0xbf, 0x36, 0xcd, 0x9e,
             0x14, 0x67, 0xf9, 0xc6, 0xf7, 0x44, 0x61, 0xc9, 0xe3, 0xa7, 0x49,
             0x33, 0x4b, 0x2f, 0x64, 0x04, 0xaa, 0x5f, 0x9f, 0x6b, 0xaf, 0xe7,
             0x6c, 0x34, 0x7d, 0x06, 0x92, 0x50, 0xb3, 0x5d, 0x1c, 0x97, 0x0c,
             0x79, 0x30, 0x59, 0xee, 0x73, 0x3a, 0x81, 0x93, 0xf3, 0x0f, 0xa7,
             0x8f, 0xec, 0x7c, 0xae, 0x45, 0x9e, 0x3d, 0xdf, 0xd7, 0x63, 0x38,
             0x05, 0xd4, 0x76, 0x94, 0x0d, 0x0c, 0xb5, 0x3d, 0x7f, 0xb3, 0x89,
             0xdc, 0xda, 0xea, 0xf6, 0xe8, 0xcf, 0x48, 0xc4, 0xb5, 0x63, 0x54,
             0x30, 0xe4, 0xf2, 0xbc, 0xdf, 0xe5, 0x05, 0xc2, 0xc0, 0xfc, 0x17,
             0xb4, 0x0d, 0x93, 0xc7, 0xed, 0xb7, 0xc2, 0x61, 0xeb, 0xf4, 0x38,
             0x95, 0xa7, 0x05, 0xe0, 0x24, 0xaa, 0x05, 0x49, 0xa6, 0x60, 0xf7,
             0x0a, 0x32, 0x15, 0x06, 0x47, 0x52, 0x2d, 0xbe, 0x6b, 0x63, 0x52,
             0x04, 0x97, 0xcf, 0xf8, 0xf8, 0xd5, 0xd7, 0x47, 0x68, 0xa2, 0x7c,
             0x5b, 0x86, 0xe5, 0x80, 0xbe, 0x3f, 0xcd, 0xc9, 0x6f, 0x19, 0x76,
             0x29, 0x3c, 0xba, 0x0d, 0x58, 0xdf, 0xc6, 0x0b, 0x51, 0x8b, 0x63,
             0x2a, 0x6d, 0xc1, 0xe9, 0x50, 0xc4, 0x3e, 0x23, 0x1f, 0xe1, 0xa3,
             0x79, 0xaa, 0x6d, 0xdc, 0xc5, 0x2c, 0x70, 0xed, 0xf8, 0x51, 0xc6,
             0xc0, 0x12, 0x3a, 0x96, 0x42, 0x61, 0xcf, 0xdb, 0x38, 0x57, 0xcd,
             0x6c, 0xd5, 0xad, 0xc3, 0x7d, 0x8d, 0xa2, 0xcc, 0x92, 0x4e, 0xda,
             0xe1, 0xd8, 0x4c, 0xf6, 0x12, 0x45, 0x87, 0xf2, 0x74, 0xc1, 0xfa,
             0x36, 0x97, 0xda, 0x29, 0x01, 0xf0, 0x26, 0x9f, 0x03, 0xb2, 0x43,
             0xc0, 0x3b, 0x61, 0x4e, 0x03, 0x85, 0xe1, 0x96, 0x1f, 0xac, 0x50,
             0x00, 0xf9, 0xbb}};

        auto rsa0 = std::make_unique<RsaSha256>(
            makeSlice(rsa0PublicKey), makeSlice(rsa0Sig));
        {
            auto rsa0EncodedFulfillment =
                "\xa3\x82\x02\x08\x80\x82\x01\x00\xe1\xef\x8b\x24\xd6\xf7\x6b"
                "\x09\xc8\x1e\xd7\x75\x2a\xa2\x62\xf0\x44\xf0\x4a\x87\x4d\x43"
                "\x80\x9d\x31\xce\xa6\x12\xf9\x9b\x0c\x97\xa8\xb4\x37\x41\x53"
                "\xe3\xee\xf3\xd6\x66\x16\x84\x3e\x0e\x41\xc2\x93\x26\x4b\x71"
                "\xb6\x17\x3d\xb1\xcf\x0d\x6c\xd5\x58\xc5\x86\x57\x70\x6f\xcf"
                "\x09\x7f\x70\x4c\x48\x3e\x59\xcb\xfd\xfd\x5b\x3e\xe7\xbc\x80"
                "\xd7\x40\xc5\xe0\xf0\x47\xf3\xe8\x5f\xc0\xd7\x58\x15\x77\x6a"
                "\x6f\x3f\x23\xc5\xdc\x5e\x79\x71\x39\xa6\x88\x2e\x38\x33\x6a"
                "\x4a\x5f\xb3\x61\x37\x62\x0f\xf3\x66\x3d\xba\xe3\x28\x47\x28"
                "\x01\x86\x2f\x72\xf2\xf8\x7b\x20\x2b\x9c\x89\xad\xd7\xcd\x5b"
                "\x0a\x07\x6f\x7c\x53\xe3\x50\x39\xf6\x7e\xd1\x7e\xc8\x15\xe5"
                "\xb4\x30\x5c\xc6\x31\x97\x06\x8d\x5e\x6e\x57\x9b\xa6\xde\x5f"
                "\x4e\x3e\x57\xdf\x5e\x4e\x07\x2f\xf2\xce\x4c\x66\xeb\x45\x23"
                "\x39\x73\x87\x52\x75\x96\x39\xf0\x25\x7b\xf5\x7d\xbd\x5c\x44"
                "\x3f\xb5\x15\x8c\xce\x0a\x3d\x36\xad\xc7\xba\x01\xf3\x3a\x0b"
                "\xb6\xdb\xb2\xbf\x98\x9d\x60\x71\x12\xf2\x34\x4d\x99\x3e\x77"
                "\xe5\x63\xc1\xd3\x61\xde\xdf\x57\xda\x96\xef\x2c\xfc\x68\x5f"
                "\x00\x2b\x63\x82\x46\xa5\xb3\x09\xb9\x81\x82\x01\x00\xbd\x42"
                "\xd6\x56\x9f\x65\x99\xae\xd4\x55\xf9\x6b\xc0\xed\x08\xed\x14"
                "\x80\xbf\x36\xcd\x9e\x14\x67\xf9\xc6\xf7\x44\x61\xc9\xe3\xa7"
                "\x49\x33\x4b\x2f\x64\x04\xaa\x5f\x9f\x6b\xaf\xe7\x6c\x34\x7d"
                "\x06\x92\x50\xb3\x5d\x1c\x97\x0c\x79\x30\x59\xee\x73\x3a\x81"
                "\x93\xf3\x0f\xa7\x8f\xec\x7c\xae\x45\x9e\x3d\xdf\xd7\x63\x38"
                "\x05\xd4\x76\x94\x0d\x0c\xb5\x3d\x7f\xb3\x89\xdc\xda\xea\xf6"
                "\xe8\xcf\x48\xc4\xb5\x63\x54\x30\xe4\xf2\xbc\xdf\xe5\x05\xc2"
                "\xc0\xfc\x17\xb4\x0d\x93\xc7\xed\xb7\xc2\x61\xeb\xf4\x38\x95"
                "\xa7\x05\xe0\x24\xaa\x05\x49\xa6\x60\xf7\x0a\x32\x15\x06\x47"
                "\x52\x2d\xbe\x6b\x63\x52\x04\x97\xcf\xf8\xf8\xd5\xd7\x47\x68"
                "\xa2\x7c\x5b\x86\xe5\x80\xbe\x3f\xcd\xc9\x6f\x19\x76\x29\x3c"
                "\xba\x0d\x58\xdf\xc6\x0b\x51\x8b\x63\x2a\x6d\xc1\xe9\x50\xc4"
                "\x3e\x23\x1f\xe1\xa3\x79\xaa\x6d\xdc\xc5\x2c\x70\xed\xf8\x51"
                "\xc6\xc0\x12\x3a\x96\x42\x61\xcf\xdb\x38\x57\xcd\x6c\xd5\xad"
                "\xc3\x7d\x8d\xa2\xcc\x92\x4e\xda\xe1\xd8\x4c\xf6\x12\x45\x87"
                "\xf2\x74\xc1\xfa\x36\x97\xda\x29\x01\xf0\x26\x9f\x03\xb2\x43"
                "\xc0\x3b\x61\x4e\x03\x85\xe1\x96\x1f\xac\x50\x00\xf9\xbb"s;
            auto const rsa0EncodedCondition =
                "\xa3\x27\x80\x20\xb3\x1f\xa8\x20\x6e\x4e\xa7\xe5\x15\x33\x7b"
                "\x3b\x33\x08\x2b\x87\x76\x51\x80\x10\x85\xed\x84\xfb\x4d\xae"
                "\xb2\x47\xbf\x69\x8d\x7f\x81\x03\x01\x00\x00"s;
            auto const rsa0EncodedFingerprint =
                "\x30\x82\x01\x04\x80\x82\x01\x00\xe1\xef\x8b\x24\xd6\xf7\x6b"
                "\x09\xc8\x1e\xd7\x75\x2a\xa2\x62\xf0\x44\xf0\x4a\x87\x4d\x43"
                "\x80\x9d\x31\xce\xa6\x12\xf9\x9b\x0c\x97\xa8\xb4\x37\x41\x53"
                "\xe3\xee\xf3\xd6\x66\x16\x84\x3e\x0e\x41\xc2\x93\x26\x4b\x71"
                "\xb6\x17\x3d\xb1\xcf\x0d\x6c\xd5\x58\xc5\x86\x57\x70\x6f\xcf"
                "\x09\x7f\x70\x4c\x48\x3e\x59\xcb\xfd\xfd\x5b\x3e\xe7\xbc\x80"
                "\xd7\x40\xc5\xe0\xf0\x47\xf3\xe8\x5f\xc0\xd7\x58\x15\x77\x6a"
                "\x6f\x3f\x23\xc5\xdc\x5e\x79\x71\x39\xa6\x88\x2e\x38\x33\x6a"
                "\x4a\x5f\xb3\x61\x37\x62\x0f\xf3\x66\x3d\xba\xe3\x28\x47\x28"
                "\x01\x86\x2f\x72\xf2\xf8\x7b\x20\x2b\x9c\x89\xad\xd7\xcd\x5b"
                "\x0a\x07\x6f\x7c\x53\xe3\x50\x39\xf6\x7e\xd1\x7e\xc8\x15\xe5"
                "\xb4\x30\x5c\xc6\x31\x97\x06\x8d\x5e\x6e\x57\x9b\xa6\xde\x5f"
                "\x4e\x3e\x57\xdf\x5e\x4e\x07\x2f\xf2\xce\x4c\x66\xeb\x45\x23"
                "\x39\x73\x87\x52\x75\x96\x39\xf0\x25\x7b\xf5\x7d\xbd\x5c\x44"
                "\x3f\xb5\x15\x8c\xce\x0a\x3d\x36\xad\xc7\xba\x01\xf3\x3a\x0b"
                "\xb6\xdb\xb2\xbf\x98\x9d\x60\x71\x12\xf2\x34\x4d\x99\x3e\x77"
                "\xe5\x63\xc1\xd3\x61\xde\xdf\x57\xda\x96\xef\x2c\xfc\x68\x5f"
                "\x00\x2b\x63\x82\x46\xa5\xb3\x09\xb9"s;
            check(
                std::move(rsa0),
                rsa0Msg,
                std::move(rsa0EncodedFulfillment),
                rsa0EncodedCondition,
                rsa0EncodedFingerprint);
        }
    }

    void
    testEd0()
    {
        testcase("Ed0");

        using namespace std::string_literals;
        using namespace ripple::cryptoconditions;

        // Fulfillment structure
        // * ed0

        auto const ed0Msg = ""s;
        std::array<std::uint8_t, 32> const ed0PublicKey{
            {0xd7, 0x5a, 0x98, 0x01, 0x82, 0xb1, 0x0a, 0xb7, 0xd5, 0x4b, 0xfe,
             0xd3, 0xc9, 0x64, 0x07, 0x3a, 0x0e, 0xe1, 0x72, 0xf3, 0xda, 0xa6,
             0x23, 0x25, 0xaf, 0x02, 0x1a, 0x68, 0xf7, 0x07, 0x51, 0x1a}};
        std::array<std::uint8_t, 64> const ed0Sig{
            {0xe5, 0x56, 0x43, 0x00, 0xc3, 0x60, 0xac, 0x72, 0x90, 0x86, 0xe2,
             0xcc, 0x80, 0x6e, 0x82, 0x8a, 0x84, 0x87, 0x7f, 0x1e, 0xb8, 0xe5,
             0xd9, 0x74, 0xd8, 0x73, 0xe0, 0x65, 0x22, 0x49, 0x01, 0x55, 0x5f,
             0xb8, 0x82, 0x15, 0x90, 0xa3, 0x3b, 0xac, 0xc6, 0x1e, 0x39, 0x70,
             0x1c, 0xf9, 0xb4, 0x6b, 0xd2, 0x5b, 0xf5, 0xf0, 0x59, 0x5b, 0xbe,
             0x24, 0x65, 0x51, 0x41, 0x43, 0x8e, 0x7a, 0x10, 0x0b}};

        auto ed0 = std::make_unique<Ed25519>(ed0PublicKey, ed0Sig);
        {
            auto ed0EncodedFulfillment =
                "\xa4\x64\x80\x20\xd7\x5a\x98\x01\x82\xb1\x0a\xb7\xd5\x4b\xfe"
                "\xd3\xc9\x64\x07\x3a\x0e\xe1\x72\xf3\xda\xa6\x23\x25\xaf\x02"
                "\x1a\x68\xf7\x07\x51\x1a\x81\x40\xe5\x56\x43\x00\xc3\x60\xac"
                "\x72\x90\x86\xe2\xcc\x80\x6e\x82\x8a\x84\x87\x7f\x1e\xb8\xe5"
                "\xd9\x74\xd8\x73\xe0\x65\x22\x49\x01\x55\x5f\xb8\x82\x15\x90"
                "\xa3\x3b\xac\xc6\x1e\x39\x70\x1c\xf9\xb4\x6b\xd2\x5b\xf5\xf0"
                "\x59\x5b\xbe\x24\x65\x51\x41\x43\x8e\x7a\x10\x0b"s;
            auto const ed0EncodedCondition =
                "\xa4\x27\x80\x20\x79\x92\x39\xab\xa8\xfc\x4f\xf7\xea\xbf\xbc"
                "\x4c\x44\xe6\x9e\x8b\xdf\xed\x99\x33\x24\xe1\x2e\xd6\x47\x92"
                "\xab\xe2\x89\xcf\x1d\x5f\x81\x03\x02\x00\x00"s;
            auto const ed0EncodedFingerprint =
                "\x30\x22\x80\x20\xd7\x5a\x98\x01\x82\xb1\x0a\xb7\xd5\x4b\xfe"
                "\xd3\xc9\x64\x07\x3a\x0e\xe1\x72\xf3\xda\xa6\x23\x25\xaf\x02"
                "\x1a\x68\xf7\x07\x51\x1a"s;
            check(
                std::move(ed0),
                ed0Msg,
                std::move(ed0EncodedFulfillment),
                ed0EncodedCondition,
                ed0EncodedFingerprint);
        }
    }

    void
    testPreim1()
    {
        testcase("Preim1");

        using namespace std::string_literals;
        using namespace ripple::cryptoconditions;

        // Fulfillment structure
        // * preim0

        auto const preim0Preimage = "aaa"s;
        auto const preim0Msg = ""s;

        auto preim0 =
            std::make_unique<PreimageSha256>(makeSlice(preim0Preimage));
        {
            auto preim0EncodedFulfillment = "\xa0\x05\x80\x03\x61\x61\x61"s;
            auto const preim0EncodedCondition =
                "\xa0\x25\x80\x20\x98\x34\x87\x6d\xcf\xb0\x5c\xb1\x67\xa5\xc2"
                "\x49\x53\xeb\xa5\x8c\x4a\xc8\x9b\x1a\xdf\x57\xf2\x8f\x2f\x9d"
                "\x09\xaf\x10\x7e\xe8\xf0\x81\x01\x03"s;
            auto const preim0EncodedFingerprint = "\x61\x61\x61"s;
            check(
                std::move(preim0),
                preim0Msg,
                std::move(preim0EncodedFulfillment),
                preim0EncodedCondition,
                preim0EncodedFingerprint);
        }
    }

    void
    testPrefix1()
    {
        testcase("Prefix1");

        using namespace std::string_literals;
        using namespace ripple::cryptoconditions;

        // Fulfillment structure
        // * prefix0
        // ** ed1

        auto const ed1Msg = ""s;
        std::array<std::uint8_t, 32> const ed1PublicKey{
            {0xd7, 0x5a, 0x98, 0x01, 0x82, 0xb1, 0x0a, 0xb7, 0xd5, 0x4b, 0xfe,
             0xd3, 0xc9, 0x64, 0x07, 0x3a, 0x0e, 0xe1, 0x72, 0xf3, 0xda, 0xa6,
             0x23, 0x25, 0xaf, 0x02, 0x1a, 0x68, 0xf7, 0x07, 0x51, 0x1a}};
        std::array<std::uint8_t, 64> const ed1Sig{
            {0x50, 0x6a, 0x1e, 0xa6, 0x83, 0x18, 0xe6, 0x2d, 0x40, 0x63, 0x5d,
             0xad, 0x04, 0x3e, 0x19, 0x87, 0xeb, 0xc2, 0x6e, 0x5b, 0x5c, 0x44,
             0x06, 0xf7, 0xbd, 0xf8, 0x5a, 0x73, 0x38, 0x8f, 0xbf, 0xe5, 0xc2,
             0x45, 0xac, 0x49, 0xf4, 0x77, 0x0e, 0xbc, 0x78, 0x77, 0x08, 0x27,
             0x0a, 0xa6, 0xa8, 0x76, 0x9f, 0xef, 0xe8, 0x93, 0x0f, 0xd0, 0xea,
             0x1e, 0xe6, 0x4b, 0x31, 0x40, 0x7d, 0x76, 0x95, 0x09}};
        auto const prefix0Prefix = "aaa"s;
        auto const prefix0Msg = ""s;
        auto const prefix0MaxMsgLength = 0;

        auto ed1 = std::make_unique<Ed25519>(ed1PublicKey, ed1Sig);
        auto prefix0 = std::make_unique<PrefixSha256>(
            makeSlice(prefix0Prefix), prefix0MaxMsgLength, std::move(ed1));
        {
            auto prefix0EncodedFulfillment =
                "\xa1\x70\x80\x03\x61\x61\x61\x81\x01\x00\xa2\x66\xa4\x64\x80"
                "\x20\xd7\x5a\x98\x01\x82\xb1\x0a\xb7\xd5\x4b\xfe\xd3\xc9\x64"
                "\x07\x3a\x0e\xe1\x72\xf3\xda\xa6\x23\x25\xaf\x02\x1a\x68\xf7"
                "\x07\x51\x1a\x81\x40\x50\x6a\x1e\xa6\x83\x18\xe6\x2d\x40\x63"
                "\x5d\xad\x04\x3e\x19\x87\xeb\xc2\x6e\x5b\x5c\x44\x06\xf7\xbd"
                "\xf8\x5a\x73\x38\x8f\xbf\xe5\xc2\x45\xac\x49\xf4\x77\x0e\xbc"
                "\x78\x77\x08\x27\x0a\xa6\xa8\x76\x9f\xef\xe8\x93\x0f\xd0\xea"
                "\x1e\xe6\x4b\x31\x40\x7d\x76\x95\x09"s;
            auto const prefix0EncodedCondition =
                "\xa1\x2b\x80\x20\x45\x1f\xe1\x5f\x16\x29\x9d\x49\x59\x93\xfe"
                "\x69\x2d\xb9\x89\xe5\x6a\x52\x30\xa9\x04\x76\xf7\x73\x92\xa3"
                "\xcd\x32\x13\xc0\x73\x3f\x81\x03\x02\x04\x03\x82\x02\x03\x08"s;
            auto const prefix0EncodedFingerprint =
                "\x30\x33\x80\x03\x61\x61\x61\x81\x01\x00\xa2\x29\xa4\x27\x80"
                "\x20\x79\x92\x39\xab\xa8\xfc\x4f\xf7\xea\xbf\xbc\x4c\x44\xe6"
                "\x9e\x8b\xdf\xed\x99\x33\x24\xe1\x2e\xd6\x47\x92\xab\xe2\x89"
                "\xcf\x1d\x5f\x81\x03\x02\x00\x00"s;
            check(
                std::move(prefix0),
                prefix0Msg,
                std::move(prefix0EncodedFulfillment),
                prefix0EncodedCondition,
                prefix0EncodedFingerprint);
        }
    }

    void
    testPrefix2()
    {
        testcase("Prefix2");

        using namespace std::string_literals;
        using namespace ripple::cryptoconditions;

        // Fulfillment structure
        // * prefix0
        // ** prefix1
        // *** ed2

        auto const ed2Msg = ""s;
        std::array<std::uint8_t, 32> const ed2PublicKey{
            {0xd7, 0x5a, 0x98, 0x01, 0x82, 0xb1, 0x0a, 0xb7, 0xd5, 0x4b, 0xfe,
             0xd3, 0xc9, 0x64, 0x07, 0x3a, 0x0e, 0xe1, 0x72, 0xf3, 0xda, 0xa6,
             0x23, 0x25, 0xaf, 0x02, 0x1a, 0x68, 0xf7, 0x07, 0x51, 0x1a}};
        std::array<std::uint8_t, 64> const ed2Sig{
            {0xa4, 0x23, 0x60, 0xf4, 0x7f, 0x7d, 0xb8, 0x6d, 0xb5, 0xf0, 0x37,
             0xc8, 0x10, 0x24, 0x22, 0x37, 0x20, 0x7d, 0x7a, 0xdd, 0x6e, 0x3e,
             0x53, 0x17, 0xe2, 0x12, 0xb2, 0x07, 0xe2, 0x5b, 0xed, 0x2a, 0xcb,
             0x48, 0x5a, 0xd0, 0xbc, 0xbb, 0x57, 0x75, 0x57, 0x26, 0x0e, 0xcb,
             0xbb, 0xd6, 0x77, 0x18, 0xd6, 0xca, 0xba, 0xdf, 0x45, 0xba, 0xd6,
             0x55, 0xd1, 0xb8, 0xce, 0x84, 0x60, 0x9e, 0x97, 0x01}};
        auto const prefix1Prefix = "aaa"s;
        auto const prefix1Msg = ""s;
        auto const prefix1MaxMsgLength = 6;
        auto const prefix0Prefix = "bbb"s;
        auto const prefix0Msg = "\x7A\x7A\x7A"s;
        auto const prefix0MaxMsgLength = 3;

        auto ed2 = std::make_unique<Ed25519>(ed2PublicKey, ed2Sig);
        auto prefix1 = std::make_unique<PrefixSha256>(
            makeSlice(prefix1Prefix), prefix1MaxMsgLength, std::move(ed2));
        auto prefix0 = std::make_unique<PrefixSha256>(
            makeSlice(prefix0Prefix), prefix0MaxMsgLength, std::move(prefix1));
        {
            auto prefix0EncodedFulfillment =
                "\xa1\x7c\x80\x03\x62\x62\x62\x81\x01\x03\xa2\x72\xa1\x70\x80"
                "\x03\x61\x61\x61\x81\x01\x06\xa2\x66\xa4\x64\x80\x20\xd7\x5a"
                "\x98\x01\x82\xb1\x0a\xb7\xd5\x4b\xfe\xd3\xc9\x64\x07\x3a\x0e"
                "\xe1\x72\xf3\xda\xa6\x23\x25\xaf\x02\x1a\x68\xf7\x07\x51\x1a"
                "\x81\x40\xa4\x23\x60\xf4\x7f\x7d\xb8\x6d\xb5\xf0\x37\xc8\x10"
                "\x24\x22\x37\x20\x7d\x7a\xdd\x6e\x3e\x53\x17\xe2\x12\xb2\x07"
                "\xe2\x5b\xed\x2a\xcb\x48\x5a\xd0\xbc\xbb\x57\x75\x57\x26\x0e"
                "\xcb\xbb\xd6\x77\x18\xd6\xca\xba\xdf\x45\xba\xd6\x55\xd1\xb8"
                "\xce\x84\x60\x9e\x97\x01"s;
            auto const prefix0EncodedCondition =
                "\xa1\x2b\x80\x20\x17\x73\x50\xad\x85\x66\xc5\x28\xb9\x2d\x9b"
                "\x53\x82\xdf\x2c\x68\xd9\xba\x9f\x9f\xa4\x1d\x43\xdb\xdd\x8e"
                "\x40\xb1\x18\xdd\x96\x41\x81\x03\x02\x08\x0f\x82\x02\x03\x08"s;
            auto const prefix0EncodedFingerprint =
                "\x30\x37\x80\x03\x62\x62\x62\x81\x01\x03\xa2\x2d\xa1\x2b\x80"
                "\x20\x7f\x19\xc9\xbb\x3b\xc7\x67\xde\x39\x65\x7e\x11\xd1\x60"
                "\x68\xf8\xca\xb0\x0e\x3e\x3c\x23\x91\x6d\xf9\x67\xb5\x84\xa2"
                "\x8b\x26\xdc\x81\x03\x02\x04\x09\x82\x02\x03\x08"s;
            check(
                std::move(prefix0),
                prefix0Msg,
                std::move(prefix0EncodedFulfillment),
                prefix0EncodedCondition,
                prefix0EncodedFingerprint);
        }
    }

    void
    testThresh1()
    {
        testcase("Thresh1");

        using namespace std::string_literals;
        using namespace ripple::cryptoconditions;

        // Fulfillment structure
        // * thresh0
        // ** Rsa4Cond
        // ** prefix1
        // *** ed2
        // ** ed3

        auto const ed2Msg = ""s;
        std::array<std::uint8_t, 32> const ed2PublicKey{
            {0xd7, 0x5a, 0x98, 0x01, 0x82, 0xb1, 0x0a, 0xb7, 0xd5, 0x4b, 0xfe,
             0xd3, 0xc9, 0x64, 0x07, 0x3a, 0x0e, 0xe1, 0x72, 0xf3, 0xda, 0xa6,
             0x23, 0x25, 0xaf, 0x02, 0x1a, 0x68, 0xf7, 0x07, 0x51, 0x1a}};
        std::array<std::uint8_t, 64> const ed2Sig{
            {0x34, 0x4c, 0x9d, 0x12, 0xa0, 0x2a, 0x57, 0xb3, 0x5f, 0xd9, 0x66,
             0x19, 0x3c, 0xee, 0x95, 0xd5, 0xdb, 0xbb, 0xc6, 0x77, 0x55, 0x3f,
             0xce, 0xbf, 0x41, 0x4c, 0x18, 0xda, 0x75, 0x00, 0x29, 0xdd, 0x39,
             0xc6, 0xe5, 0x3a, 0xdd, 0x89, 0x2c, 0xef, 0xe4, 0x42, 0x35, 0x83,
             0x1f, 0xf8, 0xe5, 0xf3, 0x68, 0x68, 0x8e, 0xf7, 0x7c, 0xcf, 0x6f,
             0x99, 0x7f, 0xd4, 0x11, 0xa6, 0xa7, 0xeb, 0xf9, 0x02}};
        auto const prefix1Prefix = "aaa"s;
        auto const prefix1Msg = ""s;
        auto const prefix1MaxMsgLength = 0;
        auto const ed3Msg = ""s;
        std::array<std::uint8_t, 32> const ed3PublicKey{
            {0xd7, 0x5a, 0x98, 0x01, 0x82, 0xb1, 0x0a, 0xb7, 0xd5, 0x4b, 0xfe,
             0xd3, 0xc9, 0x64, 0x07, 0x3a, 0x0e, 0xe1, 0x72, 0xf3, 0xda, 0xa6,
             0x23, 0x25, 0xaf, 0x02, 0x1a, 0x68, 0xf7, 0x07, 0x51, 0x1a}};
        std::array<std::uint8_t, 64> const ed3Sig{
            {0x50, 0x6a, 0x1e, 0xa6, 0x83, 0x18, 0xe6, 0x2d, 0x40, 0x63, 0x5d,
             0xad, 0x04, 0x3e, 0x19, 0x87, 0xeb, 0xc2, 0x6e, 0x5b, 0x5c, 0x44,
             0x06, 0xf7, 0xbd, 0xf8, 0x5a, 0x73, 0x38, 0x8f, 0xbf, 0xe5, 0xc2,
             0x45, 0xac, 0x49, 0xf4, 0x77, 0x0e, 0xbc, 0x78, 0x77, 0x08, 0x27,
             0x0a, 0xa6, 0xa8, 0x76, 0x9f, 0xef, 0xe8, 0x93, 0x0f, 0xd0, 0xea,
             0x1e, 0xe6, 0x4b, 0x31, 0x40, 0x7d, 0x76, 0x95, 0x09}};
        auto const thresh0Msg = "\x61\x61\x61"s;
        std::array<std::uint8_t, 32> const Rsa4CondConditionFingerprint = {
            {0x4d, 0xd2, 0xea, 0x7f, 0x85, 0xb3, 0xea, 0xcb, 0x8f, 0x19, 0x05,
             0x8e, 0x83, 0x60, 0x95, 0x5c, 0x32, 0xe7, 0x4c, 0x12, 0x43, 0x92,
             0xa1, 0xf4, 0x46, 0x60, 0x73, 0x97, 0x09, 0xc5, 0x39, 0xc3}};
        Condition const Rsa4Cond{Type::rsaSha256,
                                 262144,
                                 Rsa4CondConditionFingerprint,
                                 std::bitset<5>{0}};

        auto ed2 = std::make_unique<Ed25519>(ed2PublicKey, ed2Sig);
        auto prefix1 = std::make_unique<PrefixSha256>(
            makeSlice(prefix1Prefix), prefix1MaxMsgLength, std::move(ed2));
        auto ed3 = std::make_unique<Ed25519>(ed3PublicKey, ed3Sig);
        std::vector<std::unique_ptr<Fulfillment>> thresh0Subfulfillments;
        thresh0Subfulfillments.emplace_back(std::move(prefix1));
        thresh0Subfulfillments.emplace_back(std::move(ed3));
        std::vector<Condition> thresh0Subconditions{{Rsa4Cond}};
        auto thresh0 = std::make_unique<ThresholdSha256>(
            std::move(thresh0Subfulfillments), std::move(thresh0Subconditions));
        {
            auto thresh0EncodedFulfillment =
                "\xa2\x82\x01\x06\xa0\x81\xd8\xa1\x70\x80\x03\x61\x61\x61\x81"
                "\x01\x00\xa2\x66\xa4\x64\x80\x20\xd7\x5a\x98\x01\x82\xb1\x0a"
                "\xb7\xd5\x4b\xfe\xd3\xc9\x64\x07\x3a\x0e\xe1\x72\xf3\xda\xa6"
                "\x23\x25\xaf\x02\x1a\x68\xf7\x07\x51\x1a\x81\x40\x34\x4c\x9d"
                "\x12\xa0\x2a\x57\xb3\x5f\xd9\x66\x19\x3c\xee\x95\xd5\xdb\xbb"
                "\xc6\x77\x55\x3f\xce\xbf\x41\x4c\x18\xda\x75\x00\x29\xdd\x39"
                "\xc6\xe5\x3a\xdd\x89\x2c\xef\xe4\x42\x35\x83\x1f\xf8\xe5\xf3"
                "\x68\x68\x8e\xf7\x7c\xcf\x6f\x99\x7f\xd4\x11\xa6\xa7\xeb\xf9"
                "\x02\xa4\x64\x80\x20\xd7\x5a\x98\x01\x82\xb1\x0a\xb7\xd5\x4b"
                "\xfe\xd3\xc9\x64\x07\x3a\x0e\xe1\x72\xf3\xda\xa6\x23\x25\xaf"
                "\x02\x1a\x68\xf7\x07\x51\x1a\x81\x40\x50\x6a\x1e\xa6\x83\x18"
                "\xe6\x2d\x40\x63\x5d\xad\x04\x3e\x19\x87\xeb\xc2\x6e\x5b\x5c"
                "\x44\x06\xf7\xbd\xf8\x5a\x73\x38\x8f\xbf\xe5\xc2\x45\xac\x49"
                "\xf4\x77\x0e\xbc\x78\x77\x08\x27\x0a\xa6\xa8\x76\x9f\xef\xe8"
                "\x93\x0f\xd0\xea\x1e\xe6\x4b\x31\x40\x7d\x76\x95\x09\xa1\x29"
                "\xa3\x27\x80\x20\x4d\xd2\xea\x7f\x85\xb3\xea\xcb\x8f\x19\x05"
                "\x8e\x83\x60\x95\x5c\x32\xe7\x4c\x12\x43\x92\xa1\xf4\x46\x60"
                "\x73\x97\x09\xc5\x39\xc3\x81\x03\x04\x00\x00"s;
            auto const thresh0EncodedCondition =
                "\xa2\x2b\x80\x20\xb6\xac\xf4\x08\x3e\x43\x8b\xe4\x35\x6f\x25"
                "\xff\x92\xc2\x95\xe9\xc8\xe1\xba\xb1\x41\xb4\x60\x7b\xa4\x85"
                "\x11\xeb\xa3\x5a\xef\xcc\x81\x03\x06\x10\x03\x82\x02\x03\x58"s;
            auto const thresh0EncodedFingerprint =
                "\x30\x81\x84\x80\x01\x02\xa1\x7f\xa1\x2b\x80\x20\x45\x1f\xe1"
                "\x5f\x16\x29\x9d\x49\x59\x93\xfe\x69\x2d\xb9\x89\xe5\x6a\x52"
                "\x30\xa9\x04\x76\xf7\x73\x92\xa3\xcd\x32\x13\xc0\x73\x3f\x81"
                "\x03\x02\x04\x03\x82\x02\x03\x08\xa3\x27\x80\x20\x4d\xd2\xea"
                "\x7f\x85\xb3\xea\xcb\x8f\x19\x05\x8e\x83\x60\x95\x5c\x32\xe7"
                "\x4c\x12\x43\x92\xa1\xf4\x46\x60\x73\x97\x09\xc5\x39\xc3\x81"
                "\x03\x04\x00\x00\xa4\x27\x80\x20\x79\x92\x39\xab\xa8\xfc\x4f"
                "\xf7\xea\xbf\xbc\x4c\x44\xe6\x9e\x8b\xdf\xed\x99\x33\x24\xe1"
                "\x2e\xd6\x47\x92\xab\xe2\x89\xcf\x1d\x5f\x81\x03\x02\x00\x00"s;
            check(
                std::move(thresh0),
                thresh0Msg,
                std::move(thresh0EncodedFulfillment),
                thresh0EncodedCondition,
                thresh0EncodedFingerprint);
        }
    }

    void
    testThresh2()
    {
        testcase("Thresh2");

        using namespace std::string_literals;
        using namespace ripple::cryptoconditions;

        // Fulfillment structure
        // * thresh0
        // ** Prefix2Cond
        // ** Rsa4Cond
        // ** Prefix5Cond
        // ** Rsa7Cond
        // ** preim1

        auto const preim1Preimage = "aaa"s;
        auto const preim1Msg = ""s;
        auto const thresh0Msg = ""s;
        std::array<std::uint8_t, 32> const Prefix2CondConditionFingerprint = {
            {0x45, 0x1f, 0xe1, 0x5f, 0x16, 0x29, 0x9d, 0x49, 0x59, 0x93, 0xfe,
             0x69, 0x2d, 0xb9, 0x89, 0xe5, 0x6a, 0x52, 0x30, 0xa9, 0x04, 0x76,
             0xf7, 0x73, 0x92, 0xa3, 0xcd, 0x32, 0x13, 0xc0, 0x73, 0x3f}};
        Condition const Prefix2Cond{Type::prefixSha256,
                                    132099,
                                    Prefix2CondConditionFingerprint,
                                    std::bitset<5>{16}};
        std::array<std::uint8_t, 32> const Rsa4CondConditionFingerprint = {
            {0x4d, 0xd2, 0xea, 0x7f, 0x85, 0xb3, 0xea, 0xcb, 0x8f, 0x19, 0x05,
             0x8e, 0x83, 0x60, 0x95, 0x5c, 0x32, 0xe7, 0x4c, 0x12, 0x43, 0x92,
             0xa1, 0xf4, 0x46, 0x60, 0x73, 0x97, 0x09, 0xc5, 0x39, 0xc3}};
        Condition const Rsa4Cond{Type::rsaSha256,
                                 262144,
                                 Rsa4CondConditionFingerprint,
                                 std::bitset<5>{0}};
        std::array<std::uint8_t, 32> const Prefix5CondConditionFingerprint = {
            {0x45, 0x1f, 0xe1, 0x5f, 0x16, 0x29, 0x9d, 0x49, 0x59, 0x93, 0xfe,
             0x69, 0x2d, 0xb9, 0x89, 0xe5, 0x6a, 0x52, 0x30, 0xa9, 0x04, 0x76,
             0xf7, 0x73, 0x92, 0xa3, 0xcd, 0x32, 0x13, 0xc0, 0x73, 0x3f}};
        Condition const Prefix5Cond{Type::prefixSha256,
                                    132099,
                                    Prefix5CondConditionFingerprint,
                                    std::bitset<5>{16}};
        std::array<std::uint8_t, 32> const Rsa7CondConditionFingerprint = {
            {0x4d, 0xd2, 0xea, 0x7f, 0x85, 0xb3, 0xea, 0xcb, 0x8f, 0x19, 0x05,
             0x8e, 0x83, 0x60, 0x95, 0x5c, 0x32, 0xe7, 0x4c, 0x12, 0x43, 0x92,
             0xa1, 0xf4, 0x46, 0x60, 0x73, 0x97, 0x09, 0xc5, 0x39, 0xc3}};
        Condition const Rsa7Cond{Type::rsaSha256,
                                 262144,
                                 Rsa7CondConditionFingerprint,
                                 std::bitset<5>{0}};

        auto preim1 =
            std::make_unique<PreimageSha256>(makeSlice(preim1Preimage));
        std::vector<std::unique_ptr<Fulfillment>> thresh0Subfulfillments;
        thresh0Subfulfillments.emplace_back(std::move(preim1));
        std::vector<Condition> thresh0Subconditions{
            {Prefix2Cond, Rsa4Cond, Prefix5Cond, Rsa7Cond}};
        auto thresh0 = std::make_unique<ThresholdSha256>(
            std::move(thresh0Subfulfillments), std::move(thresh0Subconditions));
        {
            auto thresh0EncodedFulfillment =
                "\xa2\x81\xb8\xa0\x07\xa0\x05\x80\x03\x61\x61\x61\xa1\x81\xac"
                "\xa1\x2b\x80\x20\x45\x1f\xe1\x5f\x16\x29\x9d\x49\x59\x93\xfe"
                "\x69\x2d\xb9\x89\xe5\x6a\x52\x30\xa9\x04\x76\xf7\x73\x92\xa3"
                "\xcd\x32\x13\xc0\x73\x3f\x81\x03\x02\x04\x03\x82\x02\x03\x08"
                "\xa1\x2b\x80\x20\x45\x1f\xe1\x5f\x16\x29\x9d\x49\x59\x93\xfe"
                "\x69\x2d\xb9\x89\xe5\x6a\x52\x30\xa9\x04\x76\xf7\x73\x92\xa3"
                "\xcd\x32\x13\xc0\x73\x3f\x81\x03\x02\x04\x03\x82\x02\x03\x08"
                "\xa3\x27\x80\x20\x4d\xd2\xea\x7f\x85\xb3\xea\xcb\x8f\x19\x05"
                "\x8e\x83\x60\x95\x5c\x32\xe7\x4c\x12\x43\x92\xa1\xf4\x46\x60"
                "\x73\x97\x09\xc5\x39\xc3\x81\x03\x04\x00\x00\xa3\x27\x80\x20"
                "\x4d\xd2\xea\x7f\x85\xb3\xea\xcb\x8f\x19\x05\x8e\x83\x60\x95"
                "\x5c\x32\xe7\x4c\x12\x43\x92\xa1\xf4\x46\x60\x73\x97\x09\xc5"
                "\x39\xc3\x81\x03\x04\x00\x00"s;
            auto const thresh0EncodedCondition =
                "\xa2\x2b\x80\x20\x9a\x0b\x2c\x63\xdf\x80\x68\x6e\x60\x20\xd0"
                "\xca\x21\xcb\xfe\x66\x8c\xce\xc3\xd1\xaf\x82\x71\x3f\xea\xe9"
                "\xb8\xdd\x4a\x0f\x9b\xb7\x81\x03\x04\x14\x00\x82\x02\x03\xd8"s;
            auto const thresh0EncodedFingerprint =
                "\x30\x81\xd9\x80\x01\x01\xa1\x81\xd3\xa0\x25\x80\x20\x98\x34"
                "\x87\x6d\xcf\xb0\x5c\xb1\x67\xa5\xc2\x49\x53\xeb\xa5\x8c\x4a"
                "\xc8\x9b\x1a\xdf\x57\xf2\x8f\x2f\x9d\x09\xaf\x10\x7e\xe8\xf0"
                "\x81\x01\x03\xa1\x2b\x80\x20\x45\x1f\xe1\x5f\x16\x29\x9d\x49"
                "\x59\x93\xfe\x69\x2d\xb9\x89\xe5\x6a\x52\x30\xa9\x04\x76\xf7"
                "\x73\x92\xa3\xcd\x32\x13\xc0\x73\x3f\x81\x03\x02\x04\x03\x82"
                "\x02\x03\x08\xa1\x2b\x80\x20\x45\x1f\xe1\x5f\x16\x29\x9d\x49"
                "\x59\x93\xfe\x69\x2d\xb9\x89\xe5\x6a\x52\x30\xa9\x04\x76\xf7"
                "\x73\x92\xa3\xcd\x32\x13\xc0\x73\x3f\x81\x03\x02\x04\x03\x82"
                "\x02\x03\x08\xa3\x27\x80\x20\x4d\xd2\xea\x7f\x85\xb3\xea\xcb"
                "\x8f\x19\x05\x8e\x83\x60\x95\x5c\x32\xe7\x4c\x12\x43\x92\xa1"
                "\xf4\x46\x60\x73\x97\x09\xc5\x39\xc3\x81\x03\x04\x00\x00\xa3"
                "\x27\x80\x20\x4d\xd2\xea\x7f\x85\xb3\xea\xcb\x8f\x19\x05\x8e"
                "\x83\x60\x95\x5c\x32\xe7\x4c\x12\x43\x92\xa1\xf4\x46\x60\x73"
                "\x97\x09\xc5\x39\xc3\x81\x03\x04\x00\x00"s;
            check(
                std::move(thresh0),
                thresh0Msg,
                std::move(thresh0EncodedFulfillment),
                thresh0EncodedCondition,
                thresh0EncodedFingerprint);
        }
    }

    void
    testThresh3()
    {
        testcase("Thresh3");

        using namespace std::string_literals;
        using namespace ripple::cryptoconditions;

        // Fulfillment structure
        // * thresh0
        // ** prefix1
        // *** ed2
        // ** ed3
        // ** prefix4
        // *** ed5
        // ** ed6

        auto const ed2Msg = ""s;
        std::array<std::uint8_t, 32> const ed2PublicKey{
            {0xd7, 0x5a, 0x98, 0x01, 0x82, 0xb1, 0x0a, 0xb7, 0xd5, 0x4b, 0xfe,
             0xd3, 0xc9, 0x64, 0x07, 0x3a, 0x0e, 0xe1, 0x72, 0xf3, 0xda, 0xa6,
             0x23, 0x25, 0xaf, 0x02, 0x1a, 0x68, 0xf7, 0x07, 0x51, 0x1a}};
        std::array<std::uint8_t, 64> const ed2Sig{
            {0x50, 0x6a, 0x1e, 0xa6, 0x83, 0x18, 0xe6, 0x2d, 0x40, 0x63, 0x5d,
             0xad, 0x04, 0x3e, 0x19, 0x87, 0xeb, 0xc2, 0x6e, 0x5b, 0x5c, 0x44,
             0x06, 0xf7, 0xbd, 0xf8, 0x5a, 0x73, 0x38, 0x8f, 0xbf, 0xe5, 0xc2,
             0x45, 0xac, 0x49, 0xf4, 0x77, 0x0e, 0xbc, 0x78, 0x77, 0x08, 0x27,
             0x0a, 0xa6, 0xa8, 0x76, 0x9f, 0xef, 0xe8, 0x93, 0x0f, 0xd0, 0xea,
             0x1e, 0xe6, 0x4b, 0x31, 0x40, 0x7d, 0x76, 0x95, 0x09}};
        auto const prefix1Prefix = "aaa"s;
        auto const prefix1Msg = ""s;
        auto const prefix1MaxMsgLength = 0;
        auto const ed3Msg = ""s;
        std::array<std::uint8_t, 32> const ed3PublicKey{
            {0xd7, 0x5a, 0x98, 0x01, 0x82, 0xb1, 0x0a, 0xb7, 0xd5, 0x4b, 0xfe,
             0xd3, 0xc9, 0x64, 0x07, 0x3a, 0x0e, 0xe1, 0x72, 0xf3, 0xda, 0xa6,
             0x23, 0x25, 0xaf, 0x02, 0x1a, 0x68, 0xf7, 0x07, 0x51, 0x1a}};
        std::array<std::uint8_t, 64> const ed3Sig{
            {0xe5, 0x56, 0x43, 0x00, 0xc3, 0x60, 0xac, 0x72, 0x90, 0x86, 0xe2,
             0xcc, 0x80, 0x6e, 0x82, 0x8a, 0x84, 0x87, 0x7f, 0x1e, 0xb8, 0xe5,
             0xd9, 0x74, 0xd8, 0x73, 0xe0, 0x65, 0x22, 0x49, 0x01, 0x55, 0x5f,
             0xb8, 0x82, 0x15, 0x90, 0xa3, 0x3b, 0xac, 0xc6, 0x1e, 0x39, 0x70,
             0x1c, 0xf9, 0xb4, 0x6b, 0xd2, 0x5b, 0xf5, 0xf0, 0x59, 0x5b, 0xbe,
             0x24, 0x65, 0x51, 0x41, 0x43, 0x8e, 0x7a, 0x10, 0x0b}};
        auto const ed5Msg = ""s;
        std::array<std::uint8_t, 32> const ed5PublicKey{
            {0xd7, 0x5a, 0x98, 0x01, 0x82, 0xb1, 0x0a, 0xb7, 0xd5, 0x4b, 0xfe,
             0xd3, 0xc9, 0x64, 0x07, 0x3a, 0x0e, 0xe1, 0x72, 0xf3, 0xda, 0xa6,
             0x23, 0x25, 0xaf, 0x02, 0x1a, 0x68, 0xf7, 0x07, 0x51, 0x1a}};
        std::array<std::uint8_t, 64> const ed5Sig{
            {0x50, 0x6a, 0x1e, 0xa6, 0x83, 0x18, 0xe6, 0x2d, 0x40, 0x63, 0x5d,
             0xad, 0x04, 0x3e, 0x19, 0x87, 0xeb, 0xc2, 0x6e, 0x5b, 0x5c, 0x44,
             0x06, 0xf7, 0xbd, 0xf8, 0x5a, 0x73, 0x38, 0x8f, 0xbf, 0xe5, 0xc2,
             0x45, 0xac, 0x49, 0xf4, 0x77, 0x0e, 0xbc, 0x78, 0x77, 0x08, 0x27,
             0x0a, 0xa6, 0xa8, 0x76, 0x9f, 0xef, 0xe8, 0x93, 0x0f, 0xd0, 0xea,
             0x1e, 0xe6, 0x4b, 0x31, 0x40, 0x7d, 0x76, 0x95, 0x09}};
        auto const prefix4Prefix = "aaa"s;
        auto const prefix4Msg = ""s;
        auto const prefix4MaxMsgLength = 0;
        auto const ed6Msg = ""s;
        std::array<std::uint8_t, 32> const ed6PublicKey{
            {0xd7, 0x5a, 0x98, 0x01, 0x82, 0xb1, 0x0a, 0xb7, 0xd5, 0x4b, 0xfe,
             0xd3, 0xc9, 0x64, 0x07, 0x3a, 0x0e, 0xe1, 0x72, 0xf3, 0xda, 0xa6,
             0x23, 0x25, 0xaf, 0x02, 0x1a, 0x68, 0xf7, 0x07, 0x51, 0x1a}};
        std::array<std::uint8_t, 64> const ed6Sig{
            {0xe5, 0x56, 0x43, 0x00, 0xc3, 0x60, 0xac, 0x72, 0x90, 0x86, 0xe2,
             0xcc, 0x80, 0x6e, 0x82, 0x8a, 0x84, 0x87, 0x7f, 0x1e, 0xb8, 0xe5,
             0xd9, 0x74, 0xd8, 0x73, 0xe0, 0x65, 0x22, 0x49, 0x01, 0x55, 0x5f,
             0xb8, 0x82, 0x15, 0x90, 0xa3, 0x3b, 0xac, 0xc6, 0x1e, 0x39, 0x70,
             0x1c, 0xf9, 0xb4, 0x6b, 0xd2, 0x5b, 0xf5, 0xf0, 0x59, 0x5b, 0xbe,
             0x24, 0x65, 0x51, 0x41, 0x43, 0x8e, 0x7a, 0x10, 0x0b}};
        auto const thresh0Msg = ""s;

        auto ed2 = std::make_unique<Ed25519>(ed2PublicKey, ed2Sig);
        auto prefix1 = std::make_unique<PrefixSha256>(
            makeSlice(prefix1Prefix), prefix1MaxMsgLength, std::move(ed2));
        auto ed3 = std::make_unique<Ed25519>(ed3PublicKey, ed3Sig);
        auto ed5 = std::make_unique<Ed25519>(ed5PublicKey, ed5Sig);
        auto prefix4 = std::make_unique<PrefixSha256>(
            makeSlice(prefix4Prefix), prefix4MaxMsgLength, std::move(ed5));
        auto ed6 = std::make_unique<Ed25519>(ed6PublicKey, ed6Sig);
        std::vector<std::unique_ptr<Fulfillment>> thresh0Subfulfillments;
        thresh0Subfulfillments.emplace_back(std::move(prefix1));
        thresh0Subfulfillments.emplace_back(std::move(ed3));
        thresh0Subfulfillments.emplace_back(std::move(prefix4));
        thresh0Subfulfillments.emplace_back(std::move(ed6));
        std::vector<Condition> thresh0Subconditions{};
        auto thresh0 = std::make_unique<ThresholdSha256>(
            std::move(thresh0Subfulfillments), std::move(thresh0Subconditions));
        {
            auto thresh0EncodedFulfillment =
                "\xa2\x82\x01\xb6\xa0\x82\x01\xb0\xa1\x70\x80\x03\x61\x61\x61"
                "\x81\x01\x00\xa2\x66\xa4\x64\x80\x20\xd7\x5a\x98\x01\x82\xb1"
                "\x0a\xb7\xd5\x4b\xfe\xd3\xc9\x64\x07\x3a\x0e\xe1\x72\xf3\xda"
                "\xa6\x23\x25\xaf\x02\x1a\x68\xf7\x07\x51\x1a\x81\x40\x50\x6a"
                "\x1e\xa6\x83\x18\xe6\x2d\x40\x63\x5d\xad\x04\x3e\x19\x87\xeb"
                "\xc2\x6e\x5b\x5c\x44\x06\xf7\xbd\xf8\x5a\x73\x38\x8f\xbf\xe5"
                "\xc2\x45\xac\x49\xf4\x77\x0e\xbc\x78\x77\x08\x27\x0a\xa6\xa8"
                "\x76\x9f\xef\xe8\x93\x0f\xd0\xea\x1e\xe6\x4b\x31\x40\x7d\x76"
                "\x95\x09\xa1\x70\x80\x03\x61\x61\x61\x81\x01\x00\xa2\x66\xa4"
                "\x64\x80\x20\xd7\x5a\x98\x01\x82\xb1\x0a\xb7\xd5\x4b\xfe\xd3"
                "\xc9\x64\x07\x3a\x0e\xe1\x72\xf3\xda\xa6\x23\x25\xaf\x02\x1a"
                "\x68\xf7\x07\x51\x1a\x81\x40\x50\x6a\x1e\xa6\x83\x18\xe6\x2d"
                "\x40\x63\x5d\xad\x04\x3e\x19\x87\xeb\xc2\x6e\x5b\x5c\x44\x06"
                "\xf7\xbd\xf8\x5a\x73\x38\x8f\xbf\xe5\xc2\x45\xac\x49\xf4\x77"
                "\x0e\xbc\x78\x77\x08\x27\x0a\xa6\xa8\x76\x9f\xef\xe8\x93\x0f"
                "\xd0\xea\x1e\xe6\x4b\x31\x40\x7d\x76\x95\x09\xa4\x64\x80\x20"
                "\xd7\x5a\x98\x01\x82\xb1\x0a\xb7\xd5\x4b\xfe\xd3\xc9\x64\x07"
                "\x3a\x0e\xe1\x72\xf3\xda\xa6\x23\x25\xaf\x02\x1a\x68\xf7\x07"
                "\x51\x1a\x81\x40\xe5\x56\x43\x00\xc3\x60\xac\x72\x90\x86\xe2"
                "\xcc\x80\x6e\x82\x8a\x84\x87\x7f\x1e\xb8\xe5\xd9\x74\xd8\x73"
                "\xe0\x65\x22\x49\x01\x55\x5f\xb8\x82\x15\x90\xa3\x3b\xac\xc6"
                "\x1e\x39\x70\x1c\xf9\xb4\x6b\xd2\x5b\xf5\xf0\x59\x5b\xbe\x24"
                "\x65\x51\x41\x43\x8e\x7a\x10\x0b\xa4\x64\x80\x20\xd7\x5a\x98"
                "\x01\x82\xb1\x0a\xb7\xd5\x4b\xfe\xd3\xc9\x64\x07\x3a\x0e\xe1"
                "\x72\xf3\xda\xa6\x23\x25\xaf\x02\x1a\x68\xf7\x07\x51\x1a\x81"
                "\x40\xe5\x56\x43\x00\xc3\x60\xac\x72\x90\x86\xe2\xcc\x80\x6e"
                "\x82\x8a\x84\x87\x7f\x1e\xb8\xe5\xd9\x74\xd8\x73\xe0\x65\x22"
                "\x49\x01\x55\x5f\xb8\x82\x15\x90\xa3\x3b\xac\xc6\x1e\x39\x70"
                "\x1c\xf9\xb4\x6b\xd2\x5b\xf5\xf0\x59\x5b\xbe\x24\x65\x51\x41"
                "\x43\x8e\x7a\x10\x0b\xa1\x00"s;
            auto const thresh0EncodedCondition =
                "\xa2\x2b\x80\x20\x8e\x43\x3e\xf5\xd3\xea\xa0\x0a\x2b\x34\xa0"
                "\x5c\xa7\xc2\x2d\xd3\x92\x97\x3a\x19\xf1\xa2\x43\x26\x8c\xb5"
                "\x31\x11\xbd\xf1\xc8\x44\x81\x03\x08\x18\x06\x82\x02\x03\x48"s;
            auto const thresh0EncodedFingerprint =
                "\x30\x81\xb2\x80\x01\x04\xa1\x81\xac\xa1\x2b\x80\x20\x45\x1f"
                "\xe1\x5f\x16\x29\x9d\x49\x59\x93\xfe\x69\x2d\xb9\x89\xe5\x6a"
                "\x52\x30\xa9\x04\x76\xf7\x73\x92\xa3\xcd\x32\x13\xc0\x73\x3f"
                "\x81\x03\x02\x04\x03\x82\x02\x03\x08\xa1\x2b\x80\x20\x45\x1f"
                "\xe1\x5f\x16\x29\x9d\x49\x59\x93\xfe\x69\x2d\xb9\x89\xe5\x6a"
                "\x52\x30\xa9\x04\x76\xf7\x73\x92\xa3\xcd\x32\x13\xc0\x73\x3f"
                "\x81\x03\x02\x04\x03\x82\x02\x03\x08\xa4\x27\x80\x20\x79\x92"
                "\x39\xab\xa8\xfc\x4f\xf7\xea\xbf\xbc\x4c\x44\xe6\x9e\x8b\xdf"
                "\xed\x99\x33\x24\xe1\x2e\xd6\x47\x92\xab\xe2\x89\xcf\x1d\x5f"
                "\x81\x03\x02\x00\x00\xa4\x27\x80\x20\x79\x92\x39\xab\xa8\xfc"
                "\x4f\xf7\xea\xbf\xbc\x4c\x44\xe6\x9e\x8b\xdf\xed\x99\x33\x24"
                "\xe1\x2e\xd6\x47\x92\xab\xe2\x89\xcf\x1d\x5f\x81\x03\x02\x00"
                "\x00"s;
            check(
                std::move(thresh0),
                thresh0Msg,
                std::move(thresh0EncodedFulfillment),
                thresh0EncodedCondition,
                thresh0EncodedFingerprint);
        }
    }

    void
    testThresh4()
    {
        testcase("Thresh4");

        using namespace std::string_literals;
        using namespace ripple::cryptoconditions;

        // Fulfillment structure
        // * thresh0
        // ** thresh1
        // *** Rsa5Cond
        // *** prefix2
        // **** ed3
        // *** ed4
        // ** preim6

        auto const ed3Msg = ""s;
        std::array<std::uint8_t, 32> const ed3PublicKey{
            {0xd7, 0x5a, 0x98, 0x01, 0x82, 0xb1, 0x0a, 0xb7, 0xd5, 0x4b, 0xfe,
             0xd3, 0xc9, 0x64, 0x07, 0x3a, 0x0e, 0xe1, 0x72, 0xf3, 0xda, 0xa6,
             0x23, 0x25, 0xaf, 0x02, 0x1a, 0x68, 0xf7, 0x07, 0x51, 0x1a}};
        std::array<std::uint8_t, 64> const ed3Sig{
            {0x50, 0x6a, 0x1e, 0xa6, 0x83, 0x18, 0xe6, 0x2d, 0x40, 0x63, 0x5d,
             0xad, 0x04, 0x3e, 0x19, 0x87, 0xeb, 0xc2, 0x6e, 0x5b, 0x5c, 0x44,
             0x06, 0xf7, 0xbd, 0xf8, 0x5a, 0x73, 0x38, 0x8f, 0xbf, 0xe5, 0xc2,
             0x45, 0xac, 0x49, 0xf4, 0x77, 0x0e, 0xbc, 0x78, 0x77, 0x08, 0x27,
             0x0a, 0xa6, 0xa8, 0x76, 0x9f, 0xef, 0xe8, 0x93, 0x0f, 0xd0, 0xea,
             0x1e, 0xe6, 0x4b, 0x31, 0x40, 0x7d, 0x76, 0x95, 0x09}};
        auto const prefix2Prefix = "aaa"s;
        auto const prefix2Msg = ""s;
        auto const prefix2MaxMsgLength = 0;
        auto const ed4Msg = ""s;
        std::array<std::uint8_t, 32> const ed4PublicKey{
            {0xd7, 0x5a, 0x98, 0x01, 0x82, 0xb1, 0x0a, 0xb7, 0xd5, 0x4b, 0xfe,
             0xd3, 0xc9, 0x64, 0x07, 0x3a, 0x0e, 0xe1, 0x72, 0xf3, 0xda, 0xa6,
             0x23, 0x25, 0xaf, 0x02, 0x1a, 0x68, 0xf7, 0x07, 0x51, 0x1a}};
        std::array<std::uint8_t, 64> const ed4Sig{
            {0xe5, 0x56, 0x43, 0x00, 0xc3, 0x60, 0xac, 0x72, 0x90, 0x86, 0xe2,
             0xcc, 0x80, 0x6e, 0x82, 0x8a, 0x84, 0x87, 0x7f, 0x1e, 0xb8, 0xe5,
             0xd9, 0x74, 0xd8, 0x73, 0xe0, 0x65, 0x22, 0x49, 0x01, 0x55, 0x5f,
             0xb8, 0x82, 0x15, 0x90, 0xa3, 0x3b, 0xac, 0xc6, 0x1e, 0x39, 0x70,
             0x1c, 0xf9, 0xb4, 0x6b, 0xd2, 0x5b, 0xf5, 0xf0, 0x59, 0x5b, 0xbe,
             0x24, 0x65, 0x51, 0x41, 0x43, 0x8e, 0x7a, 0x10, 0x0b}};
        auto const thresh1Msg = ""s;
        std::array<std::uint8_t, 32> const Rsa5CondConditionFingerprint = {
            {0x4d, 0xd2, 0xea, 0x7f, 0x85, 0xb3, 0xea, 0xcb, 0x8f, 0x19, 0x05,
             0x8e, 0x83, 0x60, 0x95, 0x5c, 0x32, 0xe7, 0x4c, 0x12, 0x43, 0x92,
             0xa1, 0xf4, 0x46, 0x60, 0x73, 0x97, 0x09, 0xc5, 0x39, 0xc3}};
        Condition const Rsa5Cond{Type::rsaSha256,
                                 262144,
                                 Rsa5CondConditionFingerprint,
                                 std::bitset<5>{0}};
        auto const preim6Preimage = "aaa"s;
        auto const preim6Msg = ""s;
        auto const thresh0Msg = ""s;

        auto ed3 = std::make_unique<Ed25519>(ed3PublicKey, ed3Sig);
        auto prefix2 = std::make_unique<PrefixSha256>(
            makeSlice(prefix2Prefix), prefix2MaxMsgLength, std::move(ed3));
        auto ed4 = std::make_unique<Ed25519>(ed4PublicKey, ed4Sig);
        std::vector<std::unique_ptr<Fulfillment>> thresh1Subfulfillments;
        thresh1Subfulfillments.emplace_back(std::move(prefix2));
        thresh1Subfulfillments.emplace_back(std::move(ed4));
        std::vector<Condition> thresh1Subconditions{{Rsa5Cond}};
        auto thresh1 = std::make_unique<ThresholdSha256>(
            std::move(thresh1Subfulfillments), std::move(thresh1Subconditions));
        auto preim6 =
            std::make_unique<PreimageSha256>(makeSlice(preim6Preimage));
        std::vector<std::unique_ptr<Fulfillment>> thresh0Subfulfillments;
        thresh0Subfulfillments.emplace_back(std::move(thresh1));
        thresh0Subfulfillments.emplace_back(std::move(preim6));
        std::vector<Condition> thresh0Subconditions{};
        auto thresh0 = std::make_unique<ThresholdSha256>(
            std::move(thresh0Subfulfillments), std::move(thresh0Subconditions));
        {
            auto thresh0EncodedFulfillment =
                "\xa2\x82\x01\x17\xa0\x82\x01\x11\xa0\x05\x80\x03\x61\x61\x61"
                "\xa2\x82\x01\x06\xa0\x81\xd8\xa1\x70\x80\x03\x61\x61\x61\x81"
                "\x01\x00\xa2\x66\xa4\x64\x80\x20\xd7\x5a\x98\x01\x82\xb1\x0a"
                "\xb7\xd5\x4b\xfe\xd3\xc9\x64\x07\x3a\x0e\xe1\x72\xf3\xda\xa6"
                "\x23\x25\xaf\x02\x1a\x68\xf7\x07\x51\x1a\x81\x40\x50\x6a\x1e"
                "\xa6\x83\x18\xe6\x2d\x40\x63\x5d\xad\x04\x3e\x19\x87\xeb\xc2"
                "\x6e\x5b\x5c\x44\x06\xf7\xbd\xf8\x5a\x73\x38\x8f\xbf\xe5\xc2"
                "\x45\xac\x49\xf4\x77\x0e\xbc\x78\x77\x08\x27\x0a\xa6\xa8\x76"
                "\x9f\xef\xe8\x93\x0f\xd0\xea\x1e\xe6\x4b\x31\x40\x7d\x76\x95"
                "\x09\xa4\x64\x80\x20\xd7\x5a\x98\x01\x82\xb1\x0a\xb7\xd5\x4b"
                "\xfe\xd3\xc9\x64\x07\x3a\x0e\xe1\x72\xf3\xda\xa6\x23\x25\xaf"
                "\x02\x1a\x68\xf7\x07\x51\x1a\x81\x40\xe5\x56\x43\x00\xc3\x60"
                "\xac\x72\x90\x86\xe2\xcc\x80\x6e\x82\x8a\x84\x87\x7f\x1e\xb8"
                "\xe5\xd9\x74\xd8\x73\xe0\x65\x22\x49\x01\x55\x5f\xb8\x82\x15"
                "\x90\xa3\x3b\xac\xc6\x1e\x39\x70\x1c\xf9\xb4\x6b\xd2\x5b\xf5"
                "\xf0\x59\x5b\xbe\x24\x65\x51\x41\x43\x8e\x7a\x10\x0b\xa1\x29"
                "\xa3\x27\x80\x20\x4d\xd2\xea\x7f\x85\xb3\xea\xcb\x8f\x19\x05"
                "\x8e\x83\x60\x95\x5c\x32\xe7\x4c\x12\x43\x92\xa1\xf4\x46\x60"
                "\x73\x97\x09\xc5\x39\xc3\x81\x03\x04\x00\x00\xa1\x00"s;
            auto const thresh0EncodedCondition =
                "\xa2\x2b\x80\x20\x0c\x99\x63\x0a\x20\x1a\x99\xb0\x74\x8d\x2b"
                "\xad\xb2\x05\xe5\xca\x93\x96\x92\xc6\x87\xd1\xc4\xa6\x97\xe3"
                "\x9b\xa8\xba\x1e\xbe\x71\x81\x03\x06\x18\x06\x82\x02\x03\xd8"s;
            auto const thresh0EncodedFingerprint =
                "\x30\x59\x80\x01\x02\xa1\x54\xa0\x25\x80\x20\x98\x34\x87\x6d"
                "\xcf\xb0\x5c\xb1\x67\xa5\xc2\x49\x53\xeb\xa5\x8c\x4a\xc8\x9b"
                "\x1a\xdf\x57\xf2\x8f\x2f\x9d\x09\xaf\x10\x7e\xe8\xf0\x81\x01"
                "\x03\xa2\x2b\x80\x20\xb6\xac\xf4\x08\x3e\x43\x8b\xe4\x35\x6f"
                "\x25\xff\x92\xc2\x95\xe9\xc8\xe1\xba\xb1\x41\xb4\x60\x7b\xa4"
                "\x85\x11\xeb\xa3\x5a\xef\xcc\x81\x03\x06\x10\x03\x82\x02\x03"
                "\x58"s;
            check(
                std::move(thresh0),
                thresh0Msg,
                std::move(thresh0EncodedFulfillment),
                thresh0EncodedCondition,
                thresh0EncodedFingerprint);
        }
    }

    void
    testThresh5()
    {
        testcase("Thresh5");

        using namespace std::string_literals;
        using namespace ripple::cryptoconditions;

        // Fulfillment structure
        // * thresh0
        // ** Preim2Cond
        // ** preim1

        auto const preim1Preimage = "aaa"s;
        auto const preim1Msg = ""s;
        auto const thresh0Msg = ""s;
        std::array<std::uint8_t, 32> const Preim2CondConditionFingerprint = {
            {0x98, 0x34, 0x87, 0x6d, 0xcf, 0xb0, 0x5c, 0xb1, 0x67, 0xa5, 0xc2,
             0x49, 0x53, 0xeb, 0xa5, 0x8c, 0x4a, 0xc8, 0x9b, 0x1a, 0xdf, 0x57,
             0xf2, 0x8f, 0x2f, 0x9d, 0x09, 0xaf, 0x10, 0x7e, 0xe8, 0xf0}};
        Condition const Preim2Cond{Type::preimageSha256,
                                   3,
                                   Preim2CondConditionFingerprint,
                                   std::bitset<5>{0}};

        auto preim1 =
            std::make_unique<PreimageSha256>(makeSlice(preim1Preimage));
        std::vector<std::unique_ptr<Fulfillment>> thresh0Subfulfillments;
        thresh0Subfulfillments.emplace_back(std::move(preim1));
        std::vector<Condition> thresh0Subconditions{{Preim2Cond}};
        auto thresh0 = std::make_unique<ThresholdSha256>(
            std::move(thresh0Subfulfillments), std::move(thresh0Subconditions));
        {
            auto thresh0EncodedFulfillment =
                "\xa2\x32\xa0\x07\xa0\x05\x80\x03\x61\x61\x61\xa1\x27\xa0\x25"
                "\x80\x20\x98\x34\x87\x6d\xcf\xb0\x5c\xb1\x67\xa5\xc2\x49\x53"
                "\xeb\xa5\x8c\x4a\xc8\x9b\x1a\xdf\x57\xf2\x8f\x2f\x9d\x09\xaf"
                "\x10\x7e\xe8\xf0\x81\x01\x03"s;
            auto const thresh0EncodedCondition =
                "\xa2\x2a\x80\x20\xe4\xfd\xb4\x65\x2c\x6f\x17\xa3\x8b\x2a\xbe"
                "\x9a\xa0\x06\x40\xb1\xe1\x84\xfe\x7a\x8d\x0c\x97\x1b\x5d\x24"
                "\xf7\xed\xa6\xfc\x68\xbf\x81\x02\x08\x03\x82\x02\x07\x80"s;
            auto const thresh0EncodedFingerprint =
                "\x30\x53\x80\x01\x01\xa1\x4e\xa0\x25\x80\x20\x98\x34\x87\x6d"
                "\xcf\xb0\x5c\xb1\x67\xa5\xc2\x49\x53\xeb\xa5\x8c\x4a\xc8\x9b"
                "\x1a\xdf\x57\xf2\x8f\x2f\x9d\x09\xaf\x10\x7e\xe8\xf0\x81\x01"
                "\x03\xa0\x25\x80\x20\x98\x34\x87\x6d\xcf\xb0\x5c\xb1\x67\xa5"
                "\xc2\x49\x53\xeb\xa5\x8c\x4a\xc8\x9b\x1a\xdf\x57\xf2\x8f\x2f"
                "\x9d\x09\xaf\x10\x7e\xe8\xf0\x81\x01\x03"s;
            check(
                std::move(thresh0),
                thresh0Msg,
                std::move(thresh0EncodedFulfillment),
                thresh0EncodedCondition,
                thresh0EncodedFingerprint);
        }
    }

    void
    testRsa1()
    {
        testcase("Rsa1");

        using namespace std::string_literals;
        using namespace ripple::cryptoconditions;

        // Fulfillment structure
        // * rsa0

        auto const rsa0Msg = "\x61\x61\x61"s;
        std::array<std::uint8_t, 256> const rsa0PublicKey{
            {0xe1, 0xef, 0x8b, 0x24, 0xd6, 0xf7, 0x6b, 0x09, 0xc8, 0x1e, 0xd7,
             0x75, 0x2a, 0xa2, 0x62, 0xf0, 0x44, 0xf0, 0x4a, 0x87, 0x4d, 0x43,
             0x80, 0x9d, 0x31, 0xce, 0xa6, 0x12, 0xf9, 0x9b, 0x0c, 0x97, 0xa8,
             0xb4, 0x37, 0x41, 0x53, 0xe3, 0xee, 0xf3, 0xd6, 0x66, 0x16, 0x84,
             0x3e, 0x0e, 0x41, 0xc2, 0x93, 0x26, 0x4b, 0x71, 0xb6, 0x17, 0x3d,
             0xb1, 0xcf, 0x0d, 0x6c, 0xd5, 0x58, 0xc5, 0x86, 0x57, 0x70, 0x6f,
             0xcf, 0x09, 0x7f, 0x70, 0x4c, 0x48, 0x3e, 0x59, 0xcb, 0xfd, 0xfd,
             0x5b, 0x3e, 0xe7, 0xbc, 0x80, 0xd7, 0x40, 0xc5, 0xe0, 0xf0, 0x47,
             0xf3, 0xe8, 0x5f, 0xc0, 0xd7, 0x58, 0x15, 0x77, 0x6a, 0x6f, 0x3f,
             0x23, 0xc5, 0xdc, 0x5e, 0x79, 0x71, 0x39, 0xa6, 0x88, 0x2e, 0x38,
             0x33, 0x6a, 0x4a, 0x5f, 0xb3, 0x61, 0x37, 0x62, 0x0f, 0xf3, 0x66,
             0x3d, 0xba, 0xe3, 0x28, 0x47, 0x28, 0x01, 0x86, 0x2f, 0x72, 0xf2,
             0xf8, 0x7b, 0x20, 0x2b, 0x9c, 0x89, 0xad, 0xd7, 0xcd, 0x5b, 0x0a,
             0x07, 0x6f, 0x7c, 0x53, 0xe3, 0x50, 0x39, 0xf6, 0x7e, 0xd1, 0x7e,
             0xc8, 0x15, 0xe5, 0xb4, 0x30, 0x5c, 0xc6, 0x31, 0x97, 0x06, 0x8d,
             0x5e, 0x6e, 0x57, 0x9b, 0xa6, 0xde, 0x5f, 0x4e, 0x3e, 0x57, 0xdf,
             0x5e, 0x4e, 0x07, 0x2f, 0xf2, 0xce, 0x4c, 0x66, 0xeb, 0x45, 0x23,
             0x39, 0x73, 0x87, 0x52, 0x75, 0x96, 0x39, 0xf0, 0x25, 0x7b, 0xf5,
             0x7d, 0xbd, 0x5c, 0x44, 0x3f, 0xb5, 0x15, 0x8c, 0xce, 0x0a, 0x3d,
             0x36, 0xad, 0xc7, 0xba, 0x01, 0xf3, 0x3a, 0x0b, 0xb6, 0xdb, 0xb2,
             0xbf, 0x98, 0x9d, 0x60, 0x71, 0x12, 0xf2, 0x34, 0x4d, 0x99, 0x3e,
             0x77, 0xe5, 0x63, 0xc1, 0xd3, 0x61, 0xde, 0xdf, 0x57, 0xda, 0x96,
             0xef, 0x2c, 0xfc, 0x68, 0x5f, 0x00, 0x2b, 0x63, 0x82, 0x46, 0xa5,
             0xb3, 0x09, 0xb9}};
        std::array<std::uint8_t, 256> const rsa0Sig{
            {0x48, 0xe8, 0x94, 0x5e, 0xfe, 0x00, 0x75, 0x56, 0xd5, 0xbf, 0x4d,
             0x5f, 0x24, 0x9e, 0x48, 0x08, 0xf7, 0x30, 0x7e, 0x29, 0x51, 0x1d,
             0x32, 0x62, 0xda, 0xef, 0x61, 0xd8, 0x80, 0x98, 0xf9, 0xaa, 0x4a,
             0x8b, 0xc0, 0x62, 0x3a, 0x8c, 0x97, 0x57, 0x38, 0xf6, 0x5d, 0x6b,
             0xf4, 0x59, 0xd5, 0x43, 0xf2, 0x89, 0xd7, 0x3c, 0xbc, 0x7a, 0xf4,
             0xea, 0x3a, 0x33, 0xfb, 0xf3, 0xec, 0x44, 0x40, 0x44, 0x79, 0x11,
             0xd7, 0x22, 0x94, 0x09, 0x1e, 0x56, 0x18, 0x33, 0x62, 0x8e, 0x49,
             0xa7, 0x72, 0xed, 0x60, 0x8d, 0xe6, 0xc4, 0x45, 0x95, 0xa9, 0x1e,
             0x3e, 0x17, 0xd6, 0xcf, 0x5e, 0xc3, 0xb2, 0x52, 0x8d, 0x63, 0xd2,
             0xad, 0xd6, 0x46, 0x39, 0x89, 0xb1, 0x2e, 0xec, 0x57, 0x7d, 0xf6,
             0x47, 0x09, 0x60, 0xdf, 0x68, 0x32, 0xa9, 0xd8, 0x4c, 0x36, 0x0d,
             0x1c, 0x21, 0x7a, 0xd6, 0x4c, 0x86, 0x25, 0xbd, 0xb5, 0x94, 0xfb,
             0x0a, 0xda, 0x08, 0x6c, 0xde, 0xcb, 0xbd, 0xe5, 0x80, 0xd4, 0x24,
             0xbf, 0x97, 0x46, 0xd2, 0xf0, 0xc3, 0x12, 0x82, 0x6d, 0xbb, 0xb0,
             0x0a, 0xd6, 0x8b, 0x52, 0xc4, 0xcb, 0x7d, 0x47, 0x15, 0x6b, 0xa3,
             0x5e, 0x3a, 0x98, 0x1c, 0x97, 0x38, 0x63, 0x79, 0x2c, 0xc8, 0x0d,
             0x04, 0xa1, 0x80, 0x21, 0x0a, 0x52, 0x41, 0x58, 0x65, 0xb6, 0x4b,
             0x3a, 0x61, 0x77, 0x4b, 0x1d, 0x39, 0x75, 0xd7, 0x8a, 0x98, 0xb0,
             0x82, 0x1e, 0xe5, 0x5c, 0xa0, 0xf8, 0x63, 0x05, 0xd4, 0x25, 0x29,
             0xe1, 0x0e, 0xb0, 0x15, 0xce, 0xfd, 0x40, 0x2f, 0xb5, 0x9b, 0x2a,
             0xbb, 0x8d, 0xee, 0xe5, 0x2a, 0x6f, 0x24, 0x47, 0xd2, 0x28, 0x46,
             0x03, 0xd2, 0x19, 0xcd, 0x4e, 0x8c, 0xf9, 0xcf, 0xfd, 0xd5, 0x49,
             0x88, 0x89, 0xc3, 0x78, 0x0b, 0x59, 0xdd, 0x6a, 0x57, 0xef, 0x7d,
             0x73, 0x26, 0x20}};

        auto rsa0 = std::make_unique<RsaSha256>(
            makeSlice(rsa0PublicKey), makeSlice(rsa0Sig));
        {
            auto rsa0EncodedFulfillment =
                "\xa3\x82\x02\x08\x80\x82\x01\x00\xe1\xef\x8b\x24\xd6\xf7\x6b"
                "\x09\xc8\x1e\xd7\x75\x2a\xa2\x62\xf0\x44\xf0\x4a\x87\x4d\x43"
                "\x80\x9d\x31\xce\xa6\x12\xf9\x9b\x0c\x97\xa8\xb4\x37\x41\x53"
                "\xe3\xee\xf3\xd6\x66\x16\x84\x3e\x0e\x41\xc2\x93\x26\x4b\x71"
                "\xb6\x17\x3d\xb1\xcf\x0d\x6c\xd5\x58\xc5\x86\x57\x70\x6f\xcf"
                "\x09\x7f\x70\x4c\x48\x3e\x59\xcb\xfd\xfd\x5b\x3e\xe7\xbc\x80"
                "\xd7\x40\xc5\xe0\xf0\x47\xf3\xe8\x5f\xc0\xd7\x58\x15\x77\x6a"
                "\x6f\x3f\x23\xc5\xdc\x5e\x79\x71\x39\xa6\x88\x2e\x38\x33\x6a"
                "\x4a\x5f\xb3\x61\x37\x62\x0f\xf3\x66\x3d\xba\xe3\x28\x47\x28"
                "\x01\x86\x2f\x72\xf2\xf8\x7b\x20\x2b\x9c\x89\xad\xd7\xcd\x5b"
                "\x0a\x07\x6f\x7c\x53\xe3\x50\x39\xf6\x7e\xd1\x7e\xc8\x15\xe5"
                "\xb4\x30\x5c\xc6\x31\x97\x06\x8d\x5e\x6e\x57\x9b\xa6\xde\x5f"
                "\x4e\x3e\x57\xdf\x5e\x4e\x07\x2f\xf2\xce\x4c\x66\xeb\x45\x23"
                "\x39\x73\x87\x52\x75\x96\x39\xf0\x25\x7b\xf5\x7d\xbd\x5c\x44"
                "\x3f\xb5\x15\x8c\xce\x0a\x3d\x36\xad\xc7\xba\x01\xf3\x3a\x0b"
                "\xb6\xdb\xb2\xbf\x98\x9d\x60\x71\x12\xf2\x34\x4d\x99\x3e\x77"
                "\xe5\x63\xc1\xd3\x61\xde\xdf\x57\xda\x96\xef\x2c\xfc\x68\x5f"
                "\x00\x2b\x63\x82\x46\xa5\xb3\x09\xb9\x81\x82\x01\x00\x48\xe8"
                "\x94\x5e\xfe\x00\x75\x56\xd5\xbf\x4d\x5f\x24\x9e\x48\x08\xf7"
                "\x30\x7e\x29\x51\x1d\x32\x62\xda\xef\x61\xd8\x80\x98\xf9\xaa"
                "\x4a\x8b\xc0\x62\x3a\x8c\x97\x57\x38\xf6\x5d\x6b\xf4\x59\xd5"
                "\x43\xf2\x89\xd7\x3c\xbc\x7a\xf4\xea\x3a\x33\xfb\xf3\xec\x44"
                "\x40\x44\x79\x11\xd7\x22\x94\x09\x1e\x56\x18\x33\x62\x8e\x49"
                "\xa7\x72\xed\x60\x8d\xe6\xc4\x45\x95\xa9\x1e\x3e\x17\xd6\xcf"
                "\x5e\xc3\xb2\x52\x8d\x63\xd2\xad\xd6\x46\x39\x89\xb1\x2e\xec"
                "\x57\x7d\xf6\x47\x09\x60\xdf\x68\x32\xa9\xd8\x4c\x36\x0d\x1c"
                "\x21\x7a\xd6\x4c\x86\x25\xbd\xb5\x94\xfb\x0a\xda\x08\x6c\xde"
                "\xcb\xbd\xe5\x80\xd4\x24\xbf\x97\x46\xd2\xf0\xc3\x12\x82\x6d"
                "\xbb\xb0\x0a\xd6\x8b\x52\xc4\xcb\x7d\x47\x15\x6b\xa3\x5e\x3a"
                "\x98\x1c\x97\x38\x63\x79\x2c\xc8\x0d\x04\xa1\x80\x21\x0a\x52"
                "\x41\x58\x65\xb6\x4b\x3a\x61\x77\x4b\x1d\x39\x75\xd7\x8a\x98"
                "\xb0\x82\x1e\xe5\x5c\xa0\xf8\x63\x05\xd4\x25\x29\xe1\x0e\xb0"
                "\x15\xce\xfd\x40\x2f\xb5\x9b\x2a\xbb\x8d\xee\xe5\x2a\x6f\x24"
                "\x47\xd2\x28\x46\x03\xd2\x19\xcd\x4e\x8c\xf9\xcf\xfd\xd5\x49"
                "\x88\x89\xc3\x78\x0b\x59\xdd\x6a\x57\xef\x7d\x73\x26\x20"s;
            auto const rsa0EncodedCondition =
                "\xa3\x27\x80\x20\xb3\x1f\xa8\x20\x6e\x4e\xa7\xe5\x15\x33\x7b"
                "\x3b\x33\x08\x2b\x87\x76\x51\x80\x10\x85\xed\x84\xfb\x4d\xae"
                "\xb2\x47\xbf\x69\x8d\x7f\x81\x03\x01\x00\x00"s;
            auto const rsa0EncodedFingerprint =
                "\x30\x82\x01\x04\x80\x82\x01\x00\xe1\xef\x8b\x24\xd6\xf7\x6b"
                "\x09\xc8\x1e\xd7\x75\x2a\xa2\x62\xf0\x44\xf0\x4a\x87\x4d\x43"
                "\x80\x9d\x31\xce\xa6\x12\xf9\x9b\x0c\x97\xa8\xb4\x37\x41\x53"
                "\xe3\xee\xf3\xd6\x66\x16\x84\x3e\x0e\x41\xc2\x93\x26\x4b\x71"
                "\xb6\x17\x3d\xb1\xcf\x0d\x6c\xd5\x58\xc5\x86\x57\x70\x6f\xcf"
                "\x09\x7f\x70\x4c\x48\x3e\x59\xcb\xfd\xfd\x5b\x3e\xe7\xbc\x80"
                "\xd7\x40\xc5\xe0\xf0\x47\xf3\xe8\x5f\xc0\xd7\x58\x15\x77\x6a"
                "\x6f\x3f\x23\xc5\xdc\x5e\x79\x71\x39\xa6\x88\x2e\x38\x33\x6a"
                "\x4a\x5f\xb3\x61\x37\x62\x0f\xf3\x66\x3d\xba\xe3\x28\x47\x28"
                "\x01\x86\x2f\x72\xf2\xf8\x7b\x20\x2b\x9c\x89\xad\xd7\xcd\x5b"
                "\x0a\x07\x6f\x7c\x53\xe3\x50\x39\xf6\x7e\xd1\x7e\xc8\x15\xe5"
                "\xb4\x30\x5c\xc6\x31\x97\x06\x8d\x5e\x6e\x57\x9b\xa6\xde\x5f"
                "\x4e\x3e\x57\xdf\x5e\x4e\x07\x2f\xf2\xce\x4c\x66\xeb\x45\x23"
                "\x39\x73\x87\x52\x75\x96\x39\xf0\x25\x7b\xf5\x7d\xbd\x5c\x44"
                "\x3f\xb5\x15\x8c\xce\x0a\x3d\x36\xad\xc7\xba\x01\xf3\x3a\x0b"
                "\xb6\xdb\xb2\xbf\x98\x9d\x60\x71\x12\xf2\x34\x4d\x99\x3e\x77"
                "\xe5\x63\xc1\xd3\x61\xde\xdf\x57\xda\x96\xef\x2c\xfc\x68\x5f"
                "\x00\x2b\x63\x82\x46\xa5\xb3\x09\xb9"s;
            check(
                std::move(rsa0),
                rsa0Msg,
                std::move(rsa0EncodedFulfillment),
                rsa0EncodedCondition,
                rsa0EncodedFingerprint);
        }
    }

    void
    testRsa2()
    {
        testcase("Rsa2");

        using namespace std::string_literals;
        using namespace ripple::cryptoconditions;

        // Fulfillment structure
        // * rsa0

        auto const rsa0Msg = "\x61\x61\x61"s;
        std::array<std::uint8_t, 512> const rsa0PublicKey{
            {0xbb, 0xb0, 0xa1, 0xa3, 0x14, 0xff, 0x6a, 0x2f, 0xc7, 0x9a, 0xe0,
             0x6f, 0x61, 0x0d, 0xc8, 0xd4, 0x42, 0x1c, 0x93, 0x3e, 0xd0, 0xc4,
             0x35, 0x4e, 0x6a, 0xea, 0xd9, 0x1e, 0xf9, 0x47, 0xea, 0x4f, 0x8f,
             0x63, 0x2b, 0x2e, 0x2f, 0x78, 0xb8, 0xa0, 0x7a, 0x78, 0xc3, 0x32,
             0x48, 0x95, 0xc7, 0xcb, 0x09, 0x16, 0xeb, 0x33, 0x4c, 0x10, 0x09,
             0x0e, 0x5e, 0xdb, 0x9e, 0x02, 0x95, 0xed, 0x2f, 0xf1, 0xde, 0xaf,
             0xd1, 0xf4, 0x9f, 0x54, 0x41, 0x2e, 0x43, 0xed, 0xe2, 0xcb, 0x06,
             0xcf, 0xfc, 0x39, 0x53, 0x03, 0x30, 0x9a, 0x4c, 0xe3, 0xea, 0xc2,
             0x34, 0x1d, 0x9a, 0xf3, 0x18, 0x83, 0x37, 0xee, 0xba, 0xbd, 0x3d,
             0x4a, 0x4a, 0xf8, 0xf4, 0x3e, 0x15, 0x7d, 0x6f, 0x02, 0xf9, 0xd1,
             0xf2, 0x4a, 0x42, 0x77, 0x70, 0x49, 0xc9, 0x30, 0xaa, 0x23, 0x3f,
             0xed, 0x5c, 0x63, 0xb0, 0x7f, 0x72, 0x5c, 0xde, 0x6b, 0xb2, 0x44,
             0x04, 0xfc, 0xbf, 0xc0, 0xb8, 0x72, 0x47, 0xea, 0xcb, 0x7d, 0xb0,
             0x44, 0x78, 0x5b, 0xa6, 0x6e, 0xde, 0x8e, 0x79, 0x21, 0x12, 0x47,
             0x01, 0x50, 0x40, 0x1e, 0x0a, 0x84, 0x71, 0xbd, 0xe3, 0x6f, 0x2d,
             0x5e, 0xcb, 0x96, 0x0f, 0x57, 0x1b, 0xa1, 0x7d, 0xaf, 0x38, 0x1d,
             0x83, 0x78, 0xde, 0xc2, 0x1e, 0x10, 0x12, 0xe4, 0xb3, 0x76, 0xc9,
             0xe6, 0xc4, 0x6b, 0xb6, 0x7d, 0x68, 0xee, 0xf1, 0x2b, 0xa9, 0xa1,
             0x59, 0x67, 0xf4, 0x86, 0xd8, 0xbc, 0x91, 0xb3, 0xe2, 0xb0, 0x6f,
             0xa5, 0xfc, 0xa6, 0x9b, 0x75, 0x24, 0x26, 0xaf, 0x02, 0x9c, 0xb1,
             0x49, 0xea, 0x58, 0x6d, 0xed, 0x85, 0x1e, 0xba, 0x16, 0x08, 0x76,
             0xac, 0xd8, 0x5f, 0x06, 0x22, 0x71, 0xfa, 0xd7, 0x3d, 0x15, 0xf5,
             0xf0, 0xf0, 0x22, 0xe2, 0x61, 0x30, 0x92, 0x26, 0xbe, 0xe3, 0x56,
             0x7a, 0xd0, 0x52, 0xf9, 0x74, 0x6c, 0xa8, 0xbf, 0xac, 0xf0, 0xd4,
             0xf4, 0x17, 0x30, 0x08, 0x5c, 0x09, 0x7a, 0x02, 0x02, 0x83, 0x01,
             0xd9, 0x2a, 0x0c, 0x54, 0x5c, 0x03, 0x97, 0x47, 0x2c, 0x2e, 0xee,
             0x5b, 0x61, 0x22, 0x20, 0x25, 0xd6, 0x34, 0x70, 0xee, 0x68, 0x1c,
             0xe3, 0x25, 0x27, 0x47, 0xce, 0x9c, 0xa2, 0x3f, 0x19, 0xe6, 0x6f,
             0x2f, 0x63, 0x86, 0xe6, 0xad, 0x13, 0x91, 0x1d, 0x7a, 0xda, 0xcd,
             0x38, 0xce, 0x5c, 0xa8, 0xff, 0x2c, 0x2d, 0x99, 0xab, 0xb0, 0xf5,
             0xc3, 0xba, 0x84, 0x75, 0x09, 0x61, 0x3e, 0x66, 0x32, 0xa1, 0x2c,
             0xe6, 0xef, 0x78, 0xda, 0x4c, 0x82, 0x0e, 0x90, 0x83, 0x40, 0x53,
             0x00, 0x3d, 0x11, 0x97, 0xf6, 0xa5, 0x7b, 0x01, 0x00, 0xfe, 0xe1,
             0x52, 0xc8, 0x4c, 0x8b, 0x1b, 0x33, 0xbb, 0x96, 0x57, 0x19, 0x88,
             0x78, 0x0f, 0xaf, 0xbd, 0x69, 0x7c, 0xf8, 0x37, 0x0a, 0x99, 0xda,
             0x8f, 0xaa, 0xf7, 0x56, 0x87, 0xd9, 0x51, 0xcc, 0x65, 0x33, 0xc7,
             0x8b, 0x8e, 0x1c, 0xe2, 0xe1, 0xd5, 0xce, 0xb9, 0xa1, 0x16, 0x29,
             0x20, 0x1a, 0xf4, 0x37, 0x47, 0x5c, 0x94, 0x02, 0x7f, 0xa5, 0x26,
             0x14, 0xb4, 0x30, 0x0b, 0x77, 0xdc, 0xf1, 0x80, 0xac, 0x49, 0xca,
             0xa3, 0x40, 0x16, 0x8f, 0x32, 0x62, 0xfd, 0x1e, 0xe8, 0xb1, 0x38,
             0x02, 0xce, 0xa3, 0x57, 0x54, 0xb4, 0x23, 0xb8, 0x33, 0xfa, 0x14,
             0xc5, 0xdd, 0x0c, 0x47, 0x6d, 0xde, 0x5d, 0x5e, 0x7b, 0xf7, 0x37,
             0x4d, 0x61, 0xf2, 0x48, 0xc3, 0xba, 0xb9, 0x1c, 0xb0, 0x55, 0x0b,
             0xd1, 0xcb, 0xef, 0x70, 0x50, 0x7e, 0xe8, 0xdb, 0x1c, 0xf3, 0x99,
             0x30, 0x7e, 0x22, 0x8d, 0x4f, 0x45, 0x92, 0xa6, 0x6c, 0x58, 0x57,
             0x3c, 0xfe, 0xcc, 0x63, 0x96, 0x68, 0x06, 0xfa, 0xf8, 0x81, 0x09,
             0xcc, 0xb0, 0x99, 0x23, 0xea, 0x5b}};
        std::array<std::uint8_t, 512> const rsa0Sig{
            {0x41, 0x20, 0x0e, 0x42, 0x96, 0x1e, 0xea, 0x46, 0x0c, 0x82, 0x76,
             0xf2, 0x39, 0xab, 0x66, 0xb7, 0x6f, 0x8f, 0x28, 0x69, 0xea, 0xcc,
             0xc5, 0xf9, 0xe9, 0x94, 0x5c, 0x0a, 0xbc, 0x63, 0xb1, 0x43, 0x06,
             0x56, 0xf3, 0xda, 0x2d, 0x21, 0x7a, 0x43, 0x32, 0x7e, 0xe8, 0xce,
             0x11, 0x27, 0xd1, 0x4a, 0x85, 0xd7, 0x4a, 0xf4, 0x12, 0xfe, 0x93,
             0x70, 0x34, 0x5b, 0xa6, 0xb4, 0x40, 0x03, 0xd7, 0x71, 0xae, 0xdd,
             0x2b, 0xa0, 0xfc, 0x25, 0x06, 0x27, 0xfd, 0xf6, 0x16, 0x31, 0xaf,
             0xd6, 0x4e, 0x9b, 0xee, 0x71, 0x4b, 0x1b, 0xaf, 0xb6, 0x9f, 0x90,
             0x1b, 0xee, 0xb6, 0xf9, 0x59, 0xb1, 0x6a, 0x54, 0xb3, 0x28, 0xa9,
             0xa6, 0x74, 0xc8, 0x83, 0xca, 0xfc, 0x4f, 0xc2, 0xf7, 0xb0, 0x47,
             0x6d, 0xbc, 0xaa, 0x8d, 0x9b, 0xfd, 0x47, 0xf7, 0x34, 0xe3, 0x47,
             0x36, 0xbf, 0x66, 0xe9, 0x73, 0xb3, 0x15, 0xc2, 0xab, 0xf4, 0x72,
             0x7b, 0x06, 0x2c, 0xa3, 0xc4, 0x02, 0x93, 0x6e, 0xb5, 0x08, 0x49,
             0xe1, 0x81, 0x5d, 0xd1, 0x60, 0x4a, 0x60, 0x4d, 0xef, 0xa6, 0x68,
             0xcf, 0xe8, 0x23, 0xb0, 0x08, 0xbf, 0x8f, 0x7b, 0xf5, 0x7c, 0xeb,
             0x1b, 0x41, 0xe5, 0x18, 0xff, 0x53, 0xae, 0x91, 0x1e, 0xf9, 0x88,
             0x05, 0x71, 0xb3, 0xd1, 0x74, 0x23, 0x41, 0xd6, 0x0e, 0x3c, 0xb2,
             0xbe, 0xb9, 0x3e, 0xb5, 0xf1, 0x78, 0xf8, 0xef, 0x7b, 0xa0, 0xc6,
             0xf2, 0x29, 0x0b, 0x99, 0x55, 0xd8, 0x3d, 0x60, 0xf9, 0x43, 0xd6,
             0x44, 0x57, 0x1e, 0xc8, 0xd5, 0xf0, 0x64, 0xd9, 0x5f, 0x1d, 0xa1,
             0xe7, 0x59, 0xd6, 0x92, 0x0d, 0x84, 0xa5, 0x4c, 0xbc, 0x49, 0x78,
             0x36, 0x40, 0xa7, 0xb0, 0xcf, 0x3d, 0x02, 0x8e, 0x62, 0x7e, 0xc7,
             0xad, 0x25, 0xa9, 0x15, 0xf6, 0xd8, 0xc2, 0x4b, 0x2e, 0x09, 0x91,
             0x52, 0x8a, 0x02, 0xc5, 0xbe, 0xf0, 0x10, 0x6c, 0x18, 0xb3, 0xf3,
             0x9c, 0x59, 0x99, 0x56, 0xeb, 0x7a, 0x27, 0xb3, 0x37, 0x31, 0x3d,
             0xcf, 0x93, 0xe7, 0x58, 0x67, 0x60, 0x6a, 0x4a, 0xc5, 0xa1, 0xab,
             0x6f, 0x9b, 0xb4, 0x44, 0xdc, 0x1d, 0xad, 0x1f, 0x54, 0x6f, 0xae,
             0xde, 0x6a, 0xe9, 0x34, 0x88, 0x40, 0x49, 0x01, 0x76, 0x91, 0x37,
             0x1d, 0x51, 0xee, 0x1d, 0x65, 0x0e, 0xfc, 0xe5, 0x81, 0xb3, 0x0c,
             0x68, 0x43, 0x08, 0x7e, 0x70, 0x14, 0x19, 0x0f, 0x44, 0x60, 0x26,
             0x7d, 0x89, 0x4d, 0x52, 0x58, 0x14, 0xdd, 0xa3, 0x43, 0xb9, 0xa0,
             0x48, 0x9d, 0xc0, 0x8c, 0x6a, 0x34, 0x33, 0x47, 0x42, 0xce, 0xa3,
             0x0b, 0x49, 0x25, 0x12, 0xd2, 0x95, 0x74, 0x52, 0x17, 0xdd, 0x65,
             0xec, 0xfe, 0xcb, 0x9c, 0xa1, 0x8d, 0x21, 0x2f, 0x84, 0xa5, 0x06,
             0xd7, 0x00, 0x26, 0xed, 0x33, 0x28, 0x96, 0xbd, 0x24, 0xa6, 0x30,
             0x5a, 0x0c, 0xbd, 0xee, 0x9d, 0xfd, 0x8a, 0xbb, 0x85, 0x07, 0xf8,
             0x40, 0xeb, 0x94, 0xcf, 0xa8, 0xe9, 0x28, 0xe6, 0x91, 0x0c, 0x27,
             0x5f, 0x7b, 0x68, 0xd1, 0x1f, 0x96, 0x46, 0x3c, 0x10, 0x2a, 0x59,
             0x66, 0x13, 0x47, 0x86, 0x95, 0xbc, 0x76, 0x71, 0xfb, 0x27, 0x30,
             0x91, 0x39, 0xc6, 0xcd, 0x2f, 0x15, 0xca, 0x7e, 0x24, 0x05, 0x2e,
             0x47, 0xf4, 0xe3, 0x4e, 0xf8, 0xb8, 0x44, 0xec, 0x36, 0x4a, 0x85,
             0xbd, 0xc9, 0xbe, 0x84, 0x25, 0xcf, 0xf7, 0x2a, 0x77, 0xbe, 0x98,
             0xd9, 0x01, 0x69, 0x86, 0xa6, 0x67, 0x10, 0x98, 0x25, 0xf1, 0xef,
             0x18, 0xe0, 0x81, 0x70, 0xa8, 0x7b, 0x43, 0x68, 0xe7, 0xc1, 0x42,
             0xc1, 0x9d, 0xad, 0x10, 0x60, 0x5c, 0x4f, 0x34, 0x1d, 0x52, 0x09,
             0x98, 0x7a, 0xf3, 0x1a, 0xba, 0x8e, 0x2e, 0x49, 0x63, 0xf3, 0xe0,
             0x79, 0x4d, 0xd1, 0x1f, 0x0b, 0x72}};

        auto rsa0 = std::make_unique<RsaSha256>(
            makeSlice(rsa0PublicKey), makeSlice(rsa0Sig));
        {
            auto rsa0EncodedFulfillment =
                "\xa3\x82\x04\x08\x80\x82\x02\x00\xbb\xb0\xa1\xa3\x14\xff\x6a"
                "\x2f\xc7\x9a\xe0\x6f\x61\x0d\xc8\xd4\x42\x1c\x93\x3e\xd0\xc4"
                "\x35\x4e\x6a\xea\xd9\x1e\xf9\x47\xea\x4f\x8f\x63\x2b\x2e\x2f"
                "\x78\xb8\xa0\x7a\x78\xc3\x32\x48\x95\xc7\xcb\x09\x16\xeb\x33"
                "\x4c\x10\x09\x0e\x5e\xdb\x9e\x02\x95\xed\x2f\xf1\xde\xaf\xd1"
                "\xf4\x9f\x54\x41\x2e\x43\xed\xe2\xcb\x06\xcf\xfc\x39\x53\x03"
                "\x30\x9a\x4c\xe3\xea\xc2\x34\x1d\x9a\xf3\x18\x83\x37\xee\xba"
                "\xbd\x3d\x4a\x4a\xf8\xf4\x3e\x15\x7d\x6f\x02\xf9\xd1\xf2\x4a"
                "\x42\x77\x70\x49\xc9\x30\xaa\x23\x3f\xed\x5c\x63\xb0\x7f\x72"
                "\x5c\xde\x6b\xb2\x44\x04\xfc\xbf\xc0\xb8\x72\x47\xea\xcb\x7d"
                "\xb0\x44\x78\x5b\xa6\x6e\xde\x8e\x79\x21\x12\x47\x01\x50\x40"
                "\x1e\x0a\x84\x71\xbd\xe3\x6f\x2d\x5e\xcb\x96\x0f\x57\x1b\xa1"
                "\x7d\xaf\x38\x1d\x83\x78\xde\xc2\x1e\x10\x12\xe4\xb3\x76\xc9"
                "\xe6\xc4\x6b\xb6\x7d\x68\xee\xf1\x2b\xa9\xa1\x59\x67\xf4\x86"
                "\xd8\xbc\x91\xb3\xe2\xb0\x6f\xa5\xfc\xa6\x9b\x75\x24\x26\xaf"
                "\x02\x9c\xb1\x49\xea\x58\x6d\xed\x85\x1e\xba\x16\x08\x76\xac"
                "\xd8\x5f\x06\x22\x71\xfa\xd7\x3d\x15\xf5\xf0\xf0\x22\xe2\x61"
                "\x30\x92\x26\xbe\xe3\x56\x7a\xd0\x52\xf9\x74\x6c\xa8\xbf\xac"
                "\xf0\xd4\xf4\x17\x30\x08\x5c\x09\x7a\x02\x02\x83\x01\xd9\x2a"
                "\x0c\x54\x5c\x03\x97\x47\x2c\x2e\xee\x5b\x61\x22\x20\x25\xd6"
                "\x34\x70\xee\x68\x1c\xe3\x25\x27\x47\xce\x9c\xa2\x3f\x19\xe6"
                "\x6f\x2f\x63\x86\xe6\xad\x13\x91\x1d\x7a\xda\xcd\x38\xce\x5c"
                "\xa8\xff\x2c\x2d\x99\xab\xb0\xf5\xc3\xba\x84\x75\x09\x61\x3e"
                "\x66\x32\xa1\x2c\xe6\xef\x78\xda\x4c\x82\x0e\x90\x83\x40\x53"
                "\x00\x3d\x11\x97\xf6\xa5\x7b\x01\x00\xfe\xe1\x52\xc8\x4c\x8b"
                "\x1b\x33\xbb\x96\x57\x19\x88\x78\x0f\xaf\xbd\x69\x7c\xf8\x37"
                "\x0a\x99\xda\x8f\xaa\xf7\x56\x87\xd9\x51\xcc\x65\x33\xc7\x8b"
                "\x8e\x1c\xe2\xe1\xd5\xce\xb9\xa1\x16\x29\x20\x1a\xf4\x37\x47"
                "\x5c\x94\x02\x7f\xa5\x26\x14\xb4\x30\x0b\x77\xdc\xf1\x80\xac"
                "\x49\xca\xa3\x40\x16\x8f\x32\x62\xfd\x1e\xe8\xb1\x38\x02\xce"
                "\xa3\x57\x54\xb4\x23\xb8\x33\xfa\x14\xc5\xdd\x0c\x47\x6d\xde"
                "\x5d\x5e\x7b\xf7\x37\x4d\x61\xf2\x48\xc3\xba\xb9\x1c\xb0\x55"
                "\x0b\xd1\xcb\xef\x70\x50\x7e\xe8\xdb\x1c\xf3\x99\x30\x7e\x22"
                "\x8d\x4f\x45\x92\xa6\x6c\x58\x57\x3c\xfe\xcc\x63\x96\x68\x06"
                "\xfa\xf8\x81\x09\xcc\xb0\x99\x23\xea\x5b\x81\x82\x02\x00\x41"
                "\x20\x0e\x42\x96\x1e\xea\x46\x0c\x82\x76\xf2\x39\xab\x66\xb7"
                "\x6f\x8f\x28\x69\xea\xcc\xc5\xf9\xe9\x94\x5c\x0a\xbc\x63\xb1"
                "\x43\x06\x56\xf3\xda\x2d\x21\x7a\x43\x32\x7e\xe8\xce\x11\x27"
                "\xd1\x4a\x85\xd7\x4a\xf4\x12\xfe\x93\x70\x34\x5b\xa6\xb4\x40"
                "\x03\xd7\x71\xae\xdd\x2b\xa0\xfc\x25\x06\x27\xfd\xf6\x16\x31"
                "\xaf\xd6\x4e\x9b\xee\x71\x4b\x1b\xaf\xb6\x9f\x90\x1b\xee\xb6"
                "\xf9\x59\xb1\x6a\x54\xb3\x28\xa9\xa6\x74\xc8\x83\xca\xfc\x4f"
                "\xc2\xf7\xb0\x47\x6d\xbc\xaa\x8d\x9b\xfd\x47\xf7\x34\xe3\x47"
                "\x36\xbf\x66\xe9\x73\xb3\x15\xc2\xab\xf4\x72\x7b\x06\x2c\xa3"
                "\xc4\x02\x93\x6e\xb5\x08\x49\xe1\x81\x5d\xd1\x60\x4a\x60\x4d"
                "\xef\xa6\x68\xcf\xe8\x23\xb0\x08\xbf\x8f\x7b\xf5\x7c\xeb\x1b"
                "\x41\xe5\x18\xff\x53\xae\x91\x1e\xf9\x88\x05\x71\xb3\xd1\x74"
                "\x23\x41\xd6\x0e\x3c\xb2\xbe\xb9\x3e\xb5\xf1\x78\xf8\xef\x7b"
                "\xa0\xc6\xf2\x29\x0b\x99\x55\xd8\x3d\x60\xf9\x43\xd6\x44\x57"
                "\x1e\xc8\xd5\xf0\x64\xd9\x5f\x1d\xa1\xe7\x59\xd6\x92\x0d\x84"
                "\xa5\x4c\xbc\x49\x78\x36\x40\xa7\xb0\xcf\x3d\x02\x8e\x62\x7e"
                "\xc7\xad\x25\xa9\x15\xf6\xd8\xc2\x4b\x2e\x09\x91\x52\x8a\x02"
                "\xc5\xbe\xf0\x10\x6c\x18\xb3\xf3\x9c\x59\x99\x56\xeb\x7a\x27"
                "\xb3\x37\x31\x3d\xcf\x93\xe7\x58\x67\x60\x6a\x4a\xc5\xa1\xab"
                "\x6f\x9b\xb4\x44\xdc\x1d\xad\x1f\x54\x6f\xae\xde\x6a\xe9\x34"
                "\x88\x40\x49\x01\x76\x91\x37\x1d\x51\xee\x1d\x65\x0e\xfc\xe5"
                "\x81\xb3\x0c\x68\x43\x08\x7e\x70\x14\x19\x0f\x44\x60\x26\x7d"
                "\x89\x4d\x52\x58\x14\xdd\xa3\x43\xb9\xa0\x48\x9d\xc0\x8c\x6a"
                "\x34\x33\x47\x42\xce\xa3\x0b\x49\x25\x12\xd2\x95\x74\x52\x17"
                "\xdd\x65\xec\xfe\xcb\x9c\xa1\x8d\x21\x2f\x84\xa5\x06\xd7\x00"
                "\x26\xed\x33\x28\x96\xbd\x24\xa6\x30\x5a\x0c\xbd\xee\x9d\xfd"
                "\x8a\xbb\x85\x07\xf8\x40\xeb\x94\xcf\xa8\xe9\x28\xe6\x91\x0c"
                "\x27\x5f\x7b\x68\xd1\x1f\x96\x46\x3c\x10\x2a\x59\x66\x13\x47"
                "\x86\x95\xbc\x76\x71\xfb\x27\x30\x91\x39\xc6\xcd\x2f\x15\xca"
                "\x7e\x24\x05\x2e\x47\xf4\xe3\x4e\xf8\xb8\x44\xec\x36\x4a\x85"
                "\xbd\xc9\xbe\x84\x25\xcf\xf7\x2a\x77\xbe\x98\xd9\x01\x69\x86"
                "\xa6\x67\x10\x98\x25\xf1\xef\x18\xe0\x81\x70\xa8\x7b\x43\x68"
                "\xe7\xc1\x42\xc1\x9d\xad\x10\x60\x5c\x4f\x34\x1d\x52\x09\x98"
                "\x7a\xf3\x1a\xba\x8e\x2e\x49\x63\xf3\xe0\x79\x4d\xd1\x1f\x0b"
                "\x72"s;
            auto const rsa0EncodedCondition =
                "\xa3\x27\x80\x20\x4d\xd2\xea\x7f\x85\xb3\xea\xcb\x8f\x19\x05"
                "\x8e\x83\x60\x95\x5c\x32\xe7\x4c\x12\x43\x92\xa1\xf4\x46\x60"
                "\x73\x97\x09\xc5\x39\xc3\x81\x03\x04\x00\x00"s;
            auto const rsa0EncodedFingerprint =
                "\x30\x82\x02\x04\x80\x82\x02\x00\xbb\xb0\xa1\xa3\x14\xff\x6a"
                "\x2f\xc7\x9a\xe0\x6f\x61\x0d\xc8\xd4\x42\x1c\x93\x3e\xd0\xc4"
                "\x35\x4e\x6a\xea\xd9\x1e\xf9\x47\xea\x4f\x8f\x63\x2b\x2e\x2f"
                "\x78\xb8\xa0\x7a\x78\xc3\x32\x48\x95\xc7\xcb\x09\x16\xeb\x33"
                "\x4c\x10\x09\x0e\x5e\xdb\x9e\x02\x95\xed\x2f\xf1\xde\xaf\xd1"
                "\xf4\x9f\x54\x41\x2e\x43\xed\xe2\xcb\x06\xcf\xfc\x39\x53\x03"
                "\x30\x9a\x4c\xe3\xea\xc2\x34\x1d\x9a\xf3\x18\x83\x37\xee\xba"
                "\xbd\x3d\x4a\x4a\xf8\xf4\x3e\x15\x7d\x6f\x02\xf9\xd1\xf2\x4a"
                "\x42\x77\x70\x49\xc9\x30\xaa\x23\x3f\xed\x5c\x63\xb0\x7f\x72"
                "\x5c\xde\x6b\xb2\x44\x04\xfc\xbf\xc0\xb8\x72\x47\xea\xcb\x7d"
                "\xb0\x44\x78\x5b\xa6\x6e\xde\x8e\x79\x21\x12\x47\x01\x50\x40"
                "\x1e\x0a\x84\x71\xbd\xe3\x6f\x2d\x5e\xcb\x96\x0f\x57\x1b\xa1"
                "\x7d\xaf\x38\x1d\x83\x78\xde\xc2\x1e\x10\x12\xe4\xb3\x76\xc9"
                "\xe6\xc4\x6b\xb6\x7d\x68\xee\xf1\x2b\xa9\xa1\x59\x67\xf4\x86"
                "\xd8\xbc\x91\xb3\xe2\xb0\x6f\xa5\xfc\xa6\x9b\x75\x24\x26\xaf"
                "\x02\x9c\xb1\x49\xea\x58\x6d\xed\x85\x1e\xba\x16\x08\x76\xac"
                "\xd8\x5f\x06\x22\x71\xfa\xd7\x3d\x15\xf5\xf0\xf0\x22\xe2\x61"
                "\x30\x92\x26\xbe\xe3\x56\x7a\xd0\x52\xf9\x74\x6c\xa8\xbf\xac"
                "\xf0\xd4\xf4\x17\x30\x08\x5c\x09\x7a\x02\x02\x83\x01\xd9\x2a"
                "\x0c\x54\x5c\x03\x97\x47\x2c\x2e\xee\x5b\x61\x22\x20\x25\xd6"
                "\x34\x70\xee\x68\x1c\xe3\x25\x27\x47\xce\x9c\xa2\x3f\x19\xe6"
                "\x6f\x2f\x63\x86\xe6\xad\x13\x91\x1d\x7a\xda\xcd\x38\xce\x5c"
                "\xa8\xff\x2c\x2d\x99\xab\xb0\xf5\xc3\xba\x84\x75\x09\x61\x3e"
                "\x66\x32\xa1\x2c\xe6\xef\x78\xda\x4c\x82\x0e\x90\x83\x40\x53"
                "\x00\x3d\x11\x97\xf6\xa5\x7b\x01\x00\xfe\xe1\x52\xc8\x4c\x8b"
                "\x1b\x33\xbb\x96\x57\x19\x88\x78\x0f\xaf\xbd\x69\x7c\xf8\x37"
                "\x0a\x99\xda\x8f\xaa\xf7\x56\x87\xd9\x51\xcc\x65\x33\xc7\x8b"
                "\x8e\x1c\xe2\xe1\xd5\xce\xb9\xa1\x16\x29\x20\x1a\xf4\x37\x47"
                "\x5c\x94\x02\x7f\xa5\x26\x14\xb4\x30\x0b\x77\xdc\xf1\x80\xac"
                "\x49\xca\xa3\x40\x16\x8f\x32\x62\xfd\x1e\xe8\xb1\x38\x02\xce"
                "\xa3\x57\x54\xb4\x23\xb8\x33\xfa\x14\xc5\xdd\x0c\x47\x6d\xde"
                "\x5d\x5e\x7b\xf7\x37\x4d\x61\xf2\x48\xc3\xba\xb9\x1c\xb0\x55"
                "\x0b\xd1\xcb\xef\x70\x50\x7e\xe8\xdb\x1c\xf3\x99\x30\x7e\x22"
                "\x8d\x4f\x45\x92\xa6\x6c\x58\x57\x3c\xfe\xcc\x63\x96\x68\x06"
                "\xfa\xf8\x81\x09\xcc\xb0\x99\x23\xea\x5b"s;
            check(
                std::move(rsa0),
                rsa0Msg,
                std::move(rsa0EncodedFulfillment),
                rsa0EncodedCondition,
                rsa0EncodedFingerprint);
        }
    }

    void
    testEd1()
    {
        testcase("Ed1");

        using namespace std::string_literals;
        using namespace ripple::cryptoconditions;

        // Fulfillment structure
        // * ed0

        auto const ed0Msg = "\x61\x61\x61"s;
        std::array<std::uint8_t, 32> const ed0PublicKey{
            {0xd7, 0x5a, 0x98, 0x01, 0x82, 0xb1, 0x0a, 0xb7, 0xd5, 0x4b, 0xfe,
             0xd3, 0xc9, 0x64, 0x07, 0x3a, 0x0e, 0xe1, 0x72, 0xf3, 0xda, 0xa6,
             0x23, 0x25, 0xaf, 0x02, 0x1a, 0x68, 0xf7, 0x07, 0x51, 0x1a}};
        std::array<std::uint8_t, 64> const ed0Sig{
            {0x50, 0x6a, 0x1e, 0xa6, 0x83, 0x18, 0xe6, 0x2d, 0x40, 0x63, 0x5d,
             0xad, 0x04, 0x3e, 0x19, 0x87, 0xeb, 0xc2, 0x6e, 0x5b, 0x5c, 0x44,
             0x06, 0xf7, 0xbd, 0xf8, 0x5a, 0x73, 0x38, 0x8f, 0xbf, 0xe5, 0xc2,
             0x45, 0xac, 0x49, 0xf4, 0x77, 0x0e, 0xbc, 0x78, 0x77, 0x08, 0x27,
             0x0a, 0xa6, 0xa8, 0x76, 0x9f, 0xef, 0xe8, 0x93, 0x0f, 0xd0, 0xea,
             0x1e, 0xe6, 0x4b, 0x31, 0x40, 0x7d, 0x76, 0x95, 0x09}};

        auto ed0 = std::make_unique<Ed25519>(ed0PublicKey, ed0Sig);
        {
            auto ed0EncodedFulfillment =
                "\xa4\x64\x80\x20\xd7\x5a\x98\x01\x82\xb1\x0a\xb7\xd5\x4b\xfe"
                "\xd3\xc9\x64\x07\x3a\x0e\xe1\x72\xf3\xda\xa6\x23\x25\xaf\x02"
                "\x1a\x68\xf7\x07\x51\x1a\x81\x40\x50\x6a\x1e\xa6\x83\x18\xe6"
                "\x2d\x40\x63\x5d\xad\x04\x3e\x19\x87\xeb\xc2\x6e\x5b\x5c\x44"
                "\x06\xf7\xbd\xf8\x5a\x73\x38\x8f\xbf\xe5\xc2\x45\xac\x49\xf4"
                "\x77\x0e\xbc\x78\x77\x08\x27\x0a\xa6\xa8\x76\x9f\xef\xe8\x93"
                "\x0f\xd0\xea\x1e\xe6\x4b\x31\x40\x7d\x76\x95\x09"s;
            auto const ed0EncodedCondition =
                "\xa4\x27\x80\x20\x79\x92\x39\xab\xa8\xfc\x4f\xf7\xea\xbf\xbc"
                "\x4c\x44\xe6\x9e\x8b\xdf\xed\x99\x33\x24\xe1\x2e\xd6\x47\x92"
                "\xab\xe2\x89\xcf\x1d\x5f\x81\x03\x02\x00\x00"s;
            auto const ed0EncodedFingerprint =
                "\x30\x22\x80\x20\xd7\x5a\x98\x01\x82\xb1\x0a\xb7\xd5\x4b\xfe"
                "\xd3\xc9\x64\x07\x3a\x0e\xe1\x72\xf3\xda\xa6\x23\x25\xaf\x02"
                "\x1a\x68\xf7\x07\x51\x1a"s;
            check(
                std::move(ed0),
                ed0Msg,
                std::move(ed0EncodedFulfillment),
                ed0EncodedCondition,
                ed0EncodedFingerprint);
        }
    }

    void
    testThresh6()
    {
        testcase("Thresh6");

        using namespace std::string_literals;
        using namespace ripple::cryptoconditions;

        // Fulfillment structure
        // * thresh0
        // ** prefix1
        // *** ed2
        // ** preim3

        auto const ed2Msg = ""s;
        std::array<std::uint8_t, 32> const ed2PublicKey{
            {0x2e, 0x53, 0x1e, 0x88, 0xbf, 0xe8, 0xc4, 0x19, 0xf9, 0x61, 0xad,
             0x9c, 0x90, 0x1d, 0xe2, 0xbd, 0xd8, 0xe7, 0xa0, 0xe7, 0x14, 0x84,
             0x55, 0x05, 0x9e, 0x89, 0xeb, 0x79, 0x98, 0x6b, 0x25, 0x24}};
        std::array<std::uint8_t, 64> const ed2Sig{
            {0xe5, 0xfd, 0xdb, 0xbe, 0xc2, 0xe8, 0xdb, 0x59, 0xbc, 0xb6, 0xa6,
             0x80, 0xe4, 0xa7, 0x05, 0x6f, 0xdd, 0x46, 0x50, 0x0b, 0x4e, 0x99,
             0xa3, 0x71, 0x9f, 0x25, 0x73, 0x50, 0x36, 0xa1, 0x49, 0x28, 0x3e,
             0x28, 0x7c, 0xed, 0xed, 0xcd, 0x9e, 0x5a, 0x50, 0xcd, 0x97, 0x6a,
             0xb4, 0x11, 0xf8, 0x4f, 0xf5, 0x69, 0xaa, 0xbd, 0xe6, 0xf6, 0xe0,
             0xce, 0x6a, 0x0b, 0xdd, 0x0c, 0x4a, 0x82, 0x46, 0x08}};
        auto const prefix1Prefix =
            "https://notary.example/cases/657c12da-8dca-43b0-97ca-8ee8c38ab9f7/state/executed"s;
        auto const prefix1Msg = ""s;
        auto const prefix1MaxMsgLength = 0;
        auto const preim3Preimage =
            "https://notary.example/cases/657c12da-8dca-43b0-97ca-8ee8c38ab9f7/state/executed"s;
        auto const preim3Msg = ""s;
        auto const thresh0Msg = ""s;

        auto ed2 = std::make_unique<Ed25519>(ed2PublicKey, ed2Sig);
        auto prefix1 = std::make_unique<PrefixSha256>(
            makeSlice(prefix1Prefix), prefix1MaxMsgLength, std::move(ed2));
        auto preim3 =
            std::make_unique<PreimageSha256>(makeSlice(preim3Preimage));
        std::vector<std::unique_ptr<Fulfillment>> thresh0Subfulfillments;
        thresh0Subfulfillments.emplace_back(std::move(prefix1));
        thresh0Subfulfillments.emplace_back(std::move(preim3));
        std::vector<Condition> thresh0Subconditions{};
        auto thresh0 = std::make_unique<ThresholdSha256>(
            std::move(thresh0Subfulfillments), std::move(thresh0Subconditions));
        {
            auto thresh0EncodedFulfillment =
                "\xa2\x82\x01\x1a\xa0\x82\x01\x14\xa0\x52\x80\x50\x68\x74\x74"
                "\x70\x73\x3a\x2f\x2f\x6e\x6f\x74\x61\x72\x79\x2e\x65\x78\x61"
                "\x6d\x70\x6c\x65\x2f\x63\x61\x73\x65\x73\x2f\x36\x35\x37\x63"
                "\x31\x32\x64\x61\x2d\x38\x64\x63\x61\x2d\x34\x33\x62\x30\x2d"
                "\x39\x37\x63\x61\x2d\x38\x65\x65\x38\x63\x33\x38\x61\x62\x39"
                "\x66\x37\x2f\x73\x74\x61\x74\x65\x2f\x65\x78\x65\x63\x75\x74"
                "\x65\x64\xa1\x81\xbd\x80\x50\x68\x74\x74\x70\x73\x3a\x2f\x2f"
                "\x6e\x6f\x74\x61\x72\x79\x2e\x65\x78\x61\x6d\x70\x6c\x65\x2f"
                "\x63\x61\x73\x65\x73\x2f\x36\x35\x37\x63\x31\x32\x64\x61\x2d"
                "\x38\x64\x63\x61\x2d\x34\x33\x62\x30\x2d\x39\x37\x63\x61\x2d"
                "\x38\x65\x65\x38\x63\x33\x38\x61\x62\x39\x66\x37\x2f\x73\x74"
                "\x61\x74\x65\x2f\x65\x78\x65\x63\x75\x74\x65\x64\x81\x01\x00"
                "\xa2\x66\xa4\x64\x80\x20\x2e\x53\x1e\x88\xbf\xe8\xc4\x19\xf9"
                "\x61\xad\x9c\x90\x1d\xe2\xbd\xd8\xe7\xa0\xe7\x14\x84\x55\x05"
                "\x9e\x89\xeb\x79\x98\x6b\x25\x24\x81\x40\xe5\xfd\xdb\xbe\xc2"
                "\xe8\xdb\x59\xbc\xb6\xa6\x80\xe4\xa7\x05\x6f\xdd\x46\x50\x0b"
                "\x4e\x99\xa3\x71\x9f\x25\x73\x50\x36\xa1\x49\x28\x3e\x28\x7c"
                "\xed\xed\xcd\x9e\x5a\x50\xcd\x97\x6a\xb4\x11\xf8\x4f\xf5\x69"
                "\xaa\xbd\xe6\xf6\xe0\xce\x6a\x0b\xdd\x0c\x4a\x82\x46\x08\xa1"
                "\x00"s;
            auto const thresh0EncodedCondition =
                "\xa2\x2b\x80\x20\x09\xe3\x91\x00\x46\x28\x72\x5e\x88\xf8\x55"
                "\x7e\x95\x4f\xb2\xa0\xea\xe2\xb7\xc1\x51\xc4\x7d\xf3\xc4\xaf"
                "\x22\xf8\xc1\x69\x88\xf9\x81\x03\x02\x0c\xa0\x82\x02\x03\xc8"s;
            auto const thresh0EncodedFingerprint =
                "\x30\x59\x80\x01\x02\xa1\x54\xa0\x25\x80\x20\x0b\x4a\xc3\xa1"
                "\xe0\x93\x2c\xb7\x1b\x74\x30\x9f\xad\x7d\x15\xdf\x51\xbd\x4d"
                "\x13\x59\xed\x59\xff\x7c\x91\x7b\x35\xdf\x24\x46\x4a\x81\x01"
                "\x50\xa1\x2b\x80\x20\x3f\x94\x52\x55\x55\xcf\x4c\x52\x34\xbf"
                "\x77\xcb\x10\x85\x01\xd9\x7b\x9d\x8a\x28\xd1\xe7\xa3\xa7\xfe"
                "\x8d\x3d\x7f\x03\x1f\xde\xbd\x81\x03\x02\x04\x50\x82\x02\x03"
                "\x08"s;
            check(
                std::move(thresh0),
                thresh0Msg,
                std::move(thresh0EncodedFulfillment),
                thresh0EncodedCondition,
                thresh0EncodedFingerprint);
        }
    }

    void
    testThresh7()
    {
        testcase("Thresh7");

        using namespace std::string_literals;
        using namespace ripple::cryptoconditions;

        // Fulfillment structure
        // * thresh0
        // ** prefix1
        // *** thresh2
        // **** Prefix9Cond
        // **** prefix3
        // ***** ed4
        // **** prefix5
        // ***** ed6
        // **** prefix7
        // ***** ed8
        // ** preim11

        auto const ed4Msg = ""s;
        std::array<std::uint8_t, 32> const ed4PublicKey{
            {0x2e, 0x53, 0x1e, 0x88, 0xbf, 0xe8, 0xc4, 0x19, 0xf9, 0x61, 0xad,
             0x9c, 0x90, 0x1d, 0xe2, 0xbd, 0xd8, 0xe7, 0xa0, 0xe7, 0x14, 0x84,
             0x55, 0x05, 0x9e, 0x89, 0xeb, 0x79, 0x98, 0x6b, 0x25, 0x24}};
        std::array<std::uint8_t, 64> const ed4Sig{
            {0x87, 0x30, 0x1a, 0x18, 0x08, 0xf7, 0x3c, 0x20, 0x3f, 0x0e, 0x9c,
             0x81, 0x06, 0xf1, 0x30, 0x71, 0x08, 0x81, 0xda, 0xcd, 0xac, 0x80,
             0x7c, 0x10, 0xd3, 0x49, 0xb7, 0x98, 0x20, 0xdc, 0xb3, 0x40, 0x7c,
             0x77, 0xb9, 0xd2, 0x3d, 0xb4, 0x28, 0x27, 0x64, 0x0b, 0xdc, 0x41,
             0x38, 0x3f, 0xdc, 0x4e, 0xca, 0x76, 0x19, 0xc1, 0x70, 0x37, 0xe8,
             0x70, 0x37, 0xa5, 0xc7, 0xcf, 0x33, 0x81, 0x7a, 0x0e}};
        auto const prefix3Prefix = "https://notary1.example/"s;
        auto const prefix3Msg = ""s;
        auto const prefix3MaxMsgLength = 1024;
        auto const ed6Msg = ""s;
        std::array<std::uint8_t, 32> const ed6PublicKey{
            {0x59, 0x02, 0x3e, 0x76, 0x8a, 0x9c, 0x85, 0x87, 0x6c, 0x61, 0xeb,
             0xaa, 0xa3, 0x4e, 0xc1, 0x8e, 0x64, 0x85, 0x7f, 0xa7, 0x66, 0x92,
             0xc5, 0x5a, 0x99, 0x63, 0x5f, 0x9b, 0x88, 0xe5, 0xaf, 0x90}};
        std::array<std::uint8_t, 64> const ed6Sig{
            {0xac, 0xf9, 0xee, 0x83, 0x88, 0x5b, 0xa5, 0x8f, 0x62, 0xc4, 0x2b,
             0x48, 0x99, 0xe8, 0xce, 0xa9, 0x15, 0xa9, 0x19, 0x2f, 0x74, 0x88,
             0xc1, 0x59, 0x2c, 0xe9, 0x59, 0x56, 0x0b, 0x52, 0xf8, 0x7a, 0x37,
             0x90, 0xe0, 0x36, 0xd3, 0xc6, 0x95, 0x4b, 0x87, 0x55, 0x41, 0x48,
             0xd1, 0x31, 0xcc, 0xba, 0xf3, 0x69, 0xc6, 0x8a, 0x66, 0xa3, 0x13,
             0x7f, 0xe8, 0xfa, 0x43, 0x68, 0xa1, 0x65, 0xa0, 0x0a}};
        auto const prefix5Prefix = "https://notary2.example/"s;
        auto const prefix5Msg = ""s;
        auto const prefix5MaxMsgLength = 1024;
        auto const ed8Msg = ""s;
        std::array<std::uint8_t, 32> const ed8PublicKey{
            {0x9a, 0x98, 0xac, 0x6d, 0xbf, 0xf0, 0x90, 0xe9, 0x6e, 0x38, 0xd8,
             0x1f, 0x05, 0x47, 0x7d, 0xf8, 0x6b, 0x3b, 0xbb, 0x0e, 0xff, 0xc3,
             0x11, 0xbc, 0x7b, 0x42, 0xcd, 0xac, 0x99, 0xd6, 0xbd, 0xd9}};
        std::array<std::uint8_t, 64> const ed8Sig{
            {0x97, 0xa3, 0x2b, 0x0c, 0x61, 0xce, 0x15, 0x10, 0x36, 0xca, 0xd3,
             0x59, 0x69, 0xc9, 0xf9, 0x5e, 0xb5, 0x44, 0x65, 0xea, 0x5d, 0x62,
             0x9b, 0xa9, 0x65, 0xab, 0xf8, 0xa6, 0xa9, 0x17, 0xf1, 0x0d, 0xd1,
             0x4a, 0xbe, 0x55, 0xd3, 0x30, 0x54, 0x43, 0x8e, 0x68, 0xc9, 0x15,
             0xa6, 0xb6, 0x7c, 0x1d, 0xdf, 0x8a, 0x0c, 0x16, 0xd2, 0xd8, 0x01,
             0xf8, 0xd0, 0xba, 0x85, 0xef, 0xee, 0x9b, 0xbf, 0x0f}};
        auto const prefix7Prefix = "https://notary3.example/"s;
        auto const prefix7Msg = ""s;
        auto const prefix7MaxMsgLength = 1024;
        auto const thresh2Msg = ""s;
        std::array<std::uint8_t, 32> const Prefix9CondConditionFingerprint = {
            {0xee, 0x0b, 0xc0, 0x2f, 0x97, 0x7c, 0x26, 0x4b, 0x6c, 0x30, 0x6e,
             0xd1, 0xb1, 0x68, 0xfe, 0xb4, 0xfd, 0x60, 0x09, 0x50, 0xad, 0x21,
             0x75, 0x0c, 0xe8, 0xa8, 0x6e, 0xcb, 0xd4, 0x60, 0x35, 0x38}};
        Condition const Prefix9Cond{Type::prefixSha256,
                                    133145,
                                    Prefix9CondConditionFingerprint,
                                    std::bitset<5>{16}};
        auto const prefix1Prefix =
            "cases/657c12da-8dca-43b0-97ca-8ee8c38ab9f7/state/executed"s;
        auto const prefix1Msg = ""s;
        auto const prefix1MaxMsgLength = 0;
        auto const preim11Preimage =
            "https://notary.example/cases/657c12da-8dca-43b0-97ca-8ee8c38ab9f7/state/executed"s;
        auto const preim11Msg = ""s;
        auto const thresh0Msg = ""s;

        auto ed4 = std::make_unique<Ed25519>(ed4PublicKey, ed4Sig);
        auto prefix3 = std::make_unique<PrefixSha256>(
            makeSlice(prefix3Prefix), prefix3MaxMsgLength, std::move(ed4));
        auto ed6 = std::make_unique<Ed25519>(ed6PublicKey, ed6Sig);
        auto prefix5 = std::make_unique<PrefixSha256>(
            makeSlice(prefix5Prefix), prefix5MaxMsgLength, std::move(ed6));
        auto ed8 = std::make_unique<Ed25519>(ed8PublicKey, ed8Sig);
        auto prefix7 = std::make_unique<PrefixSha256>(
            makeSlice(prefix7Prefix), prefix7MaxMsgLength, std::move(ed8));
        std::vector<std::unique_ptr<Fulfillment>> thresh2Subfulfillments;
        thresh2Subfulfillments.emplace_back(std::move(prefix3));
        thresh2Subfulfillments.emplace_back(std::move(prefix5));
        thresh2Subfulfillments.emplace_back(std::move(prefix7));
        std::vector<Condition> thresh2Subconditions{{Prefix9Cond}};
        auto thresh2 = std::make_unique<ThresholdSha256>(
            std::move(thresh2Subfulfillments), std::move(thresh2Subconditions));
        auto prefix1 = std::make_unique<PrefixSha256>(
            makeSlice(prefix1Prefix), prefix1MaxMsgLength, std::move(thresh2));
        auto preim11 =
            std::make_unique<PreimageSha256>(makeSlice(preim11Preimage));
        std::vector<std::unique_ptr<Fulfillment>> thresh0Subfulfillments;
        thresh0Subfulfillments.emplace_back(std::move(prefix1));
        thresh0Subfulfillments.emplace_back(std::move(preim11));
        std::vector<Condition> thresh0Subconditions{};
        auto thresh0 = std::make_unique<ThresholdSha256>(
            std::move(thresh0Subfulfillments), std::move(thresh0Subconditions));
        {
            auto thresh0EncodedFulfillment =
                "\xa2\x82\x02\x72\xa0\x82\x02\x6c\xa0\x52\x80\x50\x68\x74\x74"
                "\x70\x73\x3a\x2f\x2f\x6e\x6f\x74\x61\x72\x79\x2e\x65\x78\x61"
                "\x6d\x70\x6c\x65\x2f\x63\x61\x73\x65\x73\x2f\x36\x35\x37\x63"
                "\x31\x32\x64\x61\x2d\x38\x64\x63\x61\x2d\x34\x33\x62\x30\x2d"
                "\x39\x37\x63\x61\x2d\x38\x65\x65\x38\x63\x33\x38\x61\x62\x39"
                "\x66\x37\x2f\x73\x74\x61\x74\x65\x2f\x65\x78\x65\x63\x75\x74"
                "\x65\x64\xa1\x82\x02\x14\x80\x39\x63\x61\x73\x65\x73\x2f\x36"
                "\x35\x37\x63\x31\x32\x64\x61\x2d\x38\x64\x63\x61\x2d\x34\x33"
                "\x62\x30\x2d\x39\x37\x63\x61\x2d\x38\x65\x65\x38\x63\x33\x38"
                "\x61\x62\x39\x66\x37\x2f\x73\x74\x61\x74\x65\x2f\x65\x78\x65"
                "\x63\x75\x74\x65\x64\x81\x01\x00\xa2\x82\x01\xd2\xa2\x82\x01"
                "\xce\xa0\x82\x01\x9b\xa1\x81\x86\x80\x18\x68\x74\x74\x70\x73"
                "\x3a\x2f\x2f\x6e\x6f\x74\x61\x72\x79\x31\x2e\x65\x78\x61\x6d"
                "\x70\x6c\x65\x2f\x81\x02\x04\x00\xa2\x66\xa4\x64\x80\x20\x2e"
                "\x53\x1e\x88\xbf\xe8\xc4\x19\xf9\x61\xad\x9c\x90\x1d\xe2\xbd"
                "\xd8\xe7\xa0\xe7\x14\x84\x55\x05\x9e\x89\xeb\x79\x98\x6b\x25"
                "\x24\x81\x40\x87\x30\x1a\x18\x08\xf7\x3c\x20\x3f\x0e\x9c\x81"
                "\x06\xf1\x30\x71\x08\x81\xda\xcd\xac\x80\x7c\x10\xd3\x49\xb7"
                "\x98\x20\xdc\xb3\x40\x7c\x77\xb9\xd2\x3d\xb4\x28\x27\x64\x0b"
                "\xdc\x41\x38\x3f\xdc\x4e\xca\x76\x19\xc1\x70\x37\xe8\x70\x37"
                "\xa5\xc7\xcf\x33\x81\x7a\x0e\xa1\x81\x86\x80\x18\x68\x74\x74"
                "\x70\x73\x3a\x2f\x2f\x6e\x6f\x74\x61\x72\x79\x32\x2e\x65\x78"
                "\x61\x6d\x70\x6c\x65\x2f\x81\x02\x04\x00\xa2\x66\xa4\x64\x80"
                "\x20\x59\x02\x3e\x76\x8a\x9c\x85\x87\x6c\x61\xeb\xaa\xa3\x4e"
                "\xc1\x8e\x64\x85\x7f\xa7\x66\x92\xc5\x5a\x99\x63\x5f\x9b\x88"
                "\xe5\xaf\x90\x81\x40\xac\xf9\xee\x83\x88\x5b\xa5\x8f\x62\xc4"
                "\x2b\x48\x99\xe8\xce\xa9\x15\xa9\x19\x2f\x74\x88\xc1\x59\x2c"
                "\xe9\x59\x56\x0b\x52\xf8\x7a\x37\x90\xe0\x36\xd3\xc6\x95\x4b"
                "\x87\x55\x41\x48\xd1\x31\xcc\xba\xf3\x69\xc6\x8a\x66\xa3\x13"
                "\x7f\xe8\xfa\x43\x68\xa1\x65\xa0\x0a\xa1\x81\x86\x80\x18\x68"
                "\x74\x74\x70\x73\x3a\x2f\x2f\x6e\x6f\x74\x61\x72\x79\x33\x2e"
                "\x65\x78\x61\x6d\x70\x6c\x65\x2f\x81\x02\x04\x00\xa2\x66\xa4"
                "\x64\x80\x20\x9a\x98\xac\x6d\xbf\xf0\x90\xe9\x6e\x38\xd8\x1f"
                "\x05\x47\x7d\xf8\x6b\x3b\xbb\x0e\xff\xc3\x11\xbc\x7b\x42\xcd"
                "\xac\x99\xd6\xbd\xd9\x81\x40\x97\xa3\x2b\x0c\x61\xce\x15\x10"
                "\x36\xca\xd3\x59\x69\xc9\xf9\x5e\xb5\x44\x65\xea\x5d\x62\x9b"
                "\xa9\x65\xab\xf8\xa6\xa9\x17\xf1\x0d\xd1\x4a\xbe\x55\xd3\x30"
                "\x54\x43\x8e\x68\xc9\x15\xa6\xb6\x7c\x1d\xdf\x8a\x0c\x16\xd2"
                "\xd8\x01\xf8\xd0\xba\x85\xef\xee\x9b\xbf\x0f\xa1\x2d\xa1\x2b"
                "\x80\x20\xee\x0b\xc0\x2f\x97\x7c\x26\x4b\x6c\x30\x6e\xd1\xb1"
                "\x68\xfe\xb4\xfd\x60\x09\x50\xad\x21\x75\x0c\xe8\xa8\x6e\xcb"
                "\xd4\x60\x35\x38\x81\x03\x02\x08\x19\x82\x02\x03\x08\xa1\x00"s;
            auto const thresh0EncodedCondition =
                "\xa2\x2b\x80\x20\x42\x4a\x70\x49\x49\x52\x92\x67\xb6\x21\xb3"
                "\xd7\x91\x19\xd7\x29\xb2\x38\x2c\xed\x8b\x29\x6c\x3c\x02\x8f"
                "\xa9\x7d\x35\x0f\x6d\x07\x81\x03\x06\x34\xd2\x82\x02\x03\xc8"s;
            auto const thresh0EncodedFingerprint =
                "\x30\x59\x80\x01\x02\xa1\x54\xa0\x25\x80\x20\x0b\x4a\xc3\xa1"
                "\xe0\x93\x2c\xb7\x1b\x74\x30\x9f\xad\x7d\x15\xdf\x51\xbd\x4d"
                "\x13\x59\xed\x59\xff\x7c\x91\x7b\x35\xdf\x24\x46\x4a\x81\x01"
                "\x50\xa1\x2b\x80\x20\x06\x2f\x2c\x1b\xdd\x08\x66\x1f\xe7\xfe"
                "\xfa\xc2\x0e\x02\xda\x8b\x01\x84\xfc\xd3\x6f\x6c\x6c\x54\xc5"
                "\x3c\xc2\x8d\x2e\x54\xdd\x11\x81\x03\x06\x2c\x82\x82\x02\x03"
                "\x28"s;
            check(
                std::move(thresh0),
                thresh0Msg,
                std::move(thresh0EncodedFulfillment),
                thresh0EncodedCondition,
                thresh0EncodedFingerprint);
        }
    }

    void
    run()
    {
        testPreim0();
        testPrefix0();
        testThresh0();
        testRsa0();
        testEd0();
        testPreim1();
        testPrefix1();
        testPrefix2();
        testThresh1();
        testThresh2();
        testThresh3();
        testThresh4();
        testThresh5();
        testRsa1();
        testRsa2();
        testEd1();
        testThresh6();
        testThresh7();
    }
};

BEAST_DEFINE_TESTSUITE(Conditions_json, conditions, ripple);
}  // cryptoconditions
}  // ripple
