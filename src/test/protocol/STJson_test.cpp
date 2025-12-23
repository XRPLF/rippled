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

#include <xrpl/basics/Buffer.h>
#include <xrpl/basics/StringUtilities.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAccount.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STBlob.h>
#include <xrpl/protocol/STCurrency.h>
#include <xrpl/protocol/STData.h>
#include <xrpl/protocol/STInteger.h>
#include <xrpl/protocol/STJson.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STPathSet.h>
#include <xrpl/protocol/STVector256.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/UintTypes.h>

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace xrpl {

struct STJson_test : public beast::unit_test::suite
{
    void
    testDefaultConstructor()
    {
        testcase("Default constructor");
        STJson json;
        BEAST_EXPECT(json.isObject());
        BEAST_EXPECT(!json.isArray());
        BEAST_EXPECT(json.getMap().empty());
    }

    void
    testSetAndGet()
    {
        testcase("setObjectField() and getObjectField()");
        STJson json;
        auto value = std::make_shared<STUInt32>(sfLedgerIndex, 12345);
        json.setObjectField("foo", value);

        auto retrieved = json.getObjectField("foo");
        BEAST_EXPECT(retrieved.has_value());
        BEAST_EXPECT((*retrieved)->getSType() == STI_UINT32);
        BEAST_EXPECT(
            std::dynamic_pointer_cast<STUInt32>(*retrieved)->value() == 12345);

        // Test non-existent key
        auto missing = json.getObjectField("bar");
        BEAST_EXPECT(!missing.has_value());
    }

    void
    testMoveConstructor()
    {
        testcase("Move constructor (Object)");
        STJson::Map map;
        map["bar"] = std::make_shared<STUInt16>(sfTransactionType, 42);
        STJson json(std::move(map));
        BEAST_EXPECT(json.isObject());
        BEAST_EXPECT(json.getMap().size() == 1);
        BEAST_EXPECT(
            std::dynamic_pointer_cast<STUInt16>(json.getMap().at("bar"))
                ->value() == 42);
    }

    void
    testArrayConstruction()
    {
        testcase("Array constructor");
        STJson::Array arr;
        arr.push_back(std::make_shared<STUInt32>(sfNetworkID, 100));
        arr.push_back(std::make_shared<STUInt32>(sfNetworkID, 200));

        STJson json(std::move(arr));
        BEAST_EXPECT(json.isArray());
        BEAST_EXPECT(!json.isObject());
        BEAST_EXPECT(json.arraySize() == 2);

        auto elem0 = json.getArrayElement(0);
        BEAST_EXPECT(elem0.has_value());
        BEAST_EXPECT(
            std::dynamic_pointer_cast<STUInt32>(*elem0)->value() == 100);
    }

    void
    testTypeChecking()
    {
        testcase("Type checking methods");
        STJson objJson;
        BEAST_EXPECT(objJson.isObject());
        BEAST_EXPECT(!objJson.isArray());
        BEAST_EXPECT(objJson.getType() == STJson::JsonType::Object);

        STJson arrJson(STJson::Array{});
        BEAST_EXPECT(arrJson.isArray());
        BEAST_EXPECT(!arrJson.isObject());
        BEAST_EXPECT(arrJson.getType() == STJson::JsonType::Array);
    }

    void
    testArrayOperations()
    {
        testcase("Array operations");
        STJson json(STJson::Array{});

        // Test push
        json.pushArrayElement(std::make_shared<STUInt8>(sfCloseResolution, 10));
        json.pushArrayElement(std::make_shared<STUInt8>(sfCloseResolution, 20));
        json.pushArrayElement(std::make_shared<STUInt8>(sfCloseResolution, 30));

        BEAST_EXPECT(json.arraySize() == 3);

        // Test get
        auto elem1 = json.getArrayElement(1);
        BEAST_EXPECT(elem1.has_value());
        BEAST_EXPECT(std::dynamic_pointer_cast<STUInt8>(*elem1)->value() == 20);

        // Test set (replace)
        json.setArrayElement(
            1, std::make_shared<STUInt8>(sfCloseResolution, 25));
        auto elem1Updated = json.getArrayElement(1);
        BEAST_EXPECT(
            std::dynamic_pointer_cast<STUInt8>(*elem1Updated)->value() == 25);

        // Test out of bounds
        auto missing = json.getArrayElement(10);
        BEAST_EXPECT(!missing.has_value());
    }

    void
    testArrayAutoResize()
    {
        testcase("Array auto-resize");
        STJson json(STJson::Array{});

        // Set element at index 5 (should auto-resize with nulls)
        json.setArrayElement(5, std::make_shared<STUInt32>(sfNetworkID, 999));

        BEAST_EXPECT(json.arraySize() == 6);

        // Check nulls were added
        for (size_t i = 0; i < 5; ++i)
        {
            auto elem = json.getArrayElement(i);
            BEAST_EXPECT(elem.has_value());
            BEAST_EXPECT(*elem == nullptr);
        }

        // Check value at index 5
        auto elem5 = json.getArrayElement(5);
        BEAST_EXPECT(elem5.has_value());
        BEAST_EXPECT(
            std::dynamic_pointer_cast<STUInt32>(*elem5)->value() == 999);
    }

    void
    testArrayElementFields()
    {
        testcase("Array element field operations");
        STJson json(STJson::Array{});

        // Set field in array element (auto-creates object)
        json.setArrayElementField(
            0, "name", std::make_shared<STUInt32>(sfNetworkID, 42));
        json.setArrayElementField(
            0, "value", std::make_shared<STUInt32>(sfNetworkID, 100));

        // Get fields
        auto name = json.getArrayElementField(0, "name");
        BEAST_EXPECT(name.has_value());
        BEAST_EXPECT(std::dynamic_pointer_cast<STUInt32>(*name)->value() == 42);

        auto value = json.getArrayElementField(0, "value");
        BEAST_EXPECT(value.has_value());
        BEAST_EXPECT(
            std::dynamic_pointer_cast<STUInt32>(*value)->value() == 100);

        // Set field at higher index (auto-resize)
        json.setArrayElementField(
            3, "test", std::make_shared<STUInt8>(sfCloseResolution, 99));
        BEAST_EXPECT(json.arraySize() == 4);

        auto test = json.getArrayElementField(3, "test");
        BEAST_EXPECT(test.has_value());
        BEAST_EXPECT(std::dynamic_pointer_cast<STUInt8>(*test)->value() == 99);
    }

    void
    testNestedObjectField()
    {
        testcase("Nested object field operations");
        STJson json;

        json.setNestedObjectField(
            "user", "id", std::make_shared<STUInt32>(sfNetworkID, 123));
        json.setNestedObjectField(
            "user", "name", std::make_shared<STUInt32>(sfNetworkID, 456));

        auto id = json.getNestedObjectField("user", "id");
        BEAST_EXPECT(id.has_value());
        BEAST_EXPECT(std::dynamic_pointer_cast<STUInt32>(*id)->value() == 123);

        auto name = json.getNestedObjectField("user", "name");
        BEAST_EXPECT(name.has_value());
        BEAST_EXPECT(
            std::dynamic_pointer_cast<STUInt32>(*name)->value() == 456);

        // Test non-existent nested key
        auto missing = json.getNestedObjectField("user", "age");
        BEAST_EXPECT(!missing.has_value());
    }

    void
    testNestedArrayOperations()
    {
        testcase("Nested array operations");
        STJson json;

        // Set entire elements in nested array
        json.setNestedArrayElement(
            "items", 0, std::make_shared<STUInt32>(sfNetworkID, 10));
        json.setNestedArrayElement(
            "items", 1, std::make_shared<STUInt32>(sfNetworkID, 20));
        json.setNestedArrayElement(
            "items", 2, std::make_shared<STUInt32>(sfNetworkID, 30));

        // Get elements
        auto item0 = json.getNestedArrayElement("items", 0);
        BEAST_EXPECT(item0.has_value());
        BEAST_EXPECT(
            std::dynamic_pointer_cast<STUInt32>(*item0)->value() == 10);

        auto item2 = json.getNestedArrayElement("items", 2);
        BEAST_EXPECT(item2.has_value());
        BEAST_EXPECT(
            std::dynamic_pointer_cast<STUInt32>(*item2)->value() == 30);

        // Auto-resize test
        json.setNestedArrayElement(
            "items", 5, std::make_shared<STUInt32>(sfNetworkID, 60));
        auto item5 = json.getNestedArrayElement("items", 5);
        BEAST_EXPECT(item5.has_value());
        BEAST_EXPECT(
            std::dynamic_pointer_cast<STUInt32>(*item5)->value() == 60);
    }

    void
    testNestedArrayElementFields()
    {
        testcase("Nested array element field operations");
        STJson json;

        // Set fields in nested array elements
        json.setNestedArrayElementField(
            "users", 0, "id", std::make_shared<STUInt32>(sfNetworkID, 100));
        json.setNestedArrayElementField(
            "users", 0, "name", std::make_shared<STUInt32>(sfNetworkID, 200));
        json.setNestedArrayElementField(
            "users", 1, "id", std::make_shared<STUInt32>(sfNetworkID, 101));
        json.setNestedArrayElementField(
            "users", 1, "name", std::make_shared<STUInt32>(sfNetworkID, 201));

        // Get fields
        auto user0id = json.getNestedArrayElementField("users", 0, "id");
        BEAST_EXPECT(user0id.has_value());
        BEAST_EXPECT(
            std::dynamic_pointer_cast<STUInt32>(*user0id)->value() == 100);

        auto user1name = json.getNestedArrayElementField("users", 1, "name");
        BEAST_EXPECT(user1name.has_value());
        BEAST_EXPECT(
            std::dynamic_pointer_cast<STUInt32>(*user1name)->value() == 201);

        // Test missing field
        auto missing = json.getNestedArrayElementField("users", 0, "age");
        BEAST_EXPECT(!missing.has_value());
    }

    void
    testDepthValidation()
    {
        testcase("Depth validation (max 1 level)");

        // Valid: Object with nested object (depth 1)
        {
            STJson json;
            auto nested = std::make_shared<STJson>();
            nested->setObjectField(
                "x", std::make_shared<STUInt32>(sfNetworkID, 42));

            try
            {
                json.setObjectField("nested", nested);
                pass();
            }
            catch (...)
            {
                fail("Should allow depth 1 nesting");
            }
        }

        // Invalid: Object with nested object containing nested object (depth 2)
        {
            STJson json;
            auto nested1 = std::make_shared<STJson>();
            auto nested2 = std::make_shared<STJson>();
            nested2->setObjectField(
                "x", std::make_shared<STUInt32>(sfNetworkID, 42));
            nested1->setObjectField("nested", nested2);

            try
            {
                json.setObjectField("nested", nested1);
                fail("Should reject depth 2 nesting");
            }
            catch (std::runtime_error const& e)
            {
                pass();
            }
        }

        // Valid: Array with object elements (depth 1)
        {
            STJson json(STJson::Array{});
            auto elem = std::make_shared<STJson>();
            elem->setObjectField(
                "x", std::make_shared<STUInt32>(sfNetworkID, 42));

            try
            {
                json.pushArrayElement(elem);
                pass();
            }
            catch (...)
            {
                fail("Should allow depth 1 in array");
            }
        }

        // Invalid: Array with nested arrays (depth 2)
        {
            STJson json(STJson::Array{});
            auto innerArray = std::make_shared<STJson>(STJson::Array{});
            innerArray->pushArrayElement(
                std::make_shared<STUInt32>(sfNetworkID, 42));

            try
            {
                json.pushArrayElement(innerArray);
                fail("Should reject array of arrays");
            }
            catch (std::runtime_error const& e)
            {
                pass();
            }
        }

        // Valid: Object with nested array (depth 1)
        {
            STJson json;
            auto arr = std::make_shared<STJson>(STJson::Array{});
            arr->pushArrayElement(std::make_shared<STUInt32>(sfNetworkID, 42));

            try
            {
                json.setObjectField("arr", arr);
                pass();
            }
            catch (...)
            {
                fail("Should allow object with array");
            }
        }

        // Test depth validation in setNestedObjectField
        {
            STJson json;
            auto nested = std::make_shared<STJson>();
            nested->setObjectField(
                "x", std::make_shared<STUInt32>(sfNetworkID, 42));

            try
            {
                json.setNestedObjectField("outer", "inner", nested);
                fail("Should reject depth 2 via setNestedObjectField");
            }
            catch (std::runtime_error const& e)
            {
                pass();
            }
        }
    }

    void
    testAddAndFromBlob()
    {
        testcase("add() and fromBlob() for objects");
        STJson json;
        json.setObjectField(
            "a", std::make_shared<STUInt8>(sfCloseResolution, 7));
        json.setObjectField(
            "b", std::make_shared<STUInt32>(sfNetworkID, 123456));

        Serializer s;
        json.add(s);

        auto blob = s.peekData();
        auto parsed = STJson::fromBlob(blob.data(), blob.size());
        BEAST_EXPECT(parsed->isObject());
        BEAST_EXPECT(parsed->getMap().size() == 2);

        auto a = parsed->getObjectField("a");
        BEAST_EXPECT(a.has_value());
        BEAST_EXPECT(std::dynamic_pointer_cast<STUInt8>(*a)->value() == 7);

        auto b = parsed->getObjectField("b");
        BEAST_EXPECT(b.has_value());
        BEAST_EXPECT(
            std::dynamic_pointer_cast<STUInt32>(*b)->value() == 123456);
    }

    void
    testArraySerialization()
    {
        testcase("Array serialization and deserialization");
        STJson json(STJson::Array{});
        json.pushArrayElement(std::make_shared<STUInt8>(sfCloseResolution, 10));
        json.pushArrayElement(std::make_shared<STUInt32>(sfNetworkID, 20));
        json.pushArrayElement(std::make_shared<STUInt64>(sfIndexNext, 30));

        Serializer s;
        json.add(s);

        auto blob = s.peekData();
        auto parsed = STJson::fromBlob(blob.data(), blob.size());

        BEAST_EXPECT(parsed->isArray());
        BEAST_EXPECT(parsed->arraySize() == 3);

        auto elem0 = parsed->getArrayElement(0);
        BEAST_EXPECT(std::dynamic_pointer_cast<STUInt8>(*elem0)->value() == 10);

        auto elem1 = parsed->getArrayElement(1);
        BEAST_EXPECT(
            std::dynamic_pointer_cast<STUInt32>(*elem1)->value() == 20);

        auto elem2 = parsed->getArrayElement(2);
        BEAST_EXPECT(
            std::dynamic_pointer_cast<STUInt64>(*elem2)->value() == 30);
    }

    void
    testFromSerialIter()
    {
        testcase("fromSerialIter()");
        STJson json;
        json.setObjectField(
            "x", std::make_shared<STUInt8>(sfCloseResolution, 99));
        Serializer s;
        json.add(s);

        SerialIter sit(s.peekData().data(), s.peekData().size());
        auto parsed = STJson::fromSerialIter(sit);
        BEAST_EXPECT(parsed->isObject());
        BEAST_EXPECT(parsed->getMap().size() == 1);

        auto x = parsed->getObjectField("x");
        BEAST_EXPECT(x.has_value());
        BEAST_EXPECT(std::dynamic_pointer_cast<STUInt8>(*x)->value() == 99);
    }

    void
    testFromSField()
    {
        testcase("Constructor from SField");
        STJson json;
        json.setObjectField(
            "x", std::make_shared<STUInt8>(sfCloseResolution, 99));
        Serializer s;
        json.add(s);

        SerialIter sit(s.peekData().data(), s.peekData().size());
        auto parsed = STJson{sit, sfContractCode};
        BEAST_EXPECT(parsed.isObject());
        BEAST_EXPECT(parsed.getMap().size() == 1);

        auto x = parsed.getObjectField("x");
        BEAST_EXPECT(x.has_value());
        BEAST_EXPECT(std::dynamic_pointer_cast<STUInt8>(*x)->value() == 99);
    }

    void
    testGetJson()
    {
        testcase("getJson() for objects");
        STJson json;
        json.setObjectField(
            "foo", std::make_shared<STUInt16>(sfTransactionType, 65535));
        json.setObjectField("bar", nullptr);  // test null value

        Json::Value jv = json.getJson(JsonOptions::none);
        BEAST_EXPECT(jv.isObject());
        BEAST_EXPECT(jv["foo"].asUInt() == 65535);
        BEAST_EXPECT(jv["bar"].isNull());
    }

    void
    testGetJsonArray()
    {
        testcase("getJson() for arrays");
        STJson json(STJson::Array{});
        json.pushArrayElement(std::make_shared<STUInt32>(sfNetworkID, 100));
        json.pushArrayElement(std::make_shared<STUInt32>(sfNetworkID, 200));
        json.pushArrayElement(nullptr);  // null element

        Json::Value jv = json.getJson(JsonOptions::none);
        BEAST_EXPECT(jv.isArray());
        BEAST_EXPECT(jv.size() == 3);
        BEAST_EXPECT(jv[Json::UInt(0)].asUInt() == 100);
        BEAST_EXPECT(jv[Json::UInt(1)].asUInt() == 200);
        BEAST_EXPECT(jv[Json::UInt(2)].isNull());
    }

    void
    testMakeValueFromVLWithType()
    {
        testcase("makeValueFromVLWithType()");
        Serializer s;
        s.add8(STI_UINT32);
        s.add32(0xDEADBEEF);
        SerialIter sit(s.peekData().data(), s.peekData().size());
        auto value = STJson::makeValueFromVLWithType(sit);
        BEAST_EXPECT(value->getSType() == STI_UINT32);
        BEAST_EXPECT(
            std::dynamic_pointer_cast<STUInt32>(value)->value() == 0xDEADBEEF);
    }

    void
    testMixedStructures()
    {
        testcase("Mixed structures (objects with arrays)");
        STJson json;

        // Add simple fields
        json.setObjectField("id", std::make_shared<STUInt32>(sfNetworkID, 1));

        // Add nested object
        json.setNestedObjectField(
            "metadata", "version", std::make_shared<STUInt32>(sfNetworkID, 2));

        // Add nested array with objects
        json.setNestedArrayElementField(
            "users", 0, "name", std::make_shared<STUInt32>(sfNetworkID, 100));
        json.setNestedArrayElementField(
            "users", 0, "age", std::make_shared<STUInt32>(sfNetworkID, 25));
        json.setNestedArrayElementField(
            "users", 1, "name", std::make_shared<STUInt32>(sfNetworkID, 101));

        // Serialize and deserialize
        Serializer s;
        json.add(s);
        auto parsed =
            STJson::fromBlob(s.peekData().data(), s.peekData().size());

        // Verify structure
        BEAST_EXPECT(parsed->isObject());

        auto id = parsed->getObjectField("id");
        BEAST_EXPECT(std::dynamic_pointer_cast<STUInt32>(*id)->value() == 1);

        auto version = parsed->getNestedObjectField("metadata", "version");
        BEAST_EXPECT(
            std::dynamic_pointer_cast<STUInt32>(*version)->value() == 2);

        auto user0name = parsed->getNestedArrayElementField("users", 0, "name");
        BEAST_EXPECT(
            std::dynamic_pointer_cast<STUInt32>(*user0name)->value() == 100);

        auto user1name = parsed->getNestedArrayElementField("users", 1, "name");
        BEAST_EXPECT(
            std::dynamic_pointer_cast<STUInt32>(*user1name)->value() == 101);
    }

    void
    testSTTypes()
    {
        testcase("All STypes roundtrip");

        // STI_UINT8
        {
            STJson json;
            json.setObjectField(
                "u8", std::make_shared<STUInt8>(sfCloseResolution, 200));
            Serializer s;
            json.add(s);
            auto parsed =
                STJson::fromBlob(s.peekData().data(), s.peekData().size());
            auto u8 = parsed->getObjectField("u8");
            BEAST_EXPECT(
                std::dynamic_pointer_cast<STUInt8>(*u8)->value() == 200);
        }

        // STI_UINT16
        {
            STJson json;
            json.setObjectField(
                "u16", std::make_shared<STUInt16>(sfSignerWeight, 4242));
            Serializer s;
            json.add(s);
            auto parsed =
                STJson::fromBlob(s.peekData().data(), s.peekData().size());
            auto u16 = parsed->getObjectField("u16");
            BEAST_EXPECT(
                std::dynamic_pointer_cast<STUInt16>(*u16)->value() == 4242);
        }

        // STI_UINT32
        {
            STJson json;
            json.setObjectField(
                "u32", std::make_shared<STUInt32>(sfNetworkID, 0xABCDEF01));
            Serializer s;
            json.add(s);
            auto parsed =
                STJson::fromBlob(s.peekData().data(), s.peekData().size());
            auto u32 = parsed->getObjectField("u32");
            BEAST_EXPECT(
                std::dynamic_pointer_cast<STUInt32>(*u32)->value() ==
                0xABCDEF01);
        }

        // STI_UINT64
        {
            STJson json;
            json.setObjectField(
                "u64",
                std::make_shared<STUInt64>(sfIndexNext, 0x123456789ABCDEF0ull));
            Serializer s;
            json.add(s);
            auto parsed =
                STJson::fromBlob(s.peekData().data(), s.peekData().size());
            auto u64 = parsed->getObjectField("u64");
            BEAST_EXPECT(
                std::dynamic_pointer_cast<STUInt64>(*u64)->value() ==
                0x123456789ABCDEF0ull);
        }

        // STI_UINT160
        {
            STJson json;
            uint160 val;
            val.data()[0] = 0x01;
            val.data()[19] = 0xFF;
            json.setObjectField(
                "u160", std::make_shared<STUInt160>(sfTakerPaysCurrency, val));
            Serializer s;
            json.add(s);
            auto parsed =
                STJson::fromBlob(s.peekData().data(), s.peekData().size());
            auto u160 = parsed->getObjectField("u160");
            BEAST_EXPECT(
                std::dynamic_pointer_cast<STUInt160>(*u160)->value() == val);
        }

        // STI_UINT256
        {
            STJson json;
            uint256 val;
            val.data()[0] = 0xAA;
            val.data()[31] = 0xBB;
            json.setObjectField(
                "u256", std::make_shared<STUInt256>(sfLedgerHash, val));
            Serializer s;
            json.add(s);
            auto parsed =
                STJson::fromBlob(s.peekData().data(), s.peekData().size());
            auto u256 = parsed->getObjectField("u256");
            BEAST_EXPECT(
                std::dynamic_pointer_cast<STUInt256>(*u256)->value() == val);
        }

        // STI_AMOUNT
        {
            STJson json;
            // XRP amount
            STAmount xrp(sfAmount, static_cast<std::int64_t>(123456789u));
            json.setObjectField("amount", std::make_shared<STAmount>(xrp));
            Serializer s;
            json.add(s);
            auto parsed =
                STJson::fromBlob(s.peekData().data(), s.peekData().size());
            auto amount = parsed->getObjectField("amount");
            auto parsedAmt = std::dynamic_pointer_cast<STAmount>(*amount);
            BEAST_EXPECT(parsedAmt->mantissa() == 123456789u);
            BEAST_EXPECT(parsedAmt->issue() == xrp.issue());
        }

        // STI_VL (STBlob)
        {
            STJson json;
            std::vector<uint8_t> blobData = {0xDE, 0xAD, 0xBE, 0xEF};
            json.setObjectField(
                "blob",
                std::make_shared<STBlob>(
                    sfPublicKey, blobData.data(), blobData.size()));
            Serializer s;
            json.add(s);
            auto parsed =
                STJson::fromBlob(s.peekData().data(), s.peekData().size());
            auto blob = parsed->getObjectField("blob");
            auto parsedBlob = std::dynamic_pointer_cast<STBlob>(*blob);
            BEAST_EXPECT(parsedBlob->size() == blobData.size());
            BEAST_EXPECT(
                std::memcmp(
                    parsedBlob->data(), blobData.data(), blobData.size()) == 0);
        }

        // STI_ACCOUNT
        {
            STJson json;
            // Use a known AccountID (20 bytes)
            AccountID acct = AccountID{};
            json.setObjectField(
                "acct", std::make_shared<STAccount>(sfAccount, acct));
            Serializer s;
            json.add(s);
            auto parsed =
                STJson::fromBlob(s.peekData().data(), s.peekData().size());
            auto account = parsed->getObjectField("acct");
            auto parsedAcct = std::dynamic_pointer_cast<STAccount>(*account);
            BEAST_EXPECT(parsedAcct->value() == acct);
        }

        // STI_CURRENCY (STCurrency)
        {
            STJson json;
            Currency cur;
            cur.data()[0] = 0xAA;
            cur.data()[19] = 0xBB;
            json.setObjectField(
                "currency", std::make_shared<STCurrency>(sfGeneric, cur));
            Serializer s;
            json.add(s);
            auto parsed =
                STJson::fromBlob(s.peekData().data(), s.peekData().size());
            auto currency = parsed->getObjectField("currency");
            auto parsedCur = std::dynamic_pointer_cast<STCurrency>(*currency);
            BEAST_EXPECT(parsedCur->value() == cur);
        }

        // STI_JSON (STJson) Nested JSON
        {
            STJson innerJson;
            // XRP amount
            STAmount xrp(sfAmount, static_cast<std::int64_t>(123456789u));
            innerJson.setObjectField("amount", std::make_shared<STAmount>(xrp));

            STJson json;
            json.setObjectField("nested", std::make_shared<STJson>(innerJson));
            Serializer s;
            json.add(s);

            auto parsed =
                STJson::fromBlob(s.peekData().data(), s.peekData().size());
            auto nested = parsed->getObjectField("nested");
            auto parsedNested = std::dynamic_pointer_cast<STJson>(*nested);
            auto amount = parsedNested->getObjectField("amount");
            BEAST_EXPECT(
                std::dynamic_pointer_cast<STAmount>(*amount)->mantissa() ==
                123456789u);
        }
    }

    void
    testEdgeCases()
    {
        testcase("Edge cases");

        // Empty object
        {
            STJson json;
            Serializer s;
            json.add(s);
            auto parsed =
                STJson::fromBlob(s.peekData().data(), s.peekData().size());
            BEAST_EXPECT(parsed->isObject());
            BEAST_EXPECT(parsed->getMap().empty());
        }

        // Empty array
        {
            STJson json(STJson::Array{});
            Serializer s;
            json.add(s);
            auto parsed =
                STJson::fromBlob(s.peekData().data(), s.peekData().size());
            BEAST_EXPECT(parsed->isArray());
            BEAST_EXPECT(parsed->arraySize() == 0);
        }

        // Array with null elements
        {
            STJson json(STJson::Array{});
            json.pushArrayElement(nullptr);
            json.pushArrayElement(std::make_shared<STUInt32>(sfNetworkID, 42));
            json.pushArrayElement(nullptr);

            BEAST_EXPECT(json.arraySize() == 3);

            auto elem0 = json.getArrayElement(0);
            BEAST_EXPECT(elem0.has_value());
            BEAST_EXPECT(*elem0 == nullptr);

            auto elem1 = json.getArrayElement(1);
            BEAST_EXPECT(elem1.has_value());
            BEAST_EXPECT(*elem1 != nullptr);
        }

        // Object with null value
        {
            STJson json;
            json.setObjectField("null_field", nullptr);

            auto val = json.getObjectField("null_field");
            BEAST_EXPECT(val.has_value());
            BEAST_EXPECT(*val == nullptr);
        }
    }

    void
    run() override
    {
        testDefaultConstructor();
        testSetAndGet();
        testMoveConstructor();
        testArrayConstruction();
        testTypeChecking();
        testArrayOperations();
        testArrayAutoResize();
        testArrayElementFields();
        testNestedObjectField();
        testNestedArrayOperations();
        testNestedArrayElementFields();
        testDepthValidation();
        testAddAndFromBlob();
        testArraySerialization();
        testFromSerialIter();
        testFromSField();
        testGetJson();
        testGetJsonArray();
        testMakeValueFromVLWithType();
        testMixedStructures();
        testSTTypes();
        testEdgeCases();
    }
};

BEAST_DEFINE_TESTSUITE(STJson, protocol, xrpl);

}  // namespace xrpl
