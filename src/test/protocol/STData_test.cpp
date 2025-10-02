//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2025 Ripple Labs Inc.

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

#include <xrpl/basics/StringUtilities.h>
#include <xrpl/beast/unit_test.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STData.h>
#include <xrpl/protocol/jss.h>

namespace ripple {

struct STData_test : public beast::unit_test::suite
{
    void
    testConstructors()
    {
        testcase("Constructors");

        auto const& sf = sfParameterValue;

        // Default constructor
        {
            STData data(sf);
            BEAST_EXPECT(data.getSType() == STI_DATA);
            BEAST_EXPECT(data.isDefault());
        }

        // Type-specific constructors
        {
            // UINT8
            STData data_u8(sf, static_cast<std::uint8_t>(8));
            BEAST_EXPECT(data_u8.getFieldU8() == 8);
            BEAST_EXPECT(data_u8.getInnerTypeString() == "UINT8");
            BEAST_EXPECT(data_u8.isDefault());

            // UINT16
            STData data_u16(sf, static_cast<std::uint16_t>(16));
            BEAST_EXPECT(data_u16.getFieldU16() == 16);
            BEAST_EXPECT(data_u16.getInnerTypeString() == "UINT16");

            // UINT32
            STData data_u32(sf, static_cast<std::uint32_t>(32));
            BEAST_EXPECT(data_u32.getFieldU32() == 32);
            BEAST_EXPECT(data_u32.getInnerTypeString() == "UINT32");

            // UINT64
            STData data_u64(sf, static_cast<std::uint64_t>(64));
            BEAST_EXPECT(data_u64.getFieldU64() == 64);
            BEAST_EXPECT(data_u64.getInnerTypeString() == "UINT64");

            // UINT128
            uint128 val128 = uint128(1);
            STData data_u128(sf, val128);
            BEAST_EXPECT(data_u128.getFieldH128() == val128);
            BEAST_EXPECT(data_u128.getInnerTypeString() == "UINT128");

            // UINT160
            uint160 val160 = uint160(1);
            STData data_u160(sf, val160);
            BEAST_EXPECT(data_u160.getFieldH160() == val160);
            BEAST_EXPECT(data_u160.getInnerTypeString() == "UINT160");

            // UINT192
            uint192 val192 = uint192(1);
            STData data_u192(sf, val192);
            BEAST_EXPECT(data_u192.getFieldH192() == val192);
            BEAST_EXPECT(data_u192.getInnerTypeString() == "UINT192");

            // UINT256
            uint256 val256 = uint256(1);
            STData data_u256(sf, val256);
            BEAST_EXPECT(data_u256.getFieldH256() == val256);
            BEAST_EXPECT(data_u256.getInnerTypeString() == "UINT256");

            // Blob
            Blob blob = strUnHex("DEADBEEFCAFEBABE").value();
            STData data_blob(sf, blob);
            BEAST_EXPECT(data_blob.getFieldVL() == blob);
            BEAST_EXPECT(data_blob.getInnerTypeString() == "VL");

            // Slice
            std::string test_str = "Hello World";
            Slice slice(test_str.data(), test_str.size());
            STData data_slice(sf, slice);
            Blob expected_blob(test_str.begin(), test_str.end());
            BEAST_EXPECT(data_slice.getFieldVL() == expected_blob);

            // AccountID
            AccountID account(0x123456789ABCDEF0);
            STData data_account(sf, account);
            BEAST_EXPECT(data_account.getAccountID() == account);
            BEAST_EXPECT(data_account.getInnerTypeString() == "ACCOUNT");

            // STAmount (Native)
            STAmount amount_native(1000);
            STData data_amount_native(sf, amount_native);
            BEAST_EXPECT(data_amount_native.getFieldAmount() == amount_native);
            BEAST_EXPECT(data_amount_native.getInnerTypeString() == "AMOUNT");

            // STAmount (IOU)
            IOUAmount iou_amount(5000);
            Issue const usd(
                Currency(0x5553440000000000),
                parseBase58<AccountID>("rG1QQv2nh2gr7RCZ1P8YYcBUKCCN633jCn")
                    .value());
            STAmount amount_iou(iou_amount, usd);
            STData data_amount_iou(sf, amount_iou);
            BEAST_EXPECT(data_amount_iou.getFieldAmount() == amount_iou);
        }
    }

