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

#include <xrpl/beast/unit_test.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/STParsedJSON.h>
#include <xrpl/protocol/STXChainBridge.h>

namespace ripple {

class STParsedJSON_test : public beast::unit_test::suite
{
    void
    testUInt8()
    {
        testcase("UInt8");
        {
            Json::Value j;
            j[sfCloseResolution] = 255;
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(obj.object.has_value());
            BEAST_EXPECT(obj.object->isFieldPresent(sfCloseResolution));
            BEAST_EXPECT(obj.object->getFieldU8(sfCloseResolution) == 255);
        }

        // test with uint value
        {
            Json::Value j;
            j[sfCloseResolution] = 255u;
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(obj.object.has_value());
            BEAST_EXPECT(obj.object->isFieldPresent(sfCloseResolution));
            BEAST_EXPECT(obj.object->getFieldU8(sfCloseResolution) == 255);
        }

        // Test with string value
        {
            Json::Value j;
            j[sfCloseResolution] = "255";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(obj.object.has_value());
            BEAST_EXPECT(obj.object->isFieldPresent(sfCloseResolution));
            BEAST_EXPECT(obj.object->getFieldU8(sfCloseResolution) == 255);
        }

        // Test min value for uint8
        {
            Json::Value j;
            j[sfCloseResolution] = 0;
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(obj.object.has_value());
            BEAST_EXPECT(obj.object->getFieldU8(sfCloseResolution) == 0);
        }

        // Test out of range value for UInt8 (negative)
        {
            Json::Value j;
            j[sfCloseResolution] = -1;
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Test out of range value for UInt8 (too large)
        {
            Json::Value j;
            j[sfCloseResolution] = 256;
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Test out of range value for uint8 (too large)
        {
            Json::Value j;
            j[sfCloseResolution] = 300u;
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Test bad_type (not a string/int/uint)
        {
            Json::Value j;
            j[sfCloseResolution] = Json::Value(Json::arrayValue);
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Test bad_type (not a string/int/uint)
        {
            Json::Value j;
            j[sfCloseResolution] = Json::Value(Json::objectValue);
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }
    }

    void
    testUInt16()
    {
        testcase("UInt16");
        // Test with int value
        {
            Json::Value j;
            j[sfLedgerEntryType] = 65535;
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(obj.object.has_value());
            BEAST_EXPECT(obj.object->isFieldPresent(sfLedgerEntryType));
            BEAST_EXPECT(obj.object->getFieldU16(sfLedgerEntryType) == 65535);
        }

        // Test with uint value
        {
            Json::Value j;
            j[sfLedgerEntryType] = 65535u;
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(obj.object.has_value());
            BEAST_EXPECT(obj.object->isFieldPresent(sfLedgerEntryType));
            BEAST_EXPECT(obj.object->getFieldU16(sfLedgerEntryType) == 65535);
        }

        // Test with string value
        {
            Json::Value j;
            j[sfLedgerEntryType] = "65535";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(obj.object.has_value());
            BEAST_EXPECT(obj.object->isFieldPresent(sfLedgerEntryType));
            BEAST_EXPECT(obj.object->getFieldU16(sfLedgerEntryType) == 65535);
        }

        // Test min value for uint16
        {
            Json::Value j;
            j[sfLedgerEntryType] = 0;
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(obj.object.has_value());
            BEAST_EXPECT(obj.object->getFieldU16(sfLedgerEntryType) == 0);
        }

        // Test out of range value for UInt16 (negative)
        {
            Json::Value j;
            j[sfLedgerEntryType] = -1;
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Test out of range value for UInt16 (too large)
        {
            Json::Value j;
            j[sfLedgerEntryType] = 65536;
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Test string value out of range
        {
            Json::Value j;
            j[sfLedgerEntryType] = "65536";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Test bad_type (not a string/int/uint)
        {
            Json::Value j;
            j[sfLedgerEntryType] = Json::Value(Json::arrayValue);
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Test bad_type (not a string/int/uint)
        {
            Json::Value j;
            j[sfLedgerEntryType] = Json::Value(Json::objectValue);
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Invalid input for other field
        {
            Json::Value j;
            j[sfTransferFee] = "Payment";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }
    }

    void
    testUInt32()
    {
        testcase("UInt32");
        {
            Json::Value j;
            j[sfNetworkID] = 4294967295u;
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(obj.object.has_value());
            BEAST_EXPECT(obj.object->isFieldPresent(sfNetworkID));
            BEAST_EXPECT(obj.object->getFieldU32(sfNetworkID) == 4294967295u);
        }

        // Test with string value
        {
            Json::Value j;
            j[sfNetworkID] = "4294967295";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(obj.object.has_value());
            BEAST_EXPECT(obj.object->isFieldPresent(sfNetworkID));
            BEAST_EXPECT(obj.object->getFieldU32(sfNetworkID) == 4294967295u);
        }

        // Test min value for uint32
        {
            Json::Value j;
            j[sfNetworkID] = 0;
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(obj.object.has_value());
            BEAST_EXPECT(obj.object->getFieldU32(sfNetworkID) == 0);
        }

        // Test out of range value for uint32 (negative)
        {
            Json::Value j;
            j[sfNetworkID] = -1;
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Test string value out of range
        {
            Json::Value j;
            j[sfNetworkID] = "4294967296";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Test bad_type (not a string/int/uint)
        {
            Json::Value j;
            j[sfNetworkID] = Json::Value(Json::arrayValue);
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Test bad_type (not a string/int/uint)
        {
            Json::Value j;
            j[sfNetworkID] = Json::Value(Json::objectValue);
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }
    }

    void
    testUInt64()
    {
        testcase("UInt64");
        {
            Json::Value j;
            j[sfIndexNext] = "ffffffffffffffff";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(obj.object.has_value());
            BEAST_EXPECT(obj.object->isFieldPresent(sfIndexNext));
            BEAST_EXPECT(
                obj.object->getFieldU64(sfIndexNext) ==
                18446744073709551615ull);
        }

        // Test min value for uint64
        {
            Json::Value j;
            j[sfIndexNext] = 0;
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(obj.object.has_value());
            BEAST_EXPECT(obj.object->getFieldU64(sfIndexNext) == 0ull);
        }

        // Test out of range value for uint64 (negative)
        {
            Json::Value j;
            j[sfIndexNext] = -1;
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // NOTE: the JSON parser doesn't support > UInt32, so those values must
        // be in hex
        // Test string value out of range
        // string is interpreted as hex
        {
            Json::Value j;
            j[sfIndexNext] = "10000000000000000";  // uint64 max + 1 (in hex)
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Test bad_type (not a string/int/uint)
        {
            Json::Value j;
            j[sfIndexNext] = Json::Value(Json::arrayValue);
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Test hex string value with 0x prefix (should fail)
        {
            Json::Value j;
            j[sfIndexNext] = "0xabcdefabcdef";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Test hex string value with invalid characters
        {
            Json::Value j;
            j[sfIndexNext] = "abcdefga";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // test arrayValue
        {
            Json::Value j;
            j[sfIndexNext] = Json::Value(Json::arrayValue);
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // test objectValue
        {
            Json::Value j;
            j[sfIndexNext] = Json::Value(Json::objectValue);
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }
    }

    void
    testUInt128()
    {
        testcase("UInt128");
        {
            Json::Value j;
            j[sfEmailHash] = "0123456789ABCDEF0123456789ABCDEF";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(obj.object.has_value());
            BEAST_EXPECT(obj.object->isFieldPresent(sfEmailHash));
            BEAST_EXPECT(obj.object->getFieldH128(sfEmailHash).size() == 16);
        }

        // Valid lowercase hex string for UInt128
        {
            Json::Value j;
            j[sfEmailHash] = "0123456789abcdef0123456789abcdef";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(obj.object.has_value());
            BEAST_EXPECT(obj.object->isFieldPresent(sfEmailHash));
            BEAST_EXPECT(obj.object->getFieldH128(sfEmailHash).size() == 16);
        }

        // Empty string for UInt128 (should be valid, all zero)
        {
            Json::Value j;
            j[sfEmailHash] = "";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(obj.object.has_value());
            BEAST_EXPECT(obj.object->isFieldPresent(sfEmailHash));
            auto const& h128 = obj.object->getFieldH128(sfEmailHash);
            BEAST_EXPECT(h128.size() == 16);
            bool allZero = std::all_of(
                h128.begin(), h128.end(), [](auto b) { return b == 0; });
            BEAST_EXPECT(allZero);
        }

        // Odd-length hex string for UInt128 (should fail)
        {
            Json::Value j;
            j[sfEmailHash] = "0123456789ABCDEF0123456789ABCDE";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Non-hex string for UInt128 (should fail)
        {
            Json::Value j;
            j[sfEmailHash] = "nothexstring";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Hex string too short for UInt128 (should fail)
        {
            Json::Value j;
            j[sfEmailHash] = "01234567";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Hex string too long for UInt128 (should fail)
        {
            Json::Value j;
            j[sfEmailHash] = "0123456789ABCDEF0123456789ABCDEF00";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Array value for UInt128 (should fail)
        {
            Json::Value j;
            j[sfEmailHash] = Json::Value(Json::arrayValue);
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Object value for UInt128 (should fail)
        {
            Json::Value j;
            j[sfEmailHash] = Json::Value(Json::objectValue);
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }
    }

    void
    testUInt160()
    {
        testcase("UInt160");
        {
            Json::Value j;
            j[sfTakerPaysCurrency] = "0123456789ABCDEF0123456789ABCDEF01234567";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(obj.object.has_value());
            BEAST_EXPECT(obj.object->isFieldPresent(sfTakerPaysCurrency));
            BEAST_EXPECT(
                obj.object->getFieldH160(sfTakerPaysCurrency).size() == 20);
        }
        // Valid lowercase hex string for UInt160
        {
            Json::Value j;
            j[sfTakerPaysCurrency] = "0123456789abcdef0123456789abcdef01234567";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(obj.object.has_value());
            BEAST_EXPECT(obj.object->isFieldPresent(sfTakerPaysCurrency));
            BEAST_EXPECT(
                obj.object->getFieldH160(sfTakerPaysCurrency).size() == 20);
        }

        // Empty string for UInt160 (should be valid, all zero)
        {
            Json::Value j;
            j[sfTakerPaysCurrency] = "";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(obj.object.has_value());
            BEAST_EXPECT(obj.object->isFieldPresent(sfTakerPaysCurrency));
            auto const& h160 = obj.object->getFieldH160(sfTakerPaysCurrency);
            BEAST_EXPECT(h160.size() == 20);
            bool allZero = std::all_of(
                h160.begin(), h160.end(), [](auto b) { return b == 0; });
            BEAST_EXPECT(allZero);
        }

        // Odd-length hex string for UInt160 (should fail)
        {
            Json::Value j;
            j[sfTakerPaysCurrency] = "0123456789ABCDEF0123456789ABCDEF0123456";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Non-hex string for UInt160 (should fail)
        {
            Json::Value j;
            j[sfTakerPaysCurrency] = "nothexstring";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Hex string too short for UInt160 (should fail)
        {
            Json::Value j;
            j[sfTakerPaysCurrency] = "01234567";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Hex string too long for UInt160 (should fail)
        {
            Json::Value j;
            j[sfTakerPaysCurrency] =
                "0123456789ABCDEF0123456789ABCDEF0123456789";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Hex string with invalid characters for UInt160 (should fail)
        {
            Json::Value j;
            j[sfTakerPaysCurrency] = "0123456789ABCDEG0123456789ABCDEF01234567";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Array value for UInt160 (should fail)
        {
            Json::Value j;
            j[sfTakerPaysCurrency] = Json::Value(Json::arrayValue);
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Object value for UInt160 (should fail)
        {
            Json::Value j;
            j[sfTakerPaysCurrency] = Json::Value(Json::objectValue);
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }
    }

    void
    testUInt192()
    {
        testcase("UInt192");
        {
            Json::Value j;
            j[sfMPTokenIssuanceID] =
                "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(obj.object.has_value());
            BEAST_EXPECT(obj.object->isFieldPresent(sfMPTokenIssuanceID));
            BEAST_EXPECT(
                obj.object->getFieldH192(sfMPTokenIssuanceID).size() == 24);
        }

        // Valid lowercase hex string for UInt192
        {
            Json::Value j;
            j[sfMPTokenIssuanceID] =
                "ffffffffffffffffffffffffffffffffffffffffffffffff";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(obj.object.has_value());
            BEAST_EXPECT(obj.object->isFieldPresent(sfMPTokenIssuanceID));
            BEAST_EXPECT(
                obj.object->getFieldH192(sfMPTokenIssuanceID).size() == 24);
        }

        // Empty string for UInt192 (should be valid, all zero)
        {
            Json::Value j;
            j[sfMPTokenIssuanceID] = "";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(obj.object.has_value());
            BEAST_EXPECT(obj.object->isFieldPresent(sfMPTokenIssuanceID));
            auto const& h192 = obj.object->getFieldH192(sfMPTokenIssuanceID);
            BEAST_EXPECT(h192.size() == 24);
            bool allZero = std::all_of(
                h192.begin(), h192.end(), [](auto b) { return b == 0; });
            BEAST_EXPECT(allZero);
        }

        // Odd-length hex string for UInt192 (should fail)
        {
            Json::Value j;
            j[sfMPTokenIssuanceID] =
                "0123456789ABCDEF0123456789ABCDEF0123456789ABCDE";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Non-hex string for UInt192 (should fail)
        {
            Json::Value j;
            j[sfMPTokenIssuanceID] = "nothexstring";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Hex string too short for UInt192 (should fail)
        {
            Json::Value j;
            j[sfMPTokenIssuanceID] = "01234567";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Hex string too long for UInt192 (should fail)
        {
            Json::Value j;
            j[sfMPTokenIssuanceID] =
                "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF00";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Array value for UInt192 (should fail)
        {
            Json::Value j;
            j[sfMPTokenIssuanceID] = Json::Value(Json::arrayValue);
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Object value for UInt192 (should fail)
        {
            Json::Value j;
            j[sfMPTokenIssuanceID] = Json::Value(Json::objectValue);
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }
    }

    void
    testUInt256()
    {
        testcase("UInt256");
        // Test with valid hex string for UInt256
        {
            Json::Value j;
            j[sfLedgerHash] =
                "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCD"
                "EF";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(obj.object.has_value());
            BEAST_EXPECT(obj.object->isFieldPresent(sfLedgerHash));
            BEAST_EXPECT(obj.object->getFieldH256(sfLedgerHash).size() == 32);
        }
        // Valid lowercase hex string for UInt256
        {
            Json::Value j;
            j[sfLedgerHash] =
                "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcd"
                "ef";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(obj.object.has_value());
            BEAST_EXPECT(obj.object->isFieldPresent(sfLedgerHash));
            BEAST_EXPECT(obj.object->getFieldH256(sfLedgerHash).size() == 32);
        }

        // Empty string for UInt256 (should be valid, all zero)
        {
            Json::Value j;
            j[sfLedgerHash] = "";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(obj.object.has_value());
            BEAST_EXPECT(obj.object->isFieldPresent(sfLedgerHash));
            auto const& h256 = obj.object->getFieldH256(sfLedgerHash);
            BEAST_EXPECT(h256.size() == 32);
            bool allZero = std::all_of(
                h256.begin(), h256.end(), [](auto b) { return b == 0; });
            BEAST_EXPECT(allZero);
        }

        // Odd-length hex string for UInt256 (should fail)
        {
            Json::Value j;
            j[sfLedgerHash] =
                "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCD"
                "E";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Non-hex string for UInt256 (should fail)
        {
            Json::Value j;
            j[sfLedgerHash] = "nothexstring";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Hex string too short for UInt256 (should fail)
        {
            Json::Value j;
            j[sfLedgerHash] = "01234567";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Hex string too long for UInt256 (should fail)
        {
            Json::Value j;
            j[sfLedgerHash] =
                "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCD"
                "EF00";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Hex string with invalid characters for UInt256 (should fail)
        {
            Json::Value j;
            j[sfLedgerHash] =
                "0123456789ABCDEG0123456789ABCDEF0123456789ABCDEF0123456789ABCD"
                "EF";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Array value for UInt256 (should fail)
        {
            Json::Value j;
            j[sfLedgerHash] = Json::Value(Json::arrayValue);
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Object value for UInt256 (should fail)
        {
            Json::Value j;
            j[sfLedgerHash] = Json::Value(Json::objectValue);
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }
    }

    void
    testBlob()
    {
        testcase("Blob");
        // Test with valid hex string for blob
        {
            Json::Value j;
            j[sfPublicKey] = "DEADBEEF";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(obj.object.has_value());
            BEAST_EXPECT(obj.object->isFieldPresent(sfPublicKey));
            auto const& blob = obj.object->getFieldVL(sfPublicKey);
            BEAST_EXPECT(blob.size() == 4);
            BEAST_EXPECT(blob[0] == 0xDE);
            BEAST_EXPECT(blob[1] == 0xAD);
            BEAST_EXPECT(blob[2] == 0xBE);
            BEAST_EXPECT(blob[3] == 0xEF);
        }

        // Test empty string for blob (should be valid, size 0)
        {
            Json::Value j;
            j[sfPublicKey] = "";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(obj.object.has_value());
            BEAST_EXPECT(obj.object->isFieldPresent(sfPublicKey));
            auto const& blob = obj.object->getFieldVL(sfPublicKey);
            BEAST_EXPECT(blob.size() == 0);
        }

        // Test lowercase hex string for blob
        {
            Json::Value j;
            j[sfPublicKey] = "deadbeef";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(obj.object.has_value());
            BEAST_EXPECT(obj.object->isFieldPresent(sfPublicKey));
            auto const& blob = obj.object->getFieldVL(sfPublicKey);
            BEAST_EXPECT(blob.size() == 4);
            BEAST_EXPECT(blob[0] == 0xDE);
            BEAST_EXPECT(blob[1] == 0xAD);
            BEAST_EXPECT(blob[2] == 0xBE);
            BEAST_EXPECT(blob[3] == 0xEF);
        }

        // Test non-hex string for blob (should fail)
        {
            Json::Value j;
            j[sfPublicKey] = "XYZ123";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Test array value for blob (should fail)
        {
            Json::Value j;
            j[sfPublicKey] = Json::Value(Json::arrayValue);
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Test object value for blob (should fail)
        {
            Json::Value j;
            j[sfPublicKey] = Json::Value(Json::objectValue);
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }
    }

    void
    testVector256()
    {
        testcase("Vector256");
        // Test with valid array of hex strings for Vector256
        {
            Json::Value j;
            Json::Value arr(Json::arrayValue);
            arr.append(
                "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCD"
                "EF");
            arr.append(
                "FEDCBA9876543210FEDCBA9876543210FEDCBA9876543210FEDCBA98765432"
                "10");
            j[sfHashes] = arr;
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(obj.object.has_value());
            BEAST_EXPECT(obj.object->isFieldPresent(sfHashes));
            auto const& vec = obj.object->getFieldV256(sfHashes);
            BEAST_EXPECT(vec.size() == 2);
            BEAST_EXPECT(vec[0].size() == 32);
            BEAST_EXPECT(vec[1].size() == 32);
        }
        // Test empty array for Vector256 (should be valid, size 0)
        {
            Json::Value j;
            Json::Value arr(Json::arrayValue);
            j[sfHashes] = arr;
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(obj.object.has_value());
            BEAST_EXPECT(obj.object->isFieldPresent(sfHashes));
            auto const& vec = obj.object->getFieldV256(sfHashes);
            BEAST_EXPECT(vec.size() == 0);
        }

        // Test array with invalid hex string (should fail)
        {
            Json::Value j;
            Json::Value arr(Json::arrayValue);
            arr.append("nothexstring");
            j[sfHashes] = arr;
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Test array with string of wrong length (should fail)
        {
            Json::Value j;
            Json::Value arr(Json::arrayValue);
            arr.append("0123456789ABCDEF");  // too short for uint256
            j[sfHashes] = arr;
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Test array with non-string element (should fail)
        {
            Json::Value j;
            Json::Value arr(Json::arrayValue);
            arr.append(12345);
            j[sfHashes] = arr;
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Test non-array value for Vector256 (should fail)
        {
            Json::Value j;
            j[sfHashes] = "notanarray";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Test array with object element (should fail)
        {
            Json::Value j;
            Json::Value arr(Json::arrayValue);
            Json::Value objElem(Json::objectValue);
            objElem["foo"] = "bar";
            arr.append(objElem);
            j[sfHashes] = arr;
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }
    }

    void
    testAccount()
    {
        testcase("Account");
        // Test with valid base58 string for AccountID
        {
            Json::Value j;
            j[sfAccount] = "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(obj.object.has_value());
            BEAST_EXPECT(obj.object->isFieldPresent(sfAccount));
            auto const& acct = obj.object->getAccountID(sfAccount);
            BEAST_EXPECT(acct.size() == 20);
        }

        // Valid hex string for AccountID
        {
            Json::Value j;
            j[sfAccount] = "000102030405060708090A0B0C0D0E0F10111213";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(obj.object.has_value());
            BEAST_EXPECT(obj.object->isFieldPresent(sfAccount));
            auto const& acct = obj.object->getAccountID(sfAccount);
            BEAST_EXPECT(acct.size() == 20);
        }

        // Invalid base58 string for AccountID
        {
            Json::Value j;
            j[sfAccount] = "notAValidBase58Account";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Invalid hex string for AccountID (too short)
        {
            Json::Value j;
            j[sfAccount] = "001122334455";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Invalid hex string for AccountID (too long)
        {
            Json::Value j;
            j[sfAccount] = "000102030405060708090A0B0C0D0E0F101112131415";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Invalid hex string for AccountID (bad chars)
        {
            Json::Value j;
            j[sfAccount] = "000102030405060708090A0B0C0D0E0F1011121G";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Empty string for AccountID (should fail)
        {
            Json::Value j;
            j[sfAccount] = "";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Array value for AccountID (should fail)
        {
            Json::Value j;
            j[sfAccount] = Json::Value(Json::arrayValue);
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Object value for AccountID (should fail)
        {
            Json::Value j;
            j[sfAccount] = Json::Value(Json::objectValue);
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }
    }

    void
    testCurrency()
    {
        testcase("Currency");
        // Test with valid ISO code for currency
        {
            Json::Value j;
            j[sfBaseAsset] = "USD";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(obj.object.has_value());
            BEAST_EXPECT(obj.object->isFieldPresent(sfBaseAsset));
            auto const& curr = obj.object->getFieldCurrency(sfBaseAsset);
            BEAST_EXPECT(curr.currency().size() == 20);
        }

        // Valid ISO code
        {
            Json::Value j;
            j[sfBaseAsset] = "EUR";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(obj.object.has_value());
            BEAST_EXPECT(obj.object->isFieldPresent(sfBaseAsset));
            auto const& curr = obj.object->getFieldCurrency(sfBaseAsset);
            BEAST_EXPECT(curr.currency().size() == 20);
        }

        // Valid hex string for currency
        {
            Json::Value j;
            j[sfBaseAsset] = "0123456789ABCDEF01230123456789ABCDEF0123";
            STParsedJSONObject obj("Test", j);
            if (BEAST_EXPECT(obj.object.has_value()))
            {
                BEAST_EXPECT(obj.object->isFieldPresent(sfBaseAsset));
                auto const& curr = obj.object->getFieldCurrency(sfBaseAsset);
                BEAST_EXPECT(curr.currency().size() == 20);
            }
        }

        // Invalid ISO code (too long)
        {
            Json::Value j;
            j[sfBaseAsset] = "USDD";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // lowercase ISO code
        {
            Json::Value j;
            j[sfBaseAsset] = "usd";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(obj.object.has_value());
            BEAST_EXPECT(obj.object->isFieldPresent(sfBaseAsset));
            auto const& curr = obj.object->getFieldCurrency(sfBaseAsset);
            BEAST_EXPECT(curr.currency().size() == 20);
        }

        // Invalid hex string (too short)
        {
            Json::Value j;
            j[sfBaseAsset] = "0123456789AB";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Invalid hex string (too long)
        {
            Json::Value j;
            j[sfBaseAsset] = "0123456789ABCDEF0123456789";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Invalid hex string (bad chars)
        {
            Json::Value j;
            j[sfBaseAsset] = "0123456789ABCDEG01234567";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Empty string for currency (should fail)
        {
            Json::Value j;
            j[sfBaseAsset] = "";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(obj.object.has_value());
            BEAST_EXPECT(obj.object->isFieldPresent(sfBaseAsset));
            auto const& curr = obj.object->getFieldCurrency(sfBaseAsset);
            BEAST_EXPECT(curr.currency().size() == 20);
        }

        // Array value for currency (should fail)
        {
            Json::Value j;
            j[sfBaseAsset] = Json::Value(Json::arrayValue);
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Object value for currency (should fail)
        {
            Json::Value j;
            j[sfBaseAsset] = Json::Value(Json::objectValue);
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }
    }

    void
    testAmount()
    {
        testcase("Amount");
        // Test with string value for Amount
        {
            Json::Value j;
            j[sfAmount] = "1000000";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(obj.object.has_value());
            BEAST_EXPECT(obj.object->isFieldPresent(sfAmount));
            BEAST_EXPECT(
                obj.object->getFieldAmount(sfAmount) == STAmount(1000000));
        }

        // Test with int value for Amount
        {
            Json::Value j;
            j[sfAmount] = 5000;
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(obj.object.has_value());
            BEAST_EXPECT(obj.object->isFieldPresent(sfAmount));
            BEAST_EXPECT(
                obj.object->getFieldAmount(sfAmount) == STAmount(5000));
        }

        // Test with decimal string for Amount (should fail)
        {
            Json::Value j;
            j[sfAmount] = "123.45";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Test with empty string for Amount (should fail)
        {
            Json::Value j;
            j[sfAmount] = "";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Test with non-numeric string for Amount (should fail)
        {
            Json::Value j;
            j[sfAmount] = "notanumber";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Test with object value for Amount (should fail)
        {
            Json::Value j;
            j[sfAmount] = Json::Value(Json::objectValue);
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }
    }

    void
    testPathSet()
    {
        testcase("PathSet");
        // Valid test: single path with single element
        {
            Json::Value j;
            Json::Value path(Json::arrayValue);
            Json::Value elem(Json::objectValue);
            elem["account"] = "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh";
            elem["currency"] = "USD";
            elem["issuer"] = "rPT1Sjq2YGrBMTttX4GZHjKu9dyfzbpAYe";
            path.append(elem);
            Json::Value pathset(Json::arrayValue);
            pathset.append(path);
            j[sfPaths] = pathset;
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(obj.object.has_value());
            BEAST_EXPECT(obj.object->isFieldPresent(sfPaths));
            auto const& ps = obj.object->getFieldPathSet(sfPaths);
            BEAST_EXPECT(!ps.empty());
        }

        // Valid test: non-standard currency code
        {
            Json::Value j;
            Json::Value path(Json::arrayValue);
            Json::Value elem(Json::objectValue);
            elem["account"] = "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh";
            elem["currency"] = "0123456789ABCDEF01230123456789ABCDEF0123";
            elem["issuer"] = "rPT1Sjq2YGrBMTttX4GZHjKu9dyfzbpAYe";
            path.append(elem);
            Json::Value pathset(Json::arrayValue);
            pathset.append(path);
            j[sfPaths] = pathset;
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(obj.object.has_value());
            BEAST_EXPECT(obj.object->isFieldPresent(sfPaths));
            auto const& ps = obj.object->getFieldPathSet(sfPaths);
            BEAST_EXPECT(!ps.empty());
        }

        // Test with non-array value for PathSet (should fail)
        {
            Json::Value j;
            j[sfPaths] = "notanarray";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Test with array containing non-array element (should fail)
        {
            Json::Value j;
            Json::Value pathset(Json::arrayValue);
            pathset.append("notanarray");
            j[sfPaths] = pathset;
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Test with array containing array with non-object element (should
        // fail)
        {
            Json::Value j;
            Json::Value path(Json::arrayValue);
            path.append("notanobject");
            Json::Value pathset(Json::arrayValue);
            pathset.append(path);
            j[sfPaths] = pathset;
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Test with array containing array with object missing required keys
        // (should fail)
        {
            Json::Value j;
            Json::Value path(Json::arrayValue);
            Json::Value elem(Json::objectValue);
            elem["foo"] = "bar";  // not a valid path element key
            path.append(elem);
            Json::Value pathset(Json::arrayValue);
            pathset.append(path);
            j[sfPaths] = pathset;
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Test with array containing array with object with invalid account
        // value (should fail)
        {
            Json::Value j;
            Json::Value path(Json::arrayValue);
            Json::Value elem(Json::objectValue);
            elem["account"] = "notAValidBase58Account";
            path.append(elem);
            Json::Value pathset(Json::arrayValue);
            pathset.append(path);
            j[sfPaths] = pathset;
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Test with account not string (should fail)
        {
            Json::Value j;
            Json::Value path(Json::arrayValue);
            Json::Value elem(Json::objectValue);
            elem["account"] = 12345;
            path.append(elem);
            Json::Value pathset(Json::arrayValue);
            pathset.append(path);
            j[sfPaths] = pathset;
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Test with currency not string (should fail)
        {
            Json::Value j;
            Json::Value path(Json::arrayValue);
            Json::Value elem(Json::objectValue);
            elem["currency"] = 12345;
            path.append(elem);
            Json::Value pathset(Json::arrayValue);
            pathset.append(path);
            j[sfPaths] = pathset;
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Test with non-standard currency not hex (should fail)
        {
            Json::Value j;
            Json::Value path(Json::arrayValue);
            Json::Value elem(Json::objectValue);
            elem["currency"] = "notAValidCurrency";
            path.append(elem);
            Json::Value pathset(Json::arrayValue);
            pathset.append(path);
            j[sfPaths] = pathset;
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Test with issuer not string (should fail)
        {
            Json::Value j;
            Json::Value path(Json::arrayValue);
            Json::Value elem(Json::objectValue);
            elem["issuer"] = 12345;
            path.append(elem);
            Json::Value pathset(Json::arrayValue);
            pathset.append(path);
            j[sfPaths] = pathset;
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Test with issuer not base58 (should fail)
        {
            Json::Value j;
            Json::Value path(Json::arrayValue);
            Json::Value elem(Json::objectValue);
            elem["issuer"] = "notAValidBase58Account";
            path.append(elem);
            Json::Value pathset(Json::arrayValue);
            pathset.append(path);
            j[sfPaths] = pathset;
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }
    }

    void
    testIssue()
    {
        testcase("Issue");
        // Valid Issue: currency and issuer as base58
        {
            Json::Value j;
            Json::Value issueJson(Json::objectValue);
            issueJson["currency"] = "USD";
            issueJson["issuer"] = "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh";
            j[sfAsset] = issueJson;
            STParsedJSONObject obj("Test", j);
            if (BEAST_EXPECTS(
                    obj.object.has_value(), obj.error.toStyledString()))
            {
                BEAST_EXPECT(obj.object->isFieldPresent(sfAsset));
                auto const& issueField = (*obj.object)[sfAsset];
                auto const issue = issueField.value().get<Issue>();
                BEAST_EXPECT(issue.currency.size() == 20);
                BEAST_EXPECT(issue.account.size() == 20);
            }
        }

        // Valid Issue: currency as hex
        {
            Json::Value j;
            Json::Value issueJson(Json::objectValue);
            issueJson["currency"] = "0123456789ABCDEF01230123456789ABCDEF0123";
            issueJson["issuer"] = "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh";
            j[sfAsset] = issueJson;
            STParsedJSONObject obj("Test", j);
            if (BEAST_EXPECT(obj.object.has_value()))
            {
                BEAST_EXPECT(obj.object->isFieldPresent(sfAsset));
                auto const& issueField = (*obj.object)[sfAsset];
                auto const issue = issueField.value().get<Issue>();
                BEAST_EXPECT(issue.currency.size() == 20);
                BEAST_EXPECT(issue.account.size() == 20);
            }
        }

        // Valid Issue: MPTID
        {
            Json::Value j;
            Json::Value issueJson(Json::objectValue);
            issueJson["mpt_issuance_id"] =
                "0000000000000000000000004D5054494431323334234234";
            j[sfAsset] = issueJson;
            STParsedJSONObject obj("Test", j);
            if (BEAST_EXPECT(obj.object.has_value()))
            {
                BEAST_EXPECT(obj.object->isFieldPresent(sfAsset));
                auto const& issueField = (*obj.object)[sfAsset];
                auto const issue = issueField.value().get<MPTIssue>();
                BEAST_EXPECT(issue.getMptID().size() == 24);
            }
        }

        // Invalid Issue: missing currency
        {
            Json::Value j;
            Json::Value issue(Json::objectValue);
            issue["issuer"] = "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh";
            j[sfAsset] = issue;
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Invalid Issue: missing issuer
        {
            Json::Value j;
            Json::Value issue(Json::objectValue);
            issue["currency"] = "USD";
            j[sfAsset] = issue;
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Invalid Issue: currency too long
        {
            Json::Value j;
            Json::Value issue(Json::objectValue);
            issue["currency"] = "USDD";
            issue["issuer"] = "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh";
            j[sfAsset] = issue;
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Invalid Issue: issuer not base58 or hex
        {
            Json::Value j;
            Json::Value issue(Json::objectValue);
            issue["currency"] = "USD";
            issue["issuer"] = "notAValidIssuer";
            j[sfAsset] = issue;
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Invalid Issue: currency not string
        {
            Json::Value j;
            Json::Value issue(Json::objectValue);
            issue["currency"] = Json::Value(Json::arrayValue);
            issue["issuer"] = "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh";
            j[sfAsset] = issue;
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Invalid Issue: issuer not string
        {
            Json::Value j;
            Json::Value issue(Json::objectValue);
            issue["currency"] = "USD";
            issue["issuer"] = Json::Value(Json::objectValue);
            j[sfAsset] = issue;
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Invalid Issue: not an object
        {
            Json::Value j;
            j[sfAsset] = "notanobject";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }
    }

    void
    testXChainBridge()
    {
        testcase("XChainBridge");
        // Valid XChainBridge
        {
            Json::Value j;
            Json::Value bridge(Json::objectValue);
            Json::Value issuingChainIssue(Json::objectValue);
            issuingChainIssue["currency"] = "USD";
            issuingChainIssue["issuer"] = "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh";
            Json::Value lockingChainIssue(Json::objectValue);
            lockingChainIssue["currency"] = "EUR";
            lockingChainIssue["issuer"] = "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh";
            bridge["LockingChainIssue"] = lockingChainIssue;
            bridge["IssuingChainIssue"] = issuingChainIssue;
            bridge["LockingChainDoor"] = "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh";
            bridge["IssuingChainDoor"] = "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh";
            j[sfXChainBridge] = bridge;
            STParsedJSONObject obj("Test", j);
            if (BEAST_EXPECT(obj.object.has_value()))
            {
                BEAST_EXPECT(obj.object->isFieldPresent(sfXChainBridge));
                auto const& bridgeField = (*obj.object)[sfXChainBridge];
                BEAST_EXPECT(
                    bridgeField->lockingChainIssue().currency.size() == 20);
                BEAST_EXPECT(
                    bridgeField->issuingChainIssue().currency.size() == 20);
            }
        }

        // Valid XChainBridge: issues as hex currency
        {
            Json::Value j;
            Json::Value bridge(Json::objectValue);
            Json::Value issuingChainIssue(Json::objectValue);
            issuingChainIssue["currency"] =
                "0123456789ABCDEF01230123456789ABCDEF0123";
            issuingChainIssue["issuer"] = "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh";
            Json::Value lockingChainIssue(Json::objectValue);
            lockingChainIssue["currency"] =
                "0123456789ABCDEF01230123456789ABCDEF0123";
            lockingChainIssue["issuer"] = "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh";
            bridge["LockingChainIssue"] = lockingChainIssue;
            bridge["IssuingChainIssue"] = issuingChainIssue;
            bridge["LockingChainDoor"] = "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh";
            bridge["IssuingChainDoor"] = "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh";
            j[sfXChainBridge] = bridge;
            STParsedJSONObject obj("Test", j);
            if (BEAST_EXPECT(obj.object.has_value()))
            {
                BEAST_EXPECT(obj.object->isFieldPresent(sfXChainBridge));
                auto const& bridgeField = (*obj.object)[sfXChainBridge];
                BEAST_EXPECT(
                    bridgeField->lockingChainIssue().currency.size() == 20);
                BEAST_EXPECT(
                    bridgeField->issuingChainIssue().currency.size() == 20);
            }
        }

        // Invalid XChainBridge: missing LockingChainIssue
        {
            Json::Value j;
            Json::Value bridge(Json::objectValue);
            Json::Value issuingChainIssue(Json::objectValue);
            issuingChainIssue["currency"] = "USD";
            issuingChainIssue["issuer"] = "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh";
            bridge["IssuingChainIssue"] = issuingChainIssue;
            bridge["LockingChainDoor"] = "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh";
            bridge["IssuingChainDoor"] = "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh";
            j[sfXChainBridge] = bridge;
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Invalid XChainBridge: missing IssuingChainIssue
        {
            Json::Value j;
            Json::Value bridge(Json::objectValue);
            Json::Value lockingChainIssue(Json::objectValue);
            lockingChainIssue["currency"] = "EUR";
            lockingChainIssue["issuer"] = "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh";
            bridge["LockingChainIssue"] = lockingChainIssue;
            bridge["LockingChainDoor"] = "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh";
            bridge["IssuingChainDoor"] = "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh";
            j[sfXChainBridge] = bridge;
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Invalid XChainBridge: missing LockingChainDoor
        {
            Json::Value j;
            Json::Value bridge(Json::objectValue);
            Json::Value issuingChainIssue(Json::objectValue);
            issuingChainIssue["currency"] = "USD";
            issuingChainIssue["issuer"] = "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh";
            bridge["IssuingChainIssue"] = issuingChainIssue;
            Json::Value lockingChainIssue(Json::objectValue);
            lockingChainIssue["currency"] = "EUR";
            lockingChainIssue["issuer"] = "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh";
            bridge["LockingChainIssue"] = lockingChainIssue;
            bridge["IssuingChainDoor"] = "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh";
            j[sfXChainBridge] = bridge;
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Invalid XChainBridge: missing IssuingChainDoor
        {
            Json::Value j;
            Json::Value bridge(Json::objectValue);
            Json::Value issuingChainIssue(Json::objectValue);
            issuingChainIssue["currency"] = "USD";
            issuingChainIssue["issuer"] = "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh";
            bridge["IssuingChainIssue"] = issuingChainIssue;
            Json::Value lockingChainIssue(Json::objectValue);
            lockingChainIssue["currency"] = "EUR";
            lockingChainIssue["issuer"] = "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh";
            bridge["LockingChainIssue"] = lockingChainIssue;
            bridge["LockingChainDoor"] = "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh";
            j[sfXChainBridge] = bridge;
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Invalid XChainBridge: IssuingChainIssue not an object
        {
            Json::Value j;
            Json::Value bridge(Json::objectValue);
            bridge["LockingChainIssue"] = "notanobject";
            bridge["IssuingChainIssue"] = "notanobject";
            j[sfXChainBridge] = bridge;
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Invalid XChainBridge: IssuingChainIssue missing currency
        {
            Json::Value j;
            Json::Value bridge(Json::objectValue);
            Json::Value asset(Json::objectValue);
            asset["issuer"] = "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh";
            Json::Value lockingChainIssue(Json::objectValue);
            lockingChainIssue["currency"] = "EUR";
            lockingChainIssue["issuer"] = "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh";
            bridge["LockingChainIssue"] = lockingChainIssue;
            bridge["IssuingChainIssue"] = asset;
            j[sfXChainBridge] = bridge;
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Invalid XChainBridge: asset missing issuer
        {
            Json::Value j;
            Json::Value bridge(Json::objectValue);
            Json::Value asset(Json::objectValue);
            asset["currency"] = "USD";
            Json::Value lockingChainIssue(Json::objectValue);
            lockingChainIssue["currency"] = "EUR";
            lockingChainIssue["issuer"] = "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh";
            bridge["LockingChainIssue"] = lockingChainIssue;
            bridge["IssuingChainIssue"] = asset;
            j[sfXChainBridge] = bridge;
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Invalid XChainBridge: asset issuer not base58
        {
            Json::Value j;
            Json::Value bridge(Json::objectValue);
            Json::Value asset(Json::objectValue);
            asset["currency"] = "USD";
            asset["issuer"] = "notAValidBase58Account";
            Json::Value lockingChainIssue(Json::objectValue);
            lockingChainIssue["currency"] = "EUR";
            lockingChainIssue["issuer"] = "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh";
            bridge["LockingChainIssue"] = lockingChainIssue;
            bridge["IssuingChainIssue"] = asset;
            j[sfXChainBridge] = bridge;
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Invalid XChainBridge: not an object
        {
            Json::Value j;
            j[sfXChainBridge] = "notanobject";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }
    }

    void
    testNumber()
    {
        testcase("Number");
        // Valid integer value for STNumber
        {
            Json::Value j;
            j[sfNumber] = 12345;
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(obj.object.has_value());
            BEAST_EXPECT(obj.object->isFieldPresent(sfNumber));
            BEAST_EXPECT(
                obj.object->getFieldNumber(sfNumber).value() ==
                Number(12345, 0));
        }

        // Valid uint value for STNumber
        {
            Json::Value j;
            j[sfNumber] = 12345u;
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(obj.object.has_value());
            BEAST_EXPECT(obj.object->isFieldPresent(sfNumber));
            BEAST_EXPECT(
                obj.object->getFieldNumber(sfNumber).value() ==
                Number(12345, 0));
        }

        // Valid string integer value for STNumber
        {
            Json::Value j;
            j[sfNumber] = "67890";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(obj.object.has_value());
            BEAST_EXPECT(obj.object->isFieldPresent(sfNumber));
            BEAST_EXPECT(
                obj.object->getFieldNumber(sfNumber).value() ==
                Number(67890, 0));
        }

        // Valid negative integer value for STNumber
        {
            Json::Value j;
            j[sfNumber] = -42;
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(obj.object.has_value());
            BEAST_EXPECT(obj.object->isFieldPresent(sfNumber));
            BEAST_EXPECT(
                obj.object->getFieldNumber(sfNumber).value() == Number(-42, 0));
        }

        // Valid string negative integer value for STNumber
        {
            Json::Value j;
            j[sfNumber] = "-123";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(obj.object.has_value());
            BEAST_EXPECT(obj.object->isFieldPresent(sfNumber));
            BEAST_EXPECT(
                obj.object->getFieldNumber(sfNumber).value() ==
                Number(-123, 0));
        }

        // Valid floating point value for STNumber
        {
            Json::Value j;
            j[sfNumber] = "3.14159";
            STParsedJSONObject obj("Test", j);
            if (BEAST_EXPECT(obj.object.has_value()))
            {
                BEAST_EXPECT(obj.object->isFieldPresent(sfNumber));
                BEAST_EXPECT(
                    obj.object->getFieldNumber(sfNumber).value() ==
                    Number(314159, -5));
            }
        }

        // Valid string floating point value for STNumber
        {
            Json::Value j;
            j[sfNumber] = "2.71828";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(obj.object.has_value());
            BEAST_EXPECT(obj.object->isFieldPresent(sfNumber));
            BEAST_EXPECT(
                obj.object->getFieldNumber(sfNumber).value() ==
                Number(271828, -5));
        }

        // Invalid string value for STNumber (not a number)
        {
            Json::Value j;
            j[sfNumber] = "notanumber";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Invalid array value for STNumber
        {
            Json::Value j;
            j[sfNumber] = Json::Value(Json::arrayValue);
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Invalid object value for STNumber
        {
            Json::Value j;
            j[sfNumber] = Json::Value(Json::objectValue);
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Empty string for STNumber (should fail)
        {
            Json::Value j;
            j[sfNumber] = "";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }
    }

    void
    testObject()
    {
        testcase("Object");
        // Test with valid object for Object
        {
            Json::Value j;
            Json::Value objVal(Json::objectValue);
            objVal[sfTransactionResult] = 1;
            j[sfTransactionMetaData] = objVal;
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(obj.object.has_value());
            BEAST_EXPECT(obj.object->isFieldPresent(sfTransactionMetaData));
        }

        // Test with non-object value for Object (should fail)
        {
            Json::Value j;
            j[sfTransactionMetaData] = "notanobject";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Test with array value for Object (should fail)
        {
            Json::Value j;
            Json::Value arr(Json::arrayValue);
            arr.append(1);
            j[sfTransactionMetaData] = arr;
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Test with null value for Object (should fail)
        {
            Json::Value j;
            j[sfTransactionMetaData] = Json::Value(Json::nullValue);
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Test with max depth (should succeed)
        // max depth is 64
        {
            Json::Value j;
            Json::Value obj(Json::objectValue);
            Json::Value* current = &obj;
            for (int i = 0; i < 63; ++i)
            {
                Json::Value next(Json::objectValue);
                (*current)[sfTransactionMetaData] = next;
                current = &((*current)[sfTransactionMetaData]);
            }
            (*current)[sfTransactionResult.getJsonName()] = 1;
            j[sfTransactionMetaData] = obj;
            STParsedJSONObject parsed("Test", j);
            BEAST_EXPECT(parsed.object.has_value());
            BEAST_EXPECT(parsed.object->isFieldPresent(sfTransactionMetaData));
        }

        // Test with depth exceeding maxDepth (should fail)
        {
            Json::Value j;
            Json::Value obj(Json::objectValue);
            Json::Value* current = &obj;
            for (int i = 0; i < 64; ++i)
            {
                Json::Value next(Json::objectValue);
                (*current)[sfTransactionMetaData] = next;
                current = &((*current)[sfTransactionMetaData]);
            }
            (*current)[sfTransactionResult.getJsonName()] = 1;
            j[sfTransactionMetaData] = obj;
            STParsedJSONObject parsed("Test", j);
            BEAST_EXPECT(!parsed.object.has_value());
        }
    }

    void
    testArray()
    {
        testcase("Array");
        // Test with valid array for Array
        {
            Json::Value j;
            Json::Value arr(Json::arrayValue);
            Json::Value elem(Json::objectValue);
            elem[sfTransactionResult] = 2;
            Json::Value elem2(Json::objectValue);
            elem2[sfTransactionMetaData] = elem;
            arr.append(elem2);
            j[sfSignerEntries] = arr;
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(obj.object.has_value());
            BEAST_EXPECT(obj.object->isFieldPresent(sfSignerEntries));
        }

        // Test with array containing non-object element (should fail)
        {
            Json::Value j;
            Json::Value arr(Json::arrayValue);
            arr.append("notanobject");
            j[sfSignerEntries] = arr;
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Test with array containing object with invalid field (should fail)
        {
            Json::Value j;
            Json::Value arr(Json::arrayValue);
            Json::Value elem(Json::objectValue);
            elem["invalidField"] = 1;
            arr.append(elem);
            j[sfSignerEntries] = arr;
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Test with array containing object with multiple keys (should fail)
        {
            Json::Value j;
            Json::Value arr(Json::arrayValue);
            Json::Value elem(Json::objectValue);
            elem[sfTransactionResult] = 2;
            elem[sfNetworkID] = 3;
            arr.append(elem);
            j[sfSignerEntries] = arr;
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Test with non-array value for Array (should fail)
        {
            Json::Value j;
            j[sfSignerEntries] = "notanarray";
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Test with array containing object with valid field but invalid value
        // (should fail)
        {
            Json::Value j;
            Json::Value arr(Json::arrayValue);
            Json::Value elem(Json::objectValue);
            elem[sfTransactionResult] = "notanint";
            arr.append(elem);
            j[sfSignerEntries] = arr;
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(!obj.object.has_value());
        }

        // Test with empty array for Array (should be valid)
        {
            Json::Value j;
            Json::Value arr(Json::arrayValue);
            j[sfSignerEntries] = arr;
            STParsedJSONObject obj("Test", j);
            BEAST_EXPECT(obj.object.has_value());
            BEAST_EXPECT(obj.object->isFieldPresent(sfSignerEntries));
        }

        // Test with object provided but not object SField
        {
            Json::Value j;
            Json::Value obj(Json::arrayValue);
            obj.append(Json::Value(Json::objectValue));
            obj[0u][sfTransactionResult] = 1;
            j[sfSignerEntries] = obj;
            STParsedJSONObject parsed("Test", j);
            BEAST_EXPECT(!parsed.object.has_value());
        }
    }

    void
    testGeneralInvalid()
    {
        testcase("General Invalid Cases");
        Json::Value j;
        j[sfLedgerEntry] = 1;  // not a valid SField for STParsedJSON
    }

    void
    run() override
    {
        testUInt8();
        testUInt16();
        testUInt32();
        testUInt64();
        testUInt128();
        testUInt160();
        testUInt192();
        testUInt256();
        testBlob();
        testVector256();
        testAccount();
        testCurrency();
        testAmount();
        testPathSet();
        testIssue();
        testXChainBridge();
        testNumber();
        testObject();
        testArray();
    }
};

BEAST_DEFINE_TESTSUITE(STParsedJSON, protocol, ripple);

}  // namespace ripple
