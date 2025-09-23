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
#include <xrpl/protocol/STDataType.h>
#include <xrpl/protocol/jss.h>

namespace ripple {
struct STDataType_test : public beast::unit_test::suite
{
    void
    testConstructors()
    {
        testcase("constructors");

        auto const& sf = sfParameterType;

        // Test default constructor
        {
            STDataType dt1(sf);
            BEAST_EXPECT(dt1.getInnerSType() == STI_NOTPRESENT);
            BEAST_EXPECT(dt1.getSType() == STI_DATATYPE);
            BEAST_EXPECT(dt1.getFName() == sf);
        }

        // Test constructor with SerializedTypeID
        {
            STDataType dt2(sf, STI_UINT32);
            BEAST_EXPECT(dt2.getInnerSType() == STI_UINT32);
            BEAST_EXPECT(!dt2.isDefault());
        }

        // Test deserialization constructor
        {
            Serializer s;
            s.add16(STI_UINT64);
            SerialIter sit(s.slice());
            STDataType dt3(sit, sf);
            BEAST_EXPECT(dt3.getInnerSType() == STI_UINT64);
        }
    }

    void
    testCopyMove()
    {
        testcase("copy and move");

        auto const& sf = sfParameterType;

        // Test copy
        {
            STDataType original(sf, STI_UINT32);

            // Use aligned storage for placement new
            alignas(STDataType) char buffer[sizeof(STDataType)];
            STBase* copied = original.copy(sizeof(buffer), buffer);

            BEAST_EXPECT(copied != nullptr);
            auto* dt_copy = dynamic_cast<STDataType*>(copied);
            BEAST_EXPECT(dt_copy != nullptr);
            BEAST_EXPECT(dt_copy->getInnerSType() == STI_UINT32);
            BEAST_EXPECT(dt_copy->getFName() == sf);

            // Clean up
            dt_copy->~STDataType();
        }

        // Test move
        {
            STDataType original(sf, STI_UINT64);

            alignas(STDataType) char buffer[sizeof(STDataType)];
            STBase* moved = original.move(sizeof(buffer), buffer);

            BEAST_EXPECT(moved != nullptr);
            auto* dt_moved = dynamic_cast<STDataType*>(moved);
            BEAST_EXPECT(dt_moved != nullptr);
            BEAST_EXPECT(dt_moved->getInnerSType() == STI_UINT64);
            BEAST_EXPECT(dt_moved->getFName() == sf);

            // Clean up
            dt_moved->~STDataType();
        }
    }

    void
    testSerialization()
    {
        testcase("serialization");

        auto const& sf = sfParameterType;

        // Test all type serializations
        struct TypeTest
        {
            SerializedTypeID type;
            std::string expectedHex;
        };

        TypeTest tests[] = {
            {STI_UINT16, "0001"},
            {STI_UINT32, "0002"},
            {STI_UINT64, "0003"},
            {STI_UINT128, "0004"},
            {STI_UINT256, "0005"},
            {STI_AMOUNT, "0006"},
            {STI_VL, "0007"},
            {STI_ACCOUNT, "0008"},
            {STI_UINT8, "0010"},
            {STI_UINT160, "0011"},
            {STI_PATHSET, "0012"},
            {STI_VECTOR256, "0013"},
            {STI_OBJECT, "000E"},
            {STI_ARRAY, "000F"},
            {STI_ISSUE, "0018"},
            {STI_XCHAIN_BRIDGE, "0019"},
            {STI_CURRENCY, "001A"},
            {STI_UINT192, "0015"},
            {STI_NUMBER, "0009"}};

        for (auto const& test : tests)
        {
            Serializer s;
            STDataType dt(sf);
            dt.setInnerSType(test.type);
            BEAST_EXPECT(dt.getInnerSType() == test.type);
            dt.add(s);
            BEAST_EXPECT(strHex(s) == test.expectedHex);
        }
    }