    void
    testSerializationDeserialization()
    {
        testcase("Serialization/Deserialization");

        auto const& sf = sfParameterValue;

        // Test each type's serialization and deserialization round-trip
        {
            // UINT8
            std::uint8_t original_u8 = 8;
            STData data_u8(sf, original_u8);

            Serializer s;
            data_u8.add(s);

            SerialIter sit(s.slice());
            STData deserialized_u8(sit, sf);

            BEAST_EXPECT(deserialized_u8.getFieldU8() == original_u8);
            BEAST_EXPECT(deserialized_u8.getInnerTypeString() == "UINT8");
        }

        {
            // UINT16
            std::uint16_t original_u16 = 16;
            STData data_u16(sf, original_u16);

            Serializer s;
            data_u16.add(s);

            SerialIter sit(s.slice());
            STData deserialized_u16(sit, sf);

            BEAST_EXPECT(deserialized_u16.getFieldU16() == original_u16);
            BEAST_EXPECT(deserialized_u16.getInnerTypeString() == "UINT16");
        }

        {
            // UINT32
            std::uint32_t original_u32 = 32;
            STData data_u32(sf, original_u32);

            Serializer s;
            data_u32.add(s);

            SerialIter sit(s.slice());
            STData deserialized_u32(sit, sf);

            BEAST_EXPECT(deserialized_u32.getFieldU32() == original_u32);
            BEAST_EXPECT(deserialized_u32.getInnerTypeString() == "UINT32");
        }

        {
            // UINT64
            std::uint64_t original_u64 = 64;
            STData data_u64(sf, original_u64);

            Serializer s;
            data_u64.add(s);

            SerialIter sit(s.slice());
            STData deserialized_u64(sit, sf);

            BEAST_EXPECT(deserialized_u64.getFieldU64() == original_u64);
            BEAST_EXPECT(deserialized_u64.getInnerTypeString() == "UINT64");
        }

        {
            // UINT128
            uint128 original_u128 = uint128(1);
            STData data_u128(sf, original_u128);

            Serializer s;
            data_u128.add(s);

            SerialIter sit(s.slice());
            STData deserialized_u128(sit, sf);

            BEAST_EXPECT(deserialized_u128.getFieldH128() == original_u128);
            BEAST_EXPECT(deserialized_u128.getInnerTypeString() == "UINT128");
        }

        {
            // UINT160
            uint160 original_u160 = uint160(1);
            STData data_u160(sf, original_u160);

            Serializer s;
            data_u160.add(s);

            SerialIter sit(s.slice());
            STData deserialized_u160(sit, sf);

            BEAST_EXPECT(deserialized_u160.getFieldH160() == original_u160);
            BEAST_EXPECT(deserialized_u160.getInnerTypeString() == "UINT160");
        }

        {
            // UINT192
            uint192 original_u192 = uint192(1);
            STData data_u192(sf, original_u192);

            Serializer s;
            data_u192.add(s);

            SerialIter sit(s.slice());
            STData deserialized_u192(sit, sf);

            BEAST_EXPECT(deserialized_u192.getFieldH192() == original_u192);
            BEAST_EXPECT(deserialized_u192.getInnerTypeString() == "UINT192");
        }

        {
            // UINT256
            uint256 original_u256 = uint256(1);
            STData data_u256(sf, original_u256);

            Serializer s;
            data_u256.add(s);

            SerialIter sit(s.slice());
            STData deserialized_u256(sit, sf);

            BEAST_EXPECT(deserialized_u256.getFieldH256() == original_u256);
            BEAST_EXPECT(deserialized_u256.getInnerTypeString() == "UINT256");
        }

        {
            // VL (Variable Length)
            Blob original_blob =
                strUnHex("DEADBEEFCAFEBABE1234567890ABCDEF").value();
            STData data_vl(sf, original_blob);

            Serializer s;
            data_vl.add(s);

            SerialIter sit(s.slice());
            STData deserialized_vl(sit, sf);

            BEAST_EXPECT(deserialized_vl.getFieldVL() == original_blob);
            BEAST_EXPECT(deserialized_vl.getInnerTypeString() == "VL");
        }

        {
            // ACCOUNT
            AccountID original_account(0xFEDCBA9876543210);
            STData data_account(sf, original_account);

            Serializer s;
            data_account.add(s);

            SerialIter sit(s.slice());
            STData deserialized_account(sit, sf);

            BEAST_EXPECT(
                deserialized_account.getAccountID() == original_account);
            BEAST_EXPECT(
                deserialized_account.getInnerTypeString() == "ACCOUNT");
        }

        {
            // AMOUNT (Native)
            STAmount original_amount(99999);
            STData data_amount(sf, original_amount);

            Serializer s;
            data_amount.add(s);

            SerialIter sit(s.slice());
            STData deserialized_amount(sit, sf);

            BEAST_EXPECT(
                deserialized_amount.getFieldAmount() == original_amount);
            BEAST_EXPECT(deserialized_amount.getInnerTypeString() == "AMOUNT");
        }

        {
            // CURRENCY
        }

        {
            // ISSUE
        }

        {
            // NUMBER
        }
    }

