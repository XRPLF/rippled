#include <xrpl/basics/Blob.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/STDataType.h>
#include <xrpl/protocol/STInteger.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/jss.h>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <stdexcept>
#include <string>
#include <vector>

namespace xrpl {
struct STDataType_test : public beast::unit_test::Suite
{
    void
    testConstructors()
    {
        testcase("constructors");

        auto const& sf = sfParameterType;

        // Test default constructor
        {
            STDataType const dt1(sf);
            BEAST_EXPECT(dt1.getInnerSType() == STI_NOTPRESENT);
            BEAST_EXPECT(dt1.getSType() == STI_DATATYPE);
            BEAST_EXPECT(dt1.getFName() == sf);
        }

        // Test constructor with SerializedTypeID
        {
            STDataType const dt2(sf, STI_UINT32);
            BEAST_EXPECT(dt2.getInnerSType() == STI_UINT32);
            BEAST_EXPECT(!dt2.isDefault());
        }

        // Test deserialization constructor
        {
            Serializer s;
            s.add16(STI_UINT64);
            SerialIter sit(s.slice());
            STDataType const dt3(sit, sf);
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
            STDataType const original(sf, STI_UINT32);

            // Use aligned storage for placement new
            alignas(STDataType) char buffer[sizeof(STDataType)];
            STBase* copied = original.copy(sizeof(buffer), buffer);

            BEAST_EXPECT(copied != nullptr);
            auto* dtCopy = dynamic_cast<STDataType*>(copied);
            BEAST_EXPECT(dtCopy != nullptr);
            BEAST_EXPECT(dtCopy->getInnerSType() == STI_UINT32);
            BEAST_EXPECT(dtCopy->getFName() == sf);

            // Clean up
            dtCopy->~STDataType();
        }

        // Test move
        {
            STDataType original(sf, STI_UINT64);

            alignas(STDataType) char buffer[sizeof(STDataType)];
            STBase* moved = original.move(sizeof(buffer), buffer);

            BEAST_EXPECT(moved != nullptr);
            auto* dtMoved = dynamic_cast<STDataType*>(moved);
            BEAST_EXPECT(dtMoved != nullptr);
            BEAST_EXPECT(dtMoved->getInnerSType() == STI_UINT64);
            BEAST_EXPECT(dtMoved->getFName() == sf);

            // Clean up
            dtMoved->~STDataType();
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

        TypeTest const tests[] = {
            {.type = STI_UINT16, .expectedHex = "0001"},
            {.type = STI_UINT32, .expectedHex = "0002"},
            {.type = STI_UINT64, .expectedHex = "0003"},
            {.type = STI_UINT128, .expectedHex = "0004"},
            {.type = STI_UINT256, .expectedHex = "0005"},
            {.type = STI_AMOUNT, .expectedHex = "0006"},
            {.type = STI_VL, .expectedHex = "0007"},
            {.type = STI_ACCOUNT, .expectedHex = "0008"},
            {.type = STI_UINT8, .expectedHex = "0010"},
            {.type = STI_UINT160, .expectedHex = "0011"},
            {.type = STI_PATHSET, .expectedHex = "0012"},
            {.type = STI_VECTOR256, .expectedHex = "0013"},
            {.type = STI_OBJECT, .expectedHex = "000E"},
            {.type = STI_ARRAY, .expectedHex = "000F"},
            {.type = STI_ISSUE, .expectedHex = "0018"},
            {.type = STI_XCHAIN_BRIDGE, .expectedHex = "0019"},
            {.type = STI_CURRENCY, .expectedHex = "001A"},
            {.type = STI_UINT192, .expectedHex = "0015"},
            {.type = STI_NUMBER, .expectedHex = "0009"},
        };

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
            STDataType const dt1(sf1, STI_UINT32);
            STDataType const dt2(sf1, STI_UINT32);
            BEAST_EXPECT(dt1.isEquivalent(dt2));
        }

        // Test non-equivalent objects (different inner types)
        {
            STDataType const dt1(sf1, STI_UINT32);
            STDataType const dt2(sf1, STI_UINT64);
            BEAST_EXPECT(!dt1.isEquivalent(dt2));
        }

        // Test non-equivalent objects (different default states)
        {
            STDataType const dt1(sf1);
            STDataType const dt2(sf1, STI_NOTPRESENT);
            // dt1 has default_ = true (implicit from first constructor)
            // dt2 has default_ = false (set in second constructor)
            BEAST_EXPECT(!dt1.isEquivalent(dt2));
        }

        // Test with non-STDataType object
        {
            STDataType const dt1(sf1, STI_UINT32);
            // Create a dummy STBase-derived object for comparison
            // Since we can't easily create other STBase types here,
            // we'll test that isEquivalent returns false for nullptr cast
            struct DummySTBase : public STBase
            {
                DummySTBase() : STBase(sfInvalid)
                {
                }
                [[nodiscard]] SerializedTypeID
                getSType() const override
                {
                    return STI_NOTPRESENT;
                }
                void
                add(Serializer&) const override
                {
                }
                [[nodiscard]] bool
                isEquivalent(STBase const&) const override
                {
                    return false;
                }
                [[nodiscard]] bool
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
            DummySTBase const dummy;
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
            STDataType const dt1(sf);
            // First constructor doesn't set default_ explicitly,
            // so it should be true (member initialization)
            BEAST_EXPECT(dt1.isDefault());
        }

        {
            STDataType const dt2(sf, STI_UINT32);
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
            STDataType const dt(sf, STI_UINT8);
            BEAST_EXPECT(dt.getText() == "STDataType{InnerType: UINT8}");
        }

        {
            STDataType const dt(sf, STI_UINT16);
            BEAST_EXPECT(dt.getText() == "STDataType{InnerType: UINT16}");
        }

        {
            STDataType const dt(sf, STI_UINT32);
            BEAST_EXPECT(dt.getText() == "STDataType{InnerType: UINT32}");
        }

        {
            STDataType const dt(sf, STI_UINT64);
            BEAST_EXPECT(dt.getText() == "STDataType{InnerType: UINT64}");
        }

        {
            STDataType const dt(sf, STI_UINT128);
            BEAST_EXPECT(dt.getText() == "STDataType{InnerType: UINT128}");
        }

        {
            STDataType const dt(sf, STI_UINT160);
            BEAST_EXPECT(dt.getText() == "STDataType{InnerType: UINT160}");
        }

        {
            STDataType const dt(sf, STI_UINT192);
            BEAST_EXPECT(dt.getText() == "STDataType{InnerType: UINT192}");
        }

        {
            STDataType const dt(sf, STI_UINT256);
            BEAST_EXPECT(dt.getText() == "STDataType{InnerType: UINT256}");
        }

        {
            STDataType const dt(sf, STI_VL);
            BEAST_EXPECT(dt.getText() == "STDataType{InnerType: VL}");
        }

        {
            STDataType const dt(sf, STI_ACCOUNT);
            BEAST_EXPECT(dt.getText() == "STDataType{InnerType: ACCOUNT}");
        }

        {
            STDataType const dt(sf, STI_AMOUNT);
            BEAST_EXPECT(dt.getText() == "STDataType{InnerType: AMOUNT}");
        }

        {
            STDataType const dt(sf, STI_ISSUE);
            BEAST_EXPECT(dt.getText() == "STDataType{InnerType: ISSUE}");
        }

        {
            STDataType const dt(sf, STI_CURRENCY);
            BEAST_EXPECT(dt.getText() == "STDataType{InnerType: CURRENCY}");
        }

        {
            STDataType const dt(sf, STI_NUMBER);
            BEAST_EXPECT(dt.getText() == "STDataType{InnerType: NUMBER}");
        }

        // Test unknown type (should return numeric string)
        {
            STDataType const dt(sf, static_cast<SerializedTypeID>(999));
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
            STDataType const dt(sf, STI_UINT32);
            json::Value json = dt.getJson(JsonOptions::Values::None);
            BEAST_EXPECT(json.isObject());
            BEAST_EXPECT(json[jss::type].asString() == "UINT32");
        }

        {
            STDataType const dt(sf, STI_AMOUNT);
            json::Value json = dt.getJson(JsonOptions::Values::None);
            BEAST_EXPECT(json.isObject());
            BEAST_EXPECT(json[jss::type].asString() == "AMOUNT");
        }

        {
            STDataType const dt(sf, STI_ACCOUNT);
            json::Value json = dt.getJson(JsonOptions::Values::None);
            BEAST_EXPECT(json.isObject());
            BEAST_EXPECT(json[jss::type].asString() == "ACCOUNT");
        }

        // Test unknown type
        {
            STDataType const dt(sf, static_cast<SerializedTypeID>(999));
            json::Value json = dt.getJson(JsonOptions::Values::None);
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
            json::Value v;
            v[jss::type] = "UINT8";
            STDataType const dt = dataTypeFromJson(sf, v);
            BEAST_EXPECT(dt.getInnerSType() == STI_UINT8);
        }

        {
            json::Value v;
            v[jss::type] = "UINT16";
            STDataType const dt = dataTypeFromJson(sf, v);
            BEAST_EXPECT(dt.getInnerSType() == STI_UINT16);
        }

        {
            json::Value v;
            v[jss::type] = "UINT32";
            STDataType const dt = dataTypeFromJson(sf, v);
            BEAST_EXPECT(dt.getInnerSType() == STI_UINT32);
        }

        {
            json::Value v;
            v[jss::type] = "UINT64";
            STDataType const dt = dataTypeFromJson(sf, v);
            BEAST_EXPECT(dt.getInnerSType() == STI_UINT64);
        }

        {
            json::Value v;
            v[jss::type] = "UINT128";
            STDataType const dt = dataTypeFromJson(sf, v);
            BEAST_EXPECT(dt.getInnerSType() == STI_UINT128);
        }

        {
            json::Value v;
            v[jss::type] = "UINT160";
            STDataType const dt = dataTypeFromJson(sf, v);
            BEAST_EXPECT(dt.getInnerSType() == STI_UINT160);
        }

        {
            json::Value v;
            v[jss::type] = "UINT192";
            STDataType const dt = dataTypeFromJson(sf, v);
            BEAST_EXPECT(dt.getInnerSType() == STI_UINT192);
        }

        {
            json::Value v;
            v[jss::type] = "UINT256";
            STDataType const dt = dataTypeFromJson(sf, v);
            BEAST_EXPECT(dt.getInnerSType() == STI_UINT256);
        }

        {
            json::Value v;
            v[jss::type] = "VL";
            STDataType const dt = dataTypeFromJson(sf, v);
            BEAST_EXPECT(dt.getInnerSType() == STI_VL);
        }

        {
            json::Value v;
            v[jss::type] = "ACCOUNT";
            STDataType const dt = dataTypeFromJson(sf, v);
            BEAST_EXPECT(dt.getInnerSType() == STI_ACCOUNT);
        }

        {
            json::Value v;
            v[jss::type] = "AMOUNT";
            STDataType const dt = dataTypeFromJson(sf, v);
            BEAST_EXPECT(dt.getInnerSType() == STI_AMOUNT);
        }

        {
            json::Value v;
            v[jss::type] = "ISSUE";
            STDataType const dt = dataTypeFromJson(sf, v);
            BEAST_EXPECT(dt.getInnerSType() == STI_ISSUE);
        }

        {
            json::Value v;
            v[jss::type] = "CURRENCY";
            STDataType const dt = dataTypeFromJson(sf, v);
            BEAST_EXPECT(dt.getInnerSType() == STI_CURRENCY);
        }

        {
            json::Value v;
            v[jss::type] = "NUMBER";
            STDataType const dt = dataTypeFromJson(sf, v);
            BEAST_EXPECT(dt.getInnerSType() == STI_NUMBER);
        }

        // Test error cases

        // Non-object JSON should throw
        {
            json::Value const v = "not an object";
            try
            {
                STDataType const dt = dataTypeFromJson(sf, v);
                BEAST_EXPECT(false);  // Should not reach here
            }
            catch (std::runtime_error const& e)
            {
                BEAST_EXPECT(std::string(e.what()) == "STData: expected object");
            }
        }

        // Unknown type string should throw
        {
            json::Value v;
            v[jss::type] = "UNKNOWN_TYPE";
            try
            {
                STDataType const dt = dataTypeFromJson(sf, v);
                BEAST_EXPECT(false);  // Should not reach here
            }
            catch (std::runtime_error const& e)
            {
                BEAST_EXPECT(
                    std::string(e.what()) == "STData: unsupported type string: UNKNOWN_TYPE");
            }
        }

        // Empty type string should throw
        {
            json::Value v;
            v[jss::type] = "";
            try
            {
                STDataType const dt = dataTypeFromJson(sf, v);
                BEAST_EXPECT(false);  // Should not reach here
            }
            catch (std::runtime_error const& e)
            {
                BEAST_EXPECT(std::string(e.what()) == "STData: unsupported type string: ");
            }
        }
    }

    void
    testRoundTrip()
    {
        testcase("round trip serialization");

        auto const& sf = sfParameterType;

        // Test serialization and deserialization round trip
        for (auto const typeId : {
                 STI_UINT8,
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
                 STI_NUMBER,
             })
        {
            // Create original
            STDataType const original(sf, typeId);

            // Serialize
            Serializer s;
            original.add(s);

            // Deserialize
            SerialIter sit(s.slice());
            STDataType const deserialized(sit, sf);

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

        std::vector<std::string> const typeStrings = {
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
            "NUMBER",
        };

        for (auto const& typeStr : typeStrings)
        {
            // Create from JSON
            json::Value input;
            input[jss::type] = typeStr;
            STDataType const dt = dataTypeFromJson(sf, input);

            // Convert back to JSON
            json::Value output = dt.getJson(JsonOptions::Values::None);

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

        TypeStringTest const tests[] = {
            {.type = STI_UINT8, .expected = "UINT8"},
            {.type = STI_UINT16, .expected = "UINT16"},
            {.type = STI_UINT32, .expected = "UINT32"},
            {.type = STI_UINT64, .expected = "UINT64"},
            {.type = STI_UINT128, .expected = "UINT128"},
            {.type = STI_UINT160, .expected = "UINT160"},
            {.type = STI_UINT192, .expected = "UINT192"},
            {.type = STI_UINT256, .expected = "UINT256"},
            {.type = STI_VL, .expected = "VL"},
            {.type = STI_ACCOUNT, .expected = "ACCOUNT"},
            {.type = STI_AMOUNT, .expected = "AMOUNT"},
            {.type = STI_ISSUE, .expected = "ISSUE"},
            {.type = STI_CURRENCY, .expected = "CURRENCY"},
            {.type = STI_NUMBER, .expected = "NUMBER"},
            {.type = static_cast<SerializedTypeID>(999), .expected = "999"},  // Unknown type
        };

        for (auto const& test : tests)
        {
            STDataType const dt(sf, test.type);
            BEAST_EXPECT(dt.getInnerTypeString() == test.expected);
        }
    }

    void
    testMalformedBytes()
    {
        testcase("malformed bytes are refused");

        auto const refused = [](Blob const& bytes) {
            try
            {
                SerialIter sit(makeSlice(bytes));
                STDataType const type(sit, sfParameterType);
                return false;
            }
            catch (std::exception const&)
            {
                return true;
            }
        };

        // No bytes, or half a type code.
        BEAST_EXPECT(refused({}));
        BEAST_EXPECT(refused({0x00}));

        // A type code that names no type an STData holds.
        for (std::uint16_t const type : {
                 static_cast<std::uint16_t>(STI_NOTPRESENT),
                 static_cast<std::uint16_t>(STI_OBJECT),
                 static_cast<std::uint16_t>(STI_ARRAY),
                 static_cast<std::uint16_t>(STI_PATHSET),
                 static_cast<std::uint16_t>(STI_VECTOR256),
                 static_cast<std::uint16_t>(STI_DATA),
                 static_cast<std::uint16_t>(STI_DATATYPE),
                 static_cast<std::uint16_t>(STI_JSON),
                 std::uint16_t{0xFFFF},
             })
        {
            BEAST_EXPECTS(
                refused({static_cast<std::uint8_t>(type >> 8), static_cast<std::uint8_t>(type)}),
                std::to_string(type));
        }

        // An object holding a ParameterType of an unknown type is refused with it.
        {
            Serializer s;
            s.addFieldID(STI_DATATYPE, sfParameterType.fieldValue);
            s.add16(0xFFFF);
            SerialIter sit(s.slice());
            try
            {
                STObject const object(sit, sfGeneric);
                fail("an unknown data type was accepted");
            }
            catch (std::exception const& e)
            {
                BEAST_EXPECT(std::string(e.what()) == "STDataType: unknown type");
            }
        }

        // An STDataType is not equivalent to a value of another class.
        BEAST_EXPECT(!STDataType(sfParameterType, STI_UINT8)
                          .isEquivalent(STUInt16(sfSignerWeight, STI_UINT8)));
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
        testMalformedBytes();
    }
};

BEAST_DEFINE_TESTSUITE(STDataType, protocol, xrpl);

}  // namespace xrpl