    void
    testEquivalence()
    {
        testcase("equivalence");

        auto const& sf1 = sfParameterType;

        // Test equivalent objects
        {
            STDataType dt1(sf1, STI_UINT32);
            STDataType dt2(sf1, STI_UINT32);
            BEAST_EXPECT(dt1.isEquivalent(dt2));
        }

        // Test non-equivalent objects (different inner types)
        {
            STDataType dt1(sf1, STI_UINT32);
            STDataType dt2(sf1, STI_UINT64);
            BEAST_EXPECT(!dt1.isEquivalent(dt2));
        }

        // Test non-equivalent objects (different default states)
        {
            STDataType dt1(sf1);
            STDataType dt2(sf1, STI_NOTPRESENT);
            // dt1 has default_ = true (implicit from first constructor)
            // dt2 has default_ = false (set in second constructor)
            BEAST_EXPECT(!dt1.isEquivalent(dt2));
        }

        // Test with non-STDataType object
        {
            STDataType dt1(sf1, STI_UINT32);
            // Create a dummy STBase-derived object for comparison
            // Since we can't easily create other STBase types here,
            // we'll test that isEquivalent returns false for nullptr cast
            struct DummySTBase : public STBase
            {
                DummySTBase() : STBase(sfInvalid)
                {
                }
                SerializedTypeID
                getSType() const override
                {
                    return STI_NOTPRESENT;
                }
                void
                add(Serializer&) const override
                {
                }
                bool
                isEquivalent(STBase const&) const override
                {
                    return false;
                }
                bool
                isDefault() const override
                {
                    return true;
                }
                STBase*
                copy(std::size_t, void*) const override
                {
                    return nullptr;
                }
                STBase*
                move(std::size_t, void*) override
                {
                    return nullptr;
                }
            };
            DummySTBase dummy;
            BEAST_EXPECT(!dt1.isEquivalent(dummy));
        }
    }

    void
    testDefault()
    {
        testcase("isDefault");

        auto const& sf = sfParameterType;

        // Test default state
        {
            STDataType dt1(sf);
            // First constructor doesn't set default_ explicitly,
            // so it should be true (member initialization)
            BEAST_EXPECT(dt1.isDefault());
        }

        {
            STDataType dt2(sf, STI_UINT32);
            BEAST_EXPECT(!dt2.isDefault());
        }
    }

    void
    testGetText()
    {
        testcase("getText");

        auto const& sf = sfParameterType;

        // Test known types
        {
            STDataType dt(sf, STI_UINT8);
            BEAST_EXPECT(dt.getText() == "STDataType{InnerType: UINT8}");
        }

        {
            STDataType dt(sf, STI_UINT16);
            BEAST_EXPECT(dt.getText() == "STDataType{InnerType: UINT16}");
        }

        {
            STDataType dt(sf, STI_UINT32);
            BEAST_EXPECT(dt.getText() == "STDataType{InnerType: UINT32}");
        }

        {
            STDataType dt(sf, STI_UINT64);
            BEAST_EXPECT(dt.getText() == "STDataType{InnerType: UINT64}");
        }

        {
            STDataType dt(sf, STI_UINT128);
            BEAST_EXPECT(dt.getText() == "STDataType{InnerType: UINT128}");
        }

        {
            STDataType dt(sf, STI_UINT160);
            BEAST_EXPECT(dt.getText() == "STDataType{InnerType: UINT160}");
        }

        {
            STDataType dt(sf, STI_UINT192);
            BEAST_EXPECT(dt.getText() == "STDataType{InnerType: UINT192}");
        }

        {
            STDataType dt(sf, STI_UINT256);
            BEAST_EXPECT(dt.getText() == "STDataType{InnerType: UINT256}");
        }

        {
            STDataType dt(sf, STI_VL);
            BEAST_EXPECT(dt.getText() == "STDataType{InnerType: VL}");
        }

        {
            STDataType dt(sf, STI_ACCOUNT);
            BEAST_EXPECT(dt.getText() == "STDataType{InnerType: ACCOUNT}");
        }

        {
            STDataType dt(sf, STI_AMOUNT);
            BEAST_EXPECT(dt.getText() == "STDataType{InnerType: AMOUNT}");
        }

        {
            STDataType dt(sf, STI_ISSUE);
            BEAST_EXPECT(dt.getText() == "STDataType{InnerType: ISSUE}");
        }

        {
            STDataType dt(sf, STI_CURRENCY);
            BEAST_EXPECT(dt.getText() == "STDataType{InnerType: CURRENCY}");
        }

        {
            STDataType dt(sf, STI_NUMBER);
            BEAST_EXPECT(dt.getText() == "STDataType{InnerType: NUMBER}");
        }

        // Test unknown type (should return numeric string)
        {
            STDataType dt(sf, static_cast<SerializedTypeID>(999));
            BEAST_EXPECT(dt.getText() == "STDataType{InnerType: 999}");
        }
    }