    void
    testSettersAndGetters()
    {
        testcase("Setters and Getters");

        auto const& sf = sfParameterValue;
        STData data(sf);

        // Test all setter/getter combinations
        {
            // UINT8
            unsigned char val_u8 = 8;
            data.setFieldU8(val_u8);
            BEAST_EXPECT(data.getFieldU8() == val_u8);
            BEAST_EXPECT(data.getInnerTypeString() == "UINT8");
        }

        {
            // UINT16
            std::uint16_t val_u16 = 16;
            data.setFieldU16(val_u16);
            BEAST_EXPECT(data.getFieldU16() == val_u16);
            BEAST_EXPECT(data.getInnerTypeString() == "UINT16");
        }

        {
            // UINT32
            std::uint32_t val_u32 = 32;
            data.setFieldU32(val_u32);
            BEAST_EXPECT(data.getFieldU32() == val_u32);
            BEAST_EXPECT(data.getInnerTypeString() == "UINT32");
        }

        {
            // UINT64
            std::uint64_t val_u64 = 64;
            data.setFieldU64(val_u64);
            BEAST_EXPECT(data.getFieldU64() == val_u64);
            BEAST_EXPECT(data.getInnerTypeString() == "UINT64");
        }

        {
            // UINT128
            uint128 val_u128 = uint128(1);
            data.setFieldH128(val_u128);
            BEAST_EXPECT(data.getFieldH128() == val_u128);
            BEAST_EXPECT(data.getInnerTypeString() == "UINT128");
        }

        {
            // UINT160
            uint160 val_u160 = uint160(1);
            data.setFieldH160(val_u160);
            BEAST_EXPECT(data.getFieldH160() == val_u160);
            BEAST_EXPECT(data.getInnerTypeString() == "UINT160");
        }

        {
            // UINT192
            uint192 val_u192 = uint192(1);
            data.setFieldH192(val_u192);
            BEAST_EXPECT(data.getFieldH192() == val_u192);
            BEAST_EXPECT(data.getInnerTypeString() == "UINT192");
        }

        {
            // UINT256
            uint256 val_u256 = uint256(1);
            data.setFieldH256(val_u256);
            BEAST_EXPECT(data.getFieldH256() == val_u256);
            BEAST_EXPECT(data.getInnerTypeString() == "UINT256");
        }

        {
            // VL (Variable Length) - Blob
            Blob val_blob =
                strUnHex("0102030405060708090A0B0C0D0E0F10").value();
            data.setFieldVL(val_blob);
            BEAST_EXPECT(data.getFieldVL() == val_blob);
            BEAST_EXPECT(data.getInnerTypeString() == "VL");
        }

        {
            // VL (Variable Length) - Slice
            std::string test_str = "Test String for Slice";
            Slice val_slice(test_str.data(), test_str.size());
            data.setFieldVL(val_slice);
            Blob expected_blob(test_str.begin(), test_str.end());
            BEAST_EXPECT(data.getFieldVL() == expected_blob);
            BEAST_EXPECT(data.getInnerTypeString() == "VL");
        }

        {
            // ACCOUNT
            AccountID val_account(0x123456789ABCDEF0);
            data.setAccountID(val_account);
            BEAST_EXPECT(data.getAccountID() == val_account);
            BEAST_EXPECT(data.getInnerTypeString() == "ACCOUNT");
        }

        {
            // AMOUNT
            STAmount val_amount(777777);
            data.setFieldAmount(val_amount);
            BEAST_EXPECT(data.getFieldAmount() == val_amount);
            BEAST_EXPECT(data.getInnerTypeString() == "AMOUNT");
        }

        {
            // CURRENCY
        }

        {
            // ISSUE
        }

        {
            // NUMBER
        }
    }

    void
    testJsonConversion()
    {
        testcase("JSON Conversion");

        auto const& sf = sfParameterValue;

        // Test JSON serialization for each type
        {
            // UINT8
            STData data_u8(sf, static_cast<unsigned char>(8));
            Json::Value json_u8 = data_u8.getJson(JsonOptions::none);
            BEAST_EXPECT(json_u8[jss::type].asString() == "UINT8");
            BEAST_EXPECT(json_u8[jss::value].asUInt() == 8);
        }

        {
            // UINT16
            STData data_u16(sf, static_cast<std::uint16_t>(16));
            Json::Value json_u16 = data_u16.getJson(JsonOptions::none);
            BEAST_EXPECT(json_u16[jss::type].asString() == "UINT16");
            BEAST_EXPECT(json_u16[jss::value].asUInt() == 16);
        }

        {
            // UINT32
            STData data_u32(sf, static_cast<std::uint32_t>(32));
            Json::Value json_u32 = data_u32.getJson(JsonOptions::none);
            BEAST_EXPECT(json_u32[jss::type].asString() == "UINT32");
            BEAST_EXPECT(json_u32[jss::value].asUInt() == 32);
        }

        {
            // UINT64
            STData data_u64(sf, static_cast<std::uint64_t>(64));
            Json::Value json_u64 = data_u64.getJson(JsonOptions::none);
            BEAST_EXPECT(json_u64[jss::type].asString() == "UINT64");
            BEAST_EXPECT(json_u64[jss::value].asString() == "40");
        }

        {
            // UINT128
            uint128 val_u128 = uint128(1);
            STData data_u128(sf, val_u128);
            Json::Value json_u128 = data_u128.getJson(JsonOptions::none);
            BEAST_EXPECT(json_u128[jss::type].asString() == "UINT128");
            BEAST_EXPECT(
                json_u128[jss::value].asString() ==
                "00000000000000000000000000000001");
        }

        {
            // UINT160
            uint160 val_u160 = uint160(1);
            STData data_u160(sf, val_u160);
            Json::Value json_u160 = data_u160.getJson(JsonOptions::none);
            BEAST_EXPECT(json_u160[jss::type].asString() == "UINT160");
            BEAST_EXPECT(
                json_u160[jss::value].asString() ==
                "0000000000000000000000000000000000000001");
        }

        {
            // UINT192
            uint192 val_u192 = uint192(1);
            STData data_u192(sf, val_u192);
            Json::Value json_u192 = data_u192.getJson(JsonOptions::none);
            BEAST_EXPECT(json_u192[jss::type].asString() == "UINT192");
            BEAST_EXPECT(
                json_u192[jss::value].asString() ==
                "000000000000000000000000000000000000000000000001");
        }

        {
            // UINT256
            uint256 val_u256 = uint256(1);
            STData data_u256(sf, val_u256);
            Json::Value json_u256 = data_u256.getJson(JsonOptions::none);
            BEAST_EXPECT(json_u256[jss::type].asString() == "UINT256");
            BEAST_EXPECT(
                json_u256[jss::value].asString() ==
                "00000000000000000000000000000000000000000000000000000000000000"
                "01");
        }

        {
            // VL
            Blob blob = strUnHex("DEADBEEF").value();
            STData data_vl(sf, blob);
            Json::Value json_vl = data_vl.getJson(JsonOptions::none);
            BEAST_EXPECT(json_vl[jss::type].asString() == "VL");
            BEAST_EXPECT(json_vl[jss::value].asString() == "DEADBEEF");
        }

        {
            // ACCOUNT
            AccountID account(0x123456789ABCDEF0);
            STData data_account(sf, account);
            Json::Value json_account = data_account.getJson(JsonOptions::none);
            BEAST_EXPECT(json_account[jss::type].asString() == "ACCOUNT");
            BEAST_EXPECT(
                json_account[jss::value].asString() ==
                "rrrrrrrrrrrrrLveWzSkxhcH3hGw6");
        }

        {
            // AMOUNT
            STAmount amount(1000);
            STData data_amount(sf, amount);
            Json::Value json_amount = data_amount.getJson(JsonOptions::none);
            BEAST_EXPECT(json_amount[jss::type].asString() == "AMOUNT");
            BEAST_EXPECT(json_amount[jss::value].asString() == "1000");
        }

        {
            // CURRENCY
        }

        {
            // ISSUE
        }

        {
            // NUMBER
        }
    }

