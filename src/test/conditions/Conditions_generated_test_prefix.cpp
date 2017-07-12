
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

class Conditions_prefix_test : public ConditionsTestBase
{
    void
    testPrefix0()
    {
        testcase("Prefix0");

        using namespace std::string_literals;
        using namespace ripple::cryptoconditions;

        // Fulfillment structure
        // * prefix0
        // ** preim1

        auto const preim1Preimage = "I am root"s;
        auto const preim1Msg = "P0abcdefghijklmnopqrstuvwxyz"s;
        auto const prefix0Prefix = "P0"s;
        auto const prefix0Msg = "abcdefghijklmnopqrstuvwxyz"s;
        auto const prefix0MaxMsgLength = 26;

        auto preim1 =
            std::make_unique<PreimageSha256>(makeSlice(preim1Preimage));
        auto prefix0 = std::make_unique<PrefixSha256>(
            makeSlice(prefix0Prefix), prefix0MaxMsgLength, std::move(preim1));
        {
            auto prefix0EncodedFulfillment =
                "\xa1\x16\x80\x02\x50\x30\x81\x01\x1a\xa2\x0d\xa0\x0b\x80\x09"
                "\x49\x20\x61\x6d\x20\x72\x6f\x6f\x74"s;
            auto const prefix0EncodedCondition =
                "\xa1\x2a\x80\x20\x33\x0a\xc8\x0a\xa2\x01\xee\x65\x14\x12\x81"
                "\x29\x6a\xc7\x2e\x21\x8d\xed\x54\x94\xfc\xef\x19\x98\x21\xe5"
                "\xa9\x79\xb4\xd9\x97\xb2\x81\x02\x04\x25\x82\x02\x07\x80"s;
            auto const prefix0EncodedFingerprint =
                "\x30\x30\x80\x02\x50\x30\x81\x01\x1a\xa2\x27\xa0\x25\x80\x20"
                "\x5d\xa0\x30\xef\xfd\xe1\x75\x11\x51\xe8\x5f\x5e\x54\x2d\x6a"
                "\x5b\xd1\x5c\xc9\x33\x21\x2c\xe2\x68\xfc\xfd\x53\xee\x93\x58"
                "\xeb\x4e\x81\x01\x09"s;
            check(
                std::move(prefix0),
                prefix0Msg,
                std::move(prefix0EncodedFulfillment),
                prefix0EncodedCondition,
                prefix0EncodedFingerprint);
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
        // ** prefix1
        // *** preim2

        auto const preim2Preimage = "I am root"s;
        auto const preim2Msg = "P1P0abcdefghijklmnopqrstuvwxyz"s;
        auto const prefix1Prefix = "P1"s;
        auto const prefix1Msg = "P0abcdefghijklmnopqrstuvwxyz"s;
        auto const prefix1MaxMsgLength = 28;
        auto const prefix0Prefix = "P0"s;
        auto const prefix0Msg = "abcdefghijklmnopqrstuvwxyz"s;
        auto const prefix0MaxMsgLength = 26;

        auto preim2 =
            std::make_unique<PreimageSha256>(makeSlice(preim2Preimage));
        auto prefix1 = std::make_unique<PrefixSha256>(
            makeSlice(prefix1Prefix), prefix1MaxMsgLength, std::move(preim2));
        auto prefix0 = std::make_unique<PrefixSha256>(
            makeSlice(prefix0Prefix), prefix0MaxMsgLength, std::move(prefix1));
        {
            auto prefix0EncodedFulfillment =
                "\xa1\x21\x80\x02\x50\x30\x81\x01\x1a\xa2\x18\xa1\x16\x80\x02"
                "\x50\x31\x81\x01\x1c\xa2\x0d\xa0\x0b\x80\x09\x49\x20\x61\x6d"
                "\x20\x72\x6f\x6f\x74"s;
            auto const prefix0EncodedCondition =
                "\xa1\x2a\x80\x20\x46\x33\x5a\x85\xdf\x1f\x18\xe9\x2b\xb5\x1b"
                "\x46\x81\xbc\x27\x4c\x69\x92\xf5\xcd\xe4\x46\x30\x3f\x1f\x27"
                "\x62\x96\x76\x91\x14\xa1\x81\x02\x08\x43\x82\x02\x07\x80"s;
            auto const prefix0EncodedFingerprint =
                "\x30\x35\x80\x02\x50\x30\x81\x01\x1a\xa2\x2c\xa1\x2a\x80\x20"
                "\x27\xd6\x44\x0f\xee\x75\x21\x3b\xb6\xb2\x4b\xa5\x42\x06\x58"
                "\x7e\x19\x9c\x02\x18\xb5\x7e\x95\x36\x06\x03\x76\xb5\x8a\xa8"
                "\x33\x7b\x81\x02\x04\x27\x82\x02\x07\x80"s;
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
        // *** prefix2
        // **** preim3

        auto const preim3Preimage = "I am root"s;
        auto const preim3Msg = "P2P1P0abcdefghijklmnopqrstuvwxyz"s;
        auto const prefix2Prefix = "P2"s;
        auto const prefix2Msg = "P1P0abcdefghijklmnopqrstuvwxyz"s;
        auto const prefix2MaxMsgLength = 30;
        auto const prefix1Prefix = "P1"s;
        auto const prefix1Msg = "P0abcdefghijklmnopqrstuvwxyz"s;
        auto const prefix1MaxMsgLength = 28;
        auto const prefix0Prefix = "P0"s;
        auto const prefix0Msg = "abcdefghijklmnopqrstuvwxyz"s;
        auto const prefix0MaxMsgLength = 26;

        auto preim3 =
            std::make_unique<PreimageSha256>(makeSlice(preim3Preimage));
        auto prefix2 = std::make_unique<PrefixSha256>(
            makeSlice(prefix2Prefix), prefix2MaxMsgLength, std::move(preim3));
        auto prefix1 = std::make_unique<PrefixSha256>(
            makeSlice(prefix1Prefix), prefix1MaxMsgLength, std::move(prefix2));
        auto prefix0 = std::make_unique<PrefixSha256>(
            makeSlice(prefix0Prefix), prefix0MaxMsgLength, std::move(prefix1));
        {
            auto prefix0EncodedFulfillment =
                "\xa1\x2c\x80\x02\x50\x30\x81\x01\x1a\xa2\x23\xa1\x21\x80\x02"
                "\x50\x31\x81\x01\x1c\xa2\x18\xa1\x16\x80\x02\x50\x32\x81\x01"
                "\x1e\xa2\x0d\xa0\x0b\x80\x09\x49\x20\x61\x6d\x20\x72\x6f\x6f"
                "\x74"s;
            auto const prefix0EncodedCondition =
                "\xa1\x2a\x80\x20\x4d\xe0\x07\xec\xab\xaa\xa4\x4d\x86\x5f\xaa"
                "\x4d\x59\x17\x5d\x0b\x5e\xd3\x1a\x37\xae\x22\xd2\x04\x2d\xba"
                "\xc2\xc8\xc9\x85\xb0\x16\x81\x02\x0c\x63\x82\x02\x07\x80"s;
            auto const prefix0EncodedFingerprint =
                "\x30\x35\x80\x02\x50\x30\x81\x01\x1a\xa2\x2c\xa1\x2a\x80\x20"
                "\x04\x88\xf9\x90\x7f\xf3\xc2\x57\x40\xe4\x29\x51\x2c\xbb\xaa"
                "\xf5\x39\xa8\x21\xb8\x39\x70\xb7\xa9\xb1\x3d\xa9\x69\xfc\xc8"
                "\xf9\x1e\x81\x02\x08\x47\x82\x02\x07\x80"s;
            check(
                std::move(prefix0),
                prefix0Msg,
                std::move(prefix0EncodedFulfillment),
                prefix0EncodedCondition,
                prefix0EncodedFingerprint);
        }
    }

    void
    testPrefix3()
    {
        testcase("Prefix3");

        using namespace std::string_literals;
        using namespace ripple::cryptoconditions;

        // Fulfillment structure
        // * prefix0
        // ** prefix1
        // *** thresh2
        // **** preim3

        auto const preim3Preimage = "I am root"s;
        auto const preim3Msg = "P1P0abcdefghijklmnopqrstuvwxyz"s;
        auto const thresh2Msg = "P1P0abcdefghijklmnopqrstuvwxyz"s;
        auto const prefix1Prefix = "P1"s;
        auto const prefix1Msg = "P0abcdefghijklmnopqrstuvwxyz"s;
        auto const prefix1MaxMsgLength = 28;
        auto const prefix0Prefix = "P0"s;
        auto const prefix0Msg = "abcdefghijklmnopqrstuvwxyz"s;
        auto const prefix0MaxMsgLength = 26;

        auto preim3 =
            std::make_unique<PreimageSha256>(makeSlice(preim3Preimage));
        std::vector<std::unique_ptr<Fulfillment>> thresh2Subfulfillments;
        thresh2Subfulfillments.emplace_back(std::move(preim3));
        std::vector<Condition> thresh2Subconditions{};
        auto thresh2 = std::make_unique<ThresholdSha256>(
            std::move(thresh2Subfulfillments), std::move(thresh2Subconditions));
        auto prefix1 = std::make_unique<PrefixSha256>(
            makeSlice(prefix1Prefix), prefix1MaxMsgLength, std::move(thresh2));
        auto prefix0 = std::make_unique<PrefixSha256>(
            makeSlice(prefix0Prefix), prefix0MaxMsgLength, std::move(prefix1));
        {
            auto prefix0EncodedFulfillment =
                "\xa1\x27\x80\x02\x50\x30\x81\x01\x1a\xa2\x1e\xa1\x1c\x80\x02"
                "\x50\x31\x81\x01\x1c\xa2\x13\xa2\x11\xa0\x0d\xa0\x0b\x80\x09"
                "\x49\x20\x61\x6d\x20\x72\x6f\x6f\x74\xa1\x00"s;
            auto const prefix0EncodedCondition =
                "\xa1\x2a\x80\x20\x94\xf0\xed\xf8\x53\x29\x14\x10\x6c\xae\x30"
                "\x7c\xee\x37\x61\x46\xd0\xc7\x16\xeb\xb9\xbb\x92\xba\x5f\xf8"
                "\x2b\x7c\x67\xe6\xae\x56\x81\x02\x0c\x43\x82\x02\x05\xa0"s;
            auto const prefix0EncodedFingerprint =
                "\x30\x35\x80\x02\x50\x30\x81\x01\x1a\xa2\x2c\xa1\x2a\x80\x20"
                "\x78\x72\x04\x67\x9d\x13\xe0\x57\x73\xcb\x59\xd0\xb4\xdb\x25"
                "\xe5\xf3\xb9\x61\xd5\x7c\x56\x6e\xf1\x40\x18\x17\xe3\x3c\x58"
                "\x6b\x43\x81\x02\x08\x27\x82\x02\x05\xa0"s;
            check(
                std::move(prefix0),
                prefix0Msg,
                std::move(prefix0EncodedFulfillment),
                prefix0EncodedCondition,
                prefix0EncodedFingerprint);
        }
    }

    void
    testPrefix4()
    {
        testcase("Prefix4");

        using namespace std::string_literals;
        using namespace ripple::cryptoconditions;

        // Fulfillment structure
        // * prefix0
        // ** prefix1
        // *** thresh2
        // **** Preim4Cond
        // **** Rsa5Cond
        // **** Ed6Cond
        // **** preim3

        auto const preim3Preimage = "I am root"s;
        auto const preim3Msg = "P1P0abcdefghijklmnopqrstuvwxyz"s;
        auto const thresh2Msg = "P1P0abcdefghijklmnopqrstuvwxyz"s;
        std::array<std::uint8_t, 32> const Preim4CondConditionFingerprint = {
            {0x5d, 0xa0, 0x30, 0xef, 0xfd, 0xe1, 0x75, 0x11, 0x51, 0xe8, 0x5f,
             0x5e, 0x54, 0x2d, 0x6a, 0x5b, 0xd1, 0x5c, 0xc9, 0x33, 0x21, 0x2c,
             0xe2, 0x68, 0xfc, 0xfd, 0x53, 0xee, 0x93, 0x58, 0xeb, 0x4e}};
        Condition const Preim4Cond{Type::preimageSha256,
                                   9,
                                   Preim4CondConditionFingerprint,
                                   std::bitset<5>{0}};
        std::array<std::uint8_t, 32> const Rsa5CondConditionFingerprint = {
            {0x99, 0xfb, 0x0b, 0x38, 0x94, 0x4d, 0x20, 0x85, 0xc8, 0xda, 0x3a,
             0x64, 0x31, 0x44, 0x6f, 0x6c, 0x3b, 0x46, 0x25, 0x50, 0xd7, 0x7f,
             0xdf, 0xee, 0x75, 0x72, 0x71, 0xf9, 0x61, 0x40, 0x63, 0xfa}};
        Condition const Rsa5Cond{Type::rsaSha256,
                                 65536,
                                 Rsa5CondConditionFingerprint,
                                 std::bitset<5>{0}};
        std::array<std::uint8_t, 32> const Ed6CondConditionFingerprint = {
            {0x00, 0xd3, 0xc9, 0x24, 0x3f, 0x2d, 0x2e, 0x64, 0x93, 0xa8, 0x49,
             0x29, 0x82, 0x75, 0xea, 0xbf, 0xe3, 0x53, 0x7f, 0x8e, 0x45, 0x16,
             0xdb, 0x5e, 0xc6, 0xdf, 0x39, 0xd2, 0xcb, 0xea, 0x62, 0xfb}};
        Condition const Ed6Cond{Type::ed25519Sha256,
                                131072,
                                Ed6CondConditionFingerprint,
                                std::bitset<5>{0}};
        auto const prefix1Prefix = "P1"s;
        auto const prefix1Msg = "P0abcdefghijklmnopqrstuvwxyz"s;
        auto const prefix1MaxMsgLength = 28;
        auto const prefix0Prefix = "P0"s;
        auto const prefix0Msg = "abcdefghijklmnopqrstuvwxyz"s;
        auto const prefix0MaxMsgLength = 26;

        auto preim3 =
            std::make_unique<PreimageSha256>(makeSlice(preim3Preimage));
        std::vector<std::unique_ptr<Fulfillment>> thresh2Subfulfillments;
        thresh2Subfulfillments.emplace_back(std::move(preim3));
        std::vector<Condition> thresh2Subconditions{
            {Preim4Cond, Rsa5Cond, Ed6Cond}};
        auto thresh2 = std::make_unique<ThresholdSha256>(
            std::move(thresh2Subfulfillments), std::move(thresh2Subconditions));
        auto prefix1 = std::make_unique<PrefixSha256>(
            makeSlice(prefix1Prefix), prefix1MaxMsgLength, std::move(thresh2));
        auto prefix0 = std::make_unique<PrefixSha256>(
            makeSlice(prefix0Prefix), prefix0MaxMsgLength, std::move(prefix1));
        {
            auto prefix0EncodedFulfillment =
                "\xa1\x81\xa4\x80\x02\x50\x30\x81\x01\x1a\xa2\x81\x9a\xa1\x81"
                "\x97\x80\x02\x50\x31\x81\x01\x1c\xa2\x81\x8d\xa2\x81\x8a\xa0"
                "\x0d\xa0\x0b\x80\x09\x49\x20\x61\x6d\x20\x72\x6f\x6f\x74\xa1"
                "\x79\xa0\x25\x80\x20\x5d\xa0\x30\xef\xfd\xe1\x75\x11\x51\xe8"
                "\x5f\x5e\x54\x2d\x6a\x5b\xd1\x5c\xc9\x33\x21\x2c\xe2\x68\xfc"
                "\xfd\x53\xee\x93\x58\xeb\x4e\x81\x01\x09\xa3\x27\x80\x20\x99"
                "\xfb\x0b\x38\x94\x4d\x20\x85\xc8\xda\x3a\x64\x31\x44\x6f\x6c"
                "\x3b\x46\x25\x50\xd7\x7f\xdf\xee\x75\x72\x71\xf9\x61\x40\x63"
                "\xfa\x81\x03\x01\x00\x00\xa4\x27\x80\x20\x00\xd3\xc9\x24\x3f"
                "\x2d\x2e\x64\x93\xa8\x49\x29\x82\x75\xea\xbf\xe3\x53\x7f\x8e"
                "\x45\x16\xdb\x5e\xc6\xdf\x39\xd2\xcb\xea\x62\xfb\x81\x03\x02"
                "\x00\x00"s;
            auto const prefix0EncodedCondition =
                "\xa1\x2b\x80\x20\x8d\x16\xce\xa7\x52\x75\x97\xa4\xdc\xfc\x01"
                "\xf5\xb6\x72\xfe\x4a\x2c\x04\x58\x23\xee\x23\x31\x37\xd5\xb0"
                "\x84\xb1\x03\x0f\x03\x47\x81\x03\x02\x18\x3a\x82\x02\x03\xb8"s;
            auto const prefix0EncodedFingerprint =
                "\x30\x36\x80\x02\x50\x30\x81\x01\x1a\xa2\x2d\xa1\x2b\x80\x20"
                "\x71\x89\x06\xbc\xe3\xb8\x3f\x0b\x8c\x97\xea\x83\x77\xb2\x90"
                "\xf2\xbc\xa3\x24\x4a\xe0\x8b\x82\xbc\x1e\xf9\xf0\x38\x98\x86"
                "\x3e\x6c\x81\x03\x02\x14\x1e\x82\x02\x03\xb8"s;
            check(
                std::move(prefix0),
                prefix0Msg,
                std::move(prefix0EncodedFulfillment),
                prefix0EncodedCondition,
                prefix0EncodedFingerprint);
        }
    }

    void
    testPrefix5()
    {
        testcase("Prefix5");

        using namespace std::string_literals;
        using namespace ripple::cryptoconditions;

        // Fulfillment structure
        // * prefix0
        // ** prefix1
        // *** thresh2
        // **** Preim9Cond
        // **** Rsa10Cond
        // **** Ed11Cond
        // **** preim3
        // **** thresh4
        // ***** Preim6Cond
        // ***** Rsa7Cond
        // ***** Ed8Cond
        // ***** preim5

        auto const preim3Preimage = "I am root"s;
        auto const preim3Msg = "P1P0abcdefghijklmnopqrstuvwxyz"s;
        auto const preim5Preimage = "I am root"s;
        auto const preim5Msg = "P1P0abcdefghijklmnopqrstuvwxyz"s;
        auto const thresh4Msg = "P1P0abcdefghijklmnopqrstuvwxyz"s;
        std::array<std::uint8_t, 32> const Preim6CondConditionFingerprint = {
            {0x5d, 0xa0, 0x30, 0xef, 0xfd, 0xe1, 0x75, 0x11, 0x51, 0xe8, 0x5f,
             0x5e, 0x54, 0x2d, 0x6a, 0x5b, 0xd1, 0x5c, 0xc9, 0x33, 0x21, 0x2c,
             0xe2, 0x68, 0xfc, 0xfd, 0x53, 0xee, 0x93, 0x58, 0xeb, 0x4e}};
        Condition const Preim6Cond{Type::preimageSha256,
                                   9,
                                   Preim6CondConditionFingerprint,
                                   std::bitset<5>{0}};
        std::array<std::uint8_t, 32> const Rsa7CondConditionFingerprint = {
            {0x6c, 0x7b, 0xea, 0x83, 0xa1, 0xf4, 0x82, 0x3d, 0x36, 0xe7, 0x6e,
             0xae, 0x1a, 0xbc, 0xa0, 0xba, 0x90, 0x3d, 0x96, 0xc1, 0xe6, 0xad,
             0x3a, 0x47, 0xa5, 0xcb, 0x88, 0xab, 0x3c, 0x5f, 0xcc, 0xd5}};
        Condition const Rsa7Cond{Type::rsaSha256,
                                 65536,
                                 Rsa7CondConditionFingerprint,
                                 std::bitset<5>{0}};
        std::array<std::uint8_t, 32> const Ed8CondConditionFingerprint = {
            {0xf1, 0x68, 0x96, 0xa6, 0x2e, 0xef, 0x7f, 0x47, 0x06, 0x51, 0x4c,
             0xc6, 0x7e, 0x24, 0xf7, 0x29, 0x84, 0x9c, 0xd6, 0xb0, 0xd9, 0x4b,
             0xd9, 0x0f, 0xc9, 0x34, 0x01, 0x9d, 0x92, 0xeb, 0xbc, 0x0a}};
        Condition const Ed8Cond{Type::ed25519Sha256,
                                131072,
                                Ed8CondConditionFingerprint,
                                std::bitset<5>{0}};
        auto const thresh2Msg = "P1P0abcdefghijklmnopqrstuvwxyz"s;
        std::array<std::uint8_t, 32> const Preim9CondConditionFingerprint = {
            {0x5d, 0xa0, 0x30, 0xef, 0xfd, 0xe1, 0x75, 0x11, 0x51, 0xe8, 0x5f,
             0x5e, 0x54, 0x2d, 0x6a, 0x5b, 0xd1, 0x5c, 0xc9, 0x33, 0x21, 0x2c,
             0xe2, 0x68, 0xfc, 0xfd, 0x53, 0xee, 0x93, 0x58, 0xeb, 0x4e}};
        Condition const Preim9Cond{Type::preimageSha256,
                                   9,
                                   Preim9CondConditionFingerprint,
                                   std::bitset<5>{0}};
        std::array<std::uint8_t, 32> const Rsa10CondConditionFingerprint = {
            {0x3c, 0x73, 0x38, 0xcf, 0x23, 0xc6, 0x31, 0x53, 0x28, 0xc4, 0x27,
             0xf8, 0x95, 0x87, 0x99, 0x83, 0x2d, 0x35, 0x3c, 0x03, 0x9b, 0xd1,
             0xff, 0xff, 0x2e, 0x53, 0x20, 0xe9, 0x5e, 0x62, 0xb9, 0xb7}};
        Condition const Rsa10Cond{Type::rsaSha256,
                                  65536,
                                  Rsa10CondConditionFingerprint,
                                  std::bitset<5>{0}};
        std::array<std::uint8_t, 32> const Ed11CondConditionFingerprint = {
            {0x41, 0x80, 0x08, 0xb2, 0x60, 0x74, 0x57, 0x6d, 0xac, 0xed, 0x74,
             0x7f, 0x54, 0xdb, 0x96, 0x18, 0x91, 0x06, 0x0a, 0x95, 0xa1, 0x49,
             0x17, 0xc7, 0x65, 0xe3, 0x94, 0xc8, 0x5e, 0x2c, 0x92, 0x20}};
        Condition const Ed11Cond{Type::ed25519Sha256,
                                 131072,
                                 Ed11CondConditionFingerprint,
                                 std::bitset<5>{0}};
        auto const prefix1Prefix = "P1"s;
        auto const prefix1Msg = "P0abcdefghijklmnopqrstuvwxyz"s;
        auto const prefix1MaxMsgLength = 28;
        auto const prefix0Prefix = "P0"s;
        auto const prefix0Msg = "abcdefghijklmnopqrstuvwxyz"s;
        auto const prefix0MaxMsgLength = 26;

        auto preim3 =
            std::make_unique<PreimageSha256>(makeSlice(preim3Preimage));
        auto preim5 =
            std::make_unique<PreimageSha256>(makeSlice(preim5Preimage));
        std::vector<std::unique_ptr<Fulfillment>> thresh4Subfulfillments;
        thresh4Subfulfillments.emplace_back(std::move(preim5));
        std::vector<Condition> thresh4Subconditions{
            {Preim6Cond, Rsa7Cond, Ed8Cond}};
        auto thresh4 = std::make_unique<ThresholdSha256>(
            std::move(thresh4Subfulfillments), std::move(thresh4Subconditions));
        std::vector<std::unique_ptr<Fulfillment>> thresh2Subfulfillments;
        thresh2Subfulfillments.emplace_back(std::move(preim3));
        thresh2Subfulfillments.emplace_back(std::move(thresh4));
        std::vector<Condition> thresh2Subconditions{
            {Preim9Cond, Rsa10Cond, Ed11Cond}};
        auto thresh2 = std::make_unique<ThresholdSha256>(
            std::move(thresh2Subfulfillments), std::move(thresh2Subconditions));
        auto prefix1 = std::make_unique<PrefixSha256>(
            makeSlice(prefix1Prefix), prefix1MaxMsgLength, std::move(thresh2));
        auto prefix0 = std::make_unique<PrefixSha256>(
            makeSlice(prefix0Prefix), prefix0MaxMsgLength, std::move(prefix1));
        {
            auto prefix0EncodedFulfillment =
                "\xa1\x82\x01\x36\x80\x02\x50\x30\x81\x01\x1a\xa2\x82\x01\x2b"
                "\xa1\x82\x01\x27\x80\x02\x50\x31\x81\x01\x1c\xa2\x82\x01\x1c"
                "\xa2\x82\x01\x18\xa0\x81\x9a\xa0\x0b\x80\x09\x49\x20\x61\x6d"
                "\x20\x72\x6f\x6f\x74\xa2\x81\x8a\xa0\x0d\xa0\x0b\x80\x09\x49"
                "\x20\x61\x6d\x20\x72\x6f\x6f\x74\xa1\x79\xa0\x25\x80\x20\x5d"
                "\xa0\x30\xef\xfd\xe1\x75\x11\x51\xe8\x5f\x5e\x54\x2d\x6a\x5b"
                "\xd1\x5c\xc9\x33\x21\x2c\xe2\x68\xfc\xfd\x53\xee\x93\x58\xeb"
                "\x4e\x81\x01\x09\xa3\x27\x80\x20\x6c\x7b\xea\x83\xa1\xf4\x82"
                "\x3d\x36\xe7\x6e\xae\x1a\xbc\xa0\xba\x90\x3d\x96\xc1\xe6\xad"
                "\x3a\x47\xa5\xcb\x88\xab\x3c\x5f\xcc\xd5\x81\x03\x01\x00\x00"
                "\xa4\x27\x80\x20\xf1\x68\x96\xa6\x2e\xef\x7f\x47\x06\x51\x4c"
                "\xc6\x7e\x24\xf7\x29\x84\x9c\xd6\xb0\xd9\x4b\xd9\x0f\xc9\x34"
                "\x01\x9d\x92\xeb\xbc\x0a\x81\x03\x02\x00\x00\xa1\x79\xa0\x25"
                "\x80\x20\x5d\xa0\x30\xef\xfd\xe1\x75\x11\x51\xe8\x5f\x5e\x54"
                "\x2d\x6a\x5b\xd1\x5c\xc9\x33\x21\x2c\xe2\x68\xfc\xfd\x53\xee"
                "\x93\x58\xeb\x4e\x81\x01\x09\xa3\x27\x80\x20\x3c\x73\x38\xcf"
                "\x23\xc6\x31\x53\x28\xc4\x27\xf8\x95\x87\x99\x83\x2d\x35\x3c"
                "\x03\x9b\xd1\xff\xff\x2e\x53\x20\xe9\x5e\x62\xb9\xb7\x81\x03"
                "\x01\x00\x00\xa4\x27\x80\x20\x41\x80\x08\xb2\x60\x74\x57\x6d"
                "\xac\xed\x74\x7f\x54\xdb\x96\x18\x91\x06\x0a\x95\xa1\x49\x17"
                "\xc7\x65\xe3\x94\xc8\x5e\x2c\x92\x20\x81\x03\x02\x00\x00"s;
            auto const prefix0EncodedCondition =
                "\xa1\x2b\x80\x20\x7b\x7c\x48\x90\x3f\x1b\x5e\x0f\x4d\x64\x09"
                "\x3d\x4b\xfd\x3e\x6f\xd5\x7d\xe7\x56\xeb\x14\x98\xdb\xc2\x65"
                "\xfb\x62\x22\x95\x43\x71\x81\x03\x04\x2c\x3a\x82\x02\x03\xb8"s;
            auto const prefix0EncodedFingerprint =
                "\x30\x36\x80\x02\x50\x30\x81\x01\x1a\xa2\x2d\xa1\x2b\x80\x20"
                "\x8c\xae\x47\xce\xb9\xc1\xd5\x3b\xa4\x9a\xc3\xce\x00\xde\x1a"
                "\x5e\xd6\x6f\x55\x5a\x7a\xfd\x73\xa0\xc1\x37\x8e\xdf\x98\x02"
                "\xd7\x27\x81\x03\x04\x28\x1e\x82\x02\x03\xb8"s;
            check(
                std::move(prefix0),
                prefix0Msg,
                std::move(prefix0EncodedFulfillment),
                prefix0EncodedCondition,
                prefix0EncodedFingerprint);
        }
    }

    void
    testPrefix6()
    {
        testcase("Prefix6");

        using namespace std::string_literals;
        using namespace ripple::cryptoconditions;

        // Fulfillment structure
        // * prefix0
        // ** prefix1
        // *** thresh2
        // **** Preim9Cond
        // **** Rsa10Cond
        // **** Ed11Cond
        // **** Thresh12Cond
        // **** preim3
        // **** thresh4
        // ***** Preim6Cond
        // ***** Rsa7Cond
        // ***** Ed8Cond
        // ***** preim5

        auto const preim3Preimage = "I am root"s;
        auto const preim3Msg = "P1P0abcdefghijklmnopqrstuvwxyz"s;
        auto const preim5Preimage = "I am root"s;
        auto const preim5Msg = "P1P0abcdefghijklmnopqrstuvwxyz"s;
        auto const thresh4Msg = "P1P0abcdefghijklmnopqrstuvwxyz"s;
        std::array<std::uint8_t, 32> const Preim6CondConditionFingerprint = {
            {0x5d, 0xa0, 0x30, 0xef, 0xfd, 0xe1, 0x75, 0x11, 0x51, 0xe8, 0x5f,
             0x5e, 0x54, 0x2d, 0x6a, 0x5b, 0xd1, 0x5c, 0xc9, 0x33, 0x21, 0x2c,
             0xe2, 0x68, 0xfc, 0xfd, 0x53, 0xee, 0x93, 0x58, 0xeb, 0x4e}};
        Condition const Preim6Cond{Type::preimageSha256,
                                   9,
                                   Preim6CondConditionFingerprint,
                                   std::bitset<5>{0}};
        std::array<std::uint8_t, 32> const Rsa7CondConditionFingerprint = {
            {0x6c, 0x7b, 0xea, 0x83, 0xa1, 0xf4, 0x82, 0x3d, 0x36, 0xe7, 0x6e,
             0xae, 0x1a, 0xbc, 0xa0, 0xba, 0x90, 0x3d, 0x96, 0xc1, 0xe6, 0xad,
             0x3a, 0x47, 0xa5, 0xcb, 0x88, 0xab, 0x3c, 0x5f, 0xcc, 0xd5}};
        Condition const Rsa7Cond{Type::rsaSha256,
                                 65536,
                                 Rsa7CondConditionFingerprint,
                                 std::bitset<5>{0}};
        std::array<std::uint8_t, 32> const Ed8CondConditionFingerprint = {
            {0xf1, 0x68, 0x96, 0xa6, 0x2e, 0xef, 0x7f, 0x47, 0x06, 0x51, 0x4c,
             0xc6, 0x7e, 0x24, 0xf7, 0x29, 0x84, 0x9c, 0xd6, 0xb0, 0xd9, 0x4b,
             0xd9, 0x0f, 0xc9, 0x34, 0x01, 0x9d, 0x92, 0xeb, 0xbc, 0x0a}};
        Condition const Ed8Cond{Type::ed25519Sha256,
                                131072,
                                Ed8CondConditionFingerprint,
                                std::bitset<5>{0}};
        auto const thresh2Msg = "P1P0abcdefghijklmnopqrstuvwxyz"s;
        std::array<std::uint8_t, 32> const Preim9CondConditionFingerprint = {
            {0x5d, 0xa0, 0x30, 0xef, 0xfd, 0xe1, 0x75, 0x11, 0x51, 0xe8, 0x5f,
             0x5e, 0x54, 0x2d, 0x6a, 0x5b, 0xd1, 0x5c, 0xc9, 0x33, 0x21, 0x2c,
             0xe2, 0x68, 0xfc, 0xfd, 0x53, 0xee, 0x93, 0x58, 0xeb, 0x4e}};
        Condition const Preim9Cond{Type::preimageSha256,
                                   9,
                                   Preim9CondConditionFingerprint,
                                   std::bitset<5>{0}};
        std::array<std::uint8_t, 32> const Rsa10CondConditionFingerprint = {
            {0x3c, 0x73, 0x38, 0xcf, 0x23, 0xc6, 0x31, 0x53, 0x28, 0xc4, 0x27,
             0xf8, 0x95, 0x87, 0x99, 0x83, 0x2d, 0x35, 0x3c, 0x03, 0x9b, 0xd1,
             0xff, 0xff, 0x2e, 0x53, 0x20, 0xe9, 0x5e, 0x62, 0xb9, 0xb7}};
        Condition const Rsa10Cond{Type::rsaSha256,
                                  65536,
                                  Rsa10CondConditionFingerprint,
                                  std::bitset<5>{0}};
        std::array<std::uint8_t, 32> const Ed11CondConditionFingerprint = {
            {0x41, 0x80, 0x08, 0xb2, 0x60, 0x74, 0x57, 0x6d, 0xac, 0xed, 0x74,
             0x7f, 0x54, 0xdb, 0x96, 0x18, 0x91, 0x06, 0x0a, 0x95, 0xa1, 0x49,
             0x17, 0xc7, 0x65, 0xe3, 0x94, 0xc8, 0x5e, 0x2c, 0x92, 0x20}};
        Condition const Ed11Cond{Type::ed25519Sha256,
                                 131072,
                                 Ed11CondConditionFingerprint,
                                 std::bitset<5>{0}};
        std::array<std::uint8_t, 32> const Thresh12CondConditionFingerprint = {
            {0x2b, 0x40, 0xdc, 0x99, 0x90, 0xf5, 0xc1, 0xc1, 0x79, 0x66, 0x76,
             0xa2, 0xc6, 0x2e, 0xb7, 0x46, 0xeb, 0x34, 0xa9, 0x67, 0x07, 0xb2,
             0xe3, 0xd4, 0x31, 0x8e, 0x61, 0xbf, 0x80, 0x1a, 0x20, 0x4a}};
        Condition const Thresh12Cond{Type::thresholdSha256,
                                     135168,
                                     Thresh12CondConditionFingerprint,
                                     std::bitset<5>{25}};
        auto const prefix1Prefix = "P1"s;
        auto const prefix1Msg = "P0abcdefghijklmnopqrstuvwxyz"s;
        auto const prefix1MaxMsgLength = 28;
        auto const prefix0Prefix = "P0"s;
        auto const prefix0Msg = "abcdefghijklmnopqrstuvwxyz"s;
        auto const prefix0MaxMsgLength = 26;

        auto preim3 =
            std::make_unique<PreimageSha256>(makeSlice(preim3Preimage));
        auto preim5 =
            std::make_unique<PreimageSha256>(makeSlice(preim5Preimage));
        std::vector<std::unique_ptr<Fulfillment>> thresh4Subfulfillments;
        thresh4Subfulfillments.emplace_back(std::move(preim5));
        std::vector<Condition> thresh4Subconditions{
            {Preim6Cond, Rsa7Cond, Ed8Cond}};
        auto thresh4 = std::make_unique<ThresholdSha256>(
            std::move(thresh4Subfulfillments), std::move(thresh4Subconditions));
        std::vector<std::unique_ptr<Fulfillment>> thresh2Subfulfillments;
        thresh2Subfulfillments.emplace_back(std::move(preim3));
        thresh2Subfulfillments.emplace_back(std::move(thresh4));
        std::vector<Condition> thresh2Subconditions{
            {Preim9Cond, Rsa10Cond, Ed11Cond, Thresh12Cond}};
        auto thresh2 = std::make_unique<ThresholdSha256>(
            std::move(thresh2Subfulfillments), std::move(thresh2Subconditions));
        auto prefix1 = std::make_unique<PrefixSha256>(
            makeSlice(prefix1Prefix), prefix1MaxMsgLength, std::move(thresh2));
        auto prefix0 = std::make_unique<PrefixSha256>(
            makeSlice(prefix0Prefix), prefix0MaxMsgLength, std::move(prefix1));
        {
            auto prefix0EncodedFulfillment =
                "\xa1\x82\x01\x64\x80\x02\x50\x30\x81\x01\x1a\xa2\x82\x01\x59"
                "\xa1\x82\x01\x55\x80\x02\x50\x31\x81\x01\x1c\xa2\x82\x01\x4a"
                "\xa2\x82\x01\x46\xa0\x81\x9a\xa0\x0b\x80\x09\x49\x20\x61\x6d"
                "\x20\x72\x6f\x6f\x74\xa2\x81\x8a\xa0\x0d\xa0\x0b\x80\x09\x49"
                "\x20\x61\x6d\x20\x72\x6f\x6f\x74\xa1\x79\xa0\x25\x80\x20\x5d"
                "\xa0\x30\xef\xfd\xe1\x75\x11\x51\xe8\x5f\x5e\x54\x2d\x6a\x5b"
                "\xd1\x5c\xc9\x33\x21\x2c\xe2\x68\xfc\xfd\x53\xee\x93\x58\xeb"
                "\x4e\x81\x01\x09\xa3\x27\x80\x20\x6c\x7b\xea\x83\xa1\xf4\x82"
                "\x3d\x36\xe7\x6e\xae\x1a\xbc\xa0\xba\x90\x3d\x96\xc1\xe6\xad"
                "\x3a\x47\xa5\xcb\x88\xab\x3c\x5f\xcc\xd5\x81\x03\x01\x00\x00"
                "\xa4\x27\x80\x20\xf1\x68\x96\xa6\x2e\xef\x7f\x47\x06\x51\x4c"
                "\xc6\x7e\x24\xf7\x29\x84\x9c\xd6\xb0\xd9\x4b\xd9\x0f\xc9\x34"
                "\x01\x9d\x92\xeb\xbc\x0a\x81\x03\x02\x00\x00\xa1\x81\xa6\xa0"
                "\x25\x80\x20\x5d\xa0\x30\xef\xfd\xe1\x75\x11\x51\xe8\x5f\x5e"
                "\x54\x2d\x6a\x5b\xd1\x5c\xc9\x33\x21\x2c\xe2\x68\xfc\xfd\x53"
                "\xee\x93\x58\xeb\x4e\x81\x01\x09\xa2\x2b\x80\x20\x2b\x40\xdc"
                "\x99\x90\xf5\xc1\xc1\x79\x66\x76\xa2\xc6\x2e\xb7\x46\xeb\x34"
                "\xa9\x67\x07\xb2\xe3\xd4\x31\x8e\x61\xbf\x80\x1a\x20\x4a\x81"
                "\x03\x02\x10\x00\x82\x02\x03\x98\xa3\x27\x80\x20\x3c\x73\x38"
                "\xcf\x23\xc6\x31\x53\x28\xc4\x27\xf8\x95\x87\x99\x83\x2d\x35"
                "\x3c\x03\x9b\xd1\xff\xff\x2e\x53\x20\xe9\x5e\x62\xb9\xb7\x81"
                "\x03\x01\x00\x00\xa4\x27\x80\x20\x41\x80\x08\xb2\x60\x74\x57"
                "\x6d\xac\xed\x74\x7f\x54\xdb\x96\x18\x91\x06\x0a\x95\xa1\x49"
                "\x17\xc7\x65\xe3\x94\xc8\x5e\x2c\x92\x20\x81\x03\x02\x00\x00"s;
            auto const prefix0EncodedCondition =
                "\xa1\x2b\x80\x20\x13\xbd\xba\xaf\x44\x4e\x80\xf6\xc1\xe3\xc4"
                "\xd9\x80\x0e\x50\xa7\xa8\x01\xe5\xcc\x55\xf4\xa1\x5e\x3b\x6c"
                "\xe5\x67\x6f\x53\xc5\x8c\x81\x03\x04\x40\x3a\x82\x02\x03\xb8"s;
            auto const prefix0EncodedFingerprint =
                "\x30\x36\x80\x02\x50\x30\x81\x01\x1a\xa2\x2d\xa1\x2b\x80\x20"
                "\x73\x58\x3e\xdc\xf8\xd0\x7d\x80\xea\x8b\xa9\x1e\xc2\x36\x57"
                "\xd5\xbc\x07\xdc\x2d\x6c\xfc\x33\xcb\xc5\x98\x37\xf9\x40\xa5"
                "\xdb\x2f\x81\x03\x04\x3c\x1e\x82\x02\x03\xb8"s;
            check(
                std::move(prefix0),
                prefix0Msg,
                std::move(prefix0EncodedFulfillment),
                prefix0EncodedCondition,
                prefix0EncodedFingerprint);
        }
    }

    void
    testPrefix7()
    {
        testcase("Prefix7");

        using namespace std::string_literals;
        using namespace ripple::cryptoconditions;

        // Fulfillment structure
        // * prefix0
        // ** rsa1

        auto const rsa1Msg = "P0abcdefghijklmnopqrstuvwxyz"s;
        std::array<std::uint8_t, 256> const rsa1PublicKey{
            {0xc4, 0x1b, 0xc9, 0x7a, 0x96, 0x81, 0xce, 0x2e, 0xf2, 0x53, 0xc0,
             0xd4, 0xa8, 0xb9, 0xa8, 0x12, 0x92, 0x45, 0x06, 0xf1, 0xf4, 0xcd,
             0x27, 0x7d, 0xff, 0xc1, 0x65, 0x75, 0xae, 0xb7, 0xc4, 0x98, 0x53,
             0xd8, 0xfa, 0x6d, 0x86, 0x63, 0xd8, 0x4e, 0xf5, 0x20, 0xe2, 0x9e,
             0x96, 0x04, 0x36, 0x0c, 0x3f, 0xac, 0x7d, 0x09, 0x42, 0x11, 0x13,
             0x30, 0x2d, 0x7f, 0x60, 0x2c, 0xec, 0x2a, 0x34, 0xc3, 0xd8, 0xba,
             0x6d, 0x14, 0x75, 0x28, 0x56, 0xdc, 0x73, 0x6c, 0xb7, 0xd6, 0xba,
             0x8a, 0xa3, 0x9e, 0x09, 0x0e, 0xa4, 0xf3, 0x6b, 0x5d, 0x12, 0xc6,
             0xe4, 0xdd, 0x8c, 0xb1, 0x98, 0xcd, 0xde, 0xca, 0xad, 0xff, 0x86,
             0xb6, 0x06, 0x25, 0x9b, 0x71, 0x84, 0xa0, 0x9b, 0x19, 0x14, 0x88,
             0xd0, 0xc7, 0x55, 0x99, 0xe0, 0x1e, 0x0e, 0x39, 0x67, 0x74, 0xdf,
             0xf6, 0x29, 0xfa, 0x92, 0xb6, 0xbb, 0xe6, 0xe1, 0x1c, 0xd9, 0xee,
             0x65, 0xda, 0x13, 0xec, 0x50, 0x6a, 0x11, 0x7e, 0xae, 0xb4, 0xac,
             0x85, 0xa5, 0xc7, 0xcb, 0x43, 0x42, 0x36, 0x73, 0x34, 0x31, 0xb6,
             0x0b, 0x5e, 0xf0, 0x9e, 0x80, 0x49, 0xab, 0xc7, 0x79, 0xc5, 0xa7,
             0xe3, 0x16, 0x35, 0x3a, 0x48, 0xe6, 0xc0, 0x69, 0xe2, 0x70, 0x30,
             0x20, 0x74, 0x47, 0x3e, 0x3a, 0x51, 0x01, 0x21, 0x60, 0x15, 0x53,
             0x40, 0x68, 0x6e, 0xe7, 0x9f, 0x73, 0xb6, 0x98, 0x3b, 0x6e, 0x50,
             0xb8, 0xb2, 0xe7, 0x42, 0x90, 0x77, 0x61, 0xd4, 0x22, 0x6c, 0x0b,
             0x3d, 0x66, 0xe0, 0x1b, 0x7d, 0xb0, 0xa1, 0xa9, 0xa9, 0xea, 0x0e,
             0xf2, 0xab, 0xe0, 0x32, 0xf4, 0x49, 0x44, 0xcb, 0xae, 0x60, 0x1d,
             0xe1, 0xac, 0xd3, 0x34, 0x2b, 0x03, 0x97, 0x98, 0x2d, 0xda, 0xf8,
             0xe8, 0x0b, 0x81, 0x94, 0x98, 0x3a, 0xbd, 0x6d, 0x17, 0x18, 0x42,
             0x58, 0x0c, 0xa3}};
        std::array<std::uint8_t, 256> const rsa1Sig{
            {0x4e, 0xf6, 0x2b, 0x2c, 0x0a, 0x42, 0x8c, 0xdd, 0xd3, 0x9d, 0xd6,
             0xb0, 0x4f, 0xa3, 0x54, 0x77, 0xc6, 0x51, 0xaa, 0x17, 0x8a, 0xc8,
             0x19, 0x41, 0xf8, 0xd3, 0x34, 0x28, 0xcc, 0xef, 0x39, 0xc8, 0x0d,
             0x31, 0x44, 0xba, 0x1b, 0x5e, 0x15, 0x8f, 0x63, 0x51, 0x73, 0x35,
             0x74, 0x97, 0x6e, 0x67, 0x94, 0xb1, 0x53, 0x20, 0x1b, 0x09, 0xf2,
             0xd2, 0xaf, 0x74, 0xc9, 0x6b, 0xab, 0x2d, 0x65, 0xee, 0xfd, 0x78,
             0xbe, 0x5b, 0xff, 0xf6, 0x34, 0x35, 0xc2, 0x3c, 0x08, 0xae, 0x5b,
             0x36, 0x22, 0x98, 0x5b, 0x0f, 0xf1, 0xe9, 0x8f, 0x6f, 0xea, 0x3d,
             0x00, 0xa1, 0x0f, 0x08, 0x41, 0x06, 0x67, 0x4a, 0x9d, 0x46, 0xf7,
             0x26, 0x88, 0x61, 0xec, 0x65, 0xf9, 0x8f, 0xe7, 0x5a, 0x07, 0x89,
             0x44, 0xe7, 0x43, 0x3a, 0x54, 0x4c, 0x5a, 0x0f, 0x53, 0x6b, 0x4a,
             0x40, 0xf8, 0xb0, 0x7d, 0xe9, 0x06, 0x5c, 0x7c, 0xfb, 0xcb, 0xf4,
             0x10, 0x95, 0x5c, 0xa0, 0xea, 0x5e, 0x47, 0xf6, 0x6f, 0x66, 0xfc,
             0xca, 0x0c, 0x8c, 0x22, 0x96, 0x29, 0x5c, 0x7e, 0xdf, 0xeb, 0x60,
             0x5c, 0x22, 0xa1, 0x12, 0x7f, 0x85, 0x12, 0xf2, 0x40, 0x18, 0x61,
             0x44, 0x7a, 0xb7, 0xa1, 0x09, 0xe4, 0x94, 0x9a, 0x01, 0x73, 0xe0,
             0xc9, 0x86, 0x6f, 0x8d, 0xf4, 0xca, 0x9c, 0xb6, 0xc5, 0xe4, 0xf5,
             0x40, 0x6a, 0xa1, 0x64, 0x3c, 0xba, 0xfb, 0x72, 0x83, 0xbd, 0xe9,
             0xf3, 0x4c, 0x49, 0xb9, 0xbb, 0x64, 0xf2, 0xf8, 0x87, 0x3f, 0x55,
             0x3a, 0xc8, 0xcf, 0x18, 0x2e, 0xaa, 0x6c, 0xa6, 0x91, 0x48, 0x94,
             0x2f, 0xf3, 0xca, 0x7c, 0x11, 0x55, 0xce, 0xf3, 0x97, 0xf8, 0xb2,
             0xa5, 0x43, 0x5d, 0xc8, 0xc6, 0x37, 0x14, 0xaa, 0x5b, 0x59, 0x9d,
             0x73, 0x52, 0x05, 0x9e, 0x36, 0xa1, 0x92, 0x4c, 0x95, 0x4c, 0x38,
             0xc4, 0xae, 0x72}};
        auto const prefix0Prefix = "P0"s;
        auto const prefix0Msg = "abcdefghijklmnopqrstuvwxyz"s;
        auto const prefix0MaxMsgLength = 26;

        auto rsa1 = std::make_unique<RsaSha256>(
            makeSlice(rsa1PublicKey), makeSlice(rsa1Sig));
        auto prefix0 = std::make_unique<PrefixSha256>(
            makeSlice(prefix0Prefix), prefix0MaxMsgLength, std::move(rsa1));
        {
            auto prefix0EncodedFulfillment =
                "\xa1\x82\x02\x17\x80\x02\x50\x30\x81\x01\x1a\xa2\x82\x02\x0c"
                "\xa3\x82\x02\x08\x80\x82\x01\x00\xc4\x1b\xc9\x7a\x96\x81\xce"
                "\x2e\xf2\x53\xc0\xd4\xa8\xb9\xa8\x12\x92\x45\x06\xf1\xf4\xcd"
                "\x27\x7d\xff\xc1\x65\x75\xae\xb7\xc4\x98\x53\xd8\xfa\x6d\x86"
                "\x63\xd8\x4e\xf5\x20\xe2\x9e\x96\x04\x36\x0c\x3f\xac\x7d\x09"
                "\x42\x11\x13\x30\x2d\x7f\x60\x2c\xec\x2a\x34\xc3\xd8\xba\x6d"
                "\x14\x75\x28\x56\xdc\x73\x6c\xb7\xd6\xba\x8a\xa3\x9e\x09\x0e"
                "\xa4\xf3\x6b\x5d\x12\xc6\xe4\xdd\x8c\xb1\x98\xcd\xde\xca\xad"
                "\xff\x86\xb6\x06\x25\x9b\x71\x84\xa0\x9b\x19\x14\x88\xd0\xc7"
                "\x55\x99\xe0\x1e\x0e\x39\x67\x74\xdf\xf6\x29\xfa\x92\xb6\xbb"
                "\xe6\xe1\x1c\xd9\xee\x65\xda\x13\xec\x50\x6a\x11\x7e\xae\xb4"
                "\xac\x85\xa5\xc7\xcb\x43\x42\x36\x73\x34\x31\xb6\x0b\x5e\xf0"
                "\x9e\x80\x49\xab\xc7\x79\xc5\xa7\xe3\x16\x35\x3a\x48\xe6\xc0"
                "\x69\xe2\x70\x30\x20\x74\x47\x3e\x3a\x51\x01\x21\x60\x15\x53"
                "\x40\x68\x6e\xe7\x9f\x73\xb6\x98\x3b\x6e\x50\xb8\xb2\xe7\x42"
                "\x90\x77\x61\xd4\x22\x6c\x0b\x3d\x66\xe0\x1b\x7d\xb0\xa1\xa9"
                "\xa9\xea\x0e\xf2\xab\xe0\x32\xf4\x49\x44\xcb\xae\x60\x1d\xe1"
                "\xac\xd3\x34\x2b\x03\x97\x98\x2d\xda\xf8\xe8\x0b\x81\x94\x98"
                "\x3a\xbd\x6d\x17\x18\x42\x58\x0c\xa3\x81\x82\x01\x00\x4e\xf6"
                "\x2b\x2c\x0a\x42\x8c\xdd\xd3\x9d\xd6\xb0\x4f\xa3\x54\x77\xc6"
                "\x51\xaa\x17\x8a\xc8\x19\x41\xf8\xd3\x34\x28\xcc\xef\x39\xc8"
                "\x0d\x31\x44\xba\x1b\x5e\x15\x8f\x63\x51\x73\x35\x74\x97\x6e"
                "\x67\x94\xb1\x53\x20\x1b\x09\xf2\xd2\xaf\x74\xc9\x6b\xab\x2d"
                "\x65\xee\xfd\x78\xbe\x5b\xff\xf6\x34\x35\xc2\x3c\x08\xae\x5b"
                "\x36\x22\x98\x5b\x0f\xf1\xe9\x8f\x6f\xea\x3d\x00\xa1\x0f\x08"
                "\x41\x06\x67\x4a\x9d\x46\xf7\x26\x88\x61\xec\x65\xf9\x8f\xe7"
                "\x5a\x07\x89\x44\xe7\x43\x3a\x54\x4c\x5a\x0f\x53\x6b\x4a\x40"
                "\xf8\xb0\x7d\xe9\x06\x5c\x7c\xfb\xcb\xf4\x10\x95\x5c\xa0\xea"
                "\x5e\x47\xf6\x6f\x66\xfc\xca\x0c\x8c\x22\x96\x29\x5c\x7e\xdf"
                "\xeb\x60\x5c\x22\xa1\x12\x7f\x85\x12\xf2\x40\x18\x61\x44\x7a"
                "\xb7\xa1\x09\xe4\x94\x9a\x01\x73\xe0\xc9\x86\x6f\x8d\xf4\xca"
                "\x9c\xb6\xc5\xe4\xf5\x40\x6a\xa1\x64\x3c\xba\xfb\x72\x83\xbd"
                "\xe9\xf3\x4c\x49\xb9\xbb\x64\xf2\xf8\x87\x3f\x55\x3a\xc8\xcf"
                "\x18\x2e\xaa\x6c\xa6\x91\x48\x94\x2f\xf3\xca\x7c\x11\x55\xce"
                "\xf3\x97\xf8\xb2\xa5\x43\x5d\xc8\xc6\x37\x14\xaa\x5b\x59\x9d"
                "\x73\x52\x05\x9e\x36\xa1\x92\x4c\x95\x4c\x38\xc4\xae\x72"s;
            auto const prefix0EncodedCondition =
                "\xa1\x2b\x80\x20\xcf\x47\x4b\x1f\xf9\xde\xac\x78\x72\xcc\x38"
                "\x0d\x3f\x7f\xe2\xe2\xe2\x49\x1f\x65\xa0\xd2\xad\xfb\x72\x1a"
                "\xc5\x19\xb0\x91\x5d\x55\x81\x03\x01\x04\x1c\x82\x02\x04\x10"s;
            auto const prefix0EncodedFingerprint =
                "\x30\x32\x80\x02\x50\x30\x81\x01\x1a\xa2\x29\xa3\x27\x80\x20"
                "\x23\xc9\xfd\x99\xdc\xf3\x74\x06\xe3\x11\x57\x5e\x7a\x90\x4b"
                "\x0d\x0c\x36\xa7\x8d\x25\xbc\xd5\x5f\x0d\x1f\x25\x08\x2e\x4f"
                "\xca\x1a\x81\x03\x01\x00\x00"s;
            check(
                std::move(prefix0),
                prefix0Msg,
                std::move(prefix0EncodedFulfillment),
                prefix0EncodedCondition,
                prefix0EncodedFingerprint);
        }
    }

    void
    testPrefix8()
    {
        testcase("Prefix8");

        using namespace std::string_literals;
        using namespace ripple::cryptoconditions;

        // Fulfillment structure
        // * prefix0
        // ** prefix1
        // *** rsa2

        auto const rsa2Msg = "P1P0abcdefghijklmnopqrstuvwxyz"s;
        std::array<std::uint8_t, 256> const rsa2PublicKey{
            {0xba, 0x2c, 0x3b, 0x50, 0xb6, 0xbf, 0xf9, 0x0f, 0x1d, 0xd7, 0x32,
             0x4c, 0x01, 0x5f, 0xff, 0x2f, 0x2a, 0xf6, 0x33, 0xd0, 0xfb, 0xea,
             0x1f, 0xa4, 0xf2, 0x2d, 0x22, 0x8a, 0x19, 0x95, 0xa9, 0x17, 0xb7,
             0x4f, 0x17, 0xcf, 0x55, 0xcd, 0x1a, 0x3a, 0x5f, 0x07, 0x73, 0xcc,
             0xaa, 0x21, 0x70, 0x64, 0xb3, 0xa0, 0xf4, 0xb7, 0x30, 0xa3, 0x82,
             0x37, 0x93, 0xc6, 0x59, 0xde, 0x1b, 0xa1, 0x16, 0x90, 0x5a, 0x1a,
             0xf6, 0x73, 0xab, 0x92, 0xc8, 0x2f, 0xf4, 0x6f, 0x5c, 0xf2, 0x22,
             0x1d, 0x30, 0xf8, 0x03, 0xd8, 0x9b, 0x5f, 0x73, 0x72, 0x8e, 0x5f,
             0xd5, 0x37, 0x4b, 0x43, 0xda, 0xfe, 0x84, 0x21, 0x67, 0xe8, 0xe3,
             0xd7, 0x91, 0x3f, 0x24, 0x1d, 0xfb, 0x1f, 0x12, 0x6e, 0xcb, 0xfc,
             0xb7, 0x5b, 0x0a, 0x35, 0x73, 0x3b, 0xce, 0x44, 0x34, 0x8e, 0xcd,
             0x53, 0xa4, 0xcf, 0xa7, 0x63, 0x73, 0xcd, 0x31, 0x0f, 0xe0, 0x75,
             0x8d, 0xe4, 0xa9, 0xdc, 0xfe, 0xf0, 0xc9, 0x3d, 0x26, 0xaf, 0xbf,
             0x7b, 0x0f, 0x0e, 0x17, 0xb9, 0xd0, 0x4a, 0x32, 0x80, 0x64, 0x6b,
             0x54, 0x73, 0x5a, 0x50, 0xc7, 0x31, 0x59, 0xf9, 0x73, 0x72, 0xa5,
             0x79, 0xba, 0xdb, 0xa1, 0x14, 0x8d, 0x77, 0x67, 0x3e, 0xc0, 0x5b,
             0xec, 0x6f, 0x0b, 0xf7, 0xc5, 0xee, 0x5a, 0xa6, 0x8d, 0x49, 0x63,
             0x81, 0xbb, 0xd1, 0xf9, 0x9e, 0xbb, 0xed, 0xb2, 0xa9, 0x18, 0x60,
             0xa7, 0xee, 0xeb, 0x30, 0xa1, 0x92, 0x93, 0xe8, 0xd8, 0x34, 0x9e,
             0xac, 0xd6, 0x23, 0xfc, 0x7f, 0xcb, 0xe7, 0xfe, 0xa7, 0xe6, 0x42,
             0xac, 0x77, 0x11, 0xc0, 0x67, 0x77, 0xd1, 0xaa, 0x5e, 0xed, 0x3b,
             0xd5, 0xa5, 0x8d, 0x34, 0x7c, 0xd9, 0x57, 0x44, 0xa7, 0xc5, 0x44,
             0x2e, 0x1e, 0xe7, 0x63, 0xd8, 0x53, 0x1b, 0x9a, 0xd9, 0x67, 0x02,
             0x13, 0x32, 0x61}};
        std::array<std::uint8_t, 256> const rsa2Sig{
            {0xb5, 0x9a, 0x0e, 0xfd, 0xb4, 0x7c, 0xe4, 0xa5, 0x43, 0xf5, 0xc2,
             0x03, 0x91, 0x13, 0xef, 0xcd, 0x7f, 0xcb, 0x41, 0xb0, 0xf3, 0x2a,
             0x08, 0xcf, 0x87, 0xd3, 0xf2, 0x20, 0x98, 0x9d, 0x3c, 0x4a, 0x4c,
             0xc2, 0x26, 0xce, 0xb6, 0xf7, 0x60, 0xe1, 0xd2, 0xcd, 0xf3, 0xed,
             0x50, 0x65, 0xa5, 0x72, 0x64, 0x76, 0x1f, 0xd5, 0x79, 0x74, 0x2b,
             0xe7, 0x87, 0x4b, 0x64, 0x36, 0xae, 0x87, 0x57, 0x2c, 0x7c, 0x46,
             0xb1, 0xb6, 0x62, 0xac, 0xda, 0x4e, 0x34, 0xb0, 0x10, 0xe7, 0xc9,
             0x0c, 0x66, 0xb1, 0xd9, 0x39, 0xdc, 0xc5, 0x29, 0x06, 0x50, 0x3d,
             0xda, 0x9f, 0x34, 0xa1, 0xd9, 0xca, 0xbc, 0x52, 0xdc, 0x4c, 0x70,
             0x87, 0x97, 0xe8, 0x96, 0xe0, 0x0d, 0x19, 0x30, 0x2b, 0x1a, 0x6c,
             0x96, 0x39, 0x35, 0x51, 0xcf, 0x9b, 0xa8, 0xa2, 0xc6, 0xf1, 0x3d,
             0x15, 0x30, 0x63, 0xef, 0x52, 0x10, 0x58, 0xe8, 0x87, 0x67, 0xe4,
             0x29, 0xe6, 0x94, 0x75, 0xba, 0x39, 0x01, 0x0c, 0x80, 0x44, 0x85,
             0x2a, 0x1a, 0x3d, 0x18, 0x40, 0x9d, 0x62, 0x80, 0x85, 0xd6, 0x6e,
             0x08, 0x35, 0xdf, 0xf6, 0x5e, 0x29, 0x0c, 0x7a, 0x62, 0xe2, 0x93,
             0x8f, 0xb9, 0xf9, 0xd0, 0xb9, 0x89, 0xf0, 0x1d, 0x79, 0xd0, 0x52,
             0xdc, 0xf4, 0x7a, 0x1f, 0xe5, 0xb9, 0x38, 0x7d, 0xee, 0x5d, 0x3a,
             0x48, 0xe7, 0x51, 0x71, 0x84, 0xb5, 0x08, 0xf2, 0xbd, 0x53, 0x68,
             0xd9, 0x38, 0x57, 0x30, 0xf5, 0xf5, 0x24, 0x22, 0x3b, 0x59, 0xda,
             0xe4, 0x44, 0x60, 0xf1, 0x76, 0x4e, 0xef, 0xd9, 0x36, 0x63, 0x25,
             0xf1, 0xf5, 0xb3, 0x80, 0xd2, 0x85, 0x90, 0xc8, 0x9a, 0x90, 0xc7,
             0x58, 0x0d, 0x2e, 0x91, 0xf4, 0xf8, 0xfc, 0x54, 0xc6, 0x75, 0x2e,
             0x52, 0xdd, 0x81, 0x43, 0xed, 0xe6, 0x71, 0x9b, 0xa7, 0x1a, 0xb5,
             0xa7, 0x41, 0xc7}};
        auto const prefix1Prefix = "P1"s;
        auto const prefix1Msg = "P0abcdefghijklmnopqrstuvwxyz"s;
        auto const prefix1MaxMsgLength = 28;
        auto const prefix0Prefix = "P0"s;
        auto const prefix0Msg = "abcdefghijklmnopqrstuvwxyz"s;
        auto const prefix0MaxMsgLength = 26;

        auto rsa2 = std::make_unique<RsaSha256>(
            makeSlice(rsa2PublicKey), makeSlice(rsa2Sig));
        auto prefix1 = std::make_unique<PrefixSha256>(
            makeSlice(prefix1Prefix), prefix1MaxMsgLength, std::move(rsa2));
        auto prefix0 = std::make_unique<PrefixSha256>(
            makeSlice(prefix0Prefix), prefix0MaxMsgLength, std::move(prefix1));
        {
            auto prefix0EncodedFulfillment =
                "\xa1\x82\x02\x26\x80\x02\x50\x30\x81\x01\x1a\xa2\x82\x02\x1b"
                "\xa1\x82\x02\x17\x80\x02\x50\x31\x81\x01\x1c\xa2\x82\x02\x0c"
                "\xa3\x82\x02\x08\x80\x82\x01\x00\xba\x2c\x3b\x50\xb6\xbf\xf9"
                "\x0f\x1d\xd7\x32\x4c\x01\x5f\xff\x2f\x2a\xf6\x33\xd0\xfb\xea"
                "\x1f\xa4\xf2\x2d\x22\x8a\x19\x95\xa9\x17\xb7\x4f\x17\xcf\x55"
                "\xcd\x1a\x3a\x5f\x07\x73\xcc\xaa\x21\x70\x64\xb3\xa0\xf4\xb7"
                "\x30\xa3\x82\x37\x93\xc6\x59\xde\x1b\xa1\x16\x90\x5a\x1a\xf6"
                "\x73\xab\x92\xc8\x2f\xf4\x6f\x5c\xf2\x22\x1d\x30\xf8\x03\xd8"
                "\x9b\x5f\x73\x72\x8e\x5f\xd5\x37\x4b\x43\xda\xfe\x84\x21\x67"
                "\xe8\xe3\xd7\x91\x3f\x24\x1d\xfb\x1f\x12\x6e\xcb\xfc\xb7\x5b"
                "\x0a\x35\x73\x3b\xce\x44\x34\x8e\xcd\x53\xa4\xcf\xa7\x63\x73"
                "\xcd\x31\x0f\xe0\x75\x8d\xe4\xa9\xdc\xfe\xf0\xc9\x3d\x26\xaf"
                "\xbf\x7b\x0f\x0e\x17\xb9\xd0\x4a\x32\x80\x64\x6b\x54\x73\x5a"
                "\x50\xc7\x31\x59\xf9\x73\x72\xa5\x79\xba\xdb\xa1\x14\x8d\x77"
                "\x67\x3e\xc0\x5b\xec\x6f\x0b\xf7\xc5\xee\x5a\xa6\x8d\x49\x63"
                "\x81\xbb\xd1\xf9\x9e\xbb\xed\xb2\xa9\x18\x60\xa7\xee\xeb\x30"
                "\xa1\x92\x93\xe8\xd8\x34\x9e\xac\xd6\x23\xfc\x7f\xcb\xe7\xfe"
                "\xa7\xe6\x42\xac\x77\x11\xc0\x67\x77\xd1\xaa\x5e\xed\x3b\xd5"
                "\xa5\x8d\x34\x7c\xd9\x57\x44\xa7\xc5\x44\x2e\x1e\xe7\x63\xd8"
                "\x53\x1b\x9a\xd9\x67\x02\x13\x32\x61\x81\x82\x01\x00\xb5\x9a"
                "\x0e\xfd\xb4\x7c\xe4\xa5\x43\xf5\xc2\x03\x91\x13\xef\xcd\x7f"
                "\xcb\x41\xb0\xf3\x2a\x08\xcf\x87\xd3\xf2\x20\x98\x9d\x3c\x4a"
                "\x4c\xc2\x26\xce\xb6\xf7\x60\xe1\xd2\xcd\xf3\xed\x50\x65\xa5"
                "\x72\x64\x76\x1f\xd5\x79\x74\x2b\xe7\x87\x4b\x64\x36\xae\x87"
                "\x57\x2c\x7c\x46\xb1\xb6\x62\xac\xda\x4e\x34\xb0\x10\xe7\xc9"
                "\x0c\x66\xb1\xd9\x39\xdc\xc5\x29\x06\x50\x3d\xda\x9f\x34\xa1"
                "\xd9\xca\xbc\x52\xdc\x4c\x70\x87\x97\xe8\x96\xe0\x0d\x19\x30"
                "\x2b\x1a\x6c\x96\x39\x35\x51\xcf\x9b\xa8\xa2\xc6\xf1\x3d\x15"
                "\x30\x63\xef\x52\x10\x58\xe8\x87\x67\xe4\x29\xe6\x94\x75\xba"
                "\x39\x01\x0c\x80\x44\x85\x2a\x1a\x3d\x18\x40\x9d\x62\x80\x85"
                "\xd6\x6e\x08\x35\xdf\xf6\x5e\x29\x0c\x7a\x62\xe2\x93\x8f\xb9"
                "\xf9\xd0\xb9\x89\xf0\x1d\x79\xd0\x52\xdc\xf4\x7a\x1f\xe5\xb9"
                "\x38\x7d\xee\x5d\x3a\x48\xe7\x51\x71\x84\xb5\x08\xf2\xbd\x53"
                "\x68\xd9\x38\x57\x30\xf5\xf5\x24\x22\x3b\x59\xda\xe4\x44\x60"
                "\xf1\x76\x4e\xef\xd9\x36\x63\x25\xf1\xf5\xb3\x80\xd2\x85\x90"
                "\xc8\x9a\x90\xc7\x58\x0d\x2e\x91\xf4\xf8\xfc\x54\xc6\x75\x2e"
                "\x52\xdd\x81\x43\xed\xe6\x71\x9b\xa7\x1a\xb5\xa7\x41\xc7"s;
            auto const prefix0EncodedCondition =
                "\xa1\x2b\x80\x20\x7b\xb9\x5c\x7b\xbd\xcc\xb0\xce\xd7\xa0\xb6"
                "\x77\x4d\x69\xa8\x33\x03\xd2\xaf\xaa\xd4\x28\xcc\xfc\x34\xe5"
                "\xd0\x27\x52\xd8\x9f\xb6\x81\x03\x01\x08\x3a\x82\x02\x04\x10"s;
            auto const prefix0EncodedFingerprint =
                "\x30\x36\x80\x02\x50\x30\x81\x01\x1a\xa2\x2d\xa1\x2b\x80\x20"
                "\x2f\x18\x13\x7f\xfa\x8a\x0e\x82\x17\x17\x42\x75\x2c\x21\x9e"
                "\x65\x1d\xae\x69\x15\xd2\x7d\xf2\xe5\xff\x48\xde\x5a\x7a\xed"
                "\x7b\x64\x81\x03\x01\x04\x1e\x82\x02\x04\x10"s;
            check(
                std::move(prefix0),
                prefix0Msg,
                std::move(prefix0EncodedFulfillment),
                prefix0EncodedCondition,
                prefix0EncodedFingerprint);
        }
    }

    void
    testPrefix9()
    {
        testcase("Prefix9");

        using namespace std::string_literals;
        using namespace ripple::cryptoconditions;

        // Fulfillment structure
        // * prefix0
        // ** prefix1
        // *** prefix2
        // **** rsa3

        auto const rsa3Msg = "P2P1P0abcdefghijklmnopqrstuvwxyz"s;
        std::array<std::uint8_t, 256> const rsa3PublicKey{
            {0xbd, 0xd1, 0xc7, 0xf0, 0xb0, 0x3a, 0xa5, 0x5b, 0x3e, 0x49, 0x8d,
             0x4e, 0x00, 0x54, 0x89, 0xb9, 0x89, 0xcd, 0x4b, 0x43, 0xde, 0x59,
             0xf6, 0x7a, 0x67, 0x5c, 0x3a, 0xc6, 0xcf, 0x82, 0x3f, 0x35, 0x9c,
             0xcc, 0xda, 0xcd, 0xd3, 0x97, 0x86, 0x5b, 0xe9, 0xf6, 0x05, 0x55,
             0x0b, 0x26, 0xef, 0x1e, 0x88, 0xd5, 0xb6, 0xba, 0x14, 0x0a, 0xb2,
             0x76, 0xb9, 0xb3, 0x46, 0x0c, 0xc0, 0x80, 0x17, 0x13, 0x68, 0x23,
             0xdc, 0xec, 0x10, 0x18, 0xfc, 0xaa, 0xbe, 0xb3, 0xc4, 0xc7, 0xa9,
             0x84, 0xa6, 0x4e, 0x5c, 0x08, 0x6b, 0x7b, 0x4c, 0x81, 0x91, 0x79,
             0x5d, 0x90, 0x06, 0x15, 0xbb, 0x76, 0x2f, 0x5c, 0x53, 0x60, 0x0f,
             0xac, 0xf3, 0x7c, 0x49, 0xc5, 0x47, 0xec, 0xb3, 0xda, 0x93, 0x87,
             0xc1, 0xb9, 0xcf, 0x2c, 0xb5, 0xf0, 0x85, 0xad, 0xb4, 0x38, 0x67,
             0x88, 0xda, 0x3d, 0xfa, 0x01, 0xb7, 0x54, 0xd9, 0x41, 0x0b, 0x7b,
             0x8a, 0x09, 0xe0, 0x84, 0x7d, 0xbb, 0x89, 0xb2, 0xfc, 0x0b, 0x70,
             0x36, 0x93, 0x56, 0x62, 0xcc, 0xb4, 0xfc, 0xf9, 0x1f, 0x37, 0x92,
             0x9b, 0x3a, 0x4e, 0x7c, 0xad, 0x4b, 0xa6, 0x76, 0x6f, 0xda, 0xc4,
             0x2f, 0x83, 0x53, 0xbd, 0x93, 0xa9, 0x76, 0x89, 0x53, 0xe1, 0x4d,
             0xee, 0x27, 0x11, 0x6f, 0xbc, 0x21, 0xad, 0x42, 0x9f, 0x29, 0xf6,
             0x03, 0xdd, 0xec, 0xfa, 0xa1, 0x78, 0xd2, 0xde, 0x29, 0x2e, 0xd8,
             0x3a, 0x7f, 0xe9, 0x9b, 0x5d, 0xeb, 0x37, 0xb8, 0xb0, 0xa0, 0x66,
             0x3f, 0x02, 0xcd, 0x2a, 0x6e, 0xd3, 0x1c, 0xa5, 0x65, 0xdc, 0x73,
             0xbe, 0x93, 0x54, 0x9a, 0x2b, 0xf8, 0x32, 0x8b, 0xe8, 0xce, 0x9a,
             0x59, 0xd0, 0x05, 0xeb, 0xbb, 0xac, 0xfc, 0x4c, 0x4b, 0x2e, 0xac,
             0x2a, 0xc3, 0x0f, 0x0a, 0xd7, 0x46, 0xaf, 0xfd, 0x22, 0x0d, 0x0d,
             0x54, 0xcc, 0x2f}};
        std::array<std::uint8_t, 256> const rsa3Sig{
            {0xad, 0xed, 0x85, 0x93, 0x7f, 0xe1, 0x8f, 0xa6, 0x1b, 0xda, 0xde,
             0x0a, 0x73, 0xcd, 0xf5, 0x5b, 0x24, 0x51, 0xfc, 0x2d, 0xad, 0x99,
             0xb0, 0x88, 0xfa, 0xe0, 0xa3, 0x0b, 0x69, 0xf1, 0xa1, 0x78, 0xa3,
             0xfc, 0x2e, 0x9e, 0xa5, 0x9b, 0x92, 0xf2, 0x69, 0x8b, 0x01, 0x60,
             0x8e, 0x28, 0xcb, 0x96, 0x33, 0x19, 0x8d, 0x16, 0xe4, 0xcf, 0x97,
             0xf0, 0xda, 0xc5, 0x5d, 0xee, 0xcd, 0x74, 0xbb, 0xce, 0x56, 0xc9,
             0x60, 0xc9, 0x4c, 0xa7, 0x5b, 0xe5, 0xcc, 0x5a, 0xd0, 0x04, 0xc6,
             0xbe, 0xcf, 0x16, 0xda, 0x71, 0x82, 0x8d, 0x8e, 0x22, 0xb6, 0x3b,
             0x1c, 0xf3, 0xe2, 0x3a, 0xf7, 0x78, 0xec, 0x22, 0x8f, 0x8c, 0xc9,
             0xe8, 0x3c, 0x8b, 0x3e, 0xbc, 0x7f, 0x0a, 0x95, 0x66, 0x94, 0x7e,
             0xb7, 0xae, 0x7f, 0xc8, 0x10, 0xb3, 0x8e, 0x8e, 0xf7, 0xd7, 0x85,
             0x1b, 0x07, 0x49, 0xa5, 0x18, 0x9a, 0x05, 0x4e, 0xee, 0xec, 0x7e,
             0x00, 0x26, 0x3c, 0xd6, 0xb0, 0xc6, 0xc2, 0x0c, 0xbf, 0x1a, 0x68,
             0x23, 0xf0, 0x26, 0x0a, 0x86, 0x0f, 0xa8, 0xd6, 0xea, 0x2b, 0xca,
             0x6d, 0x0c, 0xd5, 0xe8, 0xcc, 0xd9, 0x0c, 0x8c, 0x0a, 0x3a, 0x1a,
             0x09, 0x29, 0x58, 0x27, 0x47, 0xd3, 0x21, 0x0a, 0x58, 0x80, 0xa8,
             0xe8, 0x99, 0x84, 0xde, 0x81, 0x16, 0xea, 0xe7, 0x02, 0x8b, 0x53,
             0x7a, 0xff, 0xbf, 0x6d, 0xe2, 0x61, 0x78, 0x5b, 0x8f, 0x4c, 0x04,
             0x7d, 0xc4, 0x49, 0xe6, 0x0c, 0x50, 0x3b, 0xa3, 0xb0, 0xc2, 0x89,
             0x24, 0xe6, 0x52, 0xab, 0xe5, 0xc8, 0x4d, 0xaa, 0x4e, 0x89, 0x97,
             0xc2, 0x93, 0xb1, 0x43, 0xb3, 0xcd, 0x7a, 0xb0, 0x33, 0xa1, 0x19,
             0xa8, 0xc5, 0xac, 0x87, 0x72, 0x67, 0x36, 0xcd, 0xae, 0xfc, 0xd9,
             0xba, 0x77, 0xf9, 0xab, 0x6f, 0x4a, 0xb4, 0xd5, 0x84, 0xb8, 0xab,
             0x03, 0x7a, 0x88}};
        auto const prefix2Prefix = "P2"s;
        auto const prefix2Msg = "P1P0abcdefghijklmnopqrstuvwxyz"s;
        auto const prefix2MaxMsgLength = 30;
        auto const prefix1Prefix = "P1"s;
        auto const prefix1Msg = "P0abcdefghijklmnopqrstuvwxyz"s;
        auto const prefix1MaxMsgLength = 28;
        auto const prefix0Prefix = "P0"s;
        auto const prefix0Msg = "abcdefghijklmnopqrstuvwxyz"s;
        auto const prefix0MaxMsgLength = 26;

        auto rsa3 = std::make_unique<RsaSha256>(
            makeSlice(rsa3PublicKey), makeSlice(rsa3Sig));
        auto prefix2 = std::make_unique<PrefixSha256>(
            makeSlice(prefix2Prefix), prefix2MaxMsgLength, std::move(rsa3));
        auto prefix1 = std::make_unique<PrefixSha256>(
            makeSlice(prefix1Prefix), prefix1MaxMsgLength, std::move(prefix2));
        auto prefix0 = std::make_unique<PrefixSha256>(
            makeSlice(prefix0Prefix), prefix0MaxMsgLength, std::move(prefix1));
        {
            auto prefix0EncodedFulfillment =
                "\xa1\x82\x02\x35\x80\x02\x50\x30\x81\x01\x1a\xa2\x82\x02\x2a"
                "\xa1\x82\x02\x26\x80\x02\x50\x31\x81\x01\x1c\xa2\x82\x02\x1b"
                "\xa1\x82\x02\x17\x80\x02\x50\x32\x81\x01\x1e\xa2\x82\x02\x0c"
                "\xa3\x82\x02\x08\x80\x82\x01\x00\xbd\xd1\xc7\xf0\xb0\x3a\xa5"
                "\x5b\x3e\x49\x8d\x4e\x00\x54\x89\xb9\x89\xcd\x4b\x43\xde\x59"
                "\xf6\x7a\x67\x5c\x3a\xc6\xcf\x82\x3f\x35\x9c\xcc\xda\xcd\xd3"
                "\x97\x86\x5b\xe9\xf6\x05\x55\x0b\x26\xef\x1e\x88\xd5\xb6\xba"
                "\x14\x0a\xb2\x76\xb9\xb3\x46\x0c\xc0\x80\x17\x13\x68\x23\xdc"
                "\xec\x10\x18\xfc\xaa\xbe\xb3\xc4\xc7\xa9\x84\xa6\x4e\x5c\x08"
                "\x6b\x7b\x4c\x81\x91\x79\x5d\x90\x06\x15\xbb\x76\x2f\x5c\x53"
                "\x60\x0f\xac\xf3\x7c\x49\xc5\x47\xec\xb3\xda\x93\x87\xc1\xb9"
                "\xcf\x2c\xb5\xf0\x85\xad\xb4\x38\x67\x88\xda\x3d\xfa\x01\xb7"
                "\x54\xd9\x41\x0b\x7b\x8a\x09\xe0\x84\x7d\xbb\x89\xb2\xfc\x0b"
                "\x70\x36\x93\x56\x62\xcc\xb4\xfc\xf9\x1f\x37\x92\x9b\x3a\x4e"
                "\x7c\xad\x4b\xa6\x76\x6f\xda\xc4\x2f\x83\x53\xbd\x93\xa9\x76"
                "\x89\x53\xe1\x4d\xee\x27\x11\x6f\xbc\x21\xad\x42\x9f\x29\xf6"
                "\x03\xdd\xec\xfa\xa1\x78\xd2\xde\x29\x2e\xd8\x3a\x7f\xe9\x9b"
                "\x5d\xeb\x37\xb8\xb0\xa0\x66\x3f\x02\xcd\x2a\x6e\xd3\x1c\xa5"
                "\x65\xdc\x73\xbe\x93\x54\x9a\x2b\xf8\x32\x8b\xe8\xce\x9a\x59"
                "\xd0\x05\xeb\xbb\xac\xfc\x4c\x4b\x2e\xac\x2a\xc3\x0f\x0a\xd7"
                "\x46\xaf\xfd\x22\x0d\x0d\x54\xcc\x2f\x81\x82\x01\x00\xad\xed"
                "\x85\x93\x7f\xe1\x8f\xa6\x1b\xda\xde\x0a\x73\xcd\xf5\x5b\x24"
                "\x51\xfc\x2d\xad\x99\xb0\x88\xfa\xe0\xa3\x0b\x69\xf1\xa1\x78"
                "\xa3\xfc\x2e\x9e\xa5\x9b\x92\xf2\x69\x8b\x01\x60\x8e\x28\xcb"
                "\x96\x33\x19\x8d\x16\xe4\xcf\x97\xf0\xda\xc5\x5d\xee\xcd\x74"
                "\xbb\xce\x56\xc9\x60\xc9\x4c\xa7\x5b\xe5\xcc\x5a\xd0\x04\xc6"
                "\xbe\xcf\x16\xda\x71\x82\x8d\x8e\x22\xb6\x3b\x1c\xf3\xe2\x3a"
                "\xf7\x78\xec\x22\x8f\x8c\xc9\xe8\x3c\x8b\x3e\xbc\x7f\x0a\x95"
                "\x66\x94\x7e\xb7\xae\x7f\xc8\x10\xb3\x8e\x8e\xf7\xd7\x85\x1b"
                "\x07\x49\xa5\x18\x9a\x05\x4e\xee\xec\x7e\x00\x26\x3c\xd6\xb0"
                "\xc6\xc2\x0c\xbf\x1a\x68\x23\xf0\x26\x0a\x86\x0f\xa8\xd6\xea"
                "\x2b\xca\x6d\x0c\xd5\xe8\xcc\xd9\x0c\x8c\x0a\x3a\x1a\x09\x29"
                "\x58\x27\x47\xd3\x21\x0a\x58\x80\xa8\xe8\x99\x84\xde\x81\x16"
                "\xea\xe7\x02\x8b\x53\x7a\xff\xbf\x6d\xe2\x61\x78\x5b\x8f\x4c"
                "\x04\x7d\xc4\x49\xe6\x0c\x50\x3b\xa3\xb0\xc2\x89\x24\xe6\x52"
                "\xab\xe5\xc8\x4d\xaa\x4e\x89\x97\xc2\x93\xb1\x43\xb3\xcd\x7a"
                "\xb0\x33\xa1\x19\xa8\xc5\xac\x87\x72\x67\x36\xcd\xae\xfc\xd9"
                "\xba\x77\xf9\xab\x6f\x4a\xb4\xd5\x84\xb8\xab\x03\x7a\x88"s;
            auto const prefix0EncodedCondition =
                "\xa1\x2b\x80\x20\xa9\x5b\xbe\xc2\xfd\x4b\x3b\x17\xf8\x7c\x09"
                "\xaf\x8b\x23\xaa\x18\xe9\xa5\x10\x9e\x8c\x21\x6e\xc1\x5c\x02"
                "\x90\x0e\x25\x12\xb1\x27\x81\x03\x01\x0c\x5a\x82\x02\x04\x10"s;
            auto const prefix0EncodedFingerprint =
                "\x30\x36\x80\x02\x50\x30\x81\x01\x1a\xa2\x2d\xa1\x2b\x80\x20"
                "\x2d\xee\x1c\xf5\x83\xf7\x51\x65\x58\x17\x0a\xf6\x35\xbf\x36"
                "\xe0\x08\x4a\x32\x87\x0e\x38\x3c\xd7\x13\x6f\x01\x43\xb2\x5d"
                "\x11\xb5\x81\x03\x01\x08\x3e\x82\x02\x04\x10"s;
            check(
                std::move(prefix0),
                prefix0Msg,
                std::move(prefix0EncodedFulfillment),
                prefix0EncodedCondition,
                prefix0EncodedFingerprint);
        }
    }

    void
    testPrefix10()
    {
        testcase("Prefix10");

        using namespace std::string_literals;
        using namespace ripple::cryptoconditions;

        // Fulfillment structure
        // * prefix0
        // ** prefix1
        // *** thresh2
        // **** rsa3

        auto const rsa3Msg = "P1P0abcdefghijklmnopqrstuvwxyz"s;
        std::array<std::uint8_t, 256> const rsa3PublicKey{
            {0xbd, 0xd1, 0xc7, 0xf0, 0xb0, 0x3a, 0xa5, 0x5b, 0x3e, 0x49, 0x8d,
             0x4e, 0x00, 0x54, 0x89, 0xb9, 0x89, 0xcd, 0x4b, 0x43, 0xde, 0x59,
             0xf6, 0x7a, 0x67, 0x5c, 0x3a, 0xc6, 0xcf, 0x82, 0x3f, 0x35, 0x9c,
             0xcc, 0xda, 0xcd, 0xd3, 0x97, 0x86, 0x5b, 0xe9, 0xf6, 0x05, 0x55,
             0x0b, 0x26, 0xef, 0x1e, 0x88, 0xd5, 0xb6, 0xba, 0x14, 0x0a, 0xb2,
             0x76, 0xb9, 0xb3, 0x46, 0x0c, 0xc0, 0x80, 0x17, 0x13, 0x68, 0x23,
             0xdc, 0xec, 0x10, 0x18, 0xfc, 0xaa, 0xbe, 0xb3, 0xc4, 0xc7, 0xa9,
             0x84, 0xa6, 0x4e, 0x5c, 0x08, 0x6b, 0x7b, 0x4c, 0x81, 0x91, 0x79,
             0x5d, 0x90, 0x06, 0x15, 0xbb, 0x76, 0x2f, 0x5c, 0x53, 0x60, 0x0f,
             0xac, 0xf3, 0x7c, 0x49, 0xc5, 0x47, 0xec, 0xb3, 0xda, 0x93, 0x87,
             0xc1, 0xb9, 0xcf, 0x2c, 0xb5, 0xf0, 0x85, 0xad, 0xb4, 0x38, 0x67,
             0x88, 0xda, 0x3d, 0xfa, 0x01, 0xb7, 0x54, 0xd9, 0x41, 0x0b, 0x7b,
             0x8a, 0x09, 0xe0, 0x84, 0x7d, 0xbb, 0x89, 0xb2, 0xfc, 0x0b, 0x70,
             0x36, 0x93, 0x56, 0x62, 0xcc, 0xb4, 0xfc, 0xf9, 0x1f, 0x37, 0x92,
             0x9b, 0x3a, 0x4e, 0x7c, 0xad, 0x4b, 0xa6, 0x76, 0x6f, 0xda, 0xc4,
             0x2f, 0x83, 0x53, 0xbd, 0x93, 0xa9, 0x76, 0x89, 0x53, 0xe1, 0x4d,
             0xee, 0x27, 0x11, 0x6f, 0xbc, 0x21, 0xad, 0x42, 0x9f, 0x29, 0xf6,
             0x03, 0xdd, 0xec, 0xfa, 0xa1, 0x78, 0xd2, 0xde, 0x29, 0x2e, 0xd8,
             0x3a, 0x7f, 0xe9, 0x9b, 0x5d, 0xeb, 0x37, 0xb8, 0xb0, 0xa0, 0x66,
             0x3f, 0x02, 0xcd, 0x2a, 0x6e, 0xd3, 0x1c, 0xa5, 0x65, 0xdc, 0x73,
             0xbe, 0x93, 0x54, 0x9a, 0x2b, 0xf8, 0x32, 0x8b, 0xe8, 0xce, 0x9a,
             0x59, 0xd0, 0x05, 0xeb, 0xbb, 0xac, 0xfc, 0x4c, 0x4b, 0x2e, 0xac,
             0x2a, 0xc3, 0x0f, 0x0a, 0xd7, 0x46, 0xaf, 0xfd, 0x22, 0x0d, 0x0d,
             0x54, 0xcc, 0x2f}};
        std::array<std::uint8_t, 256> const rsa3Sig{
            {0x3e, 0x7f, 0x0d, 0xdf, 0x6b, 0xa4, 0x40, 0x02, 0x48, 0xd2, 0xdf,
             0x93, 0x31, 0x6c, 0xeb, 0xc5, 0xa0, 0xa5, 0x5c, 0x3e, 0x9b, 0xa7,
             0x50, 0x46, 0x2f, 0x6f, 0x8a, 0x05, 0xea, 0x42, 0x2a, 0x83, 0x81,
             0x0d, 0x39, 0x0b, 0x24, 0x93, 0x86, 0x67, 0xc3, 0x42, 0x2f, 0xae,
             0x41, 0x7f, 0x0e, 0xc6, 0xbd, 0xe5, 0xdb, 0x1b, 0x76, 0x7f, 0x9b,
             0x40, 0x79, 0x4b, 0xa1, 0x95, 0x80, 0x0f, 0x7b, 0x97, 0x34, 0x77,
             0x31, 0x31, 0x8d, 0x70, 0x15, 0x1f, 0x25, 0x24, 0x9d, 0x52, 0x0d,
             0x57, 0xdf, 0xba, 0x27, 0xe6, 0xe0, 0x5d, 0x6e, 0x48, 0x7c, 0x68,
             0xd1, 0x40, 0xbf, 0xba, 0xd7, 0x3c, 0xf4, 0x71, 0x52, 0x28, 0x5c,
             0x03, 0xb0, 0x74, 0x0e, 0x23, 0xbd, 0x36, 0x8e, 0x51, 0xf3, 0xdc,
             0x3a, 0xa4, 0x67, 0xfd, 0xdc, 0xe0, 0x60, 0xcc, 0x2d, 0xa7, 0x99,
             0xf7, 0x92, 0x6f, 0x33, 0x18, 0x71, 0x4a, 0x07, 0x4a, 0x59, 0x8a,
             0x29, 0x49, 0xc0, 0xdd, 0x07, 0x0b, 0xa3, 0xd5, 0xe2, 0x36, 0xb6,
             0x13, 0xfc, 0x45, 0x47, 0xf5, 0x7c, 0xe4, 0xe2, 0x84, 0x29, 0x1e,
             0x3c, 0x08, 0xb3, 0xae, 0xc7, 0xaf, 0xef, 0x4a, 0xd8, 0x58, 0x2d,
             0x68, 0x29, 0x4c, 0xfe, 0xc1, 0xba, 0x76, 0x53, 0xf7, 0x88, 0xdb,
             0xce, 0x29, 0x9c, 0x97, 0x99, 0xe8, 0x43, 0x73, 0x79, 0x74, 0x0a,
             0x20, 0x72, 0xa5, 0xc5, 0x79, 0xad, 0xbb, 0xf8, 0x94, 0x2f, 0xaa,
             0xc8, 0x09, 0x41, 0x0d, 0x61, 0x64, 0x1a, 0x21, 0x8b, 0xad, 0x42,
             0x41, 0x80, 0xc7, 0x6c, 0x9a, 0x57, 0x8a, 0xf2, 0xd8, 0xf2, 0xba,
             0xbf, 0xae, 0x8c, 0x55, 0x4f, 0x8d, 0x58, 0x41, 0x58, 0x28, 0x32,
             0xe7, 0xe8, 0x7b, 0x3b, 0xe6, 0x99, 0x4e, 0x58, 0x82, 0xc8, 0x3a,
             0x43, 0xbd, 0x26, 0x30, 0x45, 0x38, 0xb8, 0xf5, 0x00, 0x4d, 0x2c,
             0xb4, 0x7d, 0x90}};
        auto const thresh2Msg = "P1P0abcdefghijklmnopqrstuvwxyz"s;
        auto const prefix1Prefix = "P1"s;
        auto const prefix1Msg = "P0abcdefghijklmnopqrstuvwxyz"s;
        auto const prefix1MaxMsgLength = 28;
        auto const prefix0Prefix = "P0"s;
        auto const prefix0Msg = "abcdefghijklmnopqrstuvwxyz"s;
        auto const prefix0MaxMsgLength = 26;

        auto rsa3 = std::make_unique<RsaSha256>(
            makeSlice(rsa3PublicKey), makeSlice(rsa3Sig));
        std::vector<std::unique_ptr<Fulfillment>> thresh2Subfulfillments;
        thresh2Subfulfillments.emplace_back(std::move(rsa3));
        std::vector<Condition> thresh2Subconditions{};
        auto thresh2 = std::make_unique<ThresholdSha256>(
            std::move(thresh2Subfulfillments), std::move(thresh2Subconditions));
        auto prefix1 = std::make_unique<PrefixSha256>(
            makeSlice(prefix1Prefix), prefix1MaxMsgLength, std::move(thresh2));
        auto prefix0 = std::make_unique<PrefixSha256>(
            makeSlice(prefix0Prefix), prefix0MaxMsgLength, std::move(prefix1));
        {
            auto prefix0EncodedFulfillment =
                "\xa1\x82\x02\x30\x80\x02\x50\x30\x81\x01\x1a\xa2\x82\x02\x25"
                "\xa1\x82\x02\x21\x80\x02\x50\x31\x81\x01\x1c\xa2\x82\x02\x16"
                "\xa2\x82\x02\x12\xa0\x82\x02\x0c\xa3\x82\x02\x08\x80\x82\x01"
                "\x00\xbd\xd1\xc7\xf0\xb0\x3a\xa5\x5b\x3e\x49\x8d\x4e\x00\x54"
                "\x89\xb9\x89\xcd\x4b\x43\xde\x59\xf6\x7a\x67\x5c\x3a\xc6\xcf"
                "\x82\x3f\x35\x9c\xcc\xda\xcd\xd3\x97\x86\x5b\xe9\xf6\x05\x55"
                "\x0b\x26\xef\x1e\x88\xd5\xb6\xba\x14\x0a\xb2\x76\xb9\xb3\x46"
                "\x0c\xc0\x80\x17\x13\x68\x23\xdc\xec\x10\x18\xfc\xaa\xbe\xb3"
                "\xc4\xc7\xa9\x84\xa6\x4e\x5c\x08\x6b\x7b\x4c\x81\x91\x79\x5d"
                "\x90\x06\x15\xbb\x76\x2f\x5c\x53\x60\x0f\xac\xf3\x7c\x49\xc5"
                "\x47\xec\xb3\xda\x93\x87\xc1\xb9\xcf\x2c\xb5\xf0\x85\xad\xb4"
                "\x38\x67\x88\xda\x3d\xfa\x01\xb7\x54\xd9\x41\x0b\x7b\x8a\x09"
                "\xe0\x84\x7d\xbb\x89\xb2\xfc\x0b\x70\x36\x93\x56\x62\xcc\xb4"
                "\xfc\xf9\x1f\x37\x92\x9b\x3a\x4e\x7c\xad\x4b\xa6\x76\x6f\xda"
                "\xc4\x2f\x83\x53\xbd\x93\xa9\x76\x89\x53\xe1\x4d\xee\x27\x11"
                "\x6f\xbc\x21\xad\x42\x9f\x29\xf6\x03\xdd\xec\xfa\xa1\x78\xd2"
                "\xde\x29\x2e\xd8\x3a\x7f\xe9\x9b\x5d\xeb\x37\xb8\xb0\xa0\x66"
                "\x3f\x02\xcd\x2a\x6e\xd3\x1c\xa5\x65\xdc\x73\xbe\x93\x54\x9a"
                "\x2b\xf8\x32\x8b\xe8\xce\x9a\x59\xd0\x05\xeb\xbb\xac\xfc\x4c"
                "\x4b\x2e\xac\x2a\xc3\x0f\x0a\xd7\x46\xaf\xfd\x22\x0d\x0d\x54"
                "\xcc\x2f\x81\x82\x01\x00\x3e\x7f\x0d\xdf\x6b\xa4\x40\x02\x48"
                "\xd2\xdf\x93\x31\x6c\xeb\xc5\xa0\xa5\x5c\x3e\x9b\xa7\x50\x46"
                "\x2f\x6f\x8a\x05\xea\x42\x2a\x83\x81\x0d\x39\x0b\x24\x93\x86"
                "\x67\xc3\x42\x2f\xae\x41\x7f\x0e\xc6\xbd\xe5\xdb\x1b\x76\x7f"
                "\x9b\x40\x79\x4b\xa1\x95\x80\x0f\x7b\x97\x34\x77\x31\x31\x8d"
                "\x70\x15\x1f\x25\x24\x9d\x52\x0d\x57\xdf\xba\x27\xe6\xe0\x5d"
                "\x6e\x48\x7c\x68\xd1\x40\xbf\xba\xd7\x3c\xf4\x71\x52\x28\x5c"
                "\x03\xb0\x74\x0e\x23\xbd\x36\x8e\x51\xf3\xdc\x3a\xa4\x67\xfd"
                "\xdc\xe0\x60\xcc\x2d\xa7\x99\xf7\x92\x6f\x33\x18\x71\x4a\x07"
                "\x4a\x59\x8a\x29\x49\xc0\xdd\x07\x0b\xa3\xd5\xe2\x36\xb6\x13"
                "\xfc\x45\x47\xf5\x7c\xe4\xe2\x84\x29\x1e\x3c\x08\xb3\xae\xc7"
                "\xaf\xef\x4a\xd8\x58\x2d\x68\x29\x4c\xfe\xc1\xba\x76\x53\xf7"
                "\x88\xdb\xce\x29\x9c\x97\x99\xe8\x43\x73\x79\x74\x0a\x20\x72"
                "\xa5\xc5\x79\xad\xbb\xf8\x94\x2f\xaa\xc8\x09\x41\x0d\x61\x64"
                "\x1a\x21\x8b\xad\x42\x41\x80\xc7\x6c\x9a\x57\x8a\xf2\xd8\xf2"
                "\xba\xbf\xae\x8c\x55\x4f\x8d\x58\x41\x58\x28\x32\xe7\xe8\x7b"
                "\x3b\xe6\x99\x4e\x58\x82\xc8\x3a\x43\xbd\x26\x30\x45\x38\xb8"
                "\xf5\x00\x4d\x2c\xb4\x7d\x90\xa1\x00"s;
            auto const prefix0EncodedCondition =
                "\xa1\x2b\x80\x20\x90\x18\x4a\x84\xba\x52\x59\xa7\x18\xbe\xe2"
                "\x73\xb3\xc3\x59\xca\xf5\x01\x62\xb6\xd1\xcf\xfc\xe8\x64\x50"
                "\xee\xb6\x35\x58\x08\xeb\x81\x03\x01\x0c\x3a\x82\x02\x04\x30"s;
            auto const prefix0EncodedFingerprint =
                "\x30\x36\x80\x02\x50\x30\x81\x01\x1a\xa2\x2d\xa1\x2b\x80\x20"
                "\x03\x55\x19\x2f\x8d\xc8\x6c\x6c\xbb\xb6\x73\x15\xe1\x10\x76"
                "\x4b\x7c\xe1\x3d\x81\x4c\x51\x0f\x35\xe8\xee\x05\x35\xef\x9c"
                "\x30\xef\x81\x03\x01\x08\x1e\x82\x02\x04\x30"s;
            check(
                std::move(prefix0),
                prefix0Msg,
                std::move(prefix0EncodedFulfillment),
                prefix0EncodedCondition,
                prefix0EncodedFingerprint);
        }
    }

    void
    testPrefix11()
    {
        testcase("Prefix11");

        using namespace std::string_literals;
        using namespace ripple::cryptoconditions;

        // Fulfillment structure
        // * prefix0
        // ** prefix1
        // *** thresh2
        // **** Preim4Cond
        // **** Rsa5Cond
        // **** Ed6Cond
        // **** rsa3

        auto const rsa3Msg = "P1P0abcdefghijklmnopqrstuvwxyz"s;
        std::array<std::uint8_t, 256> const rsa3PublicKey{
            {0xbd, 0xd1, 0xc7, 0xf0, 0xb0, 0x3a, 0xa5, 0x5b, 0x3e, 0x49, 0x8d,
             0x4e, 0x00, 0x54, 0x89, 0xb9, 0x89, 0xcd, 0x4b, 0x43, 0xde, 0x59,
             0xf6, 0x7a, 0x67, 0x5c, 0x3a, 0xc6, 0xcf, 0x82, 0x3f, 0x35, 0x9c,
             0xcc, 0xda, 0xcd, 0xd3, 0x97, 0x86, 0x5b, 0xe9, 0xf6, 0x05, 0x55,
             0x0b, 0x26, 0xef, 0x1e, 0x88, 0xd5, 0xb6, 0xba, 0x14, 0x0a, 0xb2,
             0x76, 0xb9, 0xb3, 0x46, 0x0c, 0xc0, 0x80, 0x17, 0x13, 0x68, 0x23,
             0xdc, 0xec, 0x10, 0x18, 0xfc, 0xaa, 0xbe, 0xb3, 0xc4, 0xc7, 0xa9,
             0x84, 0xa6, 0x4e, 0x5c, 0x08, 0x6b, 0x7b, 0x4c, 0x81, 0x91, 0x79,
             0x5d, 0x90, 0x06, 0x15, 0xbb, 0x76, 0x2f, 0x5c, 0x53, 0x60, 0x0f,
             0xac, 0xf3, 0x7c, 0x49, 0xc5, 0x47, 0xec, 0xb3, 0xda, 0x93, 0x87,
             0xc1, 0xb9, 0xcf, 0x2c, 0xb5, 0xf0, 0x85, 0xad, 0xb4, 0x38, 0x67,
             0x88, 0xda, 0x3d, 0xfa, 0x01, 0xb7, 0x54, 0xd9, 0x41, 0x0b, 0x7b,
             0x8a, 0x09, 0xe0, 0x84, 0x7d, 0xbb, 0x89, 0xb2, 0xfc, 0x0b, 0x70,
             0x36, 0x93, 0x56, 0x62, 0xcc, 0xb4, 0xfc, 0xf9, 0x1f, 0x37, 0x92,
             0x9b, 0x3a, 0x4e, 0x7c, 0xad, 0x4b, 0xa6, 0x76, 0x6f, 0xda, 0xc4,
             0x2f, 0x83, 0x53, 0xbd, 0x93, 0xa9, 0x76, 0x89, 0x53, 0xe1, 0x4d,
             0xee, 0x27, 0x11, 0x6f, 0xbc, 0x21, 0xad, 0x42, 0x9f, 0x29, 0xf6,
             0x03, 0xdd, 0xec, 0xfa, 0xa1, 0x78, 0xd2, 0xde, 0x29, 0x2e, 0xd8,
             0x3a, 0x7f, 0xe9, 0x9b, 0x5d, 0xeb, 0x37, 0xb8, 0xb0, 0xa0, 0x66,
             0x3f, 0x02, 0xcd, 0x2a, 0x6e, 0xd3, 0x1c, 0xa5, 0x65, 0xdc, 0x73,
             0xbe, 0x93, 0x54, 0x9a, 0x2b, 0xf8, 0x32, 0x8b, 0xe8, 0xce, 0x9a,
             0x59, 0xd0, 0x05, 0xeb, 0xbb, 0xac, 0xfc, 0x4c, 0x4b, 0x2e, 0xac,
             0x2a, 0xc3, 0x0f, 0x0a, 0xd7, 0x46, 0xaf, 0xfd, 0x22, 0x0d, 0x0d,
             0x54, 0xcc, 0x2f}};
        std::array<std::uint8_t, 256> const rsa3Sig{
            {0x94, 0x90, 0xe3, 0xb1, 0xc0, 0x9c, 0xf6, 0xf7, 0xf3, 0xeb, 0xdd,
             0x77, 0xdc, 0x11, 0xb3, 0xcf, 0xf9, 0x02, 0xac, 0x26, 0xcc, 0xe9,
             0xc3, 0xc2, 0x2a, 0x4d, 0xa4, 0xae, 0x01, 0x0c, 0x9f, 0xc7, 0x44,
             0xce, 0x7e, 0x77, 0xba, 0x3b, 0xf5, 0xc5, 0x7a, 0xf2, 0x79, 0xd9,
             0x8c, 0xa9, 0xff, 0xbe, 0xbe, 0xd8, 0x79, 0x8d, 0x2b, 0xd1, 0x60,
             0x8a, 0x51, 0x96, 0x98, 0x5d, 0xcc, 0x03, 0x25, 0xd0, 0x91, 0x2b,
             0x88, 0xd8, 0xe5, 0x3c, 0x9c, 0x8c, 0xae, 0xab, 0x0e, 0x2b, 0x56,
             0x0a, 0xe5, 0x89, 0x11, 0x53, 0xe0, 0x94, 0x60, 0x20, 0x27, 0xb0,
             0x1b, 0xd4, 0x07, 0xbe, 0x09, 0x28, 0x7c, 0xad, 0x3f, 0x17, 0x70,
             0xac, 0x2a, 0xe6, 0x7e, 0xd3, 0x2c, 0x34, 0xb8, 0xc2, 0xcd, 0xad,
             0x92, 0x01, 0x09, 0x04, 0xbd, 0x8f, 0x9d, 0x24, 0x32, 0x04, 0x54,
             0xaf, 0x2a, 0xef, 0xd0, 0x36, 0x91, 0xce, 0xd3, 0xbc, 0xc9, 0x60,
             0x9d, 0xd0, 0xaf, 0x79, 0xe0, 0xfd, 0x42, 0x67, 0xc4, 0xcf, 0x5c,
             0xf0, 0x69, 0xd1, 0xf6, 0x5c, 0xd7, 0x83, 0x6a, 0xb2, 0x6f, 0xe0,
             0xb9, 0xc6, 0x08, 0x07, 0x93, 0xfe, 0x32, 0x49, 0x8c, 0xdd, 0xd6,
             0x21, 0xfc, 0x95, 0x0d, 0x2b, 0x93, 0x09, 0xa7, 0xa7, 0x78, 0x6b,
             0x47, 0x13, 0xce, 0x18, 0xc1, 0xaa, 0x8c, 0x55, 0x57, 0x4a, 0x94,
             0xae, 0x5a, 0xe8, 0x8f, 0x7e, 0x61, 0x1c, 0x70, 0xc8, 0x6a, 0x67,
             0x59, 0x72, 0x5c, 0x4d, 0x42, 0xc7, 0x58, 0x3f, 0x85, 0xd1, 0x06,
             0x3a, 0x25, 0xd5, 0x30, 0xe2, 0xf1, 0xc7, 0x80, 0x58, 0x06, 0x3b,
             0x0c, 0x65, 0x7a, 0x41, 0x33, 0x96, 0x4e, 0x81, 0x0a, 0x2a, 0xfa,
             0x20, 0x29, 0x98, 0xfb, 0xef, 0x82, 0x8c, 0xff, 0x91, 0x05, 0x35,
             0xde, 0xa7, 0xb3, 0x2b, 0x34, 0xf0, 0x6f, 0x61, 0xf0, 0x06, 0x9c,
             0x0c, 0x36, 0x27}};
        auto const thresh2Msg = "P1P0abcdefghijklmnopqrstuvwxyz"s;
        std::array<std::uint8_t, 32> const Preim4CondConditionFingerprint = {
            {0x5d, 0xa0, 0x30, 0xef, 0xfd, 0xe1, 0x75, 0x11, 0x51, 0xe8, 0x5f,
             0x5e, 0x54, 0x2d, 0x6a, 0x5b, 0xd1, 0x5c, 0xc9, 0x33, 0x21, 0x2c,
             0xe2, 0x68, 0xfc, 0xfd, 0x53, 0xee, 0x93, 0x58, 0xeb, 0x4e}};
        Condition const Preim4Cond{Type::preimageSha256,
                                   9,
                                   Preim4CondConditionFingerprint,
                                   std::bitset<5>{0}};
        std::array<std::uint8_t, 32> const Rsa5CondConditionFingerprint = {
            {0x99, 0xfb, 0x0b, 0x38, 0x94, 0x4d, 0x20, 0x85, 0xc8, 0xda, 0x3a,
             0x64, 0x31, 0x44, 0x6f, 0x6c, 0x3b, 0x46, 0x25, 0x50, 0xd7, 0x7f,
             0xdf, 0xee, 0x75, 0x72, 0x71, 0xf9, 0x61, 0x40, 0x63, 0xfa}};
        Condition const Rsa5Cond{Type::rsaSha256,
                                 65536,
                                 Rsa5CondConditionFingerprint,
                                 std::bitset<5>{0}};
        std::array<std::uint8_t, 32> const Ed6CondConditionFingerprint = {
            {0x00, 0xd3, 0xc9, 0x24, 0x3f, 0x2d, 0x2e, 0x64, 0x93, 0xa8, 0x49,
             0x29, 0x82, 0x75, 0xea, 0xbf, 0xe3, 0x53, 0x7f, 0x8e, 0x45, 0x16,
             0xdb, 0x5e, 0xc6, 0xdf, 0x39, 0xd2, 0xcb, 0xea, 0x62, 0xfb}};
        Condition const Ed6Cond{Type::ed25519Sha256,
                                131072,
                                Ed6CondConditionFingerprint,
                                std::bitset<5>{0}};
        auto const prefix1Prefix = "P1"s;
        auto const prefix1Msg = "P0abcdefghijklmnopqrstuvwxyz"s;
        auto const prefix1MaxMsgLength = 28;
        auto const prefix0Prefix = "P0"s;
        auto const prefix0Msg = "abcdefghijklmnopqrstuvwxyz"s;
        auto const prefix0MaxMsgLength = 26;

        auto rsa3 = std::make_unique<RsaSha256>(
            makeSlice(rsa3PublicKey), makeSlice(rsa3Sig));
        std::vector<std::unique_ptr<Fulfillment>> thresh2Subfulfillments;
        thresh2Subfulfillments.emplace_back(std::move(rsa3));
        std::vector<Condition> thresh2Subconditions{
            {Preim4Cond, Rsa5Cond, Ed6Cond}};
        auto thresh2 = std::make_unique<ThresholdSha256>(
            std::move(thresh2Subfulfillments), std::move(thresh2Subconditions));
        auto prefix1 = std::make_unique<PrefixSha256>(
            makeSlice(prefix1Prefix), prefix1MaxMsgLength, std::move(thresh2));
        auto prefix0 = std::make_unique<PrefixSha256>(
            makeSlice(prefix0Prefix), prefix0MaxMsgLength, std::move(prefix1));
        {
            auto prefix0EncodedFulfillment =
                "\xa1\x82\x02\xa9\x80\x02\x50\x30\x81\x01\x1a\xa2\x82\x02\x9e"
                "\xa1\x82\x02\x9a\x80\x02\x50\x31\x81\x01\x1c\xa2\x82\x02\x8f"
                "\xa2\x82\x02\x8b\xa0\x82\x02\x0c\xa3\x82\x02\x08\x80\x82\x01"
                "\x00\xbd\xd1\xc7\xf0\xb0\x3a\xa5\x5b\x3e\x49\x8d\x4e\x00\x54"
                "\x89\xb9\x89\xcd\x4b\x43\xde\x59\xf6\x7a\x67\x5c\x3a\xc6\xcf"
                "\x82\x3f\x35\x9c\xcc\xda\xcd\xd3\x97\x86\x5b\xe9\xf6\x05\x55"
                "\x0b\x26\xef\x1e\x88\xd5\xb6\xba\x14\x0a\xb2\x76\xb9\xb3\x46"
                "\x0c\xc0\x80\x17\x13\x68\x23\xdc\xec\x10\x18\xfc\xaa\xbe\xb3"
                "\xc4\xc7\xa9\x84\xa6\x4e\x5c\x08\x6b\x7b\x4c\x81\x91\x79\x5d"
                "\x90\x06\x15\xbb\x76\x2f\x5c\x53\x60\x0f\xac\xf3\x7c\x49\xc5"
                "\x47\xec\xb3\xda\x93\x87\xc1\xb9\xcf\x2c\xb5\xf0\x85\xad\xb4"
                "\x38\x67\x88\xda\x3d\xfa\x01\xb7\x54\xd9\x41\x0b\x7b\x8a\x09"
                "\xe0\x84\x7d\xbb\x89\xb2\xfc\x0b\x70\x36\x93\x56\x62\xcc\xb4"
                "\xfc\xf9\x1f\x37\x92\x9b\x3a\x4e\x7c\xad\x4b\xa6\x76\x6f\xda"
                "\xc4\x2f\x83\x53\xbd\x93\xa9\x76\x89\x53\xe1\x4d\xee\x27\x11"
                "\x6f\xbc\x21\xad\x42\x9f\x29\xf6\x03\xdd\xec\xfa\xa1\x78\xd2"
                "\xde\x29\x2e\xd8\x3a\x7f\xe9\x9b\x5d\xeb\x37\xb8\xb0\xa0\x66"
                "\x3f\x02\xcd\x2a\x6e\xd3\x1c\xa5\x65\xdc\x73\xbe\x93\x54\x9a"
                "\x2b\xf8\x32\x8b\xe8\xce\x9a\x59\xd0\x05\xeb\xbb\xac\xfc\x4c"
                "\x4b\x2e\xac\x2a\xc3\x0f\x0a\xd7\x46\xaf\xfd\x22\x0d\x0d\x54"
                "\xcc\x2f\x81\x82\x01\x00\x94\x90\xe3\xb1\xc0\x9c\xf6\xf7\xf3"
                "\xeb\xdd\x77\xdc\x11\xb3\xcf\xf9\x02\xac\x26\xcc\xe9\xc3\xc2"
                "\x2a\x4d\xa4\xae\x01\x0c\x9f\xc7\x44\xce\x7e\x77\xba\x3b\xf5"
                "\xc5\x7a\xf2\x79\xd9\x8c\xa9\xff\xbe\xbe\xd8\x79\x8d\x2b\xd1"
                "\x60\x8a\x51\x96\x98\x5d\xcc\x03\x25\xd0\x91\x2b\x88\xd8\xe5"
                "\x3c\x9c\x8c\xae\xab\x0e\x2b\x56\x0a\xe5\x89\x11\x53\xe0\x94"
                "\x60\x20\x27\xb0\x1b\xd4\x07\xbe\x09\x28\x7c\xad\x3f\x17\x70"
                "\xac\x2a\xe6\x7e\xd3\x2c\x34\xb8\xc2\xcd\xad\x92\x01\x09\x04"
                "\xbd\x8f\x9d\x24\x32\x04\x54\xaf\x2a\xef\xd0\x36\x91\xce\xd3"
                "\xbc\xc9\x60\x9d\xd0\xaf\x79\xe0\xfd\x42\x67\xc4\xcf\x5c\xf0"
                "\x69\xd1\xf6\x5c\xd7\x83\x6a\xb2\x6f\xe0\xb9\xc6\x08\x07\x93"
                "\xfe\x32\x49\x8c\xdd\xd6\x21\xfc\x95\x0d\x2b\x93\x09\xa7\xa7"
                "\x78\x6b\x47\x13\xce\x18\xc1\xaa\x8c\x55\x57\x4a\x94\xae\x5a"
                "\xe8\x8f\x7e\x61\x1c\x70\xc8\x6a\x67\x59\x72\x5c\x4d\x42\xc7"
                "\x58\x3f\x85\xd1\x06\x3a\x25\xd5\x30\xe2\xf1\xc7\x80\x58\x06"
                "\x3b\x0c\x65\x7a\x41\x33\x96\x4e\x81\x0a\x2a\xfa\x20\x29\x98"
                "\xfb\xef\x82\x8c\xff\x91\x05\x35\xde\xa7\xb3\x2b\x34\xf0\x6f"
                "\x61\xf0\x06\x9c\x0c\x36\x27\xa1\x79\xa0\x25\x80\x20\x5d\xa0"
                "\x30\xef\xfd\xe1\x75\x11\x51\xe8\x5f\x5e\x54\x2d\x6a\x5b\xd1"
                "\x5c\xc9\x33\x21\x2c\xe2\x68\xfc\xfd\x53\xee\x93\x58\xeb\x4e"
                "\x81\x01\x09\xa3\x27\x80\x20\x99\xfb\x0b\x38\x94\x4d\x20\x85"
                "\xc8\xda\x3a\x64\x31\x44\x6f\x6c\x3b\x46\x25\x50\xd7\x7f\xdf"
                "\xee\x75\x72\x71\xf9\x61\x40\x63\xfa\x81\x03\x01\x00\x00\xa4"
                "\x27\x80\x20\x00\xd3\xc9\x24\x3f\x2d\x2e\x64\x93\xa8\x49\x29"
                "\x82\x75\xea\xbf\xe3\x53\x7f\x8e\x45\x16\xdb\x5e\xc6\xdf\x39"
                "\xd2\xcb\xea\x62\xfb\x81\x03\x02\x00\x00"s;
            auto const prefix0EncodedCondition =
                "\xa1\x2b\x80\x20\x21\x8d\xd5\x70\x37\xbc\x0c\xd8\x38\xbc\xe0"
                "\xae\x11\x19\xca\xb1\xa6\xf4\x90\x0b\xbe\xd0\xc4\x51\xbb\x9d"
                "\x66\xec\x8e\xb5\x04\x2c\x81\x03\x02\x18\x3a\x82\x02\x03\xb8"s;
            auto const prefix0EncodedFingerprint =
                "\x30\x36\x80\x02\x50\x30\x81\x01\x1a\xa2\x2d\xa1\x2b\x80\x20"
                "\x6d\xb6\xcd\x26\xa1\xcd\x09\x4d\xd1\x49\x12\x2f\xdd\x9b\xfe"
                "\x07\x39\x5a\xd1\x45\x49\xba\x23\x78\x0c\xb1\x3b\x5d\x7b\xb7"
                "\xd8\xf6\x81\x03\x02\x14\x1e\x82\x02\x03\xb8"s;
            check(
                std::move(prefix0),
                prefix0Msg,
                std::move(prefix0EncodedFulfillment),
                prefix0EncodedCondition,
                prefix0EncodedFingerprint);
        }
    }

    void
    testPrefix12()
    {
        testcase("Prefix12");

        using namespace std::string_literals;
        using namespace ripple::cryptoconditions;

        // Fulfillment structure
        // * prefix0
        // ** prefix1
        // *** thresh2
        // **** Preim9Cond
        // **** Rsa10Cond
        // **** Ed11Cond
        // **** rsa3
        // **** thresh4
        // ***** Preim6Cond
        // ***** Rsa7Cond
        // ***** Ed8Cond
        // ***** rsa5

        auto const rsa3Msg = "P1P0abcdefghijklmnopqrstuvwxyz"s;
        std::array<std::uint8_t, 256> const rsa3PublicKey{
            {0xbd, 0xd1, 0xc7, 0xf0, 0xb0, 0x3a, 0xa5, 0x5b, 0x3e, 0x49, 0x8d,
             0x4e, 0x00, 0x54, 0x89, 0xb9, 0x89, 0xcd, 0x4b, 0x43, 0xde, 0x59,
             0xf6, 0x7a, 0x67, 0x5c, 0x3a, 0xc6, 0xcf, 0x82, 0x3f, 0x35, 0x9c,
             0xcc, 0xda, 0xcd, 0xd3, 0x97, 0x86, 0x5b, 0xe9, 0xf6, 0x05, 0x55,
             0x0b, 0x26, 0xef, 0x1e, 0x88, 0xd5, 0xb6, 0xba, 0x14, 0x0a, 0xb2,
             0x76, 0xb9, 0xb3, 0x46, 0x0c, 0xc0, 0x80, 0x17, 0x13, 0x68, 0x23,
             0xdc, 0xec, 0x10, 0x18, 0xfc, 0xaa, 0xbe, 0xb3, 0xc4, 0xc7, 0xa9,
             0x84, 0xa6, 0x4e, 0x5c, 0x08, 0x6b, 0x7b, 0x4c, 0x81, 0x91, 0x79,
             0x5d, 0x90, 0x06, 0x15, 0xbb, 0x76, 0x2f, 0x5c, 0x53, 0x60, 0x0f,
             0xac, 0xf3, 0x7c, 0x49, 0xc5, 0x47, 0xec, 0xb3, 0xda, 0x93, 0x87,
             0xc1, 0xb9, 0xcf, 0x2c, 0xb5, 0xf0, 0x85, 0xad, 0xb4, 0x38, 0x67,
             0x88, 0xda, 0x3d, 0xfa, 0x01, 0xb7, 0x54, 0xd9, 0x41, 0x0b, 0x7b,
             0x8a, 0x09, 0xe0, 0x84, 0x7d, 0xbb, 0x89, 0xb2, 0xfc, 0x0b, 0x70,
             0x36, 0x93, 0x56, 0x62, 0xcc, 0xb4, 0xfc, 0xf9, 0x1f, 0x37, 0x92,
             0x9b, 0x3a, 0x4e, 0x7c, 0xad, 0x4b, 0xa6, 0x76, 0x6f, 0xda, 0xc4,
             0x2f, 0x83, 0x53, 0xbd, 0x93, 0xa9, 0x76, 0x89, 0x53, 0xe1, 0x4d,
             0xee, 0x27, 0x11, 0x6f, 0xbc, 0x21, 0xad, 0x42, 0x9f, 0x29, 0xf6,
             0x03, 0xdd, 0xec, 0xfa, 0xa1, 0x78, 0xd2, 0xde, 0x29, 0x2e, 0xd8,
             0x3a, 0x7f, 0xe9, 0x9b, 0x5d, 0xeb, 0x37, 0xb8, 0xb0, 0xa0, 0x66,
             0x3f, 0x02, 0xcd, 0x2a, 0x6e, 0xd3, 0x1c, 0xa5, 0x65, 0xdc, 0x73,
             0xbe, 0x93, 0x54, 0x9a, 0x2b, 0xf8, 0x32, 0x8b, 0xe8, 0xce, 0x9a,
             0x59, 0xd0, 0x05, 0xeb, 0xbb, 0xac, 0xfc, 0x4c, 0x4b, 0x2e, 0xac,
             0x2a, 0xc3, 0x0f, 0x0a, 0xd7, 0x46, 0xaf, 0xfd, 0x22, 0x0d, 0x0d,
             0x54, 0xcc, 0x2f}};
        std::array<std::uint8_t, 256> const rsa3Sig{
            {0x6b, 0x0a, 0x58, 0xaa, 0x74, 0xbd, 0xd8, 0x27, 0x65, 0x5b, 0x21,
             0x39, 0x60, 0x5a, 0xcb, 0x12, 0x37, 0xae, 0xc8, 0x4e, 0xcd, 0xea,
             0x96, 0x4e, 0x4f, 0x1d, 0x56, 0x10, 0xc1, 0xdc, 0x35, 0xa4, 0xd6,
             0xc2, 0x41, 0x65, 0xec, 0xe5, 0x33, 0xc6, 0xd9, 0x55, 0x1d, 0x49,
             0x0e, 0xae, 0xb6, 0x72, 0x00, 0x56, 0xd1, 0x13, 0xdb, 0xef, 0x5c,
             0xe7, 0xc9, 0xa7, 0xe9, 0x62, 0x37, 0xea, 0x0c, 0x0d, 0xf3, 0x6c,
             0x41, 0x50, 0xa1, 0x37, 0xbc, 0xb5, 0xa3, 0x55, 0x11, 0xbd, 0x94,
             0xb6, 0x6e, 0xab, 0xde, 0xaf, 0x47, 0xfd, 0x66, 0x1e, 0x91, 0xa5,
             0x21, 0xd5, 0xbc, 0xc5, 0x8d, 0x65, 0x01, 0x3d, 0x24, 0x56, 0x2a,
             0x4d, 0xfa, 0x32, 0x0d, 0x3f, 0x40, 0xef, 0x9a, 0xa0, 0xf7, 0xa2,
             0xc6, 0x19, 0x89, 0xd5, 0x71, 0x1a, 0x79, 0xe4, 0x8c, 0x88, 0xcd,
             0x76, 0xf0, 0x33, 0x0c, 0x7d, 0x42, 0x39, 0x21, 0xa2, 0x3e, 0xc5,
             0x55, 0x0b, 0xde, 0x51, 0x23, 0xbc, 0x5d, 0x57, 0x19, 0x78, 0xd0,
             0x6a, 0x8e, 0x67, 0x0a, 0x57, 0xad, 0x86, 0xf5, 0xd5, 0x87, 0x5d,
             0xf3, 0x74, 0x02, 0x0f, 0x78, 0x5c, 0x6d, 0x1f, 0x4a, 0x6b, 0x8c,
             0x5c, 0xe7, 0xec, 0xfe, 0xc1, 0xbc, 0xbd, 0xd1, 0xcb, 0xf6, 0x3b,
             0x12, 0x34, 0xe8, 0x6d, 0xbb, 0xe8, 0x5f, 0xdd, 0x37, 0x96, 0x3c,
             0x0f, 0x5c, 0x5c, 0x29, 0x44, 0x2a, 0xf9, 0x96, 0x63, 0x03, 0x93,
             0xbe, 0x0f, 0xce, 0x64, 0xa1, 0x8d, 0xef, 0x4f, 0x40, 0x7c, 0xe5,
             0xb9, 0x19, 0x1f, 0xfe, 0x20, 0x1d, 0x4c, 0x3a, 0xc8, 0x72, 0xaa,
             0xeb, 0x4f, 0xba, 0x4a, 0x68, 0xc1, 0x73, 0x72, 0xf5, 0xd2, 0x28,
             0xa3, 0x7e, 0x70, 0xd2, 0xec, 0x37, 0xc7, 0xe4, 0xf2, 0x44, 0x60,
             0x5c, 0xd5, 0xe2, 0xc5, 0x18, 0x00, 0x04, 0x6b, 0xb3, 0xec, 0x11,
             0xc3, 0x1e, 0x1c}};
        auto const rsa5Msg = "P1P0abcdefghijklmnopqrstuvwxyz"s;
        std::array<std::uint8_t, 256> const rsa5PublicKey{
            {0xc0, 0x00, 0xef, 0x8f, 0x4b, 0x81, 0x10, 0x1e, 0x52, 0xe0, 0x07,
             0x9f, 0x68, 0xe7, 0x2f, 0x92, 0xd4, 0x77, 0x3c, 0x1f, 0xa3, 0xff,
             0x72, 0x64, 0x5b, 0x37, 0xf1, 0xf3, 0xa3, 0xc5, 0xfb, 0xcd, 0xfb,
             0xda, 0xcc, 0x8b, 0x52, 0xe1, 0xde, 0xbc, 0x28, 0x8d, 0xe5, 0xad,
             0xab, 0x86, 0x61, 0x45, 0x97, 0x65, 0x37, 0x68, 0x26, 0x21, 0x92,
             0x17, 0xa3, 0xb0, 0x74, 0x5c, 0x8a, 0x45, 0x8d, 0x87, 0x5b, 0x9b,
             0xd1, 0x7b, 0x07, 0xc4, 0x8c, 0x67, 0xa0, 0xe9, 0x82, 0x0c, 0xe0,
             0x6b, 0xea, 0x91, 0x5c, 0xba, 0xe3, 0xd9, 0x9d, 0x39, 0xfd, 0x77,
             0xac, 0xcb, 0x33, 0x9b, 0x28, 0x51, 0x8d, 0xbf, 0x3e, 0xe4, 0x94,
             0x1c, 0x9a, 0x60, 0x71, 0x4b, 0x34, 0x07, 0x30, 0xda, 0x42, 0x46,
             0x0e, 0xb8, 0xb7, 0x2c, 0xf5, 0x2f, 0x4b, 0x9e, 0xe7, 0x64, 0x81,
             0xa1, 0xa2, 0x05, 0x66, 0x92, 0xe6, 0x75, 0x9f, 0x37, 0xae, 0x40,
             0xa9, 0x16, 0x08, 0x19, 0xe8, 0xdc, 0x47, 0xd6, 0x03, 0x29, 0xab,
             0xcc, 0x58, 0xa2, 0x37, 0x2a, 0x32, 0xb8, 0x15, 0xc7, 0x51, 0x91,
             0x73, 0xb9, 0x1d, 0xc6, 0xd0, 0x4f, 0x85, 0x86, 0xd5, 0xb3, 0x21,
             0x1a, 0x2a, 0x6c, 0xeb, 0x7f, 0xfe, 0x84, 0x17, 0x10, 0x2d, 0x0e,
             0xb4, 0xe1, 0xc2, 0x48, 0x4c, 0x3f, 0x61, 0xc7, 0x59, 0x75, 0xa7,
             0xc1, 0x75, 0xce, 0x67, 0x17, 0x42, 0x2a, 0x2f, 0x96, 0xef, 0x8a,
             0x2d, 0x74, 0xd2, 0x13, 0x68, 0xe1, 0xe9, 0xea, 0xfb, 0x73, 0x68,
             0xed, 0x8d, 0xd3, 0xac, 0x49, 0x09, 0xf9, 0xec, 0x62, 0xdf, 0x53,
             0xab, 0xfe, 0x90, 0x64, 0x4b, 0x92, 0x60, 0x0d, 0xdd, 0x00, 0xfe,
             0x02, 0xe6, 0xf3, 0x9b, 0x2b, 0xac, 0x4f, 0x70, 0xe8, 0x5b, 0x69,
             0x9c, 0x40, 0xd3, 0xeb, 0x37, 0xad, 0x6f, 0x37, 0xab, 0xf3, 0x79,
             0x8e, 0xcb, 0x1d}};
        std::array<std::uint8_t, 256> const rsa5Sig{
            {0x00, 0xc1, 0x8c, 0xd8, 0x71, 0xd5, 0xf4, 0xd9, 0x32, 0xd2, 0xeb,
             0x87, 0x26, 0x58, 0x26, 0x88, 0xf7, 0x44, 0x90, 0x1e, 0x91, 0xa9,
             0x48, 0xb4, 0x1c, 0x78, 0x36, 0x0e, 0x22, 0x8a, 0x9c, 0x91, 0xcf,
             0x7f, 0x7a, 0xfa, 0x60, 0xb5, 0x1d, 0x25, 0x6f, 0x77, 0xfc, 0xa7,
             0x2e, 0x9d, 0xba, 0x2e, 0x72, 0x06, 0xb9, 0x75, 0x00, 0x3c, 0x36,
             0x92, 0xdf, 0x85, 0x9f, 0xc8, 0xec, 0xcc, 0x79, 0xf5, 0xb5, 0xac,
             0xc7, 0x71, 0x33, 0x09, 0x09, 0xd2, 0xf3, 0x3b, 0x0d, 0x4d, 0x9f,
             0x03, 0x5b, 0x91, 0x9f, 0x4b, 0x3f, 0x06, 0xf5, 0x3b, 0xc9, 0xbd,
             0x96, 0x3b, 0x6c, 0xb0, 0x3c, 0x2c, 0x48, 0x12, 0xd9, 0x1c, 0xcf,
             0x04, 0xcf, 0x63, 0x6f, 0x85, 0x7b, 0x20, 0xd7, 0x3f, 0x48, 0x34,
             0x8e, 0x4e, 0x49, 0xde, 0x27, 0xca, 0xe6, 0x1f, 0xe9, 0x23, 0xdb,
             0x88, 0x0b, 0x8e, 0xe0, 0xe8, 0xda, 0xe9, 0xf1, 0x32, 0xdc, 0xab,
             0xc9, 0xa0, 0x21, 0x01, 0xe3, 0x6f, 0xcb, 0xcd, 0x06, 0x02, 0xb9,
             0x97, 0xd3, 0xdd, 0xc4, 0x9e, 0xf8, 0xc4, 0xef, 0x0b, 0x75, 0x06,
             0xfb, 0x2f, 0xa8, 0x69, 0x38, 0x2d, 0x90, 0x50, 0x82, 0x35, 0xbd,
             0x03, 0xa2, 0xb9, 0xd4, 0xc1, 0x90, 0x9e, 0xeb, 0xd3, 0x5b, 0xf8,
             0x2b, 0x2e, 0xcc, 0xd5, 0x02, 0x30, 0x39, 0xff, 0xf0, 0xb4, 0x7d,
             0x29, 0x11, 0xab, 0xa7, 0x42, 0xfa, 0x39, 0x2c, 0x08, 0xc9, 0xcf,
             0x3b, 0xf8, 0xbc, 0x00, 0x77, 0xb1, 0x04, 0x92, 0xc3, 0x6c, 0xb6,
             0x14, 0xba, 0x6d, 0x28, 0x66, 0xde, 0x70, 0x5c, 0xb0, 0x80, 0xc6,
             0x4b, 0xf1, 0xf7, 0x1d, 0x4e, 0x84, 0x87, 0x40, 0x47, 0x7f, 0x3d,
             0x16, 0x6b, 0xee, 0x49, 0x74, 0x04, 0xac, 0x5d, 0x03, 0x9f, 0x5d,
             0xa0, 0x1a, 0x2d, 0x0e, 0xee, 0x1c, 0xe8, 0x6b, 0x2d, 0x56, 0x07,
             0x4c, 0x22, 0x52}};
        auto const thresh4Msg = "P1P0abcdefghijklmnopqrstuvwxyz"s;
        std::array<std::uint8_t, 32> const Preim6CondConditionFingerprint = {
            {0x5d, 0xa0, 0x30, 0xef, 0xfd, 0xe1, 0x75, 0x11, 0x51, 0xe8, 0x5f,
             0x5e, 0x54, 0x2d, 0x6a, 0x5b, 0xd1, 0x5c, 0xc9, 0x33, 0x21, 0x2c,
             0xe2, 0x68, 0xfc, 0xfd, 0x53, 0xee, 0x93, 0x58, 0xeb, 0x4e}};
        Condition const Preim6Cond{Type::preimageSha256,
                                   9,
                                   Preim6CondConditionFingerprint,
                                   std::bitset<5>{0}};
        std::array<std::uint8_t, 32> const Rsa7CondConditionFingerprint = {
            {0x6c, 0x7b, 0xea, 0x83, 0xa1, 0xf4, 0x82, 0x3d, 0x36, 0xe7, 0x6e,
             0xae, 0x1a, 0xbc, 0xa0, 0xba, 0x90, 0x3d, 0x96, 0xc1, 0xe6, 0xad,
             0x3a, 0x47, 0xa5, 0xcb, 0x88, 0xab, 0x3c, 0x5f, 0xcc, 0xd5}};
        Condition const Rsa7Cond{Type::rsaSha256,
                                 65536,
                                 Rsa7CondConditionFingerprint,
                                 std::bitset<5>{0}};
        std::array<std::uint8_t, 32> const Ed8CondConditionFingerprint = {
            {0xf1, 0x68, 0x96, 0xa6, 0x2e, 0xef, 0x7f, 0x47, 0x06, 0x51, 0x4c,
             0xc6, 0x7e, 0x24, 0xf7, 0x29, 0x84, 0x9c, 0xd6, 0xb0, 0xd9, 0x4b,
             0xd9, 0x0f, 0xc9, 0x34, 0x01, 0x9d, 0x92, 0xeb, 0xbc, 0x0a}};
        Condition const Ed8Cond{Type::ed25519Sha256,
                                131072,
                                Ed8CondConditionFingerprint,
                                std::bitset<5>{0}};
        auto const thresh2Msg = "P1P0abcdefghijklmnopqrstuvwxyz"s;
        std::array<std::uint8_t, 32> const Preim9CondConditionFingerprint = {
            {0x5d, 0xa0, 0x30, 0xef, 0xfd, 0xe1, 0x75, 0x11, 0x51, 0xe8, 0x5f,
             0x5e, 0x54, 0x2d, 0x6a, 0x5b, 0xd1, 0x5c, 0xc9, 0x33, 0x21, 0x2c,
             0xe2, 0x68, 0xfc, 0xfd, 0x53, 0xee, 0x93, 0x58, 0xeb, 0x4e}};
        Condition const Preim9Cond{Type::preimageSha256,
                                   9,
                                   Preim9CondConditionFingerprint,
                                   std::bitset<5>{0}};
        std::array<std::uint8_t, 32> const Rsa10CondConditionFingerprint = {
            {0x3c, 0x73, 0x38, 0xcf, 0x23, 0xc6, 0x31, 0x53, 0x28, 0xc4, 0x27,
             0xf8, 0x95, 0x87, 0x99, 0x83, 0x2d, 0x35, 0x3c, 0x03, 0x9b, 0xd1,
             0xff, 0xff, 0x2e, 0x53, 0x20, 0xe9, 0x5e, 0x62, 0xb9, 0xb7}};
        Condition const Rsa10Cond{Type::rsaSha256,
                                  65536,
                                  Rsa10CondConditionFingerprint,
                                  std::bitset<5>{0}};
        std::array<std::uint8_t, 32> const Ed11CondConditionFingerprint = {
            {0x41, 0x80, 0x08, 0xb2, 0x60, 0x74, 0x57, 0x6d, 0xac, 0xed, 0x74,
             0x7f, 0x54, 0xdb, 0x96, 0x18, 0x91, 0x06, 0x0a, 0x95, 0xa1, 0x49,
             0x17, 0xc7, 0x65, 0xe3, 0x94, 0xc8, 0x5e, 0x2c, 0x92, 0x20}};
        Condition const Ed11Cond{Type::ed25519Sha256,
                                 131072,
                                 Ed11CondConditionFingerprint,
                                 std::bitset<5>{0}};
        auto const prefix1Prefix = "P1"s;
        auto const prefix1Msg = "P0abcdefghijklmnopqrstuvwxyz"s;
        auto const prefix1MaxMsgLength = 28;
        auto const prefix0Prefix = "P0"s;
        auto const prefix0Msg = "abcdefghijklmnopqrstuvwxyz"s;
        auto const prefix0MaxMsgLength = 26;

        auto rsa3 = std::make_unique<RsaSha256>(
            makeSlice(rsa3PublicKey), makeSlice(rsa3Sig));
        auto rsa5 = std::make_unique<RsaSha256>(
            makeSlice(rsa5PublicKey), makeSlice(rsa5Sig));
        std::vector<std::unique_ptr<Fulfillment>> thresh4Subfulfillments;
        thresh4Subfulfillments.emplace_back(std::move(rsa5));
        std::vector<Condition> thresh4Subconditions{
            {Preim6Cond, Rsa7Cond, Ed8Cond}};
        auto thresh4 = std::make_unique<ThresholdSha256>(
            std::move(thresh4Subfulfillments), std::move(thresh4Subconditions));
        std::vector<std::unique_ptr<Fulfillment>> thresh2Subfulfillments;
        thresh2Subfulfillments.emplace_back(std::move(rsa3));
        thresh2Subfulfillments.emplace_back(std::move(thresh4));
        std::vector<Condition> thresh2Subconditions{
            {Preim9Cond, Rsa10Cond, Ed11Cond}};
        auto thresh2 = std::make_unique<ThresholdSha256>(
            std::move(thresh2Subfulfillments), std::move(thresh2Subconditions));
        auto prefix1 = std::make_unique<PrefixSha256>(
            makeSlice(prefix1Prefix), prefix1MaxMsgLength, std::move(thresh2));
        auto prefix0 = std::make_unique<PrefixSha256>(
            makeSlice(prefix0Prefix), prefix0MaxMsgLength, std::move(prefix1));
        {
            auto prefix0EncodedFulfillment =
                "\xa1\x82\x05\x38\x80\x02\x50\x30\x81\x01\x1a\xa2\x82\x05\x2d"
                "\xa1\x82\x05\x29\x80\x02\x50\x31\x81\x01\x1c\xa2\x82\x05\x1e"
                "\xa2\x82\x05\x1a\xa0\x82\x04\x9b\xa2\x82\x02\x8b\xa0\x82\x02"
                "\x0c\xa3\x82\x02\x08\x80\x82\x01\x00\xc0\x00\xef\x8f\x4b\x81"
                "\x10\x1e\x52\xe0\x07\x9f\x68\xe7\x2f\x92\xd4\x77\x3c\x1f\xa3"
                "\xff\x72\x64\x5b\x37\xf1\xf3\xa3\xc5\xfb\xcd\xfb\xda\xcc\x8b"
                "\x52\xe1\xde\xbc\x28\x8d\xe5\xad\xab\x86\x61\x45\x97\x65\x37"
                "\x68\x26\x21\x92\x17\xa3\xb0\x74\x5c\x8a\x45\x8d\x87\x5b\x9b"
                "\xd1\x7b\x07\xc4\x8c\x67\xa0\xe9\x82\x0c\xe0\x6b\xea\x91\x5c"
                "\xba\xe3\xd9\x9d\x39\xfd\x77\xac\xcb\x33\x9b\x28\x51\x8d\xbf"
                "\x3e\xe4\x94\x1c\x9a\x60\x71\x4b\x34\x07\x30\xda\x42\x46\x0e"
                "\xb8\xb7\x2c\xf5\x2f\x4b\x9e\xe7\x64\x81\xa1\xa2\x05\x66\x92"
                "\xe6\x75\x9f\x37\xae\x40\xa9\x16\x08\x19\xe8\xdc\x47\xd6\x03"
                "\x29\xab\xcc\x58\xa2\x37\x2a\x32\xb8\x15\xc7\x51\x91\x73\xb9"
                "\x1d\xc6\xd0\x4f\x85\x86\xd5\xb3\x21\x1a\x2a\x6c\xeb\x7f\xfe"
                "\x84\x17\x10\x2d\x0e\xb4\xe1\xc2\x48\x4c\x3f\x61\xc7\x59\x75"
                "\xa7\xc1\x75\xce\x67\x17\x42\x2a\x2f\x96\xef\x8a\x2d\x74\xd2"
                "\x13\x68\xe1\xe9\xea\xfb\x73\x68\xed\x8d\xd3\xac\x49\x09\xf9"
                "\xec\x62\xdf\x53\xab\xfe\x90\x64\x4b\x92\x60\x0d\xdd\x00\xfe"
                "\x02\xe6\xf3\x9b\x2b\xac\x4f\x70\xe8\x5b\x69\x9c\x40\xd3\xeb"
                "\x37\xad\x6f\x37\xab\xf3\x79\x8e\xcb\x1d\x81\x82\x01\x00\x00"
                "\xc1\x8c\xd8\x71\xd5\xf4\xd9\x32\xd2\xeb\x87\x26\x58\x26\x88"
                "\xf7\x44\x90\x1e\x91\xa9\x48\xb4\x1c\x78\x36\x0e\x22\x8a\x9c"
                "\x91\xcf\x7f\x7a\xfa\x60\xb5\x1d\x25\x6f\x77\xfc\xa7\x2e\x9d"
                "\xba\x2e\x72\x06\xb9\x75\x00\x3c\x36\x92\xdf\x85\x9f\xc8\xec"
                "\xcc\x79\xf5\xb5\xac\xc7\x71\x33\x09\x09\xd2\xf3\x3b\x0d\x4d"
                "\x9f\x03\x5b\x91\x9f\x4b\x3f\x06\xf5\x3b\xc9\xbd\x96\x3b\x6c"
                "\xb0\x3c\x2c\x48\x12\xd9\x1c\xcf\x04\xcf\x63\x6f\x85\x7b\x20"
                "\xd7\x3f\x48\x34\x8e\x4e\x49\xde\x27\xca\xe6\x1f\xe9\x23\xdb"
                "\x88\x0b\x8e\xe0\xe8\xda\xe9\xf1\x32\xdc\xab\xc9\xa0\x21\x01"
                "\xe3\x6f\xcb\xcd\x06\x02\xb9\x97\xd3\xdd\xc4\x9e\xf8\xc4\xef"
                "\x0b\x75\x06\xfb\x2f\xa8\x69\x38\x2d\x90\x50\x82\x35\xbd\x03"
                "\xa2\xb9\xd4\xc1\x90\x9e\xeb\xd3\x5b\xf8\x2b\x2e\xcc\xd5\x02"
                "\x30\x39\xff\xf0\xb4\x7d\x29\x11\xab\xa7\x42\xfa\x39\x2c\x08"
                "\xc9\xcf\x3b\xf8\xbc\x00\x77\xb1\x04\x92\xc3\x6c\xb6\x14\xba"
                "\x6d\x28\x66\xde\x70\x5c\xb0\x80\xc6\x4b\xf1\xf7\x1d\x4e\x84"
                "\x87\x40\x47\x7f\x3d\x16\x6b\xee\x49\x74\x04\xac\x5d\x03\x9f"
                "\x5d\xa0\x1a\x2d\x0e\xee\x1c\xe8\x6b\x2d\x56\x07\x4c\x22\x52"
                "\xa1\x79\xa0\x25\x80\x20\x5d\xa0\x30\xef\xfd\xe1\x75\x11\x51"
                "\xe8\x5f\x5e\x54\x2d\x6a\x5b\xd1\x5c\xc9\x33\x21\x2c\xe2\x68"
                "\xfc\xfd\x53\xee\x93\x58\xeb\x4e\x81\x01\x09\xa3\x27\x80\x20"
                "\x6c\x7b\xea\x83\xa1\xf4\x82\x3d\x36\xe7\x6e\xae\x1a\xbc\xa0"
                "\xba\x90\x3d\x96\xc1\xe6\xad\x3a\x47\xa5\xcb\x88\xab\x3c\x5f"
                "\xcc\xd5\x81\x03\x01\x00\x00\xa4\x27\x80\x20\xf1\x68\x96\xa6"
                "\x2e\xef\x7f\x47\x06\x51\x4c\xc6\x7e\x24\xf7\x29\x84\x9c\xd6"
                "\xb0\xd9\x4b\xd9\x0f\xc9\x34\x01\x9d\x92\xeb\xbc\x0a\x81\x03"
                "\x02\x00\x00\xa3\x82\x02\x08\x80\x82\x01\x00\xbd\xd1\xc7\xf0"
                "\xb0\x3a\xa5\x5b\x3e\x49\x8d\x4e\x00\x54\x89\xb9\x89\xcd\x4b"
                "\x43\xde\x59\xf6\x7a\x67\x5c\x3a\xc6\xcf\x82\x3f\x35\x9c\xcc"
                "\xda\xcd\xd3\x97\x86\x5b\xe9\xf6\x05\x55\x0b\x26\xef\x1e\x88"
                "\xd5\xb6\xba\x14\x0a\xb2\x76\xb9\xb3\x46\x0c\xc0\x80\x17\x13"
                "\x68\x23\xdc\xec\x10\x18\xfc\xaa\xbe\xb3\xc4\xc7\xa9\x84\xa6"
                "\x4e\x5c\x08\x6b\x7b\x4c\x81\x91\x79\x5d\x90\x06\x15\xbb\x76"
                "\x2f\x5c\x53\x60\x0f\xac\xf3\x7c\x49\xc5\x47\xec\xb3\xda\x93"
                "\x87\xc1\xb9\xcf\x2c\xb5\xf0\x85\xad\xb4\x38\x67\x88\xda\x3d"
                "\xfa\x01\xb7\x54\xd9\x41\x0b\x7b\x8a\x09\xe0\x84\x7d\xbb\x89"
                "\xb2\xfc\x0b\x70\x36\x93\x56\x62\xcc\xb4\xfc\xf9\x1f\x37\x92"
                "\x9b\x3a\x4e\x7c\xad\x4b\xa6\x76\x6f\xda\xc4\x2f\x83\x53\xbd"
                "\x93\xa9\x76\x89\x53\xe1\x4d\xee\x27\x11\x6f\xbc\x21\xad\x42"
                "\x9f\x29\xf6\x03\xdd\xec\xfa\xa1\x78\xd2\xde\x29\x2e\xd8\x3a"
                "\x7f\xe9\x9b\x5d\xeb\x37\xb8\xb0\xa0\x66\x3f\x02\xcd\x2a\x6e"
                "\xd3\x1c\xa5\x65\xdc\x73\xbe\x93\x54\x9a\x2b\xf8\x32\x8b\xe8"
                "\xce\x9a\x59\xd0\x05\xeb\xbb\xac\xfc\x4c\x4b\x2e\xac\x2a\xc3"
                "\x0f\x0a\xd7\x46\xaf\xfd\x22\x0d\x0d\x54\xcc\x2f\x81\x82\x01"
                "\x00\x6b\x0a\x58\xaa\x74\xbd\xd8\x27\x65\x5b\x21\x39\x60\x5a"
                "\xcb\x12\x37\xae\xc8\x4e\xcd\xea\x96\x4e\x4f\x1d\x56\x10\xc1"
                "\xdc\x35\xa4\xd6\xc2\x41\x65\xec\xe5\x33\xc6\xd9\x55\x1d\x49"
                "\x0e\xae\xb6\x72\x00\x56\xd1\x13\xdb\xef\x5c\xe7\xc9\xa7\xe9"
                "\x62\x37\xea\x0c\x0d\xf3\x6c\x41\x50\xa1\x37\xbc\xb5\xa3\x55"
                "\x11\xbd\x94\xb6\x6e\xab\xde\xaf\x47\xfd\x66\x1e\x91\xa5\x21"
                "\xd5\xbc\xc5\x8d\x65\x01\x3d\x24\x56\x2a\x4d\xfa\x32\x0d\x3f"
                "\x40\xef\x9a\xa0\xf7\xa2\xc6\x19\x89\xd5\x71\x1a\x79\xe4\x8c"
                "\x88\xcd\x76\xf0\x33\x0c\x7d\x42\x39\x21\xa2\x3e\xc5\x55\x0b"
                "\xde\x51\x23\xbc\x5d\x57\x19\x78\xd0\x6a\x8e\x67\x0a\x57\xad"
                "\x86\xf5\xd5\x87\x5d\xf3\x74\x02\x0f\x78\x5c\x6d\x1f\x4a\x6b"
                "\x8c\x5c\xe7\xec\xfe\xc1\xbc\xbd\xd1\xcb\xf6\x3b\x12\x34\xe8"
                "\x6d\xbb\xe8\x5f\xdd\x37\x96\x3c\x0f\x5c\x5c\x29\x44\x2a\xf9"
                "\x96\x63\x03\x93\xbe\x0f\xce\x64\xa1\x8d\xef\x4f\x40\x7c\xe5"
                "\xb9\x19\x1f\xfe\x20\x1d\x4c\x3a\xc8\x72\xaa\xeb\x4f\xba\x4a"
                "\x68\xc1\x73\x72\xf5\xd2\x28\xa3\x7e\x70\xd2\xec\x37\xc7\xe4"
                "\xf2\x44\x60\x5c\xd5\xe2\xc5\x18\x00\x04\x6b\xb3\xec\x11\xc3"
                "\x1e\x1c\xa1\x79\xa0\x25\x80\x20\x5d\xa0\x30\xef\xfd\xe1\x75"
                "\x11\x51\xe8\x5f\x5e\x54\x2d\x6a\x5b\xd1\x5c\xc9\x33\x21\x2c"
                "\xe2\x68\xfc\xfd\x53\xee\x93\x58\xeb\x4e\x81\x01\x09\xa3\x27"
                "\x80\x20\x3c\x73\x38\xcf\x23\xc6\x31\x53\x28\xc4\x27\xf8\x95"
                "\x87\x99\x83\x2d\x35\x3c\x03\x9b\xd1\xff\xff\x2e\x53\x20\xe9"
                "\x5e\x62\xb9\xb7\x81\x03\x01\x00\x00\xa4\x27\x80\x20\x41\x80"
                "\x08\xb2\x60\x74\x57\x6d\xac\xed\x74\x7f\x54\xdb\x96\x18\x91"
                "\x06\x0a\x95\xa1\x49\x17\xc7\x65\xe3\x94\xc8\x5e\x2c\x92\x20"
                "\x81\x03\x02\x00\x00"s;
            auto const prefix0EncodedCondition =
                "\xa1\x2b\x80\x20\x5d\xd0\xa2\x70\xb1\x19\x12\x97\x29\xb9\xff"
                "\x0b\xfd\xa8\xe4\x8a\x59\x43\x07\x33\x5b\x8d\x04\x04\x7c\x2e"
                "\x49\x7d\x9e\xda\xe1\x72\x81\x03\x04\x2c\x3a\x82\x02\x03\xb8"s;
            auto const prefix0EncodedFingerprint =
                "\x30\x36\x80\x02\x50\x30\x81\x01\x1a\xa2\x2d\xa1\x2b\x80\x20"
                "\x06\xdf\x91\x00\x69\x8b\xd4\xab\x4e\xeb\x67\x25\xf2\x2d\xc7"
                "\x6b\x48\x24\x90\x5c\x6f\xaf\x09\x47\x1e\x94\xf8\x0d\x93\x3f"
                "\xde\xf3\x81\x03\x04\x28\x1e\x82\x02\x03\xb8"s;
            check(
                std::move(prefix0),
                prefix0Msg,
                std::move(prefix0EncodedFulfillment),
                prefix0EncodedCondition,
                prefix0EncodedFingerprint);
        }
    }

    void
    testPrefix13()
    {
        testcase("Prefix13");

        using namespace std::string_literals;
        using namespace ripple::cryptoconditions;

        // Fulfillment structure
        // * prefix0
        // ** prefix1
        // *** thresh2
        // **** Preim9Cond
        // **** Rsa10Cond
        // **** Ed11Cond
        // **** Thresh12Cond
        // **** rsa3
        // **** thresh4
        // ***** Preim6Cond
        // ***** Rsa7Cond
        // ***** Ed8Cond
        // ***** rsa5

        auto const rsa3Msg = "P1P0abcdefghijklmnopqrstuvwxyz"s;
        std::array<std::uint8_t, 256> const rsa3PublicKey{
            {0xbd, 0xd1, 0xc7, 0xf0, 0xb0, 0x3a, 0xa5, 0x5b, 0x3e, 0x49, 0x8d,
             0x4e, 0x00, 0x54, 0x89, 0xb9, 0x89, 0xcd, 0x4b, 0x43, 0xde, 0x59,
             0xf6, 0x7a, 0x67, 0x5c, 0x3a, 0xc6, 0xcf, 0x82, 0x3f, 0x35, 0x9c,
             0xcc, 0xda, 0xcd, 0xd3, 0x97, 0x86, 0x5b, 0xe9, 0xf6, 0x05, 0x55,
             0x0b, 0x26, 0xef, 0x1e, 0x88, 0xd5, 0xb6, 0xba, 0x14, 0x0a, 0xb2,
             0x76, 0xb9, 0xb3, 0x46, 0x0c, 0xc0, 0x80, 0x17, 0x13, 0x68, 0x23,
             0xdc, 0xec, 0x10, 0x18, 0xfc, 0xaa, 0xbe, 0xb3, 0xc4, 0xc7, 0xa9,
             0x84, 0xa6, 0x4e, 0x5c, 0x08, 0x6b, 0x7b, 0x4c, 0x81, 0x91, 0x79,
             0x5d, 0x90, 0x06, 0x15, 0xbb, 0x76, 0x2f, 0x5c, 0x53, 0x60, 0x0f,
             0xac, 0xf3, 0x7c, 0x49, 0xc5, 0x47, 0xec, 0xb3, 0xda, 0x93, 0x87,
             0xc1, 0xb9, 0xcf, 0x2c, 0xb5, 0xf0, 0x85, 0xad, 0xb4, 0x38, 0x67,
             0x88, 0xda, 0x3d, 0xfa, 0x01, 0xb7, 0x54, 0xd9, 0x41, 0x0b, 0x7b,
             0x8a, 0x09, 0xe0, 0x84, 0x7d, 0xbb, 0x89, 0xb2, 0xfc, 0x0b, 0x70,
             0x36, 0x93, 0x56, 0x62, 0xcc, 0xb4, 0xfc, 0xf9, 0x1f, 0x37, 0x92,
             0x9b, 0x3a, 0x4e, 0x7c, 0xad, 0x4b, 0xa6, 0x76, 0x6f, 0xda, 0xc4,
             0x2f, 0x83, 0x53, 0xbd, 0x93, 0xa9, 0x76, 0x89, 0x53, 0xe1, 0x4d,
             0xee, 0x27, 0x11, 0x6f, 0xbc, 0x21, 0xad, 0x42, 0x9f, 0x29, 0xf6,
             0x03, 0xdd, 0xec, 0xfa, 0xa1, 0x78, 0xd2, 0xde, 0x29, 0x2e, 0xd8,
             0x3a, 0x7f, 0xe9, 0x9b, 0x5d, 0xeb, 0x37, 0xb8, 0xb0, 0xa0, 0x66,
             0x3f, 0x02, 0xcd, 0x2a, 0x6e, 0xd3, 0x1c, 0xa5, 0x65, 0xdc, 0x73,
             0xbe, 0x93, 0x54, 0x9a, 0x2b, 0xf8, 0x32, 0x8b, 0xe8, 0xce, 0x9a,
             0x59, 0xd0, 0x05, 0xeb, 0xbb, 0xac, 0xfc, 0x4c, 0x4b, 0x2e, 0xac,
             0x2a, 0xc3, 0x0f, 0x0a, 0xd7, 0x46, 0xaf, 0xfd, 0x22, 0x0d, 0x0d,
             0x54, 0xcc, 0x2f}};
        std::array<std::uint8_t, 256> const rsa3Sig{
            {0x14, 0x77, 0x7a, 0x2e, 0x34, 0xc6, 0x4c, 0x31, 0x8f, 0x2a, 0x11,
             0x56, 0x3a, 0x63, 0x62, 0xb1, 0x99, 0x5b, 0x7e, 0x63, 0x2b, 0xe3,
             0x1f, 0x36, 0xdf, 0x96, 0xe6, 0xfc, 0xb8, 0x5f, 0xf4, 0xaa, 0xa8,
             0xa3, 0xf7, 0x4f, 0x71, 0xd6, 0x59, 0x76, 0x0c, 0x05, 0xa3, 0x9b,
             0xdb, 0x40, 0x80, 0x28, 0x68, 0xe1, 0x74, 0x8c, 0x29, 0x67, 0x20,
             0x96, 0x4f, 0x1a, 0xfd, 0x1e, 0x0a, 0x55, 0x4a, 0x2f, 0x49, 0x43,
             0x66, 0x53, 0x2a, 0xa2, 0xd1, 0x08, 0xcf, 0xe3, 0x52, 0xce, 0xd8,
             0x96, 0x32, 0x05, 0x6c, 0xb8, 0x90, 0x2b, 0x5e, 0x18, 0x62, 0x6c,
             0xe1, 0xd9, 0x35, 0xa4, 0x7d, 0x0d, 0x01, 0x31, 0x8b, 0xfa, 0x98,
             0xe0, 0xa2, 0xb3, 0x2a, 0x6e, 0xc5, 0x4c, 0xe2, 0x9f, 0x37, 0xb7,
             0x4f, 0xd1, 0xaa, 0x45, 0xa4, 0x2a, 0x5d, 0x96, 0x3d, 0x5a, 0xb6,
             0xa5, 0x26, 0x76, 0xa8, 0x39, 0x78, 0xd4, 0x88, 0xa4, 0xfd, 0x5c,
             0x94, 0x1e, 0xce, 0xb0, 0xd7, 0x87, 0x4c, 0x83, 0xae, 0xac, 0xfa,
             0x4e, 0x1d, 0xf2, 0xfa, 0xfb, 0x86, 0x9d, 0x2c, 0x26, 0x76, 0x0c,
             0xfb, 0xbd, 0x30, 0xe8, 0x56, 0x7c, 0x23, 0xda, 0x01, 0x02, 0xcd,
             0x1e, 0xea, 0x9c, 0x04, 0xbf, 0xfe, 0x9f, 0xa9, 0x1d, 0x49, 0xcd,
             0x6e, 0x21, 0x58, 0x3b, 0xed, 0xac, 0xc4, 0xaa, 0xb4, 0x15, 0x11,
             0xe5, 0xdb, 0x31, 0x52, 0xbf, 0xfc, 0x76, 0xa5, 0xff, 0xf2, 0xda,
             0x77, 0x1f, 0xa0, 0xc5, 0x49, 0xeb, 0x04, 0x5c, 0x9f, 0xad, 0xcb,
             0x75, 0xfb, 0xcd, 0x0d, 0x03, 0x5c, 0x38, 0x90, 0x57, 0x47, 0xd8,
             0x42, 0xbe, 0xb3, 0x3e, 0xa1, 0xaf, 0xe9, 0x37, 0x27, 0x01, 0x52,
             0x68, 0xab, 0xe9, 0x9e, 0x12, 0x5e, 0x9d, 0xe2, 0x22, 0xde, 0xe4,
             0x85, 0x95, 0x60, 0x74, 0x02, 0xbc, 0x91, 0xf1, 0x5c, 0xf8, 0xb8,
             0x26, 0x53, 0x7f}};
        auto const rsa5Msg = "P1P0abcdefghijklmnopqrstuvwxyz"s;
        std::array<std::uint8_t, 256> const rsa5PublicKey{
            {0xc0, 0x00, 0xef, 0x8f, 0x4b, 0x81, 0x10, 0x1e, 0x52, 0xe0, 0x07,
             0x9f, 0x68, 0xe7, 0x2f, 0x92, 0xd4, 0x77, 0x3c, 0x1f, 0xa3, 0xff,
             0x72, 0x64, 0x5b, 0x37, 0xf1, 0xf3, 0xa3, 0xc5, 0xfb, 0xcd, 0xfb,
             0xda, 0xcc, 0x8b, 0x52, 0xe1, 0xde, 0xbc, 0x28, 0x8d, 0xe5, 0xad,
             0xab, 0x86, 0x61, 0x45, 0x97, 0x65, 0x37, 0x68, 0x26, 0x21, 0x92,
             0x17, 0xa3, 0xb0, 0x74, 0x5c, 0x8a, 0x45, 0x8d, 0x87, 0x5b, 0x9b,
             0xd1, 0x7b, 0x07, 0xc4, 0x8c, 0x67, 0xa0, 0xe9, 0x82, 0x0c, 0xe0,
             0x6b, 0xea, 0x91, 0x5c, 0xba, 0xe3, 0xd9, 0x9d, 0x39, 0xfd, 0x77,
             0xac, 0xcb, 0x33, 0x9b, 0x28, 0x51, 0x8d, 0xbf, 0x3e, 0xe4, 0x94,
             0x1c, 0x9a, 0x60, 0x71, 0x4b, 0x34, 0x07, 0x30, 0xda, 0x42, 0x46,
             0x0e, 0xb8, 0xb7, 0x2c, 0xf5, 0x2f, 0x4b, 0x9e, 0xe7, 0x64, 0x81,
             0xa1, 0xa2, 0x05, 0x66, 0x92, 0xe6, 0x75, 0x9f, 0x37, 0xae, 0x40,
             0xa9, 0x16, 0x08, 0x19, 0xe8, 0xdc, 0x47, 0xd6, 0x03, 0x29, 0xab,
             0xcc, 0x58, 0xa2, 0x37, 0x2a, 0x32, 0xb8, 0x15, 0xc7, 0x51, 0x91,
             0x73, 0xb9, 0x1d, 0xc6, 0xd0, 0x4f, 0x85, 0x86, 0xd5, 0xb3, 0x21,
             0x1a, 0x2a, 0x6c, 0xeb, 0x7f, 0xfe, 0x84, 0x17, 0x10, 0x2d, 0x0e,
             0xb4, 0xe1, 0xc2, 0x48, 0x4c, 0x3f, 0x61, 0xc7, 0x59, 0x75, 0xa7,
             0xc1, 0x75, 0xce, 0x67, 0x17, 0x42, 0x2a, 0x2f, 0x96, 0xef, 0x8a,
             0x2d, 0x74, 0xd2, 0x13, 0x68, 0xe1, 0xe9, 0xea, 0xfb, 0x73, 0x68,
             0xed, 0x8d, 0xd3, 0xac, 0x49, 0x09, 0xf9, 0xec, 0x62, 0xdf, 0x53,
             0xab, 0xfe, 0x90, 0x64, 0x4b, 0x92, 0x60, 0x0d, 0xdd, 0x00, 0xfe,
             0x02, 0xe6, 0xf3, 0x9b, 0x2b, 0xac, 0x4f, 0x70, 0xe8, 0x5b, 0x69,
             0x9c, 0x40, 0xd3, 0xeb, 0x37, 0xad, 0x6f, 0x37, 0xab, 0xf3, 0x79,
             0x8e, 0xcb, 0x1d}};
        std::array<std::uint8_t, 256> const rsa5Sig{
            {0x8e, 0x9d, 0x0d, 0x51, 0xd2, 0xad, 0x9f, 0x98, 0xb7, 0x8b, 0x84,
             0x54, 0x18, 0xb9, 0xc1, 0x8e, 0x6d, 0xfe, 0xdf, 0x1e, 0x90, 0x0c,
             0x68, 0x6c, 0xa4, 0x21, 0x57, 0x97, 0x39, 0xd7, 0x12, 0x24, 0x31,
             0x08, 0x3a, 0xc6, 0xc3, 0x05, 0xb2, 0x8d, 0x0b, 0xf2, 0x80, 0x3c,
             0x25, 0x42, 0xe4, 0x8e, 0x02, 0xfa, 0x01, 0xfe, 0x08, 0x6f, 0xe3,
             0x37, 0xb4, 0x29, 0x9f, 0x97, 0xbf, 0x5f, 0xbd, 0xe3, 0xa8, 0x44,
             0xa7, 0x9d, 0x60, 0x31, 0xdd, 0x46, 0x6a, 0xa7, 0xfd, 0xbf, 0xb5,
             0xaa, 0xa4, 0x5d, 0x71, 0x6f, 0xf2, 0xe3, 0x15, 0xc2, 0xb5, 0x39,
             0x5d, 0x7c, 0xb9, 0xb5, 0x39, 0xc4, 0xcb, 0x1f, 0xe4, 0xee, 0x0f,
             0x1f, 0xc6, 0x5a, 0xb5, 0x29, 0x1c, 0xe7, 0xa7, 0x53, 0x5d, 0xa4,
             0x3f, 0x89, 0x35, 0x4b, 0x33, 0x66, 0xe5, 0xb8, 0x78, 0x64, 0x74,
             0xab, 0x3f, 0x7c, 0x88, 0xe8, 0x3e, 0x4a, 0xda, 0xf0, 0x52, 0xa2,
             0x28, 0xaa, 0xd5, 0x7f, 0xaa, 0xd6, 0xfe, 0xf4, 0xeb, 0x0a, 0x04,
             0x9c, 0x96, 0x7f, 0xa9, 0x76, 0x65, 0xf2, 0x74, 0x17, 0xc6, 0x9d,
             0x52, 0x09, 0x05, 0xca, 0x51, 0x54, 0x2a, 0x52, 0xdc, 0x41, 0x33,
             0xd0, 0x5c, 0x50, 0x8e, 0xf1, 0x7d, 0x1d, 0xb2, 0xb0, 0x07, 0x20,
             0xe1, 0x69, 0x7b, 0x7d, 0x97, 0xe8, 0x06, 0x00, 0xde, 0x42, 0x36,
             0xbc, 0x19, 0x43, 0xa6, 0xc8, 0x7b, 0x53, 0x00, 0xe8, 0xd8, 0x76,
             0x97, 0x53, 0x79, 0x84, 0xd4, 0xcc, 0xeb, 0x20, 0x9c, 0x94, 0x97,
             0xd5, 0x7d, 0x72, 0x02, 0x90, 0x7d, 0xb4, 0x49, 0xda, 0x06, 0xa8,
             0xf2, 0x09, 0x35, 0xea, 0x6d, 0xf7, 0x6f, 0xbb, 0x24, 0xb1, 0x1d,
             0x3f, 0xfc, 0x87, 0x97, 0x09, 0x3c, 0xd2, 0xc6, 0xe3, 0xf7, 0x88,
             0x44, 0x20, 0x04, 0xa7, 0x1f, 0xb1, 0xf5, 0x07, 0xdf, 0x5f, 0x26,
             0xdd, 0xcc, 0x91}};
        auto const thresh4Msg = "P1P0abcdefghijklmnopqrstuvwxyz"s;
        std::array<std::uint8_t, 32> const Preim6CondConditionFingerprint = {
            {0x5d, 0xa0, 0x30, 0xef, 0xfd, 0xe1, 0x75, 0x11, 0x51, 0xe8, 0x5f,
             0x5e, 0x54, 0x2d, 0x6a, 0x5b, 0xd1, 0x5c, 0xc9, 0x33, 0x21, 0x2c,
             0xe2, 0x68, 0xfc, 0xfd, 0x53, 0xee, 0x93, 0x58, 0xeb, 0x4e}};
        Condition const Preim6Cond{Type::preimageSha256,
                                   9,
                                   Preim6CondConditionFingerprint,
                                   std::bitset<5>{0}};
        std::array<std::uint8_t, 32> const Rsa7CondConditionFingerprint = {
            {0x6c, 0x7b, 0xea, 0x83, 0xa1, 0xf4, 0x82, 0x3d, 0x36, 0xe7, 0x6e,
             0xae, 0x1a, 0xbc, 0xa0, 0xba, 0x90, 0x3d, 0x96, 0xc1, 0xe6, 0xad,
             0x3a, 0x47, 0xa5, 0xcb, 0x88, 0xab, 0x3c, 0x5f, 0xcc, 0xd5}};
        Condition const Rsa7Cond{Type::rsaSha256,
                                 65536,
                                 Rsa7CondConditionFingerprint,
                                 std::bitset<5>{0}};
        std::array<std::uint8_t, 32> const Ed8CondConditionFingerprint = {
            {0xf1, 0x68, 0x96, 0xa6, 0x2e, 0xef, 0x7f, 0x47, 0x06, 0x51, 0x4c,
             0xc6, 0x7e, 0x24, 0xf7, 0x29, 0x84, 0x9c, 0xd6, 0xb0, 0xd9, 0x4b,
             0xd9, 0x0f, 0xc9, 0x34, 0x01, 0x9d, 0x92, 0xeb, 0xbc, 0x0a}};
        Condition const Ed8Cond{Type::ed25519Sha256,
                                131072,
                                Ed8CondConditionFingerprint,
                                std::bitset<5>{0}};
        auto const thresh2Msg = "P1P0abcdefghijklmnopqrstuvwxyz"s;
        std::array<std::uint8_t, 32> const Preim9CondConditionFingerprint = {
            {0x5d, 0xa0, 0x30, 0xef, 0xfd, 0xe1, 0x75, 0x11, 0x51, 0xe8, 0x5f,
             0x5e, 0x54, 0x2d, 0x6a, 0x5b, 0xd1, 0x5c, 0xc9, 0x33, 0x21, 0x2c,
             0xe2, 0x68, 0xfc, 0xfd, 0x53, 0xee, 0x93, 0x58, 0xeb, 0x4e}};
        Condition const Preim9Cond{Type::preimageSha256,
                                   9,
                                   Preim9CondConditionFingerprint,
                                   std::bitset<5>{0}};
        std::array<std::uint8_t, 32> const Rsa10CondConditionFingerprint = {
            {0x3c, 0x73, 0x38, 0xcf, 0x23, 0xc6, 0x31, 0x53, 0x28, 0xc4, 0x27,
             0xf8, 0x95, 0x87, 0x99, 0x83, 0x2d, 0x35, 0x3c, 0x03, 0x9b, 0xd1,
             0xff, 0xff, 0x2e, 0x53, 0x20, 0xe9, 0x5e, 0x62, 0xb9, 0xb7}};
        Condition const Rsa10Cond{Type::rsaSha256,
                                  65536,
                                  Rsa10CondConditionFingerprint,
                                  std::bitset<5>{0}};
        std::array<std::uint8_t, 32> const Ed11CondConditionFingerprint = {
            {0x41, 0x80, 0x08, 0xb2, 0x60, 0x74, 0x57, 0x6d, 0xac, 0xed, 0x74,
             0x7f, 0x54, 0xdb, 0x96, 0x18, 0x91, 0x06, 0x0a, 0x95, 0xa1, 0x49,
             0x17, 0xc7, 0x65, 0xe3, 0x94, 0xc8, 0x5e, 0x2c, 0x92, 0x20}};
        Condition const Ed11Cond{Type::ed25519Sha256,
                                 131072,
                                 Ed11CondConditionFingerprint,
                                 std::bitset<5>{0}};
        std::array<std::uint8_t, 32> const Thresh12CondConditionFingerprint = {
            {0x03, 0xb7, 0x57, 0xb9, 0x56, 0x68, 0xc6, 0x36, 0x20, 0x05, 0x2b,
             0xd6, 0x6f, 0x92, 0x23, 0x03, 0x30, 0xa8, 0x6f, 0x6e, 0xd9, 0x64,
             0x9c, 0xd0, 0xc2, 0x02, 0x89, 0xc3, 0xcf, 0xe9, 0xce, 0x85}};
        Condition const Thresh12Cond{Type::thresholdSha256,
                                     135168,
                                     Thresh12CondConditionFingerprint,
                                     std::bitset<5>{25}};
        auto const prefix1Prefix = "P1"s;
        auto const prefix1Msg = "P0abcdefghijklmnopqrstuvwxyz"s;
        auto const prefix1MaxMsgLength = 28;
        auto const prefix0Prefix = "P0"s;
        auto const prefix0Msg = "abcdefghijklmnopqrstuvwxyz"s;
        auto const prefix0MaxMsgLength = 26;

        auto rsa3 = std::make_unique<RsaSha256>(
            makeSlice(rsa3PublicKey), makeSlice(rsa3Sig));
        auto rsa5 = std::make_unique<RsaSha256>(
            makeSlice(rsa5PublicKey), makeSlice(rsa5Sig));
        std::vector<std::unique_ptr<Fulfillment>> thresh4Subfulfillments;
        thresh4Subfulfillments.emplace_back(std::move(rsa5));
        std::vector<Condition> thresh4Subconditions{
            {Preim6Cond, Rsa7Cond, Ed8Cond}};
        auto thresh4 = std::make_unique<ThresholdSha256>(
            std::move(thresh4Subfulfillments), std::move(thresh4Subconditions));
        std::vector<std::unique_ptr<Fulfillment>> thresh2Subfulfillments;
        thresh2Subfulfillments.emplace_back(std::move(rsa3));
        thresh2Subfulfillments.emplace_back(std::move(thresh4));
        std::vector<Condition> thresh2Subconditions{
            {Preim9Cond, Rsa10Cond, Ed11Cond, Thresh12Cond}};
        auto thresh2 = std::make_unique<ThresholdSha256>(
            std::move(thresh2Subfulfillments), std::move(thresh2Subconditions));
        auto prefix1 = std::make_unique<PrefixSha256>(
            makeSlice(prefix1Prefix), prefix1MaxMsgLength, std::move(thresh2));
        auto prefix0 = std::make_unique<PrefixSha256>(
            makeSlice(prefix0Prefix), prefix0MaxMsgLength, std::move(prefix1));
        {
            auto prefix0EncodedFulfillment =
                "\xa1\x82\x05\x66\x80\x02\x50\x30\x81\x01\x1a\xa2\x82\x05\x5b"
                "\xa1\x82\x05\x57\x80\x02\x50\x31\x81\x01\x1c\xa2\x82\x05\x4c"
                "\xa2\x82\x05\x48\xa0\x82\x04\x9b\xa2\x82\x02\x8b\xa0\x82\x02"
                "\x0c\xa3\x82\x02\x08\x80\x82\x01\x00\xc0\x00\xef\x8f\x4b\x81"
                "\x10\x1e\x52\xe0\x07\x9f\x68\xe7\x2f\x92\xd4\x77\x3c\x1f\xa3"
                "\xff\x72\x64\x5b\x37\xf1\xf3\xa3\xc5\xfb\xcd\xfb\xda\xcc\x8b"
                "\x52\xe1\xde\xbc\x28\x8d\xe5\xad\xab\x86\x61\x45\x97\x65\x37"
                "\x68\x26\x21\x92\x17\xa3\xb0\x74\x5c\x8a\x45\x8d\x87\x5b\x9b"
                "\xd1\x7b\x07\xc4\x8c\x67\xa0\xe9\x82\x0c\xe0\x6b\xea\x91\x5c"
                "\xba\xe3\xd9\x9d\x39\xfd\x77\xac\xcb\x33\x9b\x28\x51\x8d\xbf"
                "\x3e\xe4\x94\x1c\x9a\x60\x71\x4b\x34\x07\x30\xda\x42\x46\x0e"
                "\xb8\xb7\x2c\xf5\x2f\x4b\x9e\xe7\x64\x81\xa1\xa2\x05\x66\x92"
                "\xe6\x75\x9f\x37\xae\x40\xa9\x16\x08\x19\xe8\xdc\x47\xd6\x03"
                "\x29\xab\xcc\x58\xa2\x37\x2a\x32\xb8\x15\xc7\x51\x91\x73\xb9"
                "\x1d\xc6\xd0\x4f\x85\x86\xd5\xb3\x21\x1a\x2a\x6c\xeb\x7f\xfe"
                "\x84\x17\x10\x2d\x0e\xb4\xe1\xc2\x48\x4c\x3f\x61\xc7\x59\x75"
                "\xa7\xc1\x75\xce\x67\x17\x42\x2a\x2f\x96\xef\x8a\x2d\x74\xd2"
                "\x13\x68\xe1\xe9\xea\xfb\x73\x68\xed\x8d\xd3\xac\x49\x09\xf9"
                "\xec\x62\xdf\x53\xab\xfe\x90\x64\x4b\x92\x60\x0d\xdd\x00\xfe"
                "\x02\xe6\xf3\x9b\x2b\xac\x4f\x70\xe8\x5b\x69\x9c\x40\xd3\xeb"
                "\x37\xad\x6f\x37\xab\xf3\x79\x8e\xcb\x1d\x81\x82\x01\x00\x8e"
                "\x9d\x0d\x51\xd2\xad\x9f\x98\xb7\x8b\x84\x54\x18\xb9\xc1\x8e"
                "\x6d\xfe\xdf\x1e\x90\x0c\x68\x6c\xa4\x21\x57\x97\x39\xd7\x12"
                "\x24\x31\x08\x3a\xc6\xc3\x05\xb2\x8d\x0b\xf2\x80\x3c\x25\x42"
                "\xe4\x8e\x02\xfa\x01\xfe\x08\x6f\xe3\x37\xb4\x29\x9f\x97\xbf"
                "\x5f\xbd\xe3\xa8\x44\xa7\x9d\x60\x31\xdd\x46\x6a\xa7\xfd\xbf"
                "\xb5\xaa\xa4\x5d\x71\x6f\xf2\xe3\x15\xc2\xb5\x39\x5d\x7c\xb9"
                "\xb5\x39\xc4\xcb\x1f\xe4\xee\x0f\x1f\xc6\x5a\xb5\x29\x1c\xe7"
                "\xa7\x53\x5d\xa4\x3f\x89\x35\x4b\x33\x66\xe5\xb8\x78\x64\x74"
                "\xab\x3f\x7c\x88\xe8\x3e\x4a\xda\xf0\x52\xa2\x28\xaa\xd5\x7f"
                "\xaa\xd6\xfe\xf4\xeb\x0a\x04\x9c\x96\x7f\xa9\x76\x65\xf2\x74"
                "\x17\xc6\x9d\x52\x09\x05\xca\x51\x54\x2a\x52\xdc\x41\x33\xd0"
                "\x5c\x50\x8e\xf1\x7d\x1d\xb2\xb0\x07\x20\xe1\x69\x7b\x7d\x97"
                "\xe8\x06\x00\xde\x42\x36\xbc\x19\x43\xa6\xc8\x7b\x53\x00\xe8"
                "\xd8\x76\x97\x53\x79\x84\xd4\xcc\xeb\x20\x9c\x94\x97\xd5\x7d"
                "\x72\x02\x90\x7d\xb4\x49\xda\x06\xa8\xf2\x09\x35\xea\x6d\xf7"
                "\x6f\xbb\x24\xb1\x1d\x3f\xfc\x87\x97\x09\x3c\xd2\xc6\xe3\xf7"
                "\x88\x44\x20\x04\xa7\x1f\xb1\xf5\x07\xdf\x5f\x26\xdd\xcc\x91"
                "\xa1\x79\xa0\x25\x80\x20\x5d\xa0\x30\xef\xfd\xe1\x75\x11\x51"
                "\xe8\x5f\x5e\x54\x2d\x6a\x5b\xd1\x5c\xc9\x33\x21\x2c\xe2\x68"
                "\xfc\xfd\x53\xee\x93\x58\xeb\x4e\x81\x01\x09\xa3\x27\x80\x20"
                "\x6c\x7b\xea\x83\xa1\xf4\x82\x3d\x36\xe7\x6e\xae\x1a\xbc\xa0"
                "\xba\x90\x3d\x96\xc1\xe6\xad\x3a\x47\xa5\xcb\x88\xab\x3c\x5f"
                "\xcc\xd5\x81\x03\x01\x00\x00\xa4\x27\x80\x20\xf1\x68\x96\xa6"
                "\x2e\xef\x7f\x47\x06\x51\x4c\xc6\x7e\x24\xf7\x29\x84\x9c\xd6"
                "\xb0\xd9\x4b\xd9\x0f\xc9\x34\x01\x9d\x92\xeb\xbc\x0a\x81\x03"
                "\x02\x00\x00\xa3\x82\x02\x08\x80\x82\x01\x00\xbd\xd1\xc7\xf0"
                "\xb0\x3a\xa5\x5b\x3e\x49\x8d\x4e\x00\x54\x89\xb9\x89\xcd\x4b"
                "\x43\xde\x59\xf6\x7a\x67\x5c\x3a\xc6\xcf\x82\x3f\x35\x9c\xcc"
                "\xda\xcd\xd3\x97\x86\x5b\xe9\xf6\x05\x55\x0b\x26\xef\x1e\x88"
                "\xd5\xb6\xba\x14\x0a\xb2\x76\xb9\xb3\x46\x0c\xc0\x80\x17\x13"
                "\x68\x23\xdc\xec\x10\x18\xfc\xaa\xbe\xb3\xc4\xc7\xa9\x84\xa6"
                "\x4e\x5c\x08\x6b\x7b\x4c\x81\x91\x79\x5d\x90\x06\x15\xbb\x76"
                "\x2f\x5c\x53\x60\x0f\xac\xf3\x7c\x49\xc5\x47\xec\xb3\xda\x93"
                "\x87\xc1\xb9\xcf\x2c\xb5\xf0\x85\xad\xb4\x38\x67\x88\xda\x3d"
                "\xfa\x01\xb7\x54\xd9\x41\x0b\x7b\x8a\x09\xe0\x84\x7d\xbb\x89"
                "\xb2\xfc\x0b\x70\x36\x93\x56\x62\xcc\xb4\xfc\xf9\x1f\x37\x92"
                "\x9b\x3a\x4e\x7c\xad\x4b\xa6\x76\x6f\xda\xc4\x2f\x83\x53\xbd"
                "\x93\xa9\x76\x89\x53\xe1\x4d\xee\x27\x11\x6f\xbc\x21\xad\x42"
                "\x9f\x29\xf6\x03\xdd\xec\xfa\xa1\x78\xd2\xde\x29\x2e\xd8\x3a"
                "\x7f\xe9\x9b\x5d\xeb\x37\xb8\xb0\xa0\x66\x3f\x02\xcd\x2a\x6e"
                "\xd3\x1c\xa5\x65\xdc\x73\xbe\x93\x54\x9a\x2b\xf8\x32\x8b\xe8"
                "\xce\x9a\x59\xd0\x05\xeb\xbb\xac\xfc\x4c\x4b\x2e\xac\x2a\xc3"
                "\x0f\x0a\xd7\x46\xaf\xfd\x22\x0d\x0d\x54\xcc\x2f\x81\x82\x01"
                "\x00\x14\x77\x7a\x2e\x34\xc6\x4c\x31\x8f\x2a\x11\x56\x3a\x63"
                "\x62\xb1\x99\x5b\x7e\x63\x2b\xe3\x1f\x36\xdf\x96\xe6\xfc\xb8"
                "\x5f\xf4\xaa\xa8\xa3\xf7\x4f\x71\xd6\x59\x76\x0c\x05\xa3\x9b"
                "\xdb\x40\x80\x28\x68\xe1\x74\x8c\x29\x67\x20\x96\x4f\x1a\xfd"
                "\x1e\x0a\x55\x4a\x2f\x49\x43\x66\x53\x2a\xa2\xd1\x08\xcf\xe3"
                "\x52\xce\xd8\x96\x32\x05\x6c\xb8\x90\x2b\x5e\x18\x62\x6c\xe1"
                "\xd9\x35\xa4\x7d\x0d\x01\x31\x8b\xfa\x98\xe0\xa2\xb3\x2a\x6e"
                "\xc5\x4c\xe2\x9f\x37\xb7\x4f\xd1\xaa\x45\xa4\x2a\x5d\x96\x3d"
                "\x5a\xb6\xa5\x26\x76\xa8\x39\x78\xd4\x88\xa4\xfd\x5c\x94\x1e"
                "\xce\xb0\xd7\x87\x4c\x83\xae\xac\xfa\x4e\x1d\xf2\xfa\xfb\x86"
                "\x9d\x2c\x26\x76\x0c\xfb\xbd\x30\xe8\x56\x7c\x23\xda\x01\x02"
                "\xcd\x1e\xea\x9c\x04\xbf\xfe\x9f\xa9\x1d\x49\xcd\x6e\x21\x58"
                "\x3b\xed\xac\xc4\xaa\xb4\x15\x11\xe5\xdb\x31\x52\xbf\xfc\x76"
                "\xa5\xff\xf2\xda\x77\x1f\xa0\xc5\x49\xeb\x04\x5c\x9f\xad\xcb"
                "\x75\xfb\xcd\x0d\x03\x5c\x38\x90\x57\x47\xd8\x42\xbe\xb3\x3e"
                "\xa1\xaf\xe9\x37\x27\x01\x52\x68\xab\xe9\x9e\x12\x5e\x9d\xe2"
                "\x22\xde\xe4\x85\x95\x60\x74\x02\xbc\x91\xf1\x5c\xf8\xb8\x26"
                "\x53\x7f\xa1\x81\xa6\xa0\x25\x80\x20\x5d\xa0\x30\xef\xfd\xe1"
                "\x75\x11\x51\xe8\x5f\x5e\x54\x2d\x6a\x5b\xd1\x5c\xc9\x33\x21"
                "\x2c\xe2\x68\xfc\xfd\x53\xee\x93\x58\xeb\x4e\x81\x01\x09\xa2"
                "\x2b\x80\x20\x03\xb7\x57\xb9\x56\x68\xc6\x36\x20\x05\x2b\xd6"
                "\x6f\x92\x23\x03\x30\xa8\x6f\x6e\xd9\x64\x9c\xd0\xc2\x02\x89"
                "\xc3\xcf\xe9\xce\x85\x81\x03\x02\x10\x00\x82\x02\x03\x98\xa3"
                "\x27\x80\x20\x3c\x73\x38\xcf\x23\xc6\x31\x53\x28\xc4\x27\xf8"
                "\x95\x87\x99\x83\x2d\x35\x3c\x03\x9b\xd1\xff\xff\x2e\x53\x20"
                "\xe9\x5e\x62\xb9\xb7\x81\x03\x01\x00\x00\xa4\x27\x80\x20\x41"
                "\x80\x08\xb2\x60\x74\x57\x6d\xac\xed\x74\x7f\x54\xdb\x96\x18"
                "\x91\x06\x0a\x95\xa1\x49\x17\xc7\x65\xe3\x94\xc8\x5e\x2c\x92"
                "\x20\x81\x03\x02\x00\x00"s;
            auto const prefix0EncodedCondition =
                "\xa1\x2b\x80\x20\xa8\x19\x40\x38\x3a\xfc\x00\x41\xbb\x50\xc0"
                "\x30\x63\x87\x66\x78\x36\x31\x44\xe6\xa6\xc7\xab\x97\xbf\xad"
                "\x0c\x76\x8f\xbc\xdd\x29\x81\x03\x04\x40\x3a\x82\x02\x03\xb8"s;
            auto const prefix0EncodedFingerprint =
                "\x30\x36\x80\x02\x50\x30\x81\x01\x1a\xa2\x2d\xa1\x2b\x80\x20"
                "\x4d\xe1\xc9\xcf\x63\xf6\x4a\xb7\x4d\x5e\x2a\x54\xfb\xdd\x21"
                "\x35\xfd\xb2\x2d\x3e\x74\x5b\xc5\x8d\x01\xd6\x47\x71\x13\xac"
                "\x66\xbc\x81\x03\x04\x3c\x1e\x82\x02\x03\xb8"s;
            check(
                std::move(prefix0),
                prefix0Msg,
                std::move(prefix0EncodedFulfillment),
                prefix0EncodedCondition,
                prefix0EncodedFingerprint);
        }
    }

    void
    testPrefix14()
    {
        testcase("Prefix14");

        using namespace std::string_literals;
        using namespace ripple::cryptoconditions;

        // Fulfillment structure
        // * prefix0
        // ** ed1

        auto const ed1Msg = "P0abcdefghijklmnopqrstuvwxyz"s;
        std::array<std::uint8_t, 32> const ed1PublicKey{
            {0x63, 0x99, 0x70, 0x0c, 0xa9, 0x50, 0x9c, 0xd4, 0xf5, 0x06, 0xdd,
             0x19, 0xc8, 0xa1, 0xd2, 0x9e, 0x03, 0x10, 0xb9, 0x89, 0x2e, 0x02,
             0x34, 0x62, 0x2d, 0x23, 0xca, 0xa1, 0x1d, 0xde, 0x23, 0x6a}};
        std::array<std::uint8_t, 64> const ed1Sig{
            {0x6c, 0x25, 0xd0, 0x01, 0x10, 0x29, 0x70, 0x01, 0x05, 0xc0, 0xfb,
             0x51, 0xd8, 0x59, 0x5b, 0x7b, 0x83, 0x32, 0xd6, 0x18, 0x63, 0x35,
             0x07, 0xcf, 0xdf, 0x63, 0x90, 0x39, 0xd1, 0x24, 0x42, 0x8a, 0xc0,
             0xd1, 0xac, 0xd3, 0x55, 0xf6, 0x5c, 0x96, 0xda, 0x7b, 0x2e, 0xaa,
             0xfc, 0x72, 0x1b, 0x1e, 0x2e, 0xb2, 0x97, 0xad, 0x8d, 0x04, 0x32,
             0xd9, 0x9c, 0xb7, 0xf4, 0x32, 0x38, 0x4e, 0xd7, 0x0c}};
        std::array<std::uint8_t, 32> const ed1SigningKey{
            {0xe9, 0x20, 0xaa, 0x41, 0x73, 0x35, 0x2f, 0xae, 0xa2, 0x4b, 0x4b,
             0x19, 0x64, 0xda, 0xc0, 0xd5, 0x7b, 0xfd, 0x99, 0x06, 0x90, 0x80,
             0x48, 0xe4, 0x6a, 0xc4, 0x86, 0x30, 0x7d, 0x53, 0x37, 0xb9}};
        (void)ed1SigningKey;
        auto const prefix0Prefix = "P0"s;
        auto const prefix0Msg = "abcdefghijklmnopqrstuvwxyz"s;
        auto const prefix0MaxMsgLength = 26;

        auto ed1 = std::make_unique<Ed25519>(ed1PublicKey, ed1Sig);
        auto prefix0 = std::make_unique<PrefixSha256>(
            makeSlice(prefix0Prefix), prefix0MaxMsgLength, std::move(ed1));
        {
            auto prefix0EncodedFulfillment =
                "\xa1\x6f\x80\x02\x50\x30\x81\x01\x1a\xa2\x66\xa4\x64\x80\x20"
                "\x63\x99\x70\x0c\xa9\x50\x9c\xd4\xf5\x06\xdd\x19\xc8\xa1\xd2"
                "\x9e\x03\x10\xb9\x89\x2e\x02\x34\x62\x2d\x23\xca\xa1\x1d\xde"
                "\x23\x6a\x81\x40\x6c\x25\xd0\x01\x10\x29\x70\x01\x05\xc0\xfb"
                "\x51\xd8\x59\x5b\x7b\x83\x32\xd6\x18\x63\x35\x07\xcf\xdf\x63"
                "\x90\x39\xd1\x24\x42\x8a\xc0\xd1\xac\xd3\x55\xf6\x5c\x96\xda"
                "\x7b\x2e\xaa\xfc\x72\x1b\x1e\x2e\xb2\x97\xad\x8d\x04\x32\xd9"
                "\x9c\xb7\xf4\x32\x38\x4e\xd7\x0c"s;
            auto const prefix0EncodedCondition =
                "\xa1\x2b\x80\x20\x83\xa7\x40\x23\xe2\xa9\xfa\x80\x9a\x63\xb4"
                "\xf8\xa2\x05\x92\xb9\xf6\xdf\xff\xff\x24\xca\x3c\x8e\x3c\x09"
                "\xcd\xf9\x24\xbc\xe2\x13\x81\x03\x02\x04\x1c\x82\x02\x03\x08"s;
            auto const prefix0EncodedFingerprint =
                "\x30\x32\x80\x02\x50\x30\x81\x01\x1a\xa2\x29\xa4\x27\x80\x20"
                "\x42\x58\xea\xf4\xb3\xac\xde\x0b\xd9\x1e\x0c\x53\xe5\xae\x63"
                "\x84\xee\xc0\xf5\xcf\x88\xa7\x43\x7b\x05\x47\x87\xee\x73\x3e"
                "\xa3\x83\x81\x03\x02\x00\x00"s;
            check(
                std::move(prefix0),
                prefix0Msg,
                std::move(prefix0EncodedFulfillment),
                prefix0EncodedCondition,
                prefix0EncodedFingerprint);
        }
    }

    void
    testPrefix15()
    {
        testcase("Prefix15");

        using namespace std::string_literals;
        using namespace ripple::cryptoconditions;

        // Fulfillment structure
        // * prefix0
        // ** prefix1
        // *** ed2

        auto const ed2Msg = "P1P0abcdefghijklmnopqrstuvwxyz"s;
        std::array<std::uint8_t, 32> const ed2PublicKey{
            {0xb1, 0x2f, 0x54, 0xbe, 0xb6, 0xf8, 0x76, 0x71, 0x72, 0xed, 0x44,
             0x03, 0x71, 0x74, 0x2d, 0x7f, 0x98, 0x10, 0x4b, 0x57, 0xf2, 0x45,
             0xfb, 0x3e, 0xea, 0xfd, 0xdd, 0x39, 0x42, 0xbf, 0x24, 0x4d}};
        std::array<std::uint8_t, 64> const ed2Sig{
            {0xbd, 0xa7, 0x4d, 0x84, 0x5f, 0xfd, 0x7c, 0x9b, 0xd0, 0xef, 0x41,
             0x2c, 0x54, 0x50, 0xd8, 0x26, 0xf0, 0x06, 0x61, 0x22, 0x5c, 0x90,
             0xc2, 0x72, 0x5e, 0x7a, 0x3b, 0x6f, 0xba, 0xa7, 0x22, 0x04, 0xa6,
             0x6c, 0x67, 0xbb, 0x58, 0x28, 0xf7, 0xd8, 0x0c, 0x8e, 0xd3, 0x62,
             0x66, 0x66, 0x2c, 0xe9, 0x71, 0xd0, 0x38, 0xab, 0x33, 0xfa, 0x10,
             0x10, 0x53, 0x4f, 0xf2, 0xf1, 0x97, 0x0d, 0xbd, 0x06}};
        std::array<std::uint8_t, 32> const ed2SigningKey{
            {0xa7, 0xeb, 0x15, 0xc5, 0x2a, 0x41, 0x59, 0xf9, 0xf7, 0xb4, 0x78,
             0x5f, 0xdb, 0x79, 0xe5, 0x5b, 0x16, 0x44, 0xf7, 0xc7, 0xcf, 0xe2,
             0x46, 0xc5, 0xb3, 0x54, 0x64, 0xb5, 0x2f, 0x6c, 0x8e, 0x8e}};
        (void)ed2SigningKey;
        auto const prefix1Prefix = "P1"s;
        auto const prefix1Msg = "P0abcdefghijklmnopqrstuvwxyz"s;
        auto const prefix1MaxMsgLength = 28;
        auto const prefix0Prefix = "P0"s;
        auto const prefix0Msg = "abcdefghijklmnopqrstuvwxyz"s;
        auto const prefix0MaxMsgLength = 26;

        auto ed2 = std::make_unique<Ed25519>(ed2PublicKey, ed2Sig);
        auto prefix1 = std::make_unique<PrefixSha256>(
            makeSlice(prefix1Prefix), prefix1MaxMsgLength, std::move(ed2));
        auto prefix0 = std::make_unique<PrefixSha256>(
            makeSlice(prefix0Prefix), prefix0MaxMsgLength, std::move(prefix1));
        {
            auto prefix0EncodedFulfillment =
                "\xa1\x7a\x80\x02\x50\x30\x81\x01\x1a\xa2\x71\xa1\x6f\x80\x02"
                "\x50\x31\x81\x01\x1c\xa2\x66\xa4\x64\x80\x20\xb1\x2f\x54\xbe"
                "\xb6\xf8\x76\x71\x72\xed\x44\x03\x71\x74\x2d\x7f\x98\x10\x4b"
                "\x57\xf2\x45\xfb\x3e\xea\xfd\xdd\x39\x42\xbf\x24\x4d\x81\x40"
                "\xbd\xa7\x4d\x84\x5f\xfd\x7c\x9b\xd0\xef\x41\x2c\x54\x50\xd8"
                "\x26\xf0\x06\x61\x22\x5c\x90\xc2\x72\x5e\x7a\x3b\x6f\xba\xa7"
                "\x22\x04\xa6\x6c\x67\xbb\x58\x28\xf7\xd8\x0c\x8e\xd3\x62\x66"
                "\x66\x2c\xe9\x71\xd0\x38\xab\x33\xfa\x10\x10\x53\x4f\xf2\xf1"
                "\x97\x0d\xbd\x06"s;
            auto const prefix0EncodedCondition =
                "\xa1\x2b\x80\x20\x51\x26\xdf\x1b\xd5\x14\xf3\xb8\x50\x22\x02"
                "\x97\xda\x45\x7d\x91\x9e\xeb\x48\xc6\x35\xf3\xff\x52\xbf\xed"
                "\x07\x76\x72\xa8\xcf\xd3\x81\x03\x02\x08\x3a\x82\x02\x03\x08"s;
            auto const prefix0EncodedFingerprint =
                "\x30\x36\x80\x02\x50\x30\x81\x01\x1a\xa2\x2d\xa1\x2b\x80\x20"
                "\x67\x52\xef\xad\xb9\x51\x22\x0f\x21\xb3\xac\x10\xa8\x51\xfc"
                "\xfe\x2f\x60\xe9\xf7\x80\x15\xad\x24\x7f\x8b\x4f\x49\xb8\xb3"
                "\x22\xaa\x81\x03\x02\x04\x1e\x82\x02\x03\x08"s;
            check(
                std::move(prefix0),
                prefix0Msg,
                std::move(prefix0EncodedFulfillment),
                prefix0EncodedCondition,
                prefix0EncodedFingerprint);
        }
    }

    void
    testPrefix16()
    {
        testcase("Prefix16");

        using namespace std::string_literals;
        using namespace ripple::cryptoconditions;

        // Fulfillment structure
        // * prefix0
        // ** prefix1
        // *** prefix2
        // **** ed3

        auto const ed3Msg = "P2P1P0abcdefghijklmnopqrstuvwxyz"s;
        std::array<std::uint8_t, 32> const ed3PublicKey{
            {0x42, 0x0a, 0x60, 0x63, 0x9e, 0xca, 0xaa, 0x48, 0xac, 0xcb, 0x2c,
             0xd9, 0x46, 0x79, 0x69, 0x34, 0x35, 0xf3, 0x8e, 0x29, 0xd5, 0x4b,
             0x24, 0x57, 0x9d, 0xd7, 0x3b, 0x04, 0x5d, 0x9c, 0x2f, 0x32}};
        std::array<std::uint8_t, 64> const ed3Sig{
            {0x33, 0x01, 0x0b, 0xe8, 0x91, 0xa9, 0xa0, 0x9b, 0x54, 0x9a, 0xfc,
             0xde, 0xcd, 0xac, 0x70, 0x42, 0xeb, 0x1e, 0x41, 0x7f, 0xb8, 0xe0,
             0x62, 0x39, 0x8d, 0x55, 0xa8, 0xe8, 0xc5, 0xba, 0x2b, 0xfd, 0xf6,
             0xfb, 0xc9, 0x96, 0x83, 0x17, 0x54, 0x06, 0xa9, 0xb3, 0xa9, 0x64,
             0xe8, 0xd3, 0xa7, 0x92, 0xde, 0xb2, 0x4d, 0x85, 0x49, 0x2c, 0x2a,
             0xeb, 0x48, 0x1f, 0x7a, 0x33, 0x60, 0x83, 0x56, 0x0f}};
        std::array<std::uint8_t, 32> const ed3SigningKey{
            {0x1b, 0x90, 0xf2, 0x2c, 0xd5, 0x5d, 0xcd, 0xe5, 0x2d, 0x71, 0x84,
             0x66, 0x10, 0xf0, 0xf1, 0x91, 0xfe, 0xb5, 0xbe, 0x94, 0x85, 0xc1,
             0xc8, 0xf3, 0x61, 0x91, 0xc3, 0xa4, 0x41, 0x58, 0xcd, 0xfc}};
        (void)ed3SigningKey;
        auto const prefix2Prefix = "P2"s;
        auto const prefix2Msg = "P1P0abcdefghijklmnopqrstuvwxyz"s;
        auto const prefix2MaxMsgLength = 30;
        auto const prefix1Prefix = "P1"s;
        auto const prefix1Msg = "P0abcdefghijklmnopqrstuvwxyz"s;
        auto const prefix1MaxMsgLength = 28;
        auto const prefix0Prefix = "P0"s;
        auto const prefix0Msg = "abcdefghijklmnopqrstuvwxyz"s;
        auto const prefix0MaxMsgLength = 26;

        auto ed3 = std::make_unique<Ed25519>(ed3PublicKey, ed3Sig);
        auto prefix2 = std::make_unique<PrefixSha256>(
            makeSlice(prefix2Prefix), prefix2MaxMsgLength, std::move(ed3));
        auto prefix1 = std::make_unique<PrefixSha256>(
            makeSlice(prefix1Prefix), prefix1MaxMsgLength, std::move(prefix2));
        auto prefix0 = std::make_unique<PrefixSha256>(
            makeSlice(prefix0Prefix), prefix0MaxMsgLength, std::move(prefix1));
        {
            auto prefix0EncodedFulfillment =
                "\xa1\x81\x85\x80\x02\x50\x30\x81\x01\x1a\xa2\x7c\xa1\x7a\x80"
                "\x02\x50\x31\x81\x01\x1c\xa2\x71\xa1\x6f\x80\x02\x50\x32\x81"
                "\x01\x1e\xa2\x66\xa4\x64\x80\x20\x42\x0a\x60\x63\x9e\xca\xaa"
                "\x48\xac\xcb\x2c\xd9\x46\x79\x69\x34\x35\xf3\x8e\x29\xd5\x4b"
                "\x24\x57\x9d\xd7\x3b\x04\x5d\x9c\x2f\x32\x81\x40\x33\x01\x0b"
                "\xe8\x91\xa9\xa0\x9b\x54\x9a\xfc\xde\xcd\xac\x70\x42\xeb\x1e"
                "\x41\x7f\xb8\xe0\x62\x39\x8d\x55\xa8\xe8\xc5\xba\x2b\xfd\xf6"
                "\xfb\xc9\x96\x83\x17\x54\x06\xa9\xb3\xa9\x64\xe8\xd3\xa7\x92"
                "\xde\xb2\x4d\x85\x49\x2c\x2a\xeb\x48\x1f\x7a\x33\x60\x83\x56"
                "\x0f"s;
            auto const prefix0EncodedCondition =
                "\xa1\x2b\x80\x20\x35\x20\xd9\xcf\xd3\x4d\x95\xe3\xa9\xd6\x60"
                "\x72\x8c\xa7\xa5\xa9\x78\xea\x69\x7d\x5b\xa6\xf7\x02\x6f\x51"
                "\xd1\x27\xc8\xcc\x86\x55\x81\x03\x02\x0c\x5a\x82\x02\x03\x08"s;
            auto const prefix0EncodedFingerprint =
                "\x30\x36\x80\x02\x50\x30\x81\x01\x1a\xa2\x2d\xa1\x2b\x80\x20"
                "\xae\x13\x99\x34\x3d\x07\xe4\xb4\x04\xfd\x8b\x6f\x5f\x3b\xcc"
                "\xbe\x88\x2c\x68\xb0\xdc\x7c\x0c\xa2\x78\x99\xd1\x46\x2e\x9f"
                "\x3e\x7e\x81\x03\x02\x08\x3e\x82\x02\x03\x08"s;
            check(
                std::move(prefix0),
                prefix0Msg,
                std::move(prefix0EncodedFulfillment),
                prefix0EncodedCondition,
                prefix0EncodedFingerprint);
        }
    }

    void
    testPrefix17()
    {
        testcase("Prefix17");

        using namespace std::string_literals;
        using namespace ripple::cryptoconditions;

        // Fulfillment structure
        // * prefix0
        // ** prefix1
        // *** thresh2
        // **** ed3

        auto const ed3Msg = "P1P0abcdefghijklmnopqrstuvwxyz"s;
        std::array<std::uint8_t, 32> const ed3PublicKey{
            {0x42, 0x0a, 0x60, 0x63, 0x9e, 0xca, 0xaa, 0x48, 0xac, 0xcb, 0x2c,
             0xd9, 0x46, 0x79, 0x69, 0x34, 0x35, 0xf3, 0x8e, 0x29, 0xd5, 0x4b,
             0x24, 0x57, 0x9d, 0xd7, 0x3b, 0x04, 0x5d, 0x9c, 0x2f, 0x32}};
        std::array<std::uint8_t, 64> const ed3Sig{
            {0x8e, 0xa4, 0xd9, 0x6a, 0x9b, 0x3e, 0x19, 0xe8, 0x89, 0xa4, 0x4e,
             0xf2, 0xa8, 0x1c, 0xc2, 0xbd, 0xd3, 0xe0, 0x6f, 0xe0, 0xd1, 0x6b,
             0x85, 0x47, 0x9a, 0x58, 0x2e, 0x9f, 0x38, 0x09, 0x1f, 0x6d, 0x02,
             0x9a, 0xf6, 0x9c, 0x60, 0x3f, 0x16, 0x6c, 0xa5, 0x0e, 0xfb, 0xa3,
             0x08, 0xd6, 0xb6, 0x97, 0x5f, 0x2e, 0x94, 0xfb, 0x49, 0x56, 0xbe,
             0x2c, 0x58, 0x48, 0x15, 0x49, 0x73, 0xa2, 0xae, 0x09}};
        std::array<std::uint8_t, 32> const ed3SigningKey{
            {0x1b, 0x90, 0xf2, 0x2c, 0xd5, 0x5d, 0xcd, 0xe5, 0x2d, 0x71, 0x84,
             0x66, 0x10, 0xf0, 0xf1, 0x91, 0xfe, 0xb5, 0xbe, 0x94, 0x85, 0xc1,
             0xc8, 0xf3, 0x61, 0x91, 0xc3, 0xa4, 0x41, 0x58, 0xcd, 0xfc}};
        (void)ed3SigningKey;
        auto const thresh2Msg = "P1P0abcdefghijklmnopqrstuvwxyz"s;
        auto const prefix1Prefix = "P1"s;
        auto const prefix1Msg = "P0abcdefghijklmnopqrstuvwxyz"s;
        auto const prefix1MaxMsgLength = 28;
        auto const prefix0Prefix = "P0"s;
        auto const prefix0Msg = "abcdefghijklmnopqrstuvwxyz"s;
        auto const prefix0MaxMsgLength = 26;

        auto ed3 = std::make_unique<Ed25519>(ed3PublicKey, ed3Sig);
        std::vector<std::unique_ptr<Fulfillment>> thresh2Subfulfillments;
        thresh2Subfulfillments.emplace_back(std::move(ed3));
        std::vector<Condition> thresh2Subconditions{};
        auto thresh2 = std::make_unique<ThresholdSha256>(
            std::move(thresh2Subfulfillments), std::move(thresh2Subconditions));
        auto prefix1 = std::make_unique<PrefixSha256>(
            makeSlice(prefix1Prefix), prefix1MaxMsgLength, std::move(thresh2));
        auto prefix0 = std::make_unique<PrefixSha256>(
            makeSlice(prefix0Prefix), prefix0MaxMsgLength, std::move(prefix1));
        {
            auto prefix0EncodedFulfillment =
                "\xa1\x81\x80\x80\x02\x50\x30\x81\x01\x1a\xa2\x77\xa1\x75\x80"
                "\x02\x50\x31\x81\x01\x1c\xa2\x6c\xa2\x6a\xa0\x66\xa4\x64\x80"
                "\x20\x42\x0a\x60\x63\x9e\xca\xaa\x48\xac\xcb\x2c\xd9\x46\x79"
                "\x69\x34\x35\xf3\x8e\x29\xd5\x4b\x24\x57\x9d\xd7\x3b\x04\x5d"
                "\x9c\x2f\x32\x81\x40\x8e\xa4\xd9\x6a\x9b\x3e\x19\xe8\x89\xa4"
                "\x4e\xf2\xa8\x1c\xc2\xbd\xd3\xe0\x6f\xe0\xd1\x6b\x85\x47\x9a"
                "\x58\x2e\x9f\x38\x09\x1f\x6d\x02\x9a\xf6\x9c\x60\x3f\x16\x6c"
                "\xa5\x0e\xfb\xa3\x08\xd6\xb6\x97\x5f\x2e\x94\xfb\x49\x56\xbe"
                "\x2c\x58\x48\x15\x49\x73\xa2\xae\x09\xa1\x00"s;
            auto const prefix0EncodedCondition =
                "\xa1\x2b\x80\x20\x4c\x15\x59\xc5\x8b\x8c\x33\x5b\x8b\xe0\x67"
                "\x85\xd4\xa2\x3f\xde\x46\xc3\xe8\x00\xa0\x57\xc0\xf9\x65\x50"
                "\x89\x8f\xa9\x64\x22\x16\x81\x03\x02\x0c\x3a\x82\x02\x03\x28"s;
            auto const prefix0EncodedFingerprint =
                "\x30\x36\x80\x02\x50\x30\x81\x01\x1a\xa2\x2d\xa1\x2b\x80\x20"
                "\xc8\x81\x32\xa7\x94\xba\x41\x23\xce\x25\xcb\xfa\xda\xb5\xcd"
                "\xef\x9c\x74\xe4\x19\xe9\x90\x84\x92\xbd\xc5\xef\x4c\x06\x2f"
                "\xfa\xcc\x81\x03\x02\x08\x1e\x82\x02\x03\x28"s;
            check(
                std::move(prefix0),
                prefix0Msg,
                std::move(prefix0EncodedFulfillment),
                prefix0EncodedCondition,
                prefix0EncodedFingerprint);
        }
    }

    void
    testPrefix18()
    {
        testcase("Prefix18");

        using namespace std::string_literals;
        using namespace ripple::cryptoconditions;

        // Fulfillment structure
        // * prefix0
        // ** prefix1
        // *** thresh2
        // **** Preim4Cond
        // **** Rsa5Cond
        // **** Ed6Cond
        // **** ed3

        auto const ed3Msg = "P1P0abcdefghijklmnopqrstuvwxyz"s;
        std::array<std::uint8_t, 32> const ed3PublicKey{
            {0x42, 0x0a, 0x60, 0x63, 0x9e, 0xca, 0xaa, 0x48, 0xac, 0xcb, 0x2c,
             0xd9, 0x46, 0x79, 0x69, 0x34, 0x35, 0xf3, 0x8e, 0x29, 0xd5, 0x4b,
             0x24, 0x57, 0x9d, 0xd7, 0x3b, 0x04, 0x5d, 0x9c, 0x2f, 0x32}};
        std::array<std::uint8_t, 64> const ed3Sig{
            {0x8e, 0xa4, 0xd9, 0x6a, 0x9b, 0x3e, 0x19, 0xe8, 0x89, 0xa4, 0x4e,
             0xf2, 0xa8, 0x1c, 0xc2, 0xbd, 0xd3, 0xe0, 0x6f, 0xe0, 0xd1, 0x6b,
             0x85, 0x47, 0x9a, 0x58, 0x2e, 0x9f, 0x38, 0x09, 0x1f, 0x6d, 0x02,
             0x9a, 0xf6, 0x9c, 0x60, 0x3f, 0x16, 0x6c, 0xa5, 0x0e, 0xfb, 0xa3,
             0x08, 0xd6, 0xb6, 0x97, 0x5f, 0x2e, 0x94, 0xfb, 0x49, 0x56, 0xbe,
             0x2c, 0x58, 0x48, 0x15, 0x49, 0x73, 0xa2, 0xae, 0x09}};
        std::array<std::uint8_t, 32> const ed3SigningKey{
            {0x1b, 0x90, 0xf2, 0x2c, 0xd5, 0x5d, 0xcd, 0xe5, 0x2d, 0x71, 0x84,
             0x66, 0x10, 0xf0, 0xf1, 0x91, 0xfe, 0xb5, 0xbe, 0x94, 0x85, 0xc1,
             0xc8, 0xf3, 0x61, 0x91, 0xc3, 0xa4, 0x41, 0x58, 0xcd, 0xfc}};
        (void)ed3SigningKey;
        auto const thresh2Msg = "P1P0abcdefghijklmnopqrstuvwxyz"s;
        std::array<std::uint8_t, 32> const Preim4CondConditionFingerprint = {
            {0x5d, 0xa0, 0x30, 0xef, 0xfd, 0xe1, 0x75, 0x11, 0x51, 0xe8, 0x5f,
             0x5e, 0x54, 0x2d, 0x6a, 0x5b, 0xd1, 0x5c, 0xc9, 0x33, 0x21, 0x2c,
             0xe2, 0x68, 0xfc, 0xfd, 0x53, 0xee, 0x93, 0x58, 0xeb, 0x4e}};
        Condition const Preim4Cond{Type::preimageSha256,
                                   9,
                                   Preim4CondConditionFingerprint,
                                   std::bitset<5>{0}};
        std::array<std::uint8_t, 32> const Rsa5CondConditionFingerprint = {
            {0x99, 0xfb, 0x0b, 0x38, 0x94, 0x4d, 0x20, 0x85, 0xc8, 0xda, 0x3a,
             0x64, 0x31, 0x44, 0x6f, 0x6c, 0x3b, 0x46, 0x25, 0x50, 0xd7, 0x7f,
             0xdf, 0xee, 0x75, 0x72, 0x71, 0xf9, 0x61, 0x40, 0x63, 0xfa}};
        Condition const Rsa5Cond{Type::rsaSha256,
                                 65536,
                                 Rsa5CondConditionFingerprint,
                                 std::bitset<5>{0}};
        std::array<std::uint8_t, 32> const Ed6CondConditionFingerprint = {
            {0x00, 0xd3, 0xc9, 0x24, 0x3f, 0x2d, 0x2e, 0x64, 0x93, 0xa8, 0x49,
             0x29, 0x82, 0x75, 0xea, 0xbf, 0xe3, 0x53, 0x7f, 0x8e, 0x45, 0x16,
             0xdb, 0x5e, 0xc6, 0xdf, 0x39, 0xd2, 0xcb, 0xea, 0x62, 0xfb}};
        Condition const Ed6Cond{Type::ed25519Sha256,
                                131072,
                                Ed6CondConditionFingerprint,
                                std::bitset<5>{0}};
        auto const prefix1Prefix = "P1"s;
        auto const prefix1Msg = "P0abcdefghijklmnopqrstuvwxyz"s;
        auto const prefix1MaxMsgLength = 28;
        auto const prefix0Prefix = "P0"s;
        auto const prefix0Msg = "abcdefghijklmnopqrstuvwxyz"s;
        auto const prefix0MaxMsgLength = 26;

        auto ed3 = std::make_unique<Ed25519>(ed3PublicKey, ed3Sig);
        std::vector<std::unique_ptr<Fulfillment>> thresh2Subfulfillments;
        thresh2Subfulfillments.emplace_back(std::move(ed3));
        std::vector<Condition> thresh2Subconditions{
            {Preim4Cond, Rsa5Cond, Ed6Cond}};
        auto thresh2 = std::make_unique<ThresholdSha256>(
            std::move(thresh2Subfulfillments), std::move(thresh2Subconditions));
        auto prefix1 = std::make_unique<PrefixSha256>(
            makeSlice(prefix1Prefix), prefix1MaxMsgLength, std::move(thresh2));
        auto prefix0 = std::make_unique<PrefixSha256>(
            makeSlice(prefix0Prefix), prefix0MaxMsgLength, std::move(prefix1));
        {
            auto prefix0EncodedFulfillment =
                "\xa1\x81\xfd\x80\x02\x50\x30\x81\x01\x1a\xa2\x81\xf3\xa1\x81"
                "\xf0\x80\x02\x50\x31\x81\x01\x1c\xa2\x81\xe6\xa2\x81\xe3\xa0"
                "\x66\xa4\x64\x80\x20\x42\x0a\x60\x63\x9e\xca\xaa\x48\xac\xcb"
                "\x2c\xd9\x46\x79\x69\x34\x35\xf3\x8e\x29\xd5\x4b\x24\x57\x9d"
                "\xd7\x3b\x04\x5d\x9c\x2f\x32\x81\x40\x8e\xa4\xd9\x6a\x9b\x3e"
                "\x19\xe8\x89\xa4\x4e\xf2\xa8\x1c\xc2\xbd\xd3\xe0\x6f\xe0\xd1"
                "\x6b\x85\x47\x9a\x58\x2e\x9f\x38\x09\x1f\x6d\x02\x9a\xf6\x9c"
                "\x60\x3f\x16\x6c\xa5\x0e\xfb\xa3\x08\xd6\xb6\x97\x5f\x2e\x94"
                "\xfb\x49\x56\xbe\x2c\x58\x48\x15\x49\x73\xa2\xae\x09\xa1\x79"
                "\xa0\x25\x80\x20\x5d\xa0\x30\xef\xfd\xe1\x75\x11\x51\xe8\x5f"
                "\x5e\x54\x2d\x6a\x5b\xd1\x5c\xc9\x33\x21\x2c\xe2\x68\xfc\xfd"
                "\x53\xee\x93\x58\xeb\x4e\x81\x01\x09\xa3\x27\x80\x20\x99\xfb"
                "\x0b\x38\x94\x4d\x20\x85\xc8\xda\x3a\x64\x31\x44\x6f\x6c\x3b"
                "\x46\x25\x50\xd7\x7f\xdf\xee\x75\x72\x71\xf9\x61\x40\x63\xfa"
                "\x81\x03\x01\x00\x00\xa4\x27\x80\x20\x00\xd3\xc9\x24\x3f\x2d"
                "\x2e\x64\x93\xa8\x49\x29\x82\x75\xea\xbf\xe3\x53\x7f\x8e\x45"
                "\x16\xdb\x5e\xc6\xdf\x39\xd2\xcb\xea\x62\xfb\x81\x03\x02\x00"
                "\x00"s;
            auto const prefix0EncodedCondition =
                "\xa1\x2b\x80\x20\xc2\x7a\x27\xd3\x91\x16\x58\x27\x2f\x53\xe0"
                "\x23\x4b\xe3\x3b\xeb\xfa\x83\xb1\xb7\x13\x38\x60\x79\x00\x38"
                "\xc2\x15\x25\x2e\x17\x18\x81\x03\x02\x18\x3a\x82\x02\x03\xb8"s;
            auto const prefix0EncodedFingerprint =
                "\x30\x36\x80\x02\x50\x30\x81\x01\x1a\xa2\x2d\xa1\x2b\x80\x20"
                "\x63\xdf\xba\x5a\x14\xb6\xb3\x15\xfb\xbd\x44\xf2\x29\x36\xdc"
                "\x71\x34\x88\x94\x52\xf3\x57\xf6\x6a\xde\xe3\x70\x51\x65\x67"
                "\xfb\xed\x81\x03\x02\x14\x1e\x82\x02\x03\xb8"s;
            check(
                std::move(prefix0),
                prefix0Msg,
                std::move(prefix0EncodedFulfillment),
                prefix0EncodedCondition,
                prefix0EncodedFingerprint);
        }
    }

    void
    testPrefix19()
    {
        testcase("Prefix19");

        using namespace std::string_literals;
        using namespace ripple::cryptoconditions;

        // Fulfillment structure
        // * prefix0
        // ** prefix1
        // *** thresh2
        // **** Preim9Cond
        // **** Rsa10Cond
        // **** Ed11Cond
        // **** ed3
        // **** thresh4
        // ***** Preim6Cond
        // ***** Rsa7Cond
        // ***** Ed8Cond
        // ***** ed5

        auto const ed3Msg = "P1P0abcdefghijklmnopqrstuvwxyz"s;
        std::array<std::uint8_t, 32> const ed3PublicKey{
            {0x42, 0x0a, 0x60, 0x63, 0x9e, 0xca, 0xaa, 0x48, 0xac, 0xcb, 0x2c,
             0xd9, 0x46, 0x79, 0x69, 0x34, 0x35, 0xf3, 0x8e, 0x29, 0xd5, 0x4b,
             0x24, 0x57, 0x9d, 0xd7, 0x3b, 0x04, 0x5d, 0x9c, 0x2f, 0x32}};
        std::array<std::uint8_t, 64> const ed3Sig{
            {0x8e, 0xa4, 0xd9, 0x6a, 0x9b, 0x3e, 0x19, 0xe8, 0x89, 0xa4, 0x4e,
             0xf2, 0xa8, 0x1c, 0xc2, 0xbd, 0xd3, 0xe0, 0x6f, 0xe0, 0xd1, 0x6b,
             0x85, 0x47, 0x9a, 0x58, 0x2e, 0x9f, 0x38, 0x09, 0x1f, 0x6d, 0x02,
             0x9a, 0xf6, 0x9c, 0x60, 0x3f, 0x16, 0x6c, 0xa5, 0x0e, 0xfb, 0xa3,
             0x08, 0xd6, 0xb6, 0x97, 0x5f, 0x2e, 0x94, 0xfb, 0x49, 0x56, 0xbe,
             0x2c, 0x58, 0x48, 0x15, 0x49, 0x73, 0xa2, 0xae, 0x09}};
        std::array<std::uint8_t, 32> const ed3SigningKey{
            {0x1b, 0x90, 0xf2, 0x2c, 0xd5, 0x5d, 0xcd, 0xe5, 0x2d, 0x71, 0x84,
             0x66, 0x10, 0xf0, 0xf1, 0x91, 0xfe, 0xb5, 0xbe, 0x94, 0x85, 0xc1,
             0xc8, 0xf3, 0x61, 0x91, 0xc3, 0xa4, 0x41, 0x58, 0xcd, 0xfc}};
        (void)ed3SigningKey;
        auto const ed5Msg = "P1P0abcdefghijklmnopqrstuvwxyz"s;
        std::array<std::uint8_t, 32> const ed5PublicKey{
            {0xae, 0xbc, 0xe5, 0x4b, 0x88, 0x09, 0x8d, 0x4f, 0xc4, 0xe1, 0x22,
             0xa0, 0x7c, 0x41, 0x05, 0xd7, 0x9f, 0xbe, 0xc8, 0x3d, 0x1d, 0x7e,
             0xd6, 0x55, 0xf4, 0x01, 0x67, 0x68, 0x93, 0x55, 0x85, 0xdf}};
        std::array<std::uint8_t, 64> const ed5Sig{
            {0x1a, 0xdc, 0xf3, 0xaa, 0xc3, 0x2e, 0xe4, 0xed, 0x1d, 0x40, 0xe0,
             0xac, 0xfa, 0x29, 0xec, 0xe4, 0xc4, 0x9e, 0x92, 0xbd, 0x49, 0x0d,
             0x18, 0xc7, 0xeb, 0x01, 0xa6, 0x6a, 0x92, 0xa3, 0xc4, 0xc2, 0x92,
             0xd6, 0x62, 0x26, 0x6a, 0x3f, 0x8b, 0x0b, 0xa7, 0x11, 0xe0, 0x37,
             0x3e, 0x71, 0x77, 0x97, 0x7e, 0x66, 0x69, 0x4f, 0x58, 0x2e, 0xc0,
             0x05, 0xd5, 0x2e, 0x3f, 0x2b, 0xec, 0xc7, 0x48, 0x02}};
        std::array<std::uint8_t, 32> const ed5SigningKey{
            {0x42, 0x67, 0x67, 0xc0, 0xba, 0xdf, 0xb4, 0xd3, 0xf5, 0xc5, 0x1f,
             0x71, 0x97, 0x8a, 0xb4, 0x8e, 0x9a, 0xea, 0x3e, 0xec, 0xaf, 0xdc,
             0xc7, 0x2b, 0x01, 0x1b, 0x06, 0x8f, 0x05, 0x56, 0x63, 0xbc}};
        (void)ed5SigningKey;
        auto const thresh4Msg = "P1P0abcdefghijklmnopqrstuvwxyz"s;
        std::array<std::uint8_t, 32> const Preim6CondConditionFingerprint = {
            {0x5d, 0xa0, 0x30, 0xef, 0xfd, 0xe1, 0x75, 0x11, 0x51, 0xe8, 0x5f,
             0x5e, 0x54, 0x2d, 0x6a, 0x5b, 0xd1, 0x5c, 0xc9, 0x33, 0x21, 0x2c,
             0xe2, 0x68, 0xfc, 0xfd, 0x53, 0xee, 0x93, 0x58, 0xeb, 0x4e}};
        Condition const Preim6Cond{Type::preimageSha256,
                                   9,
                                   Preim6CondConditionFingerprint,
                                   std::bitset<5>{0}};
        std::array<std::uint8_t, 32> const Rsa7CondConditionFingerprint = {
            {0x6c, 0x7b, 0xea, 0x83, 0xa1, 0xf4, 0x82, 0x3d, 0x36, 0xe7, 0x6e,
             0xae, 0x1a, 0xbc, 0xa0, 0xba, 0x90, 0x3d, 0x96, 0xc1, 0xe6, 0xad,
             0x3a, 0x47, 0xa5, 0xcb, 0x88, 0xab, 0x3c, 0x5f, 0xcc, 0xd5}};
        Condition const Rsa7Cond{Type::rsaSha256,
                                 65536,
                                 Rsa7CondConditionFingerprint,
                                 std::bitset<5>{0}};
        std::array<std::uint8_t, 32> const Ed8CondConditionFingerprint = {
            {0xf1, 0x68, 0x96, 0xa6, 0x2e, 0xef, 0x7f, 0x47, 0x06, 0x51, 0x4c,
             0xc6, 0x7e, 0x24, 0xf7, 0x29, 0x84, 0x9c, 0xd6, 0xb0, 0xd9, 0x4b,
             0xd9, 0x0f, 0xc9, 0x34, 0x01, 0x9d, 0x92, 0xeb, 0xbc, 0x0a}};
        Condition const Ed8Cond{Type::ed25519Sha256,
                                131072,
                                Ed8CondConditionFingerprint,
                                std::bitset<5>{0}};
        auto const thresh2Msg = "P1P0abcdefghijklmnopqrstuvwxyz"s;
        std::array<std::uint8_t, 32> const Preim9CondConditionFingerprint = {
            {0x5d, 0xa0, 0x30, 0xef, 0xfd, 0xe1, 0x75, 0x11, 0x51, 0xe8, 0x5f,
             0x5e, 0x54, 0x2d, 0x6a, 0x5b, 0xd1, 0x5c, 0xc9, 0x33, 0x21, 0x2c,
             0xe2, 0x68, 0xfc, 0xfd, 0x53, 0xee, 0x93, 0x58, 0xeb, 0x4e}};
        Condition const Preim9Cond{Type::preimageSha256,
                                   9,
                                   Preim9CondConditionFingerprint,
                                   std::bitset<5>{0}};
        std::array<std::uint8_t, 32> const Rsa10CondConditionFingerprint = {
            {0x3c, 0x73, 0x38, 0xcf, 0x23, 0xc6, 0x31, 0x53, 0x28, 0xc4, 0x27,
             0xf8, 0x95, 0x87, 0x99, 0x83, 0x2d, 0x35, 0x3c, 0x03, 0x9b, 0xd1,
             0xff, 0xff, 0x2e, 0x53, 0x20, 0xe9, 0x5e, 0x62, 0xb9, 0xb7}};
        Condition const Rsa10Cond{Type::rsaSha256,
                                  65536,
                                  Rsa10CondConditionFingerprint,
                                  std::bitset<5>{0}};
        std::array<std::uint8_t, 32> const Ed11CondConditionFingerprint = {
            {0x41, 0x80, 0x08, 0xb2, 0x60, 0x74, 0x57, 0x6d, 0xac, 0xed, 0x74,
             0x7f, 0x54, 0xdb, 0x96, 0x18, 0x91, 0x06, 0x0a, 0x95, 0xa1, 0x49,
             0x17, 0xc7, 0x65, 0xe3, 0x94, 0xc8, 0x5e, 0x2c, 0x92, 0x20}};
        Condition const Ed11Cond{Type::ed25519Sha256,
                                 131072,
                                 Ed11CondConditionFingerprint,
                                 std::bitset<5>{0}};
        auto const prefix1Prefix = "P1"s;
        auto const prefix1Msg = "P0abcdefghijklmnopqrstuvwxyz"s;
        auto const prefix1MaxMsgLength = 28;
        auto const prefix0Prefix = "P0"s;
        auto const prefix0Msg = "abcdefghijklmnopqrstuvwxyz"s;
        auto const prefix0MaxMsgLength = 26;

        auto ed3 = std::make_unique<Ed25519>(ed3PublicKey, ed3Sig);
        auto ed5 = std::make_unique<Ed25519>(ed5PublicKey, ed5Sig);
        std::vector<std::unique_ptr<Fulfillment>> thresh4Subfulfillments;
        thresh4Subfulfillments.emplace_back(std::move(ed5));
        std::vector<Condition> thresh4Subconditions{
            {Preim6Cond, Rsa7Cond, Ed8Cond}};
        auto thresh4 = std::make_unique<ThresholdSha256>(
            std::move(thresh4Subfulfillments), std::move(thresh4Subconditions));
        std::vector<std::unique_ptr<Fulfillment>> thresh2Subfulfillments;
        thresh2Subfulfillments.emplace_back(std::move(ed3));
        thresh2Subfulfillments.emplace_back(std::move(thresh4));
        std::vector<Condition> thresh2Subconditions{
            {Preim9Cond, Rsa10Cond, Ed11Cond}};
        auto thresh2 = std::make_unique<ThresholdSha256>(
            std::move(thresh2Subfulfillments), std::move(thresh2Subconditions));
        auto prefix1 = std::make_unique<PrefixSha256>(
            makeSlice(prefix1Prefix), prefix1MaxMsgLength, std::move(thresh2));
        auto prefix0 = std::make_unique<PrefixSha256>(
            makeSlice(prefix0Prefix), prefix0MaxMsgLength, std::move(prefix1));
        {
            auto prefix0EncodedFulfillment =
                "\xa1\x82\x01\xe9\x80\x02\x50\x30\x81\x01\x1a\xa2\x82\x01\xde"
                "\xa1\x82\x01\xda\x80\x02\x50\x31\x81\x01\x1c\xa2\x82\x01\xcf"
                "\xa2\x82\x01\xcb\xa0\x82\x01\x4c\xa2\x81\xe3\xa0\x66\xa4\x64"
                "\x80\x20\xae\xbc\xe5\x4b\x88\x09\x8d\x4f\xc4\xe1\x22\xa0\x7c"
                "\x41\x05\xd7\x9f\xbe\xc8\x3d\x1d\x7e\xd6\x55\xf4\x01\x67\x68"
                "\x93\x55\x85\xdf\x81\x40\x1a\xdc\xf3\xaa\xc3\x2e\xe4\xed\x1d"
                "\x40\xe0\xac\xfa\x29\xec\xe4\xc4\x9e\x92\xbd\x49\x0d\x18\xc7"
                "\xeb\x01\xa6\x6a\x92\xa3\xc4\xc2\x92\xd6\x62\x26\x6a\x3f\x8b"
                "\x0b\xa7\x11\xe0\x37\x3e\x71\x77\x97\x7e\x66\x69\x4f\x58\x2e"
                "\xc0\x05\xd5\x2e\x3f\x2b\xec\xc7\x48\x02\xa1\x79\xa0\x25\x80"
                "\x20\x5d\xa0\x30\xef\xfd\xe1\x75\x11\x51\xe8\x5f\x5e\x54\x2d"
                "\x6a\x5b\xd1\x5c\xc9\x33\x21\x2c\xe2\x68\xfc\xfd\x53\xee\x93"
                "\x58\xeb\x4e\x81\x01\x09\xa3\x27\x80\x20\x6c\x7b\xea\x83\xa1"
                "\xf4\x82\x3d\x36\xe7\x6e\xae\x1a\xbc\xa0\xba\x90\x3d\x96\xc1"
                "\xe6\xad\x3a\x47\xa5\xcb\x88\xab\x3c\x5f\xcc\xd5\x81\x03\x01"
                "\x00\x00\xa4\x27\x80\x20\xf1\x68\x96\xa6\x2e\xef\x7f\x47\x06"
                "\x51\x4c\xc6\x7e\x24\xf7\x29\x84\x9c\xd6\xb0\xd9\x4b\xd9\x0f"
                "\xc9\x34\x01\x9d\x92\xeb\xbc\x0a\x81\x03\x02\x00\x00\xa4\x64"
                "\x80\x20\x42\x0a\x60\x63\x9e\xca\xaa\x48\xac\xcb\x2c\xd9\x46"
                "\x79\x69\x34\x35\xf3\x8e\x29\xd5\x4b\x24\x57\x9d\xd7\x3b\x04"
                "\x5d\x9c\x2f\x32\x81\x40\x8e\xa4\xd9\x6a\x9b\x3e\x19\xe8\x89"
                "\xa4\x4e\xf2\xa8\x1c\xc2\xbd\xd3\xe0\x6f\xe0\xd1\x6b\x85\x47"
                "\x9a\x58\x2e\x9f\x38\x09\x1f\x6d\x02\x9a\xf6\x9c\x60\x3f\x16"
                "\x6c\xa5\x0e\xfb\xa3\x08\xd6\xb6\x97\x5f\x2e\x94\xfb\x49\x56"
                "\xbe\x2c\x58\x48\x15\x49\x73\xa2\xae\x09\xa1\x79\xa0\x25\x80"
                "\x20\x5d\xa0\x30\xef\xfd\xe1\x75\x11\x51\xe8\x5f\x5e\x54\x2d"
                "\x6a\x5b\xd1\x5c\xc9\x33\x21\x2c\xe2\x68\xfc\xfd\x53\xee\x93"
                "\x58\xeb\x4e\x81\x01\x09\xa3\x27\x80\x20\x3c\x73\x38\xcf\x23"
                "\xc6\x31\x53\x28\xc4\x27\xf8\x95\x87\x99\x83\x2d\x35\x3c\x03"
                "\x9b\xd1\xff\xff\x2e\x53\x20\xe9\x5e\x62\xb9\xb7\x81\x03\x01"
                "\x00\x00\xa4\x27\x80\x20\x41\x80\x08\xb2\x60\x74\x57\x6d\xac"
                "\xed\x74\x7f\x54\xdb\x96\x18\x91\x06\x0a\x95\xa1\x49\x17\xc7"
                "\x65\xe3\x94\xc8\x5e\x2c\x92\x20\x81\x03\x02\x00\x00"s;
            auto const prefix0EncodedCondition =
                "\xa1\x2b\x80\x20\x0e\xed\xea\x70\x12\x69\x3e\x06\x78\xb6\xfc"
                "\x3b\xa4\x53\x2f\x95\x74\xcd\x52\x70\x76\xd3\xee\x3e\x5b\xb9"
                "\x10\xbb\x34\x48\x21\x4c\x81\x03\x04\x2c\x3a\x82\x02\x03\xb8"s;
            auto const prefix0EncodedFingerprint =
                "\x30\x36\x80\x02\x50\x30\x81\x01\x1a\xa2\x2d\xa1\x2b\x80\x20"
                "\x93\xc4\x8b\x6e\x4e\x9b\xab\x04\x5c\x8e\xf9\x36\x24\xef\x21"
                "\xa2\x8b\x40\xef\x0c\x11\xaa\x18\x3e\x32\x3c\xa4\xe6\x94\xdc"
                "\xe7\xb7\x81\x03\x04\x28\x1e\x82\x02\x03\xb8"s;
            check(
                std::move(prefix0),
                prefix0Msg,
                std::move(prefix0EncodedFulfillment),
                prefix0EncodedCondition,
                prefix0EncodedFingerprint);
        }
    }

    void
    testPrefix20()
    {
        testcase("Prefix20");

        using namespace std::string_literals;
        using namespace ripple::cryptoconditions;

        // Fulfillment structure
        // * prefix0
        // ** prefix1
        // *** thresh2
        // **** Preim9Cond
        // **** Rsa10Cond
        // **** Ed11Cond
        // **** Thresh12Cond
        // **** ed3
        // **** thresh4
        // ***** Preim6Cond
        // ***** Rsa7Cond
        // ***** Ed8Cond
        // ***** ed5

        auto const ed3Msg = "P1P0abcdefghijklmnopqrstuvwxyz"s;
        std::array<std::uint8_t, 32> const ed3PublicKey{
            {0x42, 0x0a, 0x60, 0x63, 0x9e, 0xca, 0xaa, 0x48, 0xac, 0xcb, 0x2c,
             0xd9, 0x46, 0x79, 0x69, 0x34, 0x35, 0xf3, 0x8e, 0x29, 0xd5, 0x4b,
             0x24, 0x57, 0x9d, 0xd7, 0x3b, 0x04, 0x5d, 0x9c, 0x2f, 0x32}};
        std::array<std::uint8_t, 64> const ed3Sig{
            {0x8e, 0xa4, 0xd9, 0x6a, 0x9b, 0x3e, 0x19, 0xe8, 0x89, 0xa4, 0x4e,
             0xf2, 0xa8, 0x1c, 0xc2, 0xbd, 0xd3, 0xe0, 0x6f, 0xe0, 0xd1, 0x6b,
             0x85, 0x47, 0x9a, 0x58, 0x2e, 0x9f, 0x38, 0x09, 0x1f, 0x6d, 0x02,
             0x9a, 0xf6, 0x9c, 0x60, 0x3f, 0x16, 0x6c, 0xa5, 0x0e, 0xfb, 0xa3,
             0x08, 0xd6, 0xb6, 0x97, 0x5f, 0x2e, 0x94, 0xfb, 0x49, 0x56, 0xbe,
             0x2c, 0x58, 0x48, 0x15, 0x49, 0x73, 0xa2, 0xae, 0x09}};
        std::array<std::uint8_t, 32> const ed3SigningKey{
            {0x1b, 0x90, 0xf2, 0x2c, 0xd5, 0x5d, 0xcd, 0xe5, 0x2d, 0x71, 0x84,
             0x66, 0x10, 0xf0, 0xf1, 0x91, 0xfe, 0xb5, 0xbe, 0x94, 0x85, 0xc1,
             0xc8, 0xf3, 0x61, 0x91, 0xc3, 0xa4, 0x41, 0x58, 0xcd, 0xfc}};
        (void)ed3SigningKey;
        auto const ed5Msg = "P1P0abcdefghijklmnopqrstuvwxyz"s;
        std::array<std::uint8_t, 32> const ed5PublicKey{
            {0xae, 0xbc, 0xe5, 0x4b, 0x88, 0x09, 0x8d, 0x4f, 0xc4, 0xe1, 0x22,
             0xa0, 0x7c, 0x41, 0x05, 0xd7, 0x9f, 0xbe, 0xc8, 0x3d, 0x1d, 0x7e,
             0xd6, 0x55, 0xf4, 0x01, 0x67, 0x68, 0x93, 0x55, 0x85, 0xdf}};
        std::array<std::uint8_t, 64> const ed5Sig{
            {0x1a, 0xdc, 0xf3, 0xaa, 0xc3, 0x2e, 0xe4, 0xed, 0x1d, 0x40, 0xe0,
             0xac, 0xfa, 0x29, 0xec, 0xe4, 0xc4, 0x9e, 0x92, 0xbd, 0x49, 0x0d,
             0x18, 0xc7, 0xeb, 0x01, 0xa6, 0x6a, 0x92, 0xa3, 0xc4, 0xc2, 0x92,
             0xd6, 0x62, 0x26, 0x6a, 0x3f, 0x8b, 0x0b, 0xa7, 0x11, 0xe0, 0x37,
             0x3e, 0x71, 0x77, 0x97, 0x7e, 0x66, 0x69, 0x4f, 0x58, 0x2e, 0xc0,
             0x05, 0xd5, 0x2e, 0x3f, 0x2b, 0xec, 0xc7, 0x48, 0x02}};
        std::array<std::uint8_t, 32> const ed5SigningKey{
            {0x42, 0x67, 0x67, 0xc0, 0xba, 0xdf, 0xb4, 0xd3, 0xf5, 0xc5, 0x1f,
             0x71, 0x97, 0x8a, 0xb4, 0x8e, 0x9a, 0xea, 0x3e, 0xec, 0xaf, 0xdc,
             0xc7, 0x2b, 0x01, 0x1b, 0x06, 0x8f, 0x05, 0x56, 0x63, 0xbc}};
        (void)ed5SigningKey;
        auto const thresh4Msg = "P1P0abcdefghijklmnopqrstuvwxyz"s;
        std::array<std::uint8_t, 32> const Preim6CondConditionFingerprint = {
            {0x5d, 0xa0, 0x30, 0xef, 0xfd, 0xe1, 0x75, 0x11, 0x51, 0xe8, 0x5f,
             0x5e, 0x54, 0x2d, 0x6a, 0x5b, 0xd1, 0x5c, 0xc9, 0x33, 0x21, 0x2c,
             0xe2, 0x68, 0xfc, 0xfd, 0x53, 0xee, 0x93, 0x58, 0xeb, 0x4e}};
        Condition const Preim6Cond{Type::preimageSha256,
                                   9,
                                   Preim6CondConditionFingerprint,
                                   std::bitset<5>{0}};
        std::array<std::uint8_t, 32> const Rsa7CondConditionFingerprint = {
            {0x6c, 0x7b, 0xea, 0x83, 0xa1, 0xf4, 0x82, 0x3d, 0x36, 0xe7, 0x6e,
             0xae, 0x1a, 0xbc, 0xa0, 0xba, 0x90, 0x3d, 0x96, 0xc1, 0xe6, 0xad,
             0x3a, 0x47, 0xa5, 0xcb, 0x88, 0xab, 0x3c, 0x5f, 0xcc, 0xd5}};
        Condition const Rsa7Cond{Type::rsaSha256,
                                 65536,
                                 Rsa7CondConditionFingerprint,
                                 std::bitset<5>{0}};
        std::array<std::uint8_t, 32> const Ed8CondConditionFingerprint = {
            {0xf1, 0x68, 0x96, 0xa6, 0x2e, 0xef, 0x7f, 0x47, 0x06, 0x51, 0x4c,
             0xc6, 0x7e, 0x24, 0xf7, 0x29, 0x84, 0x9c, 0xd6, 0xb0, 0xd9, 0x4b,
             0xd9, 0x0f, 0xc9, 0x34, 0x01, 0x9d, 0x92, 0xeb, 0xbc, 0x0a}};
        Condition const Ed8Cond{Type::ed25519Sha256,
                                131072,
                                Ed8CondConditionFingerprint,
                                std::bitset<5>{0}};
        auto const thresh2Msg = "P1P0abcdefghijklmnopqrstuvwxyz"s;
        std::array<std::uint8_t, 32> const Preim9CondConditionFingerprint = {
            {0x5d, 0xa0, 0x30, 0xef, 0xfd, 0xe1, 0x75, 0x11, 0x51, 0xe8, 0x5f,
             0x5e, 0x54, 0x2d, 0x6a, 0x5b, 0xd1, 0x5c, 0xc9, 0x33, 0x21, 0x2c,
             0xe2, 0x68, 0xfc, 0xfd, 0x53, 0xee, 0x93, 0x58, 0xeb, 0x4e}};
        Condition const Preim9Cond{Type::preimageSha256,
                                   9,
                                   Preim9CondConditionFingerprint,
                                   std::bitset<5>{0}};
        std::array<std::uint8_t, 32> const Rsa10CondConditionFingerprint = {
            {0x3c, 0x73, 0x38, 0xcf, 0x23, 0xc6, 0x31, 0x53, 0x28, 0xc4, 0x27,
             0xf8, 0x95, 0x87, 0x99, 0x83, 0x2d, 0x35, 0x3c, 0x03, 0x9b, 0xd1,
             0xff, 0xff, 0x2e, 0x53, 0x20, 0xe9, 0x5e, 0x62, 0xb9, 0xb7}};
        Condition const Rsa10Cond{Type::rsaSha256,
                                  65536,
                                  Rsa10CondConditionFingerprint,
                                  std::bitset<5>{0}};
        std::array<std::uint8_t, 32> const Ed11CondConditionFingerprint = {
            {0x41, 0x80, 0x08, 0xb2, 0x60, 0x74, 0x57, 0x6d, 0xac, 0xed, 0x74,
             0x7f, 0x54, 0xdb, 0x96, 0x18, 0x91, 0x06, 0x0a, 0x95, 0xa1, 0x49,
             0x17, 0xc7, 0x65, 0xe3, 0x94, 0xc8, 0x5e, 0x2c, 0x92, 0x20}};
        Condition const Ed11Cond{Type::ed25519Sha256,
                                 131072,
                                 Ed11CondConditionFingerprint,
                                 std::bitset<5>{0}};
        std::array<std::uint8_t, 32> const Thresh12CondConditionFingerprint = {
            {0x16, 0x7f, 0xbc, 0x09, 0xc8, 0x59, 0x71, 0x7b, 0x2e, 0xb0, 0x65,
             0x72, 0xdf, 0x23, 0xe9, 0x85, 0x65, 0x99, 0x10, 0x58, 0x49, 0x09,
             0x94, 0xda, 0x03, 0x4d, 0x77, 0xf8, 0xcc, 0x91, 0xac, 0x32}};
        Condition const Thresh12Cond{Type::thresholdSha256,
                                     135168,
                                     Thresh12CondConditionFingerprint,
                                     std::bitset<5>{25}};
        auto const prefix1Prefix = "P1"s;
        auto const prefix1Msg = "P0abcdefghijklmnopqrstuvwxyz"s;
        auto const prefix1MaxMsgLength = 28;
        auto const prefix0Prefix = "P0"s;
        auto const prefix0Msg = "abcdefghijklmnopqrstuvwxyz"s;
        auto const prefix0MaxMsgLength = 26;

        auto ed3 = std::make_unique<Ed25519>(ed3PublicKey, ed3Sig);
        auto ed5 = std::make_unique<Ed25519>(ed5PublicKey, ed5Sig);
        std::vector<std::unique_ptr<Fulfillment>> thresh4Subfulfillments;
        thresh4Subfulfillments.emplace_back(std::move(ed5));
        std::vector<Condition> thresh4Subconditions{
            {Preim6Cond, Rsa7Cond, Ed8Cond}};
        auto thresh4 = std::make_unique<ThresholdSha256>(
            std::move(thresh4Subfulfillments), std::move(thresh4Subconditions));
        std::vector<std::unique_ptr<Fulfillment>> thresh2Subfulfillments;
        thresh2Subfulfillments.emplace_back(std::move(ed3));
        thresh2Subfulfillments.emplace_back(std::move(thresh4));
        std::vector<Condition> thresh2Subconditions{
            {Preim9Cond, Rsa10Cond, Ed11Cond, Thresh12Cond}};
        auto thresh2 = std::make_unique<ThresholdSha256>(
            std::move(thresh2Subfulfillments), std::move(thresh2Subconditions));
        auto prefix1 = std::make_unique<PrefixSha256>(
            makeSlice(prefix1Prefix), prefix1MaxMsgLength, std::move(thresh2));
        auto prefix0 = std::make_unique<PrefixSha256>(
            makeSlice(prefix0Prefix), prefix0MaxMsgLength, std::move(prefix1));
        {
            auto prefix0EncodedFulfillment =
                "\xa1\x82\x02\x17\x80\x02\x50\x30\x81\x01\x1a\xa2\x82\x02\x0c"
                "\xa1\x82\x02\x08\x80\x02\x50\x31\x81\x01\x1c\xa2\x82\x01\xfd"
                "\xa2\x82\x01\xf9\xa0\x82\x01\x4c\xa2\x81\xe3\xa0\x66\xa4\x64"
                "\x80\x20\xae\xbc\xe5\x4b\x88\x09\x8d\x4f\xc4\xe1\x22\xa0\x7c"
                "\x41\x05\xd7\x9f\xbe\xc8\x3d\x1d\x7e\xd6\x55\xf4\x01\x67\x68"
                "\x93\x55\x85\xdf\x81\x40\x1a\xdc\xf3\xaa\xc3\x2e\xe4\xed\x1d"
                "\x40\xe0\xac\xfa\x29\xec\xe4\xc4\x9e\x92\xbd\x49\x0d\x18\xc7"
                "\xeb\x01\xa6\x6a\x92\xa3\xc4\xc2\x92\xd6\x62\x26\x6a\x3f\x8b"
                "\x0b\xa7\x11\xe0\x37\x3e\x71\x77\x97\x7e\x66\x69\x4f\x58\x2e"
                "\xc0\x05\xd5\x2e\x3f\x2b\xec\xc7\x48\x02\xa1\x79\xa0\x25\x80"
                "\x20\x5d\xa0\x30\xef\xfd\xe1\x75\x11\x51\xe8\x5f\x5e\x54\x2d"
                "\x6a\x5b\xd1\x5c\xc9\x33\x21\x2c\xe2\x68\xfc\xfd\x53\xee\x93"
                "\x58\xeb\x4e\x81\x01\x09\xa3\x27\x80\x20\x6c\x7b\xea\x83\xa1"
                "\xf4\x82\x3d\x36\xe7\x6e\xae\x1a\xbc\xa0\xba\x90\x3d\x96\xc1"
                "\xe6\xad\x3a\x47\xa5\xcb\x88\xab\x3c\x5f\xcc\xd5\x81\x03\x01"
                "\x00\x00\xa4\x27\x80\x20\xf1\x68\x96\xa6\x2e\xef\x7f\x47\x06"
                "\x51\x4c\xc6\x7e\x24\xf7\x29\x84\x9c\xd6\xb0\xd9\x4b\xd9\x0f"
                "\xc9\x34\x01\x9d\x92\xeb\xbc\x0a\x81\x03\x02\x00\x00\xa4\x64"
                "\x80\x20\x42\x0a\x60\x63\x9e\xca\xaa\x48\xac\xcb\x2c\xd9\x46"
                "\x79\x69\x34\x35\xf3\x8e\x29\xd5\x4b\x24\x57\x9d\xd7\x3b\x04"
                "\x5d\x9c\x2f\x32\x81\x40\x8e\xa4\xd9\x6a\x9b\x3e\x19\xe8\x89"
                "\xa4\x4e\xf2\xa8\x1c\xc2\xbd\xd3\xe0\x6f\xe0\xd1\x6b\x85\x47"
                "\x9a\x58\x2e\x9f\x38\x09\x1f\x6d\x02\x9a\xf6\x9c\x60\x3f\x16"
                "\x6c\xa5\x0e\xfb\xa3\x08\xd6\xb6\x97\x5f\x2e\x94\xfb\x49\x56"
                "\xbe\x2c\x58\x48\x15\x49\x73\xa2\xae\x09\xa1\x81\xa6\xa0\x25"
                "\x80\x20\x5d\xa0\x30\xef\xfd\xe1\x75\x11\x51\xe8\x5f\x5e\x54"
                "\x2d\x6a\x5b\xd1\x5c\xc9\x33\x21\x2c\xe2\x68\xfc\xfd\x53\xee"
                "\x93\x58\xeb\x4e\x81\x01\x09\xa2\x2b\x80\x20\x16\x7f\xbc\x09"
                "\xc8\x59\x71\x7b\x2e\xb0\x65\x72\xdf\x23\xe9\x85\x65\x99\x10"
                "\x58\x49\x09\x94\xda\x03\x4d\x77\xf8\xcc\x91\xac\x32\x81\x03"
                "\x02\x10\x00\x82\x02\x03\x98\xa3\x27\x80\x20\x3c\x73\x38\xcf"
                "\x23\xc6\x31\x53\x28\xc4\x27\xf8\x95\x87\x99\x83\x2d\x35\x3c"
                "\x03\x9b\xd1\xff\xff\x2e\x53\x20\xe9\x5e\x62\xb9\xb7\x81\x03"
                "\x01\x00\x00\xa4\x27\x80\x20\x41\x80\x08\xb2\x60\x74\x57\x6d"
                "\xac\xed\x74\x7f\x54\xdb\x96\x18\x91\x06\x0a\x95\xa1\x49\x17"
                "\xc7\x65\xe3\x94\xc8\x5e\x2c\x92\x20\x81\x03\x02\x00\x00"s;
            auto const prefix0EncodedCondition =
                "\xa1\x2b\x80\x20\xb5\x1f\x67\xde\x5c\xd9\xff\xeb\xfd\xd0\xd8"
                "\x47\xad\xdb\xd3\x0c\xaa\x76\xb3\x07\xdf\xac\x77\x09\xb0\xd7"
                "\x22\x32\x05\x4b\x72\x6f\x81\x03\x04\x40\x3a\x82\x02\x03\xb8"s;
            auto const prefix0EncodedFingerprint =
                "\x30\x36\x80\x02\x50\x30\x81\x01\x1a\xa2\x2d\xa1\x2b\x80\x20"
                "\x19\xda\x6c\x14\xec\x59\xe3\xba\x19\x5b\xc5\x0e\x52\xa1\x75"
                "\xc1\xfa\x37\x4d\x2d\x5f\xb5\x73\x7c\xd9\x33\x67\x1d\xd5\x2f"
                "\xce\xfa\x81\x03\x04\x3c\x1e\x82\x02\x03\xb8"s;
            check(
                std::move(prefix0),
                prefix0Msg,
                std::move(prefix0EncodedFulfillment),
                prefix0EncodedCondition,
                prefix0EncodedFingerprint);
        }
    }

    void
    run()
    {
        testPrefix0();
        testPrefix1();
        testPrefix2();
        testPrefix3();
        testPrefix4();
        testPrefix5();
        testPrefix6();
        testPrefix7();
        testPrefix8();
        testPrefix9();
        testPrefix10();
        testPrefix11();
        testPrefix12();
        testPrefix13();
        testPrefix14();
        testPrefix15();
        testPrefix16();
        testPrefix17();
        testPrefix18();
        testPrefix19();
        testPrefix20();
    }
};

BEAST_DEFINE_TESTSUITE(Conditions_prefix, conditions, ripple);
}  // cryptoconditions
}  // ripple
