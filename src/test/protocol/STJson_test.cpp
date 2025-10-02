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

namespace ripple {

struct STJson_test : public beast::unit_test::suite
{
    void
    testDefaultConstructor()
    {
        testcase("Default constructor");
        STJson json;
        BEAST_EXPECT(json.getMap().empty());
    }

    void
    testSetAndGet()
    {
        testcase("set() and getMap()");
        STJson json;
        auto value = std::make_shared<STUInt32>(sfLedgerIndex, 12345);
        json.set("foo", value);
        auto const& map = json.getMap();
        BEAST_EXPECT(map.size() == 1);
        BEAST_EXPECT(map.at("foo")->getSType() == STI_UINT32);
        BEAST_EXPECT(
            std::dynamic_pointer_cast<STUInt32>(map.at("foo"))->value() ==
            12345);
    }

    void
    testMoveConstructor()
    {
        testcase("Move constructor");
        STJson::Map map;
        map["bar"] = std::make_shared<STUInt16>(sfTransactionType, 42);
        STJson json(std::move(map));
        BEAST_EXPECT(json.getMap().size() == 1);
        BEAST_EXPECT(
            std::dynamic_pointer_cast<STUInt16>(json.getMap().at("bar"))
                ->value() == 42);
    }

    void
    testAddAndFromBlob()
    {
        testcase("add() and fromBlob()");
        STJson json;
        json.set("a", std::make_shared<STUInt8>(sfCloseResolution, 7));
        json.set("b", std::make_shared<STUInt32>(sfNetworkID, 123456));

        Serializer s;
        json.add(s);

        auto blob = s.peekData();
        auto parsed = STJson::fromBlob(blob.data(), blob.size());
        BEAST_EXPECT(parsed->getMap().size() == 2);
        BEAST_EXPECT(
            std::dynamic_pointer_cast<STUInt8>(parsed->getMap().at("a"))
                ->value() == 7);
        BEAST_EXPECT(
            std::dynamic_pointer_cast<STUInt32>(parsed->getMap().at("b"))
                ->value() == 123456);
    }

    void
    testFromSerialIter()
    {
        testcase("fromSerialIter()");
        STJson json;
        json.set("x", std::make_shared<STUInt8>(sfCloseResolution, 99));
        Serializer s;
        json.add(s);

        SerialIter sit(s.peekData().data(), s.peekData().size());
        auto parsed = STJson::fromSerialIter(sit);
        BEAST_EXPECT(parsed->getMap().size() == 1);
        BEAST_EXPECT(
            std::dynamic_pointer_cast<STUInt8>(parsed->getMap().at("x"))
                ->value() == 99);
    }

    void
    testFromSField()
    {
        testcase("fromSField()");
        STJson json;
        json.set("x", std::make_shared<STUInt8>(sfCloseResolution, 99));
        Serializer s;
        json.add(s);

        SerialIter sit(s.peekData().data(), s.peekData().size());
        auto parsed = STJson{sit, sfContractCode};
        BEAST_EXPECT(parsed.getMap().size() == 1);
        BEAST_EXPECT(
            std::dynamic_pointer_cast<STUInt8>(parsed.getMap().at("x"))
                ->value() == 99);
    }