    void
    testDataFromJson()
    {
        testcase("Data From JSON");

        auto const& sf = sfParameterValue;

        // Test JSON deserialization for each type
        {
            // UINT8
            Json::Value json_u8(Json::objectValue);
            json_u8[jss::type] = "UINT8";
            json_u8[jss::value] = 8;

            STData data_u8 = dataFromJson(sf, json_u8);
            BEAST_EXPECT(data_u8.getFieldU8() == 8);
            BEAST_EXPECT(data_u8.getInnerTypeString() == "UINT8");
        }

        {
            // UINT16
            Json::Value json_u16(Json::objectValue);
            json_u16[jss::type] = "UINT16";
            json_u16[jss::value] = 16;

            STData data_u16 = dataFromJson(sf, json_u16);
            BEAST_EXPECT(data_u16.getFieldU16() == 16);
            BEAST_EXPECT(data_u16.getInnerTypeString() == "UINT16");
        }

        {
            // UINT32
            Json::Value json_u32(Json::objectValue);
            json_u32[jss::type] = "UINT32";
            json_u32[jss::value] = 32;

            STData data_u32 = dataFromJson(sf, json_u32);
            BEAST_EXPECT(data_u32.getFieldU32() == 32);
            BEAST_EXPECT(data_u32.getInnerTypeString() == "UINT32");
        }

        {
            // UINT64
            Json::Value json_u64(Json::objectValue);
            json_u64[jss::type] = "UINT64";
            json_u64[jss::value] = 64;
            STData data_u64 = dataFromJson(sf, json_u64);
            BEAST_EXPECT(data_u64.getFieldU64() == 64);
            BEAST_EXPECT(data_u64.getInnerTypeString() == "UINT64");
        }

        {
            // UINT128
            Json::Value json_u128(Json::objectValue);
            json_u128[jss::type] = "UINT128";
            json_u128[jss::value] = "00000000000000000000000000000001";
            STData data_u128 = dataFromJson(sf, json_u128);
            uint128 expected;
            bool ok = expected.parseHex("00000000000000000000000000000001");
            BEAST_EXPECT(ok);
            BEAST_EXPECT(data_u128.getFieldH128() == expected);
            BEAST_EXPECT(data_u128.getInnerTypeString() == "UINT128");
        }

        {
            // UINT160
            Json::Value json_u160(Json::objectValue);
            json_u160[jss::type] = "UINT160";
            json_u160[jss::value] = "0000000000000000000000000000000000000001";
            STData data_u160 = dataFromJson(sf, json_u160);
            uint160 expected;
            bool ok =
                expected.parseHex("0000000000000000000000000000000000000001");
            BEAST_EXPECT(ok);
            BEAST_EXPECT(data_u160.getFieldH160() == expected);
            BEAST_EXPECT(data_u160.getInnerTypeString() == "UINT160");
        }

        {
            // UINT192
            Json::Value json_u192(Json::objectValue);
            json_u192[jss::type] = "UINT192";
            json_u192[jss::value] =
                "000000000000000000000000000000000000000000000001";
            STData data_u192 = dataFromJson(sf, json_u192);
            uint192 expected;
            bool ok = expected.parseHex(
                "000000000000000000000000000000000000000000000001");
            BEAST_EXPECT(ok);
            BEAST_EXPECT(data_u192.getFieldH192() == expected);
            BEAST_EXPECT(data_u192.getInnerTypeString() == "UINT192");
        }

        {
            // UINT256
            Json::Value json_u256(Json::objectValue);
            json_u256[jss::type] = "UINT256";
            json_u256[jss::value] =
                "00000000000000000000000000000000000000000000000000000000000000"
                "01";
            STData data_u256 = dataFromJson(sf, json_u256);
            uint256 expected;
            bool ok = expected.parseHex(
                "00000000000000000000000000000000000000000000000000000000000000"
                "01");
            BEAST_EXPECT(ok);
            BEAST_EXPECT(data_u256.getFieldH256() == expected);
            BEAST_EXPECT(data_u256.getInnerTypeString() == "UINT256");
        }

        {
            // VL
            Json::Value json_vl(Json::objectValue);
            json_vl[jss::type] = "VL";
            json_vl[jss::value] = "DEADBEEFCAFEBABE";

            STData data_vl = dataFromJson(sf, json_vl);
            Blob expected_blob = strUnHex("DEADBEEFCAFEBABE").value();
            BEAST_EXPECT(data_vl.getFieldVL() == expected_blob);
            BEAST_EXPECT(data_vl.getInnerTypeString() == "VL");
        }

        {
            // ACCOUNT
            Json::Value json_account(Json::objectValue);
            json_account[jss::type] = "ACCOUNT";
            json_account[jss::value] = "rG1QQv2nh2gr7RCZ1P8YYcBUKCCN633jCn";

            STData data_account = dataFromJson(sf, json_account);
            AccountID expected_account =
                parseBase58<AccountID>("rG1QQv2nh2gr7RCZ1P8YYcBUKCCN633jCn")
                    .value();
            BEAST_EXPECT(data_account.getAccountID() == expected_account);
            BEAST_EXPECT(data_account.getInnerTypeString() == "ACCOUNT");
        }

        {
            // AMOUNT
            Json::Value json_amount(Json::objectValue);
            json_amount[jss::type] = "AMOUNT";
            json_amount[jss::value] = "1000";

            STData data_amount = dataFromJson(sf, json_amount);
            STAmount expected_amount(1000);
            BEAST_EXPECT(data_amount.getFieldAmount() == expected_amount);
            BEAST_EXPECT(data_amount.getInnerTypeString() == "AMOUNT");
        }

        {
            // CURRENCY
        }

        {
            // ISSUE
        }

        {
            // NUMBER
        }
    }