    void
    testGetJson()
    {
        testcase("getJson");

        auto const& sf = sfParameterType;

        // Test JSON output for various types
        {
            STDataType dt(sf, STI_UINT32);
            Json::Value json = dt.getJson(JsonOptions::none);
            BEAST_EXPECT(json.isObject());
            BEAST_EXPECT(json[jss::type].asString() == "UINT32");
        }

        {
            STDataType dt(sf, STI_AMOUNT);
            Json::Value json = dt.getJson(JsonOptions::none);
            BEAST_EXPECT(json.isObject());
            BEAST_EXPECT(json[jss::type].asString() == "AMOUNT");
        }

        {
            STDataType dt(sf, STI_ACCOUNT);
            Json::Value json = dt.getJson(JsonOptions::none);
            BEAST_EXPECT(json.isObject());
            BEAST_EXPECT(json[jss::type].asString() == "ACCOUNT");
        }

        // Test unknown type
        {
            STDataType dt(sf, static_cast<SerializedTypeID>(999));
            Json::Value json = dt.getJson(JsonOptions::none);
            BEAST_EXPECT(json.isObject());
            BEAST_EXPECT(json[jss::type].asString() == "999");
        }
    }

    void
    testDataTypeFromJson()
    {
        testcase("dataTypeFromJson");

        auto const& sf = sfParameterType;

        // Test all valid type strings
        {
            Json::Value v;
            v[jss::type] = "UINT8";
            STDataType dt = dataTypeFromJson(sf, v);
            BEAST_EXPECT(dt.getInnerSType() == STI_UINT8);
        }

        {
            Json::Value v;
            v[jss::type] = "UINT16";
            STDataType dt = dataTypeFromJson(sf, v);
            BEAST_EXPECT(dt.getInnerSType() == STI_UINT16);
        }

        {
            Json::Value v;
            v[jss::type] = "UINT32";
            STDataType dt = dataTypeFromJson(sf, v);
            BEAST_EXPECT(dt.getInnerSType() == STI_UINT32);
        }

        {
            Json::Value v;
            v[jss::type] = "UINT64";
            STDataType dt = dataTypeFromJson(sf, v);
            BEAST_EXPECT(dt.getInnerSType() == STI_UINT64);
        }

        {
            Json::Value v;
            v[jss::type] = "UINT128";
            STDataType dt = dataTypeFromJson(sf, v);
            BEAST_EXPECT(dt.getInnerSType() == STI_UINT128);
        }

        {
            Json::Value v;
            v[jss::type] = "UINT160";
            STDataType dt = dataTypeFromJson(sf, v);
            BEAST_EXPECT(dt.getInnerSType() == STI_UINT160);
        }

        {
            Json::Value v;
            v[jss::type] = "UINT192";
            STDataType dt = dataTypeFromJson(sf, v);
            BEAST_EXPECT(dt.getInnerSType() == STI_UINT192);
        }

        {
            Json::Value v;
            v[jss::type] = "UINT256";
            STDataType dt = dataTypeFromJson(sf, v);
            BEAST_EXPECT(dt.getInnerSType() == STI_UINT256);
        }

        {
            Json::Value v;
            v[jss::type] = "VL";
            STDataType dt = dataTypeFromJson(sf, v);
            BEAST_EXPECT(dt.getInnerSType() == STI_VL);
        }

        {
            Json::Value v;
            v[jss::type] = "ACCOUNT";
            STDataType dt = dataTypeFromJson(sf, v);
            BEAST_EXPECT(dt.getInnerSType() == STI_ACCOUNT);
        }

        {
            Json::Value v;
            v[jss::type] = "AMOUNT";
            STDataType dt = dataTypeFromJson(sf, v);
            BEAST_EXPECT(dt.getInnerSType() == STI_AMOUNT);
        }

        {
            Json::Value v;
            v[jss::type] = "ISSUE";
            STDataType dt = dataTypeFromJson(sf, v);
            BEAST_EXPECT(dt.getInnerSType() == STI_ISSUE);
        }

        {
            Json::Value v;
            v[jss::type] = "CURRENCY";
            STDataType dt = dataTypeFromJson(sf, v);
            BEAST_EXPECT(dt.getInnerSType() == STI_CURRENCY);
        }

        {
            Json::Value v;
            v[jss::type] = "NUMBER";
            STDataType dt = dataTypeFromJson(sf, v);
            BEAST_EXPECT(dt.getInnerSType() == STI_NUMBER);
        }

        // Test error cases

        // Non-object JSON should throw
        {
            Json::Value v = "not an object";
            try
            {
                STDataType dt = dataTypeFromJson(sf, v);
                BEAST_EXPECT(false);  // Should not reach here
            }
            catch (std::runtime_error const& e)
            {
                BEAST_EXPECT(
                    std::string(e.what()) == "STData: expected object");
            }
        }

        // Unknown type string should throw
        {
            Json::Value v;
            v[jss::type] = "UNKNOWN_TYPE";
            try
            {
                STDataType dt = dataTypeFromJson(sf, v);
                BEAST_EXPECT(false);  // Should not reach here
            }
            catch (std::runtime_error const& e)
            {
                BEAST_EXPECT(
                    std::string(e.what()) ==
                    "STData: unsupported type string: UNKNOWN_TYPE");
            }
        }

        // Empty type string should throw
        {
            Json::Value v;
            v[jss::type] = "";
            try
            {
                STDataType dt = dataTypeFromJson(sf, v);
                BEAST_EXPECT(false);  // Should not reach here
            }
            catch (std::runtime_error const& e)
            {
                BEAST_EXPECT(
                    std::string(e.what()) ==
                    "STData: unsupported type string: ");
            }
        }
    }