    void
    testGetJson()
    {
        testcase("getJson()");
        STJson json;
        json.set("foo", std::make_shared<STUInt16>(sfTransactionType, 65535));
        json.set("bar", nullptr);  // test null value

        Json::Value jv = json.getJson(JsonOptions::none);
        BEAST_EXPECT(jv.isObject());
        BEAST_EXPECT(jv["foo"].asUInt() == 65535);
        BEAST_EXPECT(jv["bar"].isNull());
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
    testSTTypes()
    {
        testcase("All STypes roundtrip");

        // STI_UINT8
        {
            STJson json;
            json.set("u8", std::make_shared<STUInt8>(sfCloseResolution, 200));
            Serializer s;
            json.add(s);
            auto parsed =
                STJson::fromBlob(s.peekData().data(), s.peekData().size());
            BEAST_EXPECT(
                std::dynamic_pointer_cast<STUInt8>(parsed->getMap().at("u8"))
                    ->value() == 200);
        }

        // STI_UINT16
        {
            STJson json;
            json.set("u16", std::make_shared<STUInt16>(sfSignerWeight, 4242));
            Serializer s;
            json.add(s);
            auto parsed =
                STJson::fromBlob(s.peekData().data(), s.peekData().size());
            BEAST_EXPECT(
                std::dynamic_pointer_cast<STUInt16>(parsed->getMap().at("u16"))
                    ->value() == 4242);
        }

        // STI_UINT32
        {
            STJson json;
            json.set(
                "u32", std::make_shared<STUInt32>(sfNetworkID, 0xABCDEF01));
            Serializer s;
            json.add(s);
            auto parsed =
                STJson::fromBlob(s.peekData().data(), s.peekData().size());
            BEAST_EXPECT(
                std::dynamic_pointer_cast<STUInt32>(parsed->getMap().at("u32"))
                    ->value() == 0xABCDEF01);
        }

        // STI_UINT64
        {
            STJson json;
            json.set(
                "u64",
                std::make_shared<STUInt64>(sfIndexNext, 0x123456789ABCDEF0ull));
            Serializer s;
            json.add(s);
            auto parsed =
                STJson::fromBlob(s.peekData().data(), s.peekData().size());
            BEAST_EXPECT(
                std::dynamic_pointer_cast<STUInt64>(parsed->getMap().at("u64"))
                    ->value() == 0x123456789ABCDEF0ull);
        }

        // STI_UINT128
        // {
        //     STJson json;
        //     uint128 val{};
        //     json.set("u128", std::make_shared<STUInt128>(sfEmailHash, val));
        //     Serializer s;
        //     json.add(s);
        //     auto parsed = STJson::fromBlob(s.peekData().data(),
        //     s.peekData().size());
        //     BEAST_EXPECT(std::dynamic_pointer_cast<STUInt128>(parsed->getMap().at("u128"))->value()
        //     == val);
        // }

        // STI_UINT160
        {
            STJson json;
            uint160 val;
            val.data()[0] = 0x01;
            val.data()[19] = 0xFF;
            json.set(
                "u160", std::make_shared<STUInt160>(sfTakerPaysCurrency, val));
            Serializer s;
            json.add(s);
            auto parsed =
                STJson::fromBlob(s.peekData().data(), s.peekData().size());
            BEAST_EXPECT(
                std::dynamic_pointer_cast<STUInt160>(
                    parsed->getMap().at("u160"))
                    ->value() == val);
        }

        // STI_UINT256
        {
            STJson json;
            uint256 val;
            val.data()[0] = 0xAA;
            val.data()[31] = 0xBB;
            json.set("u256", std::make_shared<STUInt256>(sfLedgerHash, val));
            Serializer s;
            json.add(s);
            auto parsed =
                STJson::fromBlob(s.peekData().data(), s.peekData().size());
            BEAST_EXPECT(
                std::dynamic_pointer_cast<STUInt256>(
                    parsed->getMap().at("u256"))
                    ->value() == val);
        }

        // STI_AMOUNT
        {
            STJson json;
            // XRP amount
            STAmount xrp(sfAmount, static_cast<std::int64_t>(123456789u));
            json.set("amount", std::make_shared<STAmount>(xrp));
            Serializer s;
            json.add(s);
            auto parsed =
                STJson::fromBlob(s.peekData().data(), s.peekData().size());
            auto parsedAmt = std::dynamic_pointer_cast<STAmount>(
                parsed->getMap().at("amount"));
            BEAST_EXPECT(parsedAmt->mantissa() == 123456789u);
            BEAST_EXPECT(parsedAmt->issue() == xrp.issue());
        }

        // STI_VL (STBlob)
        {
            STJson json;
            std::vector<uint8_t> blobData = {0xDE, 0xAD, 0xBE, 0xEF};
            json.set(
                "blob",
                std::make_shared<STBlob>(
                    sfPublicKey, blobData.data(), blobData.size()));
            Serializer s;
            json.add(s);
            auto parsed =
                STJson::fromBlob(s.peekData().data(), s.peekData().size());
            auto parsedBlob =
                std::dynamic_pointer_cast<STBlob>(parsed->getMap().at("blob"));
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
            json.set("acct", std::make_shared<STAccount>(sfAccount, acct));
            Serializer s;
            json.add(s);
            auto parsed =
                STJson::fromBlob(s.peekData().data(), s.peekData().size());
            auto parsedAcct = std::dynamic_pointer_cast<STAccount>(
                parsed->getMap().at("acct"));
            BEAST_EXPECT(parsedAcct->value() == acct);
        }

        // STI_OBJECT (STObject)
        // NOT IMPLEMENTED

        // STI_ARRAY (STArray)
        // NOT IMPLEMENTED

        // STI_PATHSET (STPathSet)
        // NOT IMPLEMENTED

        // STI_VECTOR256 (STVector256)
        // NOT IMPLEMENTED

        // STI_CURRENCY (STCurrency)
        {
            STJson json;
            Currency cur;
            cur.data()[0] = 0xAA;
            cur.data()[19] = 0xBB;
            json.set("currency", std::make_shared<STCurrency>(sfGeneric, cur));
            Serializer s;
            json.add(s);
            auto parsed =
                STJson::fromBlob(s.peekData().data(), s.peekData().size());
            auto parsedCur = std::dynamic_pointer_cast<STCurrency>(
                parsed->getMap().at("currency"));
            BEAST_EXPECT(parsedCur->value() == cur);
        }

        // STI_JSON (STJson) Nested JSON
        {
            STJson innerJson;
            // XRP amount
            STAmount xrp(sfAmount, static_cast<std::int64_t>(123456789u));
            innerJson.set("amount", std::make_shared<STAmount>(xrp));

            STJson json;
            json.set("nested", std::make_shared<STJson>(innerJson));
            Serializer s;
            json.add(s);

            auto parsed =
                STJson::fromBlob(s.peekData().data(), s.peekData().size());
            auto parsedNested = std::dynamic_pointer_cast<STJson>(
                parsed->getMap().at("nested"));
            BEAST_EXPECT(
                std::dynamic_pointer_cast<STAmount>(
                    parsedNested->getMap().at("amount"))
                    ->mantissa() == 123456789u);
        }
    }

    void
    run() override
    {
        testDefaultConstructor();
        testSetAndGet();
        testMoveConstructor();
        testAddAndFromBlob();
        testFromSerialIter();
        testFromSField();
        testGetJson();
        testMakeValueFromVLWithType();
        testSTTypes();
        // TODO: test 0 STAmount
    }
};

BEAST_DEFINE_TESTSUITE(STJson, protocol, ripple);

}  // namespace ripple