    // void
    // testErrorCases()
    // {
    //     testcase("Error Cases");

    //     auto const& sf = sfParameterValue;

    //     // Test JSON parsing errors
    //     {
    //         // Missing type
    //         Json::Value json_no_type(Json::objectValue);
    //         json_no_type[jss::value] = 123;

    //         try {
    //             STData data = dataFromJson(sf, json_no_type);
    //             fail("Expected exception for missing type");
    //         } catch (std::runtime_error const& e) {
    //             pass();
    //         }
    //     }

    //     {
    //         // Missing value
    //         Json::Value json_no_value(Json::objectValue);
    //         json_no_value[jss::type] = "UINT8";

    //         try {
    //             STData data = dataFromJson(sf, json_no_value);
    //             fail("Expected exception for missing value");
    //         } catch (std::runtime_error const& e) {
    //             pass();
    //         }
    //     }

    //     {
    //         // Invalid type string
    //         Json::Value json_invalid_type(Json::objectValue);
    //         json_invalid_type[jss::type] = "INVALID_TYPE";
    //         json_invalid_type[jss::value] = 123;

    //         try {
    //             STData data = dataFromJson(sf, json_invalid_type);
    //             fail("Expected exception for invalid type");
    //         } catch (std::runtime_error const& e) {
    //             pass();
    //         }
    //     }

    //     {
    //         // Invalid UINT256 hex
    //         Json::Value json_invalid_hex(Json::objectValue);
    //         json_invalid_hex[jss::type] = "UINT256";
    //         json_invalid_hex[jss::value] = "INVALID_HEX_STRING";

    //         try {
    //             STData data = dataFromJson(sf, json_invalid_hex);
    //             fail("Expected exception for invalid hex");
    //         } catch (std::runtime_error const& e) {
    //             pass();
    //         }
    //     }

    //     {
    //         // Invalid VL hex
    //         Json::Value json_invalid_vl(Json::objectValue);
    //         json_invalid_vl[jss::type] = "VL";
    //         json_invalid_vl[jss::value] = "INVALID_HEX";

    //         try {
    //             STData data = dataFromJson(sf, json_invalid_vl);
    //             fail("Expected exception for invalid VL data");
    //         } catch (std::invalid_argument const& e) {
    //             pass();
    //         }
    //     }

    //     {
    //         // Invalid account
    //         Json::Value json_invalid_account(Json::objectValue);
    //         json_invalid_account[jss::type] = "ACCOUNT";
    //         json_invalid_account[jss::value] = "INVALID_ACCOUNT_STRING";

    //         try {
    //             STData data = dataFromJson(sf, json_invalid_account);
    //             fail("Expected exception for invalid account");
    //         } catch (std::runtime_error const& e) {
    //             pass();
    //         }
    //     }

    //     {
    //         // Non-object JSON
    //         Json::Value json_not_object = "not an object";

    //         try {
    //             STData data = dataFromJson(sf, json_not_object);
    //             fail("Expected exception for non-object JSON");
    //         } catch (std::runtime_error const& e) {
    //             pass();
    //         }
    //     }
    // }

    // void
    // testEquivalence()
    // {
    //     testcase("Equivalence");

    //     auto const& sf = sfParameterValue;

    //     // Test equivalence for same types and values
    //     {
    //         STData data1(sf, static_cast<unsigned char>(42));
    //         STData data2(sf, static_cast<unsigned char>(42));
    //         BEAST_EXPECT(data1.isEquivalent(data2));
    //     }

    //     // Test non-equivalence for different values
    //     {
    //         STData data1(sf, static_cast<unsigned char>(42));
    //         STData data2(sf, static_cast<unsigned char>(43));
    //         BEAST_EXPECT(!data1.isEquivalent(data2));
    //     }

    //     // Test non-equivalence for different types
    //     {
    //         STData data1(sf, static_cast<unsigned char>(42));
    //         STData data2(sf, static_cast<std::uint16_t>(42));
    //         BEAST_EXPECT(!data1.isEquivalent(data2));
    //     }

    //     // Test equivalence with complex types
    //     {
    //         uint256 val;
    //         val.parseHex("123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0");
    //         STData data1(sf, val);
    //         STData data2(sf, val);
    //         BEAST_EXPECT(data1.isEquivalent(data2));
    //     }
    // }

