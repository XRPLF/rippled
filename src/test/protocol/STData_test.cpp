#include <xrpl/basics/Blob.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/StringUtilities.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/IOUAmount.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STData.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/jss.h>

#include <cstdint>

namespace xrpl {

struct STData_test : public beast::unit_test::Suite
{
    void
    testConstructors()
    {
        testcase("Constructors");

        auto const& sf = sfParameterValue;

        // Default constructor
        {
            STData const data(sf);
            BEAST_EXPECT(data.getSType() == STI_DATA);
            BEAST_EXPECT(data.isDefault());
        }

        // Type-specific constructors
        {
            // UINT8
            STData const dataU8(sf, static_cast<std::uint8_t>(8));
            BEAST_EXPECT(dataU8.getFieldU8() == 8);
            BEAST_EXPECT(dataU8.getInnerTypeString() == "UINT8");
            BEAST_EXPECT(dataU8.isDefault());

            // UINT16
            STData const dataU16(sf, static_cast<std::uint16_t>(16));
            BEAST_EXPECT(dataU16.getFieldU16() == 16);
            BEAST_EXPECT(dataU16.getInnerTypeString() == "UINT16");

            // UINT32
            STData const dataU32(sf, static_cast<std::uint32_t>(32));
            BEAST_EXPECT(dataU32.getFieldU32() == 32);
            BEAST_EXPECT(dataU32.getInnerTypeString() == "UINT32");

            // UINT64
            STData const dataU64(sf, static_cast<std::uint64_t>(64));
            BEAST_EXPECT(dataU64.getFieldU64() == 64);
            BEAST_EXPECT(dataU64.getInnerTypeString() == "UINT64");

            // UINT128
            uint128 const val128 = uint128(1);
            STData const dataU128(sf, val128);
            BEAST_EXPECT(dataU128.getFieldH128() == val128);
            BEAST_EXPECT(dataU128.getInnerTypeString() == "UINT128");

            // UINT160
            uint160 const val160 = uint160(1);
            STData const dataU160(sf, val160);
            BEAST_EXPECT(dataU160.getFieldH160() == val160);
            BEAST_EXPECT(dataU160.getInnerTypeString() == "UINT160");

            // UINT192
            uint192 const val192 = uint192(1);
            STData const dataU192(sf, val192);
            BEAST_EXPECT(dataU192.getFieldH192() == val192);
            BEAST_EXPECT(dataU192.getInnerTypeString() == "UINT192");

            // UINT256
            uint256 const val256 = uint256(1);
            STData const dataU256(sf, val256);
            BEAST_EXPECT(dataU256.getFieldH256() == val256);
            BEAST_EXPECT(dataU256.getInnerTypeString() == "UINT256");

            // Blob
            Blob const blob = strUnHex("DEADBEEFCAFEBABE").value_or(Blob{});
            STData const dataBlob(sf, blob);
            BEAST_EXPECT(dataBlob.getFieldVL() == blob);
            BEAST_EXPECT(dataBlob.getInnerTypeString() == "VL");

            // Slice
            std::string testStr = "Hello World";
            Slice const slice(testStr.data(), testStr.size());
            STData const dataSlice(sf, slice);
            Blob const expectedBlob(testStr.begin(), testStr.end());
            BEAST_EXPECT(dataSlice.getFieldVL() == expectedBlob);

            // AccountID
            AccountID const account(0x123456789ABCDEF0);
            STData const dataAccount(sf, account);
            BEAST_EXPECT(dataAccount.getAccountID() == account);
            BEAST_EXPECT(dataAccount.getInnerTypeString() == "ACCOUNT");

            // STAmount (Native)
            STAmount const amountNative(1000);
            STData const dataAmountNative(sf, amountNative);
            BEAST_EXPECT(dataAmountNative.getFieldAmount() == amountNative);
            BEAST_EXPECT(dataAmountNative.getInnerTypeString() == "AMOUNT");

            // STAmount (IOU)
            IOUAmount const iouAmount(5000);
            Issue const usd(
                Currency(0x5553440000000000),
                parseBase58<AccountID>("rG1QQv2nh2gr7RCZ1P8YYcBUKCCN633jCn").value_or(AccountID{}));
            STAmount const amountIou(iouAmount, usd);
            STData const dataAmountIou(sf, amountIou);
            BEAST_EXPECT(dataAmountIou.getFieldAmount() == amountIou);
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
            std::uint8_t const originalU8 = 8;
            STData const dataU8(sf, originalU8);

            Serializer s;
            dataU8.add(s);

            SerialIter sit(s.slice());
            STData const deserializedU8(sit, sf);

            BEAST_EXPECT(deserializedU8.getFieldU8() == originalU8);
            BEAST_EXPECT(deserializedU8.getInnerTypeString() == "UINT8");
        }

        {
            // UINT16
            std::uint16_t const originalU16 = 16;
            STData const dataU16(sf, originalU16);

            Serializer s;
            dataU16.add(s);

            SerialIter sit(s.slice());
            STData const deserializedU16(sit, sf);

            BEAST_EXPECT(deserializedU16.getFieldU16() == originalU16);
            BEAST_EXPECT(deserializedU16.getInnerTypeString() == "UINT16");
        }

        {
            // UINT32
            std::uint32_t const originalU32 = 32;
            STData const dataU32(sf, originalU32);

            Serializer s;
            dataU32.add(s);

            SerialIter sit(s.slice());
            STData const deserializedU32(sit, sf);

            BEAST_EXPECT(deserializedU32.getFieldU32() == originalU32);
            BEAST_EXPECT(deserializedU32.getInnerTypeString() == "UINT32");
        }

        {
            // UINT64
            std::uint64_t const originalU64 = 64;
            STData const dataU64(sf, originalU64);

            Serializer s;
            dataU64.add(s);

            SerialIter sit(s.slice());
            STData const deserializedU64(sit, sf);

            BEAST_EXPECT(deserializedU64.getFieldU64() == originalU64);
            BEAST_EXPECT(deserializedU64.getInnerTypeString() == "UINT64");
        }

        {
            // UINT128
            uint128 const originalU128 = uint128(1);
            STData const dataU128(sf, originalU128);

            Serializer s;
            dataU128.add(s);

            SerialIter sit(s.slice());
            STData const deserializedU128(sit, sf);

            BEAST_EXPECT(deserializedU128.getFieldH128() == originalU128);
            BEAST_EXPECT(deserializedU128.getInnerTypeString() == "UINT128");
        }

        {
            // UINT160
            uint160 const originalU160 = uint160(1);
            STData const dataU160(sf, originalU160);

            Serializer s;
            dataU160.add(s);

            SerialIter sit(s.slice());
            STData const deserializedU160(sit, sf);

            BEAST_EXPECT(deserializedU160.getFieldH160() == originalU160);
            BEAST_EXPECT(deserializedU160.getInnerTypeString() == "UINT160");
        }

        {
            // UINT192
            uint192 const originalU192 = uint192(1);
            STData const dataU192(sf, originalU192);

            Serializer s;
            dataU192.add(s);

            SerialIter sit(s.slice());
            STData const deserializedU192(sit, sf);

            BEAST_EXPECT(deserializedU192.getFieldH192() == originalU192);
            BEAST_EXPECT(deserializedU192.getInnerTypeString() == "UINT192");
        }

        {
            // UINT256
            uint256 const originalU256 = uint256(1);
            STData const dataU256(sf, originalU256);

            Serializer s;
            dataU256.add(s);

            SerialIter sit(s.slice());
            STData const deserializedU256(sit, sf);

            BEAST_EXPECT(deserializedU256.getFieldH256() == originalU256);
            BEAST_EXPECT(deserializedU256.getInnerTypeString() == "UINT256");
        }

        {
            // VL (Variable Length)
            Blob const originalBlob = strUnHex("DEADBEEFCAFEBABE1234567890ABCDEF").value_or(Blob{});
            STData const dataVl(sf, originalBlob);

            Serializer s;
            dataVl.add(s);

            SerialIter sit(s.slice());
            STData const deserializedVl(sit, sf);

            BEAST_EXPECT(deserializedVl.getFieldVL() == originalBlob);
            BEAST_EXPECT(deserializedVl.getInnerTypeString() == "VL");
        }

        {
            // ACCOUNT
            AccountID const originalAccount(0xFEDCBA9876543210);
            STData const dataAccount(sf, originalAccount);

            Serializer s;
            dataAccount.add(s);

            SerialIter sit(s.slice());
            STData const deserializedAccount(sit, sf);

            BEAST_EXPECT(deserializedAccount.getAccountID() == originalAccount);
            BEAST_EXPECT(deserializedAccount.getInnerTypeString() == "ACCOUNT");
        }

        {
            // AMOUNT (Native)
            STAmount const originalAmount(99999);
            STData const dataAmount(sf, originalAmount);

            Serializer s;
            dataAmount.add(s);

            SerialIter sit(s.slice());
            STData const deserializedAmount(sit, sf);

            BEAST_EXPECT(deserializedAmount.getFieldAmount() == originalAmount);
            BEAST_EXPECT(deserializedAmount.getInnerTypeString() == "AMOUNT");
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
            unsigned char const valU8 = 8;
            data.setFieldU8(valU8);
            BEAST_EXPECT(data.getFieldU8() == valU8);
            BEAST_EXPECT(data.getInnerTypeString() == "UINT8");
        }

        {
            // UINT16
            std::uint16_t const valU16 = 16;
            data.setFieldU16(valU16);
            BEAST_EXPECT(data.getFieldU16() == valU16);
            BEAST_EXPECT(data.getInnerTypeString() == "UINT16");
        }

        {
            // UINT32
            std::uint32_t const valU32 = 32;
            data.setFieldU32(valU32);
            BEAST_EXPECT(data.getFieldU32() == valU32);
            BEAST_EXPECT(data.getInnerTypeString() == "UINT32");
        }

        {
            // UINT64
            std::uint64_t const valU64 = 64;
            data.setFieldU64(valU64);
            BEAST_EXPECT(data.getFieldU64() == valU64);
            BEAST_EXPECT(data.getInnerTypeString() == "UINT64");
        }

        {
            // UINT128
            uint128 const valU128 = uint128(1);
            data.setFieldH128(valU128);
            BEAST_EXPECT(data.getFieldH128() == valU128);
            BEAST_EXPECT(data.getInnerTypeString() == "UINT128");
        }

        {
            // UINT160
            uint160 const valU160 = uint160(1);
            data.setFieldH160(valU160);
            BEAST_EXPECT(data.getFieldH160() == valU160);
            BEAST_EXPECT(data.getInnerTypeString() == "UINT160");
        }

        {
            // UINT192
            uint192 const valU192 = uint192(1);
            data.setFieldH192(valU192);
            BEAST_EXPECT(data.getFieldH192() == valU192);
            BEAST_EXPECT(data.getInnerTypeString() == "UINT192");
        }

        {
            // UINT256
            uint256 const valU256 = uint256(1);
            data.setFieldH256(valU256);
            BEAST_EXPECT(data.getFieldH256() == valU256);
            BEAST_EXPECT(data.getInnerTypeString() == "UINT256");
        }

        {
            // VL (Variable Length) - Blob
            Blob const valBlob = strUnHex("0102030405060708090A0B0C0D0E0F10").value_or(Blob{});
            data.setFieldVL(valBlob);
            BEAST_EXPECT(data.getFieldVL() == valBlob);
            BEAST_EXPECT(data.getInnerTypeString() == "VL");
        }

        {
            // VL (Variable Length) - Slice
            std::string testStr = "Test String for Slice";
            Slice const valSlice(testStr.data(), testStr.size());
            data.setFieldVL(valSlice);
            Blob const expectedBlob(testStr.begin(), testStr.end());
            BEAST_EXPECT(data.getFieldVL() == expectedBlob);
            BEAST_EXPECT(data.getInnerTypeString() == "VL");
        }

        {
            // ACCOUNT
            AccountID const valAccount(0x123456789ABCDEF0);
            data.setAccountID(valAccount);
            BEAST_EXPECT(data.getAccountID() == valAccount);
            BEAST_EXPECT(data.getInnerTypeString() == "ACCOUNT");
        }

        {
            // AMOUNT
            STAmount const valAmount(777777);
            data.setFieldAmount(valAmount);
            BEAST_EXPECT(data.getFieldAmount() == valAmount);
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
            STData const dataU8(sf, static_cast<unsigned char>(8));
            json::Value jsonU8 = dataU8.getJson(JsonOptions::Values::None);
            BEAST_EXPECT(jsonU8[jss::type].asString() == "UINT8");
            BEAST_EXPECT(jsonU8[jss::value].asUInt() == 8);
        }

        {
            // UINT16
            STData const dataU16(sf, static_cast<std::uint16_t>(16));
            json::Value jsonU16 = dataU16.getJson(JsonOptions::Values::None);
            BEAST_EXPECT(jsonU16[jss::type].asString() == "UINT16");
            BEAST_EXPECT(jsonU16[jss::value].asUInt() == 16);
        }

        {
            // UINT32
            STData const dataU32(sf, static_cast<std::uint32_t>(32));
            json::Value jsonU32 = dataU32.getJson(JsonOptions::Values::None);
            BEAST_EXPECT(jsonU32[jss::type].asString() == "UINT32");
            BEAST_EXPECT(jsonU32[jss::value].asUInt() == 32);
        }

        {
            // UINT64
            STData const dataU64(sf, static_cast<std::uint64_t>(64));
            json::Value jsonU64 = dataU64.getJson(JsonOptions::Values::None);
            BEAST_EXPECT(jsonU64[jss::type].asString() == "UINT64");
            BEAST_EXPECT(jsonU64[jss::value].asString() == "40");
        }

        {
            // UINT128
            uint128 const valU128 = uint128(1);
            STData const dataU128(sf, valU128);
            json::Value jsonU128 = dataU128.getJson(JsonOptions::Values::None);
            BEAST_EXPECT(jsonU128[jss::type].asString() == "UINT128");
            BEAST_EXPECT(jsonU128[jss::value].asString() == "00000000000000000000000000000001");
        }

        {
            // UINT160
            uint160 const valU160 = uint160(1);
            STData const dataU160(sf, valU160);
            json::Value jsonU160 = dataU160.getJson(JsonOptions::Values::None);
            BEAST_EXPECT(jsonU160[jss::type].asString() == "UINT160");
            BEAST_EXPECT(
                jsonU160[jss::value].asString() == "0000000000000000000000000000000000000001");
        }

        {
            // UINT192
            uint192 const valU192 = uint192(1);
            STData const dataU192(sf, valU192);
            json::Value jsonU192 = dataU192.getJson(JsonOptions::Values::None);
            BEAST_EXPECT(jsonU192[jss::type].asString() == "UINT192");
            BEAST_EXPECT(
                jsonU192[jss::value].asString() ==
                "000000000000000000000000000000000000000000000001");
        }

        {
            // UINT256
            uint256 const valU256 = uint256(1);
            STData const dataU256(sf, valU256);
            json::Value jsonU256 = dataU256.getJson(JsonOptions::Values::None);
            BEAST_EXPECT(jsonU256[jss::type].asString() == "UINT256");
            BEAST_EXPECT(
                jsonU256[jss::value].asString() ==
                "00000000000000000000000000000000000000000000000000000000000000"
                "01");
        }

        {
            // VL
            Blob const blob = strUnHex("DEADBEEF").value_or(Blob{});
            STData const dataVl(sf, blob);
            json::Value jsonVl = dataVl.getJson(JsonOptions::Values::None);
            BEAST_EXPECT(jsonVl[jss::type].asString() == "VL");
            BEAST_EXPECT(jsonVl[jss::value].asString() == "DEADBEEF");
        }

        {
            // ACCOUNT
            AccountID const account(0x123456789ABCDEF0);
            STData const dataAccount(sf, account);
            json::Value jsonAccount = dataAccount.getJson(JsonOptions::Values::None);
            BEAST_EXPECT(jsonAccount[jss::type].asString() == "ACCOUNT");
            BEAST_EXPECT(jsonAccount[jss::value].asString() == "rrrrrrrrrrrrrLveWzSkxhcH3hGw6");
        }

        {
            // AMOUNT
            STAmount const amount(1000);
            STData const dataAmount(sf, amount);
            json::Value jsonAmount = dataAmount.getJson(JsonOptions::Values::None);
            BEAST_EXPECT(jsonAmount[jss::type].asString() == "AMOUNT");
            BEAST_EXPECT(jsonAmount[jss::value].asString() == "1000");
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
            json::Value jsonU8(json::ValueType::Object);
            jsonU8[jss::type] = "UINT8";
            jsonU8[jss::value] = 8;

            STData const dataU8 = dataFromJson(sf, jsonU8);
            BEAST_EXPECT(dataU8.getFieldU8() == 8);
            BEAST_EXPECT(dataU8.getInnerTypeString() == "UINT8");
        }

        {
            // UINT16
            json::Value jsonU16(json::ValueType::Object);
            jsonU16[jss::type] = "UINT16";
            jsonU16[jss::value] = 16;

            STData const dataU16 = dataFromJson(sf, jsonU16);
            BEAST_EXPECT(dataU16.getFieldU16() == 16);
            BEAST_EXPECT(dataU16.getInnerTypeString() == "UINT16");
        }

        {
            // UINT32
            json::Value jsonU32(json::ValueType::Object);
            jsonU32[jss::type] = "UINT32";
            jsonU32[jss::value] = 32;

            STData const dataU32 = dataFromJson(sf, jsonU32);
            BEAST_EXPECT(dataU32.getFieldU32() == 32);
            BEAST_EXPECT(dataU32.getInnerTypeString() == "UINT32");
        }

        {
            // UINT64
            json::Value jsonU64(json::ValueType::Object);
            jsonU64[jss::type] = "UINT64";
            jsonU64[jss::value] = 64;
            STData const dataU64 = dataFromJson(sf, jsonU64);
            BEAST_EXPECT(dataU64.getFieldU64() == 64);
            BEAST_EXPECT(dataU64.getInnerTypeString() == "UINT64");
        }

        {
            // UINT128
            json::Value jsonU128(json::ValueType::Object);
            jsonU128[jss::type] = "UINT128";
            jsonU128[jss::value] = "00000000000000000000000000000001";
            STData const dataU128 = dataFromJson(sf, jsonU128);
            uint128 expected;
            bool const ok = expected.parseHex("00000000000000000000000000000001");
            BEAST_EXPECT(ok);
            BEAST_EXPECT(dataU128.getFieldH128() == expected);
            BEAST_EXPECT(dataU128.getInnerTypeString() == "UINT128");
        }

        {
            // UINT160
            json::Value jsonU160(json::ValueType::Object);
            jsonU160[jss::type] = "UINT160";
            jsonU160[jss::value] = "0000000000000000000000000000000000000001";
            STData const dataU160 = dataFromJson(sf, jsonU160);
            uint160 expected;
            bool const ok = expected.parseHex("0000000000000000000000000000000000000001");
            BEAST_EXPECT(ok);
            BEAST_EXPECT(dataU160.getFieldH160() == expected);
            BEAST_EXPECT(dataU160.getInnerTypeString() == "UINT160");
        }

        {
            // UINT192
            json::Value jsonU192(json::ValueType::Object);
            jsonU192[jss::type] = "UINT192";
            jsonU192[jss::value] = "000000000000000000000000000000000000000000000001";
            STData const dataU192 = dataFromJson(sf, jsonU192);
            uint192 expected;
            bool const ok = expected.parseHex("000000000000000000000000000000000000000000000001");
            BEAST_EXPECT(ok);
            BEAST_EXPECT(dataU192.getFieldH192() == expected);
            BEAST_EXPECT(dataU192.getInnerTypeString() == "UINT192");
        }

        {
            // UINT256
            json::Value jsonU256(json::ValueType::Object);
            jsonU256[jss::type] = "UINT256";
            jsonU256[jss::value] =
                "00000000000000000000000000000000000000000000000000000000000000"
                "01";
            STData const dataU256 = dataFromJson(sf, jsonU256);
            uint256 expected;
            bool const ok = expected.parseHex(
                "00000000000000000000000000000000000000000000000000000000000000"
                "01");
            BEAST_EXPECT(ok);
            BEAST_EXPECT(dataU256.getFieldH256() == expected);
            BEAST_EXPECT(dataU256.getInnerTypeString() == "UINT256");
        }

        {
            // VL
            json::Value jsonVl(json::ValueType::Object);
            jsonVl[jss::type] = "VL";
            jsonVl[jss::value] = "DEADBEEFCAFEBABE";

            STData const dataVl = dataFromJson(sf, jsonVl);
            Blob const expectedBlob = strUnHex("DEADBEEFCAFEBABE").value_or(Blob{});
            BEAST_EXPECT(dataVl.getFieldVL() == expectedBlob);
            BEAST_EXPECT(dataVl.getInnerTypeString() == "VL");
        }

        {
            // ACCOUNT
            json::Value jsonAccount(json::ValueType::Object);
            jsonAccount[jss::type] = "ACCOUNT";
            jsonAccount[jss::value] = "rG1QQv2nh2gr7RCZ1P8YYcBUKCCN633jCn";

            STData const dataAccount = dataFromJson(sf, jsonAccount);
            AccountID const expectedAccount =
                parseBase58<AccountID>("rG1QQv2nh2gr7RCZ1P8YYcBUKCCN633jCn").value_or(AccountID{});
            BEAST_EXPECT(dataAccount.getAccountID() == expectedAccount);
            BEAST_EXPECT(dataAccount.getInnerTypeString() == "ACCOUNT");
        }

        {
            // AMOUNT
            json::Value jsonAmount(json::ValueType::Object);
            jsonAmount[jss::type] = "AMOUNT";
            jsonAmount[jss::value] = "1000";

            STData const dataAmount = dataFromJson(sf, jsonAmount);
            STAmount const expectedAmount(1000);
            BEAST_EXPECT(dataAmount.getFieldAmount() == expectedAmount);
            BEAST_EXPECT(dataAmount.getInnerTypeString() == "AMOUNT");
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
    //         json::Value json_no_type(json::ValueType::Object);
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
    //         json::Value json_no_value(json::ValueType::Object);
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
    //         json::Value json_invalid_type(json::ValueType::Object);
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
    //         json::Value json_invalid_hex(json::ValueType::Object);
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
    //         json::Value json_invalid_vl(json::ValueType::Object);
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
    //         json::Value json_invalid_account(json::ValueType::Object);
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
    //         json::Value json_not_object = "not an object";

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
    //         Blob blob = strUnHex("DEADBEEFCAFEBABE").value_or(Blob{});
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
    //             parseBase58<AccountID>("rG1QQv2nh2gr7RCZ1P8YYcBUKCCN633jCn").value_or(AccountID{}));
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
    //         Blob blob = strUnHex("DEADBEEF").value_or(Blob{});
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
    //         Blob blob = strUnHex("DEADBEEFCAFEBABE").value_or(Blob{});
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
    //         Blob test_blob = strUnHex("DEADBEEF").value_or(Blob{});
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
    //         Blob blob = strUnHex("DEADBEEF").value_or(Blob{});
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
    //             parseBase58<AccountID>("rG1QQv2nh2gr7RCZ1P8YYcBUKCCN633jCn").value_or(AccountID{}));
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
    //         json::Value json_u8 = original_u8.getJson(JsonOptions::Values::None);
    //         STData restored_u8 = dataFromJson(sf, json_u8);
    //         BEAST_EXPECT(original_u8.isEquivalent(restored_u8));
    //     }

    //     {
    //         // UINT256
    //         uint256 val;
    //         val.parseHex("FEDCBA9876543210FEDCBA9876543210FEDCBA9876543210FEDCBA9876543210");
    //         STData original_u256(sf, val);
    //         json::Value json_u256 = original_u256.getJson(JsonOptions::Values::None);
    //         STData restored_u256 = dataFromJson(sf, json_u256);
    //         BEAST_EXPECT(original_u256.isEquivalent(restored_u256));
    //     }

    //     {
    //         // VL
    //         Blob blob = strUnHex("0123456789ABCDEF").value_or(Blob{});
    //         STData original_vl(sf, blob);
    //         json::Value json_vl = original_vl.getJson(JsonOptions::Values::None);
    //         STData restored_vl = dataFromJson(sf, json_vl);
    //         BEAST_EXPECT(original_vl.isEquivalent(restored_vl));
    //     }

    //     {
    //         // ACCOUNT
    //         AccountID account_id =
    //         parseBase58<AccountID>("rG1QQv2nh2gr7RCZ1P8YYcBUKCCN633jCn").value_or(AccountID{});
    //         STData original_account(sf, account_id);
    //         json::Value json_account =
    //         original_account.getJson(JsonOptions::Values::None); STData
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

    //     Blob blob = strUnHex("DEADBEEFCAFEBABE1234567890ABCDEF").value_or(Blob{});
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

BEAST_DEFINE_TESTSUITE(STData, protocol, xrpl);

}  // namespace xrpl
