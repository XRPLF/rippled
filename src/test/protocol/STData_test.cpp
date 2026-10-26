#include <xrpl/basics/Blob.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/StringUtilities.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/IOUAmount.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STCurrency.h>
#include <xrpl/protocol/STData.h>
#include <xrpl/protocol/STInteger.h>
#include <xrpl/protocol/STIssue.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol/jss.h>

#include <cstdint>
#include <exception>
#include <string>
#include <vector>

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
    }

    // One value of every type an STData holds, with each kind of amount and issue.
    static std::vector<STData>
    everyType()
    {
        auto const& sf = sfParameterValue;
        AccountID const gw{0x4321};
        Issue const usd{toCurrency("USD"), gw};
        MPTIssue const mpt{makeMptID(7, gw)};
        return {
            STData(sf, static_cast<unsigned char>(0xFE)),
            STData(sf, static_cast<std::uint16_t>(0xFEDC)),
            STData(sf, static_cast<std::uint32_t>(0xFEDCBA98)),
            STData(sf, static_cast<std::uint64_t>(0xFEDCBA9876543210)),
            STData(sf, uint128{0x1234}),
            STData(sf, uint160{0x1234}),
            STData(sf, uint192{0x1234}),
            STData(sf, uint256{0x1234}),
            STData(sf, Blob{0x01, 0x02, 0x03}),
            STData(sf, gw),
            STData(sf, STAmount(XRPAmount(1'000))),
            STData(sf, STAmount(usd, 5)),
            STData(sf, STAmount(mpt, 7)),
            STData(sf, STIssue(sfAsset, xrpIssue())),
            STData(sf, STIssue(sfAsset, usd)),
            STData(sf, STIssue(sfAsset, mpt)),
            STData(sf, STCurrency(sfBaseAsset, usd.currency)),
            STData(sf, STNumber(sfNumber, Number(12'345, -2))),
        };
    }

    static Blob
    serialized(STData const& data)
    {
        Serializer s;
        data.add(s);
        return s.peekData();
    }

    // Whether decoding `bytes` as an STData throws.
    static bool
    refused(Blob const& bytes)
    {
        try
        {
            SerialIter sit(makeSlice(bytes));
            STData const data(sit, sfParameterValue);
            return false;
        }
        catch (std::exception const&)
        {
            return true;
        }
    }

    // Whether `dataFromJson` throws for `value` given as `type`.
    static bool
    refusedJson(std::string const& type, json::Value const& value)
    {
        json::Value v(json::ValueType::Object);
        v[jss::type] = type;
        v[jss::value] = value;
        try
        {
            (void)dataFromJson(sfParameterValue, v);
            return false;
        }
        catch (std::exception const&)
        {
            return true;
        }
    }

    void
    testRoundTripEveryType()
    {
        testcase("every type round trips through bytes, JSON and an object");

        for (auto const& data : everyType())
        {
            auto const bytes = serialized(data);
            SerialIter sit(makeSlice(bytes));
            STData const decoded(sit, sfParameterValue);
            BEAST_EXPECT(sit.empty());
            BEAST_EXPECT(decoded.isEquivalent(data));
            BEAST_EXPECT(serialized(decoded) == bytes);

            auto const fromJson =
                dataFromJson(sfParameterValue, data.getJson(JsonOptions::Values::None));
            BEAST_EXPECT(fromJson.isEquivalent(data));

            BEAST_EXPECT(data.getText().contains(data.getInnerTypeString()));

            // Placing the value in an object moves it there whole.
            STObject object(sfGeneric);
            object.set(STData(data));
            BEAST_EXPECT(object.getFieldData(sfParameterValue).isEquivalent(data));
        }
    }

    void
    testMalformedBinary()
    {
        testcase("malformed bytes are refused");

        // No bytes, or half a type code.
        BEAST_EXPECT(refused({}));
        BEAST_EXPECT(refused({0x00}));

        // A type code that is not a data type, followed by bytes enough for any type.
        for (std::uint16_t const type : {
                 static_cast<std::uint16_t>(STI_NOTPRESENT),
                 static_cast<std::uint16_t>(STI_OBJECT),
                 static_cast<std::uint16_t>(STI_ARRAY),
                 static_cast<std::uint16_t>(STI_VECTOR256),
                 static_cast<std::uint16_t>(STI_PATHSET),
                 static_cast<std::uint16_t>(STI_DATA),
                 static_cast<std::uint16_t>(STI_DATATYPE),
                 static_cast<std::uint16_t>(STI_JSON),
                 std::uint16_t{0xFFFF},
             })
        {
            Blob bytes{static_cast<std::uint8_t>(type >> 8), static_cast<std::uint8_t>(type)};
            bytes.resize(bytes.size() + 64, 0x01);
            BEAST_EXPECT(refused(bytes));
        }

        // Every type with its last byte cut off.
        for (auto const& data : everyType())
        {
            auto bytes = serialized(data);
            bytes.pop_back();
            BEAST_EXPECTS(refused(bytes), data.getInnerTypeString());
        }

        // A VL whose length runs past the bytes there are.
        BEAST_EXPECT(refused({0x00, static_cast<std::uint8_t>(STI_VL), 0x05, 0xAA, 0xBB}));

        // An object holding a ParameterValue of an unknown type is refused with it.
        {
            Serializer s;
            s.addFieldID(STI_DATA, sfParameterValue.fieldValue);
            s.add16(0xFFFF);
            s.add32(0);
            SerialIter sit(s.slice());
            try
            {
                STObject const object(sit, sfGeneric);
                fail("an unknown data type was accepted");
            }
            catch (std::exception const& e)
            {
                BEAST_EXPECT(std::string(e.what()) == "STData: unknown type");
            }
        }
    }

    void
    testFieldAccess()
    {
        testcase("reading a value as the wrong type");

        auto const& sf = sfParameterValue;

        // An empty STData holds no value: reading a number throws, other reads are empty.
        STData const empty(sf);
        BEAST_EXPECT(empty.getInnerTypeString() == std::to_string(STI_NOTPRESENT));
        BEAST_EXPECT(empty.getFieldVL().empty());
        BEAST_EXPECT(empty.getFieldAmount() == STAmount{});
        BEAST_EXPECT(empty.getFieldIssue().value() == STIssue{}.value());
        BEAST_EXPECT(empty.getFieldCurrency().value() == STCurrency{}.value());
        BEAST_EXPECT(empty.getFieldNumber().value() == STNumber{}.value());
        auto const throwsWith = [&](auto const& read, std::string const& message) {
            try
            {
                read();
                return false;
            }
            catch (std::exception const& e)
            {
                return e.what() == message;
            }
        };
        BEAST_EXPECT(throwsWith([&] { (void)empty.getFieldU8(); }, "Field not present"));
        BEAST_EXPECT(throwsWith(
            [&] {
                Serializer s;
                empty.add(s);
            },
            "STData: unknown type"));

        // A value of one type cannot be read as another.
        STData const u16(sf, static_cast<std::uint16_t>(16));
        BEAST_EXPECT(throwsWith([&] { (void)u16.getFieldU8(); }, "Wrong field type"));
        BEAST_EXPECT(throwsWith([&] { (void)u16.getFieldVL(); }, "Wrong field type"));
        BEAST_EXPECT(throwsWith([&] { (void)u16.getFieldAmount(); }, "Wrong field type"));

        // An STData is not equivalent to a value of another class.
        BEAST_EXPECT(!u16.isEquivalent(STUInt16(sfSignerWeight, 16)));

        // Setting a value of another type replaces the type.
        STData data(sf);
        data.setIssue(STIssue(sfAsset, xrpIssue()));
        BEAST_EXPECT(data.getInnerTypeString() == "ISSUE");
        BEAST_EXPECT(data.getFieldIssue().value() == Asset{xrpIssue()});
        data.setCurrency(STCurrency(sfBaseAsset, toCurrency("USD")));
        BEAST_EXPECT(data.getInnerTypeString() == "CURRENCY");
        BEAST_EXPECT(data.getFieldCurrency().value() == toCurrency("USD"));
        data.setFieldNumber(STNumber(sfNumber, Number(5)));
        BEAST_EXPECT(data.getInnerTypeString() == "NUMBER");
        BEAST_EXPECT(data.getFieldNumber().value() == Number(5));
    }

    void
    testMalformedJson()
    {
        testcase("malformed JSON is refused");

        // Not an object, no type, no value, or a type STData does not hold.
        BEAST_EXPECT([] {
            try
            {
                (void)dataFromJson(sfParameterValue, json::Value("UINT8"));
                return false;
            }
            catch (std::exception const&)
            {
                return true;
            }
        }());
        {
            json::Value noType(json::ValueType::Object);
            noType[jss::value] = 1;
            json::Value noValue(json::ValueType::Object);
            noValue[jss::type] = "UINT8";
            for (auto const& v : {noType, noValue})
            {
                try
                {
                    (void)dataFromJson(sfParameterValue, v);
                    fail("an incomplete value was accepted");
                }
                catch (std::exception const&)
                {
                    pass();
                }
            }
        }
        BEAST_EXPECT(refusedJson("OBJECT", 1));
        BEAST_EXPECT(refusedJson("uint8", 1));

        // Integers out of their type's range, negative, of the wrong JSON kind, or not
        // numbers at all.
        BEAST_EXPECT(refusedJson("UINT8", 256u));
        BEAST_EXPECT(refusedJson("UINT8", 300));
        BEAST_EXPECT(refusedJson("UINT8", -1));
        BEAST_EXPECT(refusedJson("UINT8", "256"));
        BEAST_EXPECT(refusedJson("UINT8", true));
        BEAST_EXPECT(refusedJson("UINT16", 65'536u));
        BEAST_EXPECT(refusedJson("UINT16", 70'000));
        BEAST_EXPECT(refusedJson("UINT16", "x"));
        BEAST_EXPECT(refusedJson("UINT32", -1));
        BEAST_EXPECT(refusedJson("UINT32", "4294967296"));
        BEAST_EXPECT(refusedJson("UINT32", "x"));
        BEAST_EXPECT(refusedJson("UINT32", true));
        BEAST_EXPECT(refusedJson("UINT64", -1));
        BEAST_EXPECT(refusedJson("UINT64", "XYZ"));
        BEAST_EXPECT(refusedJson("UINT64", "10000000000000000"));
        BEAST_EXPECT(refusedJson("UINT64", true));
        BEAST_EXPECT(
            dataFromJson(
                sfParameterValue,
                [] {
                    json::Value v(json::ValueType::Object);
                    v[jss::type] = "UINT64";
                    v[jss::value] = 5u;
                    return v;
                }())
                .getFieldU64() == 5);

        // The largest value of each width is accepted, as a number or a string.
        BEAST_EXPECT(
            dataFromJson(
                sfParameterValue,
                [] {
                    json::Value v(json::ValueType::Object);
                    v[jss::type] = "UINT8";
                    v[jss::value] = "255";
                    return v;
                }())
                .getFieldU8() == 255);
        BEAST_EXPECT(
            dataFromJson(
                sfParameterValue,
                [] {
                    json::Value v(json::ValueType::Object);
                    v[jss::type] = "UINT16";
                    v[jss::value] = 65'535u;
                    return v;
                }())
                .getFieldU16() == 65'535);
        BEAST_EXPECT(
            dataFromJson(
                sfParameterValue,
                [] {
                    json::Value v(json::ValueType::Object);
                    v[jss::type] = "UINT32";
                    v[jss::value] = "4294967295";
                    return v;
                }())
                .getFieldU32() == 4'294'967'295U);
        BEAST_EXPECT(
            dataFromJson(
                sfParameterValue,
                [] {
                    json::Value v(json::ValueType::Object);
                    v[jss::type] = "UINT64";
                    v[jss::value] = "FFFFFFFFFFFFFFFF";
                    return v;
                }())
                .getFieldU64() == 0xFFFFFFFFFFFFFFFF);

        // Hashes must be hex strings of their width; an empty string is zero.
        for (std::string const type : {"UINT128", "UINT160", "UINT192", "UINT256"})
        {
            BEAST_EXPECT(refusedJson(type, 1));
            BEAST_EXPECT(refusedJson(type, "XYZ"));
            BEAST_EXPECT(refusedJson(type, "00"));
            BEAST_EXPECT(!refusedJson(type, ""));
        }

        BEAST_EXPECT(refusedJson("VL", 1));
        BEAST_EXPECT(refusedJson("VL", "XYZ"));
        BEAST_EXPECT(refusedJson("AMOUNT", "x"));
        BEAST_EXPECT(refusedJson("ACCOUNT", 1));
        BEAST_EXPECT(refusedJson("ACCOUNT", "rNotAnAccount"));
        BEAST_EXPECT(!refusedJson("ACCOUNT", strHex(AccountID{0x4321})));
        BEAST_EXPECT(refusedJson("ISSUE", "x"));
        BEAST_EXPECT(refusedJson("CURRENCY", 1));
        BEAST_EXPECT(refusedJson("NUMBER", 1));
        BEAST_EXPECT(refusedJson("NUMBER", "x"));
    }

    void
    run() override
    {
        testConstructors();
        testSerializationDeserialization();
        testSettersAndGetters();
        testJsonConversion();
        testDataFromJson();
        testRoundTripEveryType();
        testMalformedBinary();
        testFieldAccess();
        testMalformedJson();
    }
};

BEAST_DEFINE_TESTSUITE(STData, protocol, xrpl);

}  // namespace xrpl