    // void
    // testSize()
    // {
    //     testcase("Size Calculation");

    //     auto const& sf = sfParameterValue;

    //     // Test size calculation for each type
    //     {
    //         STData data_u8(sf, static_cast<unsigned char>(42));
    //         BEAST_EXPECT(data_u8.size() == sizeof(uint8_t));
    //     }

    //     {
    //         STData data_u16(sf, static_cast<std::uint16_t>(1234));
    //         BEAST_EXPECT(data_u16.size() == sizeof(uint16_t));
    //     }

    //     {
    //         STData data_u32(sf, static_cast<std::uint32_t>(123456));
    //         BEAST_EXPECT(data_u32.size() == sizeof(uint32_t));
    //     }

    //     {
    //         STData data_u64(sf, static_cast<std::uint64_t>(123456789));
    //         BEAST_EXPECT(data_u64.size() == sizeof(uint64_t));
    //     }

    //     {
    //         uint128 val_u128(0x12345678, 0x9ABCDEF0);
    //         STData data_u128(sf, val_u128);
    //         BEAST_EXPECT(data_u128.size() == uint128::size());
    //     }

    //     {
    //         uint256 val_u256;
    //         val_u256.parseHex("123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0");
    //         STData data_u256(sf, val_u256);
    //         BEAST_EXPECT(data_u256.size() == uint256::size());
    //     }

    //     {
    //         Blob blob = strUnHex("DEADBEEFCAFEBABE").value();
    //         STData data_vl(sf, blob);
    //         BEAST_EXPECT(data_vl.size() == blob.size());
    //     }

    //     {
    //         AccountID account(0x123456789ABCDEF0);
    //         STData data_account(sf, account);
    //         BEAST_EXPECT(data_account.size() == uint160::size());
    //     }

    //     {
    //         // Native amount
    //         STAmount amount_native(1000);
    //         STData data_amount_native(sf, amount_native);
    //         BEAST_EXPECT(data_amount_native.size() == 8); // Native amounts
    //         are 8 bytes
    //     }

    //     {
    //         // IOU amount
    //         IOUAmount iou_amount(5000);
    //         Issue const usd(
    //             Currency(0x5553440000000000),
    //             parseBase58<AccountID>("rG1QQv2nh2gr7RCZ1P8YYcBUKCCN633jCn").value());
    //         STAmount amount_iou(iou_amount, usd);
    //         STData data_amount_iou(sf, amount_iou);
    //         BEAST_EXPECT(data_amount_iou.size() == 48); // IOU amounts are 48
    //         bytes
    //     }
    // }

    // void
    // testTextRepresentation()
    // {
    //     testcase("Text Representation");

    //     auto const& sf = sfParameterValue;

    //     // Test getText() for various types
    //     {
    //         STData data_u8(sf, static_cast<unsigned char>(42));
    //         std::string text = data_u8.getText();
    //         BEAST_EXPECT(text.find("STData") != std::string::npos);
    //         BEAST_EXPECT(text.find("UINT8") != std::string::npos);
    //     }

    //     {
    //         uint256 val;
    //         val.parseHex("123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0");
    //         STData data_u256(sf, val);
    //         std::string text = data_u256.getText();
    //         BEAST_EXPECT(text.find("STData") != std::string::npos);
    //         BEAST_EXPECT(text.find("UINT256") != std::string::npos);
    //     }

    //     {
    //         Blob blob = strUnHex("DEADBEEF").value();
    //         STData data_vl(sf, blob);
    //         std::string text = data_vl.getText();
    //         BEAST_EXPECT(text.find("STData") != std::string::npos);
    //         BEAST_EXPECT(text.find("VL") != std::string::npos);
    //     }
    // }

    // void
    // testCopyAndMove()
    // {
    //     testcase("Copy and Move Operations");

    //     auto const& sf = sfParameterValue;

    //     // Test copy functionality
    //     {
    //         uint256 val;
    //         val.parseHex("123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0");
    //         STData original(sf, val);

    //         // Test copy
    //         char buffer[1024];
    //         STBase* copied = original.copy(sizeof(buffer), buffer);
    //         BEAST_EXPECT(copied != nullptr);

    //         STData* copied_data = dynamic_cast<STData*>(copied);
    //         BEAST_EXPECT(copied_data != nullptr);
    //         BEAST_EXPECT(copied_data->getFieldH256() == val);
    //         BEAST_EXPECT(copied_data->getInnerTypeString() == "UINT256");
    //     }

    //     // Test move functionality
    //     {
    //         Blob blob = strUnHex("DEADBEEFCAFEBABE").value();
    //         STData original(sf, blob);

    //         char buffer[1024];
    //         STBase* moved = original.move(sizeof(buffer), buffer);
    //         BEAST_EXPECT(moved != nullptr);

    //         STData* moved_data = dynamic_cast<STData*>(moved);
    //         BEAST_EXPECT(moved_data != nullptr);
    //         BEAST_EXPECT(moved_data->getFieldVL() == blob);
    //         BEAST_EXPECT(moved_data->getInnerTypeString() == "VL");
    //     }
    // }

    // void
    // testBoundaryValues()
    // {
    //     testcase("Boundary Values");

    //     auto const& sf = sfParameterValue;

    //     // Test minimum and maximum values for each numeric type
    //     {
    //         // UINT8 boundaries
    //         STData data_u8_min(sf, static_cast<unsigned char>(0));
    //         BEAST_EXPECT(data_u8_min.getFieldU8() == 0);