    void
    testRoundTrip()
    {
        testcase("round trip serialization");

        auto const& sf = sfParameterType;

        // Test serialization and deserialization round trip
        for (auto typeId :
             {STI_UINT8,
              STI_UINT16,
              STI_UINT32,
              STI_UINT64,
              STI_UINT128,
              STI_UINT160,
              STI_UINT192,
              STI_UINT256,
              STI_VL,
              STI_ACCOUNT,
              STI_AMOUNT,
              STI_ISSUE,
              STI_CURRENCY,
              STI_NUMBER})
        {
            // Create original
            STDataType original(sf, typeId);

            // Serialize
            Serializer s;
            original.add(s);

            // Deserialize
            SerialIter sit(s.slice());
            STDataType deserialized(sit, sf);

            // Compare
            BEAST_EXPECT(deserialized.getInnerSType() == typeId);
            BEAST_EXPECT(original.isEquivalent(deserialized));
        }
    }

    void
    testJsonRoundTrip()
    {
        testcase("JSON round trip");

        auto const& sf = sfParameterType;

        std::vector<std::string> typeStrings = {
            "UINT8",
            "UINT16",
            "UINT32",
            "UINT64",
            "UINT128",
            "UINT160",
            "UINT192",
            "UINT256",
            "VL",
            "ACCOUNT",
            "AMOUNT",
            "ISSUE",
            "CURRENCY",
            "NUMBER"};

        for (auto const& typeStr : typeStrings)
        {
            // Create from JSON
            Json::Value input;
            input[jss::type] = typeStr;
            STDataType dt = dataTypeFromJson(sf, input);

            // Convert back to JSON
            Json::Value output = dt.getJson(JsonOptions::none);

            // Verify
            BEAST_EXPECT(output[jss::type].asString() == typeStr);
        }
    }

    void
    testGetInnerTypeString()
    {
        testcase("getInnerTypeString");

        auto const& sf = sfParameterType;

        struct TypeStringTest
        {
            SerializedTypeID type;
            std::string expected;
        };

        TypeStringTest tests[] = {
            {STI_UINT8, "UINT8"},
            {STI_UINT16, "UINT16"},
            {STI_UINT32, "UINT32"},
            {STI_UINT64, "UINT64"},
            {STI_UINT128, "UINT128"},
            {STI_UINT160, "UINT160"},
            {STI_UINT192, "UINT192"},
            {STI_UINT256, "UINT256"},
            {STI_VL, "VL"},
            {STI_ACCOUNT, "ACCOUNT"},
            {STI_AMOUNT, "AMOUNT"},
            {STI_ISSUE, "ISSUE"},
            {STI_CURRENCY, "CURRENCY"},
            {STI_NUMBER, "NUMBER"},
            {static_cast<SerializedTypeID>(999), "999"}  // Unknown type
        };

        for (auto const& test : tests)
        {
            STDataType dt(sf, test.type);
            BEAST_EXPECT(dt.getInnerTypeString() == test.expected);
        }
    }

    void
    run() override
    {
        testConstructors();
        testCopyMove();
        testSerialization();
        testEquivalence();
        testDefault();
        testGetText();
        testGetJson();
        testDataTypeFromJson();
        testRoundTrip();
        testJsonRoundTrip();
        testGetInnerTypeString();
    }
};

BEAST_DEFINE_TESTSUITE(STDataType, protocol, ripple);

}  // namespace ripple