    //         STData data_u8_max(sf, static_cast<unsigned char>(255));
    //         BEAST_EXPECT(data_u8_max.getFieldU8() == 255);
    //     }

    //     {
    //         // UINT16 boundaries
    //         STData data_u16_min(sf, static_cast<std::uint16_t>(0));
    //         BEAST_EXPECT(data_u16_min.getFieldU16() == 0);

    //         STData data_u16_max(sf, static_cast<std::uint16_t>(65535));
    //         BEAST_EXPECT(data_u16_max.getFieldU16() == 65535);
    //     }

    //     {
    //         // UINT32 boundaries
    //         STData data_u32_min(sf, static_cast<std::uint32_t>(0));
    //         BEAST_EXPECT(data_u32_min.getFieldU32() == 0);

    //         STData data_u32_max(sf, static_cast<std::uint32_t>(0xFFFFFFFF));
    //         BEAST_EXPECT(data_u32_max.getFieldU32() == 0xFFFFFFFF);
    //     }

    //     {
    //         // UINT64 boundaries
    //         STData data_u64_min(sf, static_cast<std::uint64_t>(0));
    //         BEAST_EXPECT(data_u64_min.getFieldU64() == 0);

    //         STData data_u64_max(sf,
    //         static_cast<std::uint64_t>(0xFFFFFFFFFFFFFFFF));
    //         BEAST_EXPECT(data_u64_max.getFieldU64() == 0xFFFFFFFFFFFFFFFF);
    //     }

    //     {
    //         // Empty blob
    //         Blob empty_blob;
    //         STData data_empty_vl(sf, empty_blob);
    //         BEAST_EXPECT(data_empty_vl.getFieldVL() == empty_blob);
    //         BEAST_EXPECT(data_empty_vl.size() == 0);
    //     }

    //     {
    //         // Large blob (test with reasonably sized data)
    //         Blob large_blob(1000, 0xAB); // 1000 bytes of 0xAB
    //         STData data_large_vl(sf, large_blob);
    //         BEAST_EXPECT(data_large_vl.getFieldVL() == large_blob);
    //         BEAST_EXPECT(data_large_vl.size() == 1000);
    //     }
    // }

    // void
    // testSpecialSerializationCases()
    // {
    //     testcase("Special Serialization Cases");

    //     auto const& sf = sfParameterValue;

    //     // Test serialization format compliance
    //     {
    //         // Verify type prefix is included in serialization
    //         STData data_u8(sf, static_cast<unsigned char>(0x42));
    //         Serializer s;
    //         data_u8.add(s);

    //         // Should start with type identifier (STI_UINT8 = 0x0010)
    //         auto slice = s.slice();
    //         BEAST_EXPECT(slice.size() >= 2);
    //         std::uint16_t type_id = (static_cast<std::uint16_t>(slice[0]) <<
    //         8) | slice[1]; BEAST_EXPECT(type_id == STI_UINT8);
    //     }

    //     {
    //         // Test VL serialization includes length
    //         Blob test_blob = strUnHex("DEADBEEF").value();
    //         STData data_vl(sf, test_blob);
    //         Serializer s;
    //         data_vl.add(s);

    //         auto slice = s.slice();
    //         BEAST_EXPECT(slice.size() >= 2);
    //         std::uint16_t type_id = (static_cast<std::uint16_t>(slice[0]) <<
    //         8) | slice[1]; BEAST_EXPECT(type_id == STI_VL);
    //     }
    // }

    // void
    // testTypeConsistency()
    // {
    //     testcase("Type Consistency");

    //     auto const& sf = sfParameterValue;

    //     // Verify that changing types properly updates internal state
    //     {
    //         STData data(sf);

    //         // Start with UINT8
    //         data.setFieldU8(42);
    //         BEAST_EXPECT(data.getInnerTypeString() == "UINT8");
    //         BEAST_EXPECT(data.getFieldU8() == 42);

    //         // Change to UINT16
    //         data.setFieldU16(1234);
    //         BEAST_EXPECT(data.getInnerTypeString() == "UINT16");
    //         BEAST_EXPECT(data.getFieldU16() == 1234);

    //         // Change to VL
    //         Blob blob = strUnHex("DEADBEEF").value();
    //         data.setFieldVL(blob);
    //         BEAST_EXPECT(data.getInnerTypeString() == "VL");
    //         BEAST_EXPECT(data.getFieldVL() == blob);

    //         // Change to UINT256
    //         uint256 val;
    //         val.parseHex("123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0");
    //         data.setFieldH256(val);
    //         BEAST_EXPECT(data.getInnerTypeString() == "UINT256");
    //         BEAST_EXPECT(data.getFieldH256() == val);
    //     }
    // }

    // void
    // testComplexAmountTypes()
    // {
    //     testcase("Complex Amount Types");

    //     auto const& sf = sfParameterValue;

    //     // Test various STAmount configurations
    //     {
    //         // Zero native amount
    //         STAmount zero_native(0);
    //         STData data_zero(sf, zero_native);
    //         BEAST_EXPECT(data_zero.getFieldAmount() == zero_native);
    //         BEAST_EXPECT(data_zero.size() == 8); // Native amounts are 8
    //         bytes
    //     }

    //     {
    //         // Maximum native amount
    //         STAmount max_native(100000000000000000ULL); // Max XRP in drops
    //         STData data_max(sf, max_native);
    //         BEAST_EXPECT(data_max.getFieldAmount() == max_native);
    //     }

    //     {
    //         // IOU with zero value
    //         IOUAmount zero_iou(0);
    //         Issue const eur(
    //             Currency(0x4555520000000000),
    //             parseBase58<AccountID>("rG1QQv2nh2gr7RCZ1P8YYcBUKCCN633jCn").value());
    //         STAmount zero_iou_amount(zero_iou, eur);
    //         STData data_zero_iou(sf, zero_iou_amount);
    //         BEAST_EXPECT(data_zero_iou.getFieldAmount() == zero_iou_amount);
    //         BEAST_EXPECT(data_zero_iou.size() == 48); // IOU amounts are 48
    //         bytes
    //     }
    // }

    // void
    // testJsonRoundTrip()
    // {
    //     testcase("JSON Round Trip");

    //     auto const& sf = sfParameterValue;

    //     // Test complete round trip: STData -> JSON -> STData
    //     {
    //         // UINT8
    //         STData original_u8(sf, static_cast<unsigned char>(123));
    //         Json::Value json_u8 = original_u8.getJson(JsonOptions::none);
    //         STData restored_u8 = dataFromJson(sf, json_u8);
    //         BEAST_EXPECT(original_u8.isEquivalent(restored_u8));
    //     }

    //     {
    //         // UINT256
    //         uint256 val;
    //         val.parseHex("FEDCBA9876543210FEDCBA9876543210FEDCBA9876543210FEDCBA9876543210");
    //         STData original_u256(sf, val);
    //         Json::Value json_u256 = original_u256.getJson(JsonOptions::none);
    //         STData restored_u256 = dataFromJson(sf, json_u256);
    //         BEAST_EXPECT(original_u256.isEquivalent(restored_u256));
    //     }

    //     {
    //         // VL
    //         Blob blob = strUnHex("0123456789ABCDEF").value();
    //         STData original_vl(sf, blob);
    //         Json::Value json_vl = original_vl.getJson(JsonOptions::none);
    //         STData restored_vl = dataFromJson(sf, json_vl);
    //         BEAST_EXPECT(original_vl.isEquivalent(restored_vl));
    //     }

    //     {
    //         // ACCOUNT
    //         AccountID account_id =
    //         parseBase58<AccountID>("rG1QQv2nh2gr7RCZ1P8YYcBUKCCN633jCn").value();
    //         STData original_account(sf, account_id);
    //         Json::Value json_account =
    //         original_account.getJson(JsonOptions::none); STData
    //         restored_account = dataFromJson(sf, json_account);
    //         BEAST_EXPECT(original_account.isEquivalent(restored_account));
    //     }
    // }

    // void
    // testSerializationRoundTrip()
    // {
    //     testcase("Serialization Round Trip");

    //     auto const& sf = sfParameterValue;

    //     // Test complete serialization round trip for all types
    //     std::vector<STData> test_data;

    //     // Populate test data with various types
    //     test_data.emplace_back(sf, static_cast<unsigned char>(0xFF));
    //     test_data.emplace_back(sf, static_cast<std::uint16_t>(0xFFFF));
    //     test_data.emplace_back(sf, static_cast<std::uint32_t>(0xFFFFFFFF));
    //     test_data.emplace_back(sf,
    //     static_cast<std::uint64_t>(0xFFFFFFFFFFFFFFFF));

    //     uint128 val128(0xFFFFFFFF, 0xFFFFFFFF);
    //     test_data.emplace_back(sf, val128);

    //     uint256 val256;
    //     val256.parseHex("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF");
    //     test_data.emplace_back(sf, val256);

    //     Blob blob = strUnHex("DEADBEEFCAFEBABE1234567890ABCDEF").value();
    //     test_data.emplace_back(sf, blob);

    //     AccountID account(0xFFFFFFFFFFFFFFFF);
    //     test_data.emplace_back(sf, account);

    //     STAmount amount(999999);
    //     test_data.emplace_back(sf, amount);

    //     // Test round trip for each
    //     for (auto const& original : test_data)
    //     {
    //         Serializer s;
    //         original.add(s);

    //         SerialIter sit(s.slice());
    //         STData deserialized(sit, sf);

    //         BEAST_EXPECT(original.isEquivalent(deserialized));
    //         BEAST_EXPECT(original.getInnerTypeString() ==
    //         deserialized.getInnerTypeString());
    //     }
    // }

    // void
    // testMakeFieldPresent()
    // {
    //     testcase("Make Field Present");

    //     auto const& sf = sfParameterValue;

    //     // Test makeFieldPresent functionality
    //     {
    //         STData data(sf);
    //         STBase* field = data.makeFieldPresent();
    //         BEAST_EXPECT(field != nullptr);

    //         // Field should now be present (not STI_NOTPRESENT)
    //         BEAST_EXPECT(field->getSType() != STI_NOTPRESENT);
    //     }
    // }

    void
    run() override
    {
        testConstructors();
        testSerializationDeserialization();
        testSettersAndGetters();
        testJsonConversion();
        testDataFromJson();
        // testErrorCases();
        // testEquivalence();
        // testSize();
        // testTextRepresentation();
        // testCopyAndMove();
        // testBoundaryValues();
        // testSpecialSerializationCases();
        // testTypeConsistency();
        // testComplexAmountTypes();
        // testJsonRoundTrip();
        // testSerializationRoundTrip();
        // testMakeFieldPresent();
    }
};

BEAST_DEFINE_TESTSUITE(STData, protocol, ripple);

}  // namespace ripple
