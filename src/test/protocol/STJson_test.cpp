#include <xrpl/basics/Blob.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_forwards.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAccount.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STBitString.h>
#include <xrpl/protocol/STBlob.h>
#include <xrpl/protocol/STCurrency.h>
#include <xrpl/protocol/STInteger.h>
#include <xrpl/protocol/STIssue.h>
#include <xrpl/protocol/STJson.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/XRPAmount.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <initializer_list>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace xrpl {

struct STJson_test : public beast::unit_test::Suite
{
    void
    testDefaultConstructor()
    {
        testcase("Default constructor");
        STJson const json;
        BEAST_EXPECT(json.isObject());
        BEAST_EXPECT(!json.isArray());
        BEAST_EXPECT(json.getMap().empty());
    }

    void
    testSetAndGet()
    {
        testcase("setObjectField() and getObjectField()");
        STJson json;
        auto const value = std::make_shared<STUInt32>(sfLedgerIndex, 12345);
        json.setObjectField("foo", value);

        auto retrieved = json.getObjectField("foo");
        BEAST_EXPECT(retrieved.has_value());
        if (!retrieved)
            return;
        BEAST_EXPECT((*retrieved)->getSType() == STI_UINT32);
        BEAST_EXPECT(std::dynamic_pointer_cast<STUInt32>(*retrieved)->value() == 12345);

        // Test non-existent key
        auto const missing = json.getObjectField("bar");
        BEAST_EXPECT(!missing.has_value());
    }

    void
    testMoveConstructor()
    {
        testcase("Move constructor (Object)");
        STJson::Map map;
        map["bar"] = std::make_shared<STUInt16>(sfTransactionType, 42);
        STJson const json(std::move(map));
        BEAST_EXPECT(json.isObject());
        BEAST_EXPECT(json.getMap().size() == 1);
        BEAST_EXPECT(std::dynamic_pointer_cast<STUInt16>(json.getMap().at("bar"))->value() == 42);
    }

    void
    testArrayConstruction()
    {
        testcase("Array constructor");
        STJson::Array arr;
        arr.push_back(std::make_shared<STUInt32>(sfNetworkID, 100));
        arr.push_back(std::make_shared<STUInt32>(sfNetworkID, 200));

        STJson const json(std::move(arr));
        BEAST_EXPECT(json.isArray());
        BEAST_EXPECT(!json.isObject());
        BEAST_EXPECT(json.arraySize() == 2);

        auto elem0 = json.getArrayElement(0);
        BEAST_EXPECT(elem0.has_value());
        if (!elem0)
            return;
        BEAST_EXPECT(std::dynamic_pointer_cast<STUInt32>(*elem0)->value() == 100);
    }

    void
    testTypeChecking()
    {
        testcase("Type checking methods");
        STJson const objJson;
        BEAST_EXPECT(objJson.isObject());
        BEAST_EXPECT(!objJson.isArray());
        BEAST_EXPECT(objJson.getType() == STJson::JsonType::Object);

        STJson const arrJson(STJson::Array{});
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
        if (!elem1)
            return;
        BEAST_EXPECT(std::dynamic_pointer_cast<STUInt8>(*elem1)->value() == 20);

        // Test set (replace)
        json.setArrayElement(1, std::make_shared<STUInt8>(sfCloseResolution, 25));
        auto elem1Updated = json.getArrayElement(1);
        BEAST_EXPECT(elem1Updated.has_value());
        if (!elem1Updated)
            return;
        BEAST_EXPECT(std::dynamic_pointer_cast<STUInt8>(*elem1Updated)->value() == 25);

        // Test out of bounds
        auto const missing = json.getArrayElement(10);
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
            if (!elem)
                return;
            BEAST_EXPECT(*elem == nullptr);
        }

        // Check value at index 5
        auto elem5 = json.getArrayElement(5);
        BEAST_EXPECT(elem5.has_value());
        if (!elem5)
            return;
        BEAST_EXPECT(std::dynamic_pointer_cast<STUInt32>(*elem5)->value() == 999);
    }

    void
    testArrayElementFields()
    {
        testcase("Array element field operations");
        STJson json(STJson::Array{});

        // Set field in array element (auto-creates object)
        json.setArrayElementField(0, "name", std::make_shared<STUInt32>(sfNetworkID, 42));
        json.setArrayElementField(0, "value", std::make_shared<STUInt32>(sfNetworkID, 100));

        // Get fields
        auto name = json.getArrayElementField(0, "name");
        BEAST_EXPECT(name.has_value());
        if (!name)
            return;
        BEAST_EXPECT(std::dynamic_pointer_cast<STUInt32>(*name)->value() == 42);

        auto value = json.getArrayElementField(0, "value");
        BEAST_EXPECT(value.has_value());
        if (!value)
            return;
        BEAST_EXPECT(std::dynamic_pointer_cast<STUInt32>(*value)->value() == 100);

        // Set field at higher index (auto-resize)
        json.setArrayElementField(3, "test", std::make_shared<STUInt8>(sfCloseResolution, 99));
        BEAST_EXPECT(json.arraySize() == 4);

        auto test = json.getArrayElementField(3, "test");
        BEAST_EXPECT(test.has_value());
        if (!test)
            return;
        BEAST_EXPECT(std::dynamic_pointer_cast<STUInt8>(*test)->value() == 99);
    }

    void
    testNestedObjectField()
    {
        testcase("Nested object field operations");
        STJson json;

        json.setNestedObjectField("user", "id", std::make_shared<STUInt32>(sfNetworkID, 123));
        json.setNestedObjectField("user", "name", std::make_shared<STUInt32>(sfNetworkID, 456));

        auto id = json.getNestedObjectField("user", "id");
        BEAST_EXPECT(id.has_value());
        if (!id)
            return;
        BEAST_EXPECT(std::dynamic_pointer_cast<STUInt32>(*id)->value() == 123);

        auto name = json.getNestedObjectField("user", "name");
        BEAST_EXPECT(name.has_value());
        if (!name)
            return;
        BEAST_EXPECT(std::dynamic_pointer_cast<STUInt32>(*name)->value() == 456);

        // Test non-existent nested key
        auto const missing = json.getNestedObjectField("user", "age");
        BEAST_EXPECT(!missing.has_value());
    }

    void
    testNestedArrayOperations()
    {
        testcase("Nested array operations");
        STJson json;

        // Set entire elements in nested array
        json.setNestedArrayElement("items", 0, std::make_shared<STUInt32>(sfNetworkID, 10));
        json.setNestedArrayElement("items", 1, std::make_shared<STUInt32>(sfNetworkID, 20));
        json.setNestedArrayElement("items", 2, std::make_shared<STUInt32>(sfNetworkID, 30));

        // Get elements
        auto item0 = json.getNestedArrayElement("items", 0);
        BEAST_EXPECT(item0.has_value());
        if (!item0)
            return;
        BEAST_EXPECT(std::dynamic_pointer_cast<STUInt32>(*item0)->value() == 10);

        auto item2 = json.getNestedArrayElement("items", 2);
        BEAST_EXPECT(item2.has_value());
        if (!item2)
            return;
        BEAST_EXPECT(std::dynamic_pointer_cast<STUInt32>(*item2)->value() == 30);

        // Auto-resize test
        json.setNestedArrayElement("items", 5, std::make_shared<STUInt32>(sfNetworkID, 60));
        auto item5 = json.getNestedArrayElement("items", 5);
        BEAST_EXPECT(item5.has_value());
        if (!item5)
            return;
        BEAST_EXPECT(std::dynamic_pointer_cast<STUInt32>(*item5)->value() == 60);
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
        if (!user0id)
            return;
        BEAST_EXPECT(std::dynamic_pointer_cast<STUInt32>(*user0id)->value() == 100);

        auto user1name = json.getNestedArrayElementField("users", 1, "name");
        BEAST_EXPECT(user1name.has_value());
        if (!user1name)
            return;
        BEAST_EXPECT(std::dynamic_pointer_cast<STUInt32>(*user1name)->value() == 201);

        // Test missing field
        auto const missing = json.getNestedArrayElementField("users", 0, "age");
        BEAST_EXPECT(!missing.has_value());
    }

    void
    testDepthValidation()
    {
        testcase("Depth validation (max 1 level)");

        // Valid: Object with nested object (depth 1)
        {
            STJson json;
            auto const nested = std::make_shared<STJson>();
            nested->setObjectField("x", std::make_shared<STUInt32>(sfNetworkID, 42));

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
            auto const nested1 = std::make_shared<STJson>();
            auto const nested2 = std::make_shared<STJson>();
            nested2->setObjectField("x", std::make_shared<STUInt32>(sfNetworkID, 42));
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
            auto const elem = std::make_shared<STJson>();
            elem->setObjectField("x", std::make_shared<STUInt32>(sfNetworkID, 42));

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
            auto const innerArray = std::make_shared<STJson>(STJson::Array{});
            innerArray->pushArrayElement(std::make_shared<STUInt32>(sfNetworkID, 42));

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
            auto const arr = std::make_shared<STJson>(STJson::Array{});
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
            auto const nested = std::make_shared<STJson>();
            nested->setObjectField("x", std::make_shared<STUInt32>(sfNetworkID, 42));

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
        json.setObjectField("a", std::make_shared<STUInt8>(sfCloseResolution, 7));
        json.setObjectField("b", std::make_shared<STUInt32>(sfNetworkID, 123456));

        Serializer s;
        json.add(s);

        auto blob = s.peekData();
        auto const parsed = STJson::fromBlob(blob.data(), blob.size());
        BEAST_EXPECT(parsed->isObject());
        BEAST_EXPECT(parsed->getMap().size() == 2);

        auto a = parsed->getObjectField("a");
        BEAST_EXPECT(a.has_value());
        if (!a)
            return;
        BEAST_EXPECT(std::dynamic_pointer_cast<STUInt8>(*a)->value() == 7);

        auto b = parsed->getObjectField("b");
        BEAST_EXPECT(b.has_value());
        if (!b)
            return;
        BEAST_EXPECT(std::dynamic_pointer_cast<STUInt32>(*b)->value() == 123456);
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
        auto const parsed = STJson::fromBlob(blob.data(), blob.size());

        BEAST_EXPECT(parsed->isArray());
        BEAST_EXPECT(parsed->arraySize() == 3);

        auto elem0 = parsed->getArrayElement(0);
        BEAST_EXPECT(elem0.has_value());
        if (!elem0)
            return;
        BEAST_EXPECT(std::dynamic_pointer_cast<STUInt8>(*elem0)->value() == 10);

        auto elem1 = parsed->getArrayElement(1);
        BEAST_EXPECT(elem1.has_value());
        if (!elem1)
            return;
        BEAST_EXPECT(std::dynamic_pointer_cast<STUInt32>(*elem1)->value() == 20);

        auto elem2 = parsed->getArrayElement(2);
        BEAST_EXPECT(elem2.has_value());
        if (!elem2)
            return;
        BEAST_EXPECT(std::dynamic_pointer_cast<STUInt64>(*elem2)->value() == 30);
    }

    // `body` framed as an STJson: its length, then the container type and entries.
    static Blob
    framed(Blob const& body)
    {
        Serializer s;
        s.addVL(body.data(), body.size());
        return s.peekData();
    }

    // An object entry: the key, then the value's bytes, each length-prefixed.
    static Blob
    entry(std::string const& key, Blob const& value)
    {
        Serializer s;
        s.addVL(key.data(), key.size());
        s.addVL(value.data(), value.size());
        return s.peekData();
    }

    // An array element: the value's bytes, length-prefixed.
    static Blob
    element(Blob const& value)
    {
        Serializer s;
        s.addVL(value.data(), value.size());
        return s.peekData();
    }

    static Blob
    concat(std::initializer_list<Blob> parts)
    {
        Blob out;
        for (auto const& part : parts)
            out.insert(out.end(), part.begin(), part.end());
        return out;
    }

    static Blob
    serialized(STJson const& json)
    {
        Serializer s;
        json.add(s);
        return s.peekData();
    }

    // An STJson value's bytes: its type code, then the STJson.
    static Blob
    jsonValue(Blob const& stjson)
    {
        return concat({Blob{static_cast<std::uint8_t>(STI_JSON)}, stjson});
    }

    static Blob const kObject;
    static Blob const kArray;

    // Whether decoding `bytes` as an STJson field, or as a whole blob, throws.
    static bool
    refused(Blob const& bytes)
    {
        bool fieldRefused = false;
        try
        {
            SerialIter sit(makeSlice(bytes));
            STJson const json(sit, sfContractJson);
        }
        catch (std::exception const&)
        {
            fieldRefused = true;
        }
        bool blobRefused = false;
        try
        {
            (void)STJson::fromBlob(bytes.data(), bytes.size());
        }
        catch (std::exception const&)
        {
            blobRefused = true;
        }
        return fieldRefused && blobRefused;
    }

    void
    testMalformedBytes()
    {
        testcase("malformed bytes are refused");

        Blob const u8{static_cast<std::uint8_t>(STI_UINT8), 0x01};

        // No bytes, or a length of zero that leaves no room for the container type.
        BEAST_EXPECT(refused({}));
        BEAST_EXPECT(refused({0x00}));

        // A container type that is neither object nor array.
        BEAST_EXPECT(refused(framed({0x02})));
        BEAST_EXPECT(refused(framed({0xFF})));

        // A length that runs past the bytes there are.
        BEAST_EXPECT(refused({0x05, 0x00}));

        // A key with no value, and a value whose length runs past the container.
        BEAST_EXPECT(refused(framed({0x00, 0x01, 'a'})));
        BEAST_EXPECT(refused(framed({0x00, 0x01, 'a', 0x05, u8[0], u8[1]})));

        // A value of a type STJson does not hold.
        for (auto const type : {
                 0xEE,
                 static_cast<int>(STI_OBJECT),
                 static_cast<int>(STI_ARRAY),
                 static_cast<int>(STI_NUMBER),
                 static_cast<int>(STI_DATA),
                 static_cast<int>(STI_VECTOR256),
                 static_cast<int>(STI_PATHSET),
             })
        {
            Blob value{static_cast<std::uint8_t>(type)};
            value.resize(40, 0x00);
            BEAST_EXPECTS(
                refused(framed(concat({kObject, entry("a", value)}))), std::to_string(type));
        }

        // A value with bytes left over, and a value cut short.
        BEAST_EXPECT(refused(framed(concat({kObject, entry("a", concat({u8, Blob{0x02}}))}))));
        BEAST_EXPECT(refused(
            framed(concat({kObject, entry("a", {static_cast<std::uint8_t>(STI_UINT32), 0x01})}))));

        // The same key twice.
        BEAST_EXPECT(refused(framed(concat({kObject, entry("a", u8), entry("a", u8)}))));

        // Bytes after the STJson in a blob.
        auto const good = framed(concat({kObject, entry("a", u8)}));
        BEAST_EXPECT(!refused(good));
        BEAST_EXPECT([&] {
            try
            {
                auto const extra = concat({good, Blob{0x00}});
                (void)STJson::fromBlob(extra.data(), extra.size());
                return false;
            }
            catch (std::exception const&)
            {
                return true;
            }
        }());

        // A value that is not there, or is not one value, is not a value.
        for (Blob const& bytes : {Blob{}, concat({u8, Blob{0x02}})})
        {
            try
            {
                SerialIter sit(makeSlice(bytes));
                auto const value = STJson::makeValueFromVLWithType(sit);
                BEAST_EXPECT(value == nullptr || !sit.empty());
            }
            catch (std::exception const&)
            {
                pass();
            }
        }
    }

    void
    testNestingFromBytes()
    {
        testcase("nesting from bytes stops where the setters stop");

        Blob const u8{static_cast<std::uint8_t>(STI_UINT8), 0x01};
        auto const leafObject = framed(concat({kObject, entry("x", u8)}));
        auto const leafArray = framed(concat({kArray, element(u8)}));

        // An object holding an array of objects is what setNestedArrayElementField builds.
        {
            STJson built;
            built.setNestedArrayElementField(
                "list", 1, "x", std::make_shared<STUInt8>(sfCloseResolution, 1));
            auto const bytes = serialized(built);
            SerialIter sit(makeSlice(bytes));
            STJson const decoded(sit, sfContractJson);
            BEAST_EXPECT(decoded.isEquivalent(built));
            BEAST_EXPECT(serialized(decoded) == bytes);
        }

        // One container deeper than that is refused, wherever the nesting is.
        auto const arrayOfObjects = framed(concat({kArray, element(jsonValue(leafObject))}));
        auto const twoDeep = framed(concat({kObject, entry("a", jsonValue(arrayOfObjects))}));
        BEAST_EXPECT(!refused(twoDeep));
        BEAST_EXPECT(refused(framed(concat({kObject, entry("b", jsonValue(twoDeep))}))));

        // An array never holds an array.
        BEAST_EXPECT(refused(framed(concat({kArray, element(jsonValue(leafArray))}))));

        // Nesting far past the limit is refused rather than followed.
        {
            Blob nested = leafObject;
            for (int i = 0; i < 2'000; ++i)
                nested = framed(concat({kObject, entry("", jsonValue(nested))}));
            BEAST_EXPECT(refused(nested));
        }
    }

    void
    testRoundTripEveryType()
    {
        testcase("every value type round trips");

        AccountID const gw{0x4321};
        Issue const usd{toCurrency("USD"), gw};
        MPTIssue const mpt{makeMptID(7, gw)};
        std::vector<STJson::Value> const values{
            std::make_shared<STUInt8>(sfCloseResolution, 0xFE),
            std::make_shared<STUInt16>(sfSignerWeight, 0xFEDC),
            std::make_shared<STUInt32>(sfNetworkID, 0xFEDCBA98),
            std::make_shared<STUInt64>(sfIndexNext, 0xFEDCBA9876543210),
            std::make_shared<STUInt128>(sfEmailHash, uint128{0x1234}),
            std::make_shared<STUInt160>(sfTakerPaysCurrency, uint160{0x1234}),
            std::make_shared<STUInt192>(sfMPTokenIssuanceID, uint192{0x1234}),
            std::make_shared<STUInt256>(sfLedgerHash, uint256{0x1234}),
            std::make_shared<STBlob>(sfData, "abc", 3),
            std::make_shared<STAccount>(sfAccount, gw),
            std::make_shared<STAmount>(sfAmount, STAmount(XRPAmount(1'000))),
            std::make_shared<STAmount>(sfAmount, STAmount(usd, 5)),
            std::make_shared<STAmount>(sfAmount, STAmount(mpt, 7)),
            std::make_shared<STIssue>(sfAsset, xrpIssue()),
            std::make_shared<STIssue>(sfAsset, usd),
            std::make_shared<STIssue>(sfAsset, mpt),
            std::make_shared<STCurrency>(sfBaseAsset, usd.currency),
            nullptr,
        };

        STJson object;
        STJson array(STJson::Array{});
        for (std::size_t i = 0; i < values.size(); ++i)
        {
            object.setObjectField(std::to_string(i), values[i]);
            array.pushArrayElement(values[i]);
        }
        auto const nested = std::make_shared<STJson>();
        nested->setObjectField("inner", values[0]);
        object.setObjectField("object", nested);
        array.pushArrayElement(nested);
        auto const list = std::make_shared<STJson>(STJson::Array{});
        list->pushArrayElement(values[1]);
        object.setObjectField("array", list);

        for (auto const* json : {&object, &array})
        {
            auto const bytes = serialized(*json);
            SerialIter sit(makeSlice(bytes));
            STJson const decoded(sit, sfContractJson);
            BEAST_EXPECT(sit.empty());
            BEAST_EXPECT(decoded.isEquivalent(*json));
            BEAST_EXPECT(serialized(decoded) == bytes);
        }

        // An STJson moves whole into an object field.
        STObject holder(sfGeneric);
        STJson field(sfContractJson);
        field.setObjectField("a", values[0]);
        holder.set(STJson(field));
        BEAST_EXPECT(holder.getFieldJson(sfContractJson).isEquivalent(field));
    }

    void
    testEquivalence()
    {
        testcase("equivalence compares contents");

        auto const build = [](std::uint8_t v) {
            STJson json;
            json.setObjectField("a", std::make_shared<STUInt8>(sfCloseResolution, v));
            json.setObjectField("n", nullptr);
            return json;
        };
        BEAST_EXPECT(build(1).isEquivalent(build(1)));
        BEAST_EXPECT(!build(1).isEquivalent(build(2)));

        STJson renamed;
        renamed.setObjectField("b", std::make_shared<STUInt8>(sfCloseResolution, 1));
        renamed.setObjectField("n", nullptr);
        BEAST_EXPECT(!build(1).isEquivalent(renamed));

        STJson nulled;
        nulled.setObjectField("a", nullptr);
        nulled.setObjectField("n", nullptr);
        BEAST_EXPECT(!build(1).isEquivalent(nulled));
        BEAST_EXPECT(!nulled.isEquivalent(build(1)));

        STJson wider;
        wider.setObjectField("a", std::make_shared<STUInt16>(sfSignerWeight, 1));
        wider.setObjectField("n", nullptr);
        BEAST_EXPECT(!build(1).isEquivalent(wider));

        STJson array(STJson::Array{});
        array.pushArrayElement(std::make_shared<STUInt8>(sfCloseResolution, 1));
        STJson sameArray(STJson::Array{});
        sameArray.pushArrayElement(std::make_shared<STUInt8>(sfCloseResolution, 1));
        BEAST_EXPECT(array.isEquivalent(sameArray));
        BEAST_EXPECT(!array.isEquivalent(STJson{}));
        BEAST_EXPECT(!STJson{}.isEquivalent(array));
        BEAST_EXPECT(!array.isEquivalent(STUInt8(sfCloseResolution, 1)));
    }

    void
    testWrongContainer()
    {
        testcase("each accessor keeps to its container");

        auto const v = std::make_shared<STUInt8>(sfCloseResolution, 1);
        STJson array(STJson::Array{});
        STJson object;

        auto const throws = [](auto const& f) {
            try
            {
                f();
                return false;
            }
            catch (std::runtime_error const&)
            {
                return true;
            }
        };

        // Object operations on an array throw or find nothing.
        BEAST_EXPECT(throws([&] { array.setObjectField("a", v); }));
        BEAST_EXPECT(throws([&] { array.eraseObjectField("a"); }));
        BEAST_EXPECT(throws([&] { array.setNestedObjectField("a", "b", v); }));
        BEAST_EXPECT(throws([&] { array.setNestedArrayElement("a", 0, v); }));
        BEAST_EXPECT(throws([&] { array.setNestedArrayElementField("a", 0, "b", v); }));
        BEAST_EXPECT(throws([&] { (void)array.getMap(); }));
        BEAST_EXPECT(!array.getObjectField("a"));
        BEAST_EXPECT(!array.getNestedObjectField("a", "b"));
        BEAST_EXPECT(!array.getNestedArrayElement("a", 0));
        BEAST_EXPECT(!array.getNestedArrayElementField("a", 0, "b"));

        // Array operations on an object throw or find nothing.
        BEAST_EXPECT(throws([&] { object.pushArrayElement(v); }));
        BEAST_EXPECT(throws([&] { object.setArrayElement(0, v); }));
        BEAST_EXPECT(throws([&] { object.setArrayElementField(0, "a", v); }));
        BEAST_EXPECT(throws([&] { (void)object.getArray(); }));
        BEAST_EXPECT(!object.getArrayElement(0));
        BEAST_EXPECT(!object.getArrayElementField(0, "a"));
        BEAST_EXPECT(object.arraySize() == 0);

        // A null element holds no fields.
        array.pushArrayElement(nullptr);
        BEAST_EXPECT(!array.getArrayElementField(0, "a"));

        // Nested reads find nothing under a missing key, a null, a leaf or an object.
        object.setObjectField("null", nullptr);
        object.setObjectField("leaf", v);
        object.setObjectField("obj", std::make_shared<STJson>());
        for (std::string const key : {"missing", "null", "leaf", "obj"})
        {
            BEAST_EXPECT(!object.getNestedArrayElement(key, 0));
            BEAST_EXPECT(!object.getNestedArrayElementField(key, 0, "a"));
        }

        // Writing a nested array, element or object over a null or a leaf replaces it.
        object.setNestedArrayElement("null", 0, v);
        BEAST_EXPECT(object.getNestedArrayElement("null", 0) == v);
        object.setNestedArrayElementField("leaf", 0, "a", v);
        BEAST_EXPECT(object.getNestedArrayElementField("leaf", 0, "a") == v);
        object.setObjectField("null2", nullptr);
        object.setNestedObjectField("null2", "a", v);
        BEAST_EXPECT(object.getNestedObjectField("null2", "a") == v);
        array.setArrayElementField(0, "a", v);
        BEAST_EXPECT(array.getArrayElementField(0, "a") == v);

        // An array holding an object is one level deep; nulls and leaves add no depth.
        BEAST_EXPECT(array.getDepth() == 1);
        STJson flat(STJson::Array{});
        flat.pushArrayElement(nullptr);
        flat.pushArrayElement(v);
        BEAST_EXPECT(flat.getDepth() == 0);
        STJson flatObject;
        flatObject.setObjectField("n", nullptr);
        flatObject.setObjectField("v", v);
        BEAST_EXPECT(flatObject.getDepth() == 0);
    }

    void
    testFromSField()
    {
        testcase("Constructor from SField");
        STJson json;
        json.setObjectField("x", std::make_shared<STUInt8>(sfCloseResolution, 99));
        Serializer s;
        json.add(s);

        SerialIter sit(s.peekData().data(), s.peekData().size());
        auto const parsed = STJson{sit, sfContractCode};
        BEAST_EXPECT(parsed.isObject());
        BEAST_EXPECT(parsed.getMap().size() == 1);

        auto x = parsed.getObjectField("x");
        BEAST_EXPECT(x.has_value());
        if (!x)
            return;
        BEAST_EXPECT(std::dynamic_pointer_cast<STUInt8>(*x)->value() == 99);
    }

    void
    testGetJson()
    {
        testcase("getJson() for objects");
        STJson json;
        json.setObjectField("foo", std::make_shared<STUInt16>(sfTransactionType, 65535));
        json.setObjectField("bar", nullptr);  // test null value
        json.setNestedObjectField("meta", "version", std::make_shared<STUInt32>(sfNetworkID, 2));

        json::Value jv = json.getJson(JsonOptions::Values::None);
        BEAST_EXPECT(jv.isObject());
        BEAST_EXPECT(jv[strHex(std::string{"foo"})].asUInt() == 65535);
        BEAST_EXPECT(jv[strHex(std::string{"bar"})].isNull());
        BEAST_EXPECT(jv[strHex(std::string{"meta"})][strHex(std::string{"version"})].asUInt() == 2);
    }

    void
    testGetJsonArray()
    {
        testcase("getJson() for arrays");
        STJson json(STJson::Array{});
        json.pushArrayElement(std::make_shared<STUInt32>(sfNetworkID, 100));
        json.pushArrayElement(std::make_shared<STUInt32>(sfNetworkID, 200));
        json.pushArrayElement(nullptr);  // null element

        json::Value jv = json.getJson(JsonOptions::Values::None);
        BEAST_EXPECT(jv.isArray());
        BEAST_EXPECT(jv.size() == 3);
        BEAST_EXPECT(jv[json::UInt(0)].asUInt() == 100);
        BEAST_EXPECT(jv[json::UInt(1)].asUInt() == 200);
        BEAST_EXPECT(jv[json::UInt(2)].isNull());
    }

    void
    testMakeValueFromVLWithType()
    {
        testcase("makeValueFromVLWithType()");
        Serializer s;
        s.add8(STI_UINT32);
        s.add32(0xDEADBEEF);
        SerialIter sit(s.peekData().data(), s.peekData().size());
        auto const value = STJson::makeValueFromVLWithType(sit);
        BEAST_EXPECT(value->getSType() == STI_UINT32);
        BEAST_EXPECT(std::dynamic_pointer_cast<STUInt32>(value)->value() == 0xDEADBEEF);
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
        auto const parsed = STJson::fromBlob(s.peekData().data(), s.peekData().size());

        // Verify structure
        BEAST_EXPECT(parsed->isObject());

        auto id = parsed->getObjectField("id");
        BEAST_EXPECT(id.has_value());
        if (!id)
            return;
        BEAST_EXPECT(std::dynamic_pointer_cast<STUInt32>(*id)->value() == 1);

        auto version = parsed->getNestedObjectField("metadata", "version");
        BEAST_EXPECT(version.has_value());
        if (!version)
            return;
        BEAST_EXPECT(std::dynamic_pointer_cast<STUInt32>(*version)->value() == 2);

        auto user0name = parsed->getNestedArrayElementField("users", 0, "name");
        BEAST_EXPECT(user0name.has_value());
        if (!user0name)
            return;
        BEAST_EXPECT(std::dynamic_pointer_cast<STUInt32>(*user0name)->value() == 100);

        auto user1name = parsed->getNestedArrayElementField("users", 1, "name");
        BEAST_EXPECT(user1name.has_value());
        if (!user1name)
            return;
        BEAST_EXPECT(std::dynamic_pointer_cast<STUInt32>(*user1name)->value() == 101);
    }

    void
    testSTTypes()
    {
        testcase("All STypes roundtrip");

        // STI_UINT8
        {
            STJson json;
            json.setObjectField("u8", std::make_shared<STUInt8>(sfCloseResolution, 200));
            Serializer s;
            json.add(s);
            auto const parsed = STJson::fromBlob(s.peekData().data(), s.peekData().size());
            auto u8 = parsed->getObjectField("u8");
            BEAST_EXPECT(u8.has_value());
            if (!u8)
                return;
            BEAST_EXPECT(std::dynamic_pointer_cast<STUInt8>(*u8)->value() == 200);
        }

        // STI_UINT16
        {
            STJson json;
            json.setObjectField("u16", std::make_shared<STUInt16>(sfSignerWeight, 4242));
            Serializer s;
            json.add(s);
            auto const parsed = STJson::fromBlob(s.peekData().data(), s.peekData().size());
            auto u16 = parsed->getObjectField("u16");
            BEAST_EXPECT(u16.has_value());
            if (!u16)
                return;
            BEAST_EXPECT(std::dynamic_pointer_cast<STUInt16>(*u16)->value() == 4242);
        }

        // STI_UINT32
        {
            STJson json;
            json.setObjectField("u32", std::make_shared<STUInt32>(sfNetworkID, 0xABCDEF01));
            Serializer s;
            json.add(s);
            auto const parsed = STJson::fromBlob(s.peekData().data(), s.peekData().size());
            auto u32 = parsed->getObjectField("u32");
            BEAST_EXPECT(u32.has_value());
            if (!u32)
                return;
            BEAST_EXPECT(std::dynamic_pointer_cast<STUInt32>(*u32)->value() == 0xABCDEF01);
        }

        // STI_UINT64
        {
            STJson json;
            json.setObjectField(
                "u64", std::make_shared<STUInt64>(sfIndexNext, 0x123456789ABCDEF0ull));
            Serializer s;
            json.add(s);
            auto const parsed = STJson::fromBlob(s.peekData().data(), s.peekData().size());
            auto u64 = parsed->getObjectField("u64");
            BEAST_EXPECT(u64.has_value());
            if (!u64)
                return;
            BEAST_EXPECT(
                std::dynamic_pointer_cast<STUInt64>(*u64)->value() == 0x123456789ABCDEF0ull);
        }

        // STI_UINT160
        {
            STJson json;
            uint160 val;
            val.data()[0] = 0x01;
            val.data()[19] = 0xFF;
            json.setObjectField("u160", std::make_shared<STUInt160>(sfTakerPaysCurrency, val));
            Serializer s;
            json.add(s);
            auto const parsed = STJson::fromBlob(s.peekData().data(), s.peekData().size());
            auto u160 = parsed->getObjectField("u160");
            BEAST_EXPECT(u160.has_value());
            if (!u160)
                return;
            BEAST_EXPECT(std::dynamic_pointer_cast<STUInt160>(*u160)->value() == val);
        }

        // STI_UINT256
        {
            STJson json;
            uint256 val;
            val.data()[0] = 0xAA;
            val.data()[31] = 0xBB;
            json.setObjectField("u256", std::make_shared<STUInt256>(sfLedgerHash, val));
            Serializer s;
            json.add(s);
            auto const parsed = STJson::fromBlob(s.peekData().data(), s.peekData().size());
            auto u256 = parsed->getObjectField("u256");
            BEAST_EXPECT(u256.has_value());
            if (!u256)
                return;
            BEAST_EXPECT(std::dynamic_pointer_cast<STUInt256>(*u256)->value() == val);
        }

        // STI_AMOUNT
        {
            STJson json;
            // XRP amount
            STAmount const xrp(sfAmount, static_cast<std::int64_t>(123456789u));
            json.setObjectField("amount", std::make_shared<STAmount>(xrp));
            Serializer s;
            json.add(s);
            auto const parsed = STJson::fromBlob(s.peekData().data(), s.peekData().size());
            auto amount = parsed->getObjectField("amount");
            BEAST_EXPECT(amount.has_value());
            if (!amount)
                return;
            auto const parsedAmt = std::dynamic_pointer_cast<STAmount>(*amount);
            BEAST_EXPECT(parsedAmt->mantissa() == 123456789u);
            BEAST_EXPECT(parsedAmt->asset() == xrp.asset());
        }

        // STI_VL (STBlob)
        {
            STJson json;
            std::vector<uint8_t> blobData = {0xDE, 0xAD, 0xBE, 0xEF};
            json.setObjectField(
                "blob", std::make_shared<STBlob>(sfPublicKey, blobData.data(), blobData.size()));
            Serializer s;
            json.add(s);
            auto const parsed = STJson::fromBlob(s.peekData().data(), s.peekData().size());
            auto blob = parsed->getObjectField("blob");
            BEAST_EXPECT(blob.has_value());
            if (!blob)
                return;
            auto const parsedBlob = std::dynamic_pointer_cast<STBlob>(*blob);
            BEAST_EXPECT(parsedBlob->size() == blobData.size());
            BEAST_EXPECT(std::memcmp(parsedBlob->data(), blobData.data(), blobData.size()) == 0);
        }

        // STI_ACCOUNT
        {
            STJson json;
            // Use a known AccountID (20 bytes)
            AccountID const acct = AccountID{};
            json.setObjectField("acct", std::make_shared<STAccount>(sfAccount, acct));
            Serializer s;
            json.add(s);
            auto const parsed = STJson::fromBlob(s.peekData().data(), s.peekData().size());
            auto account = parsed->getObjectField("acct");
            BEAST_EXPECT(account.has_value());
            if (!account)
                return;
            auto const parsedAcct = std::dynamic_pointer_cast<STAccount>(*account);
            BEAST_EXPECT(parsedAcct->value() == acct);
        }

        // STI_CURRENCY (STCurrency)
        {
            STJson json;
            Currency cur;
            cur.data()[0] = 0xAA;
            cur.data()[19] = 0xBB;
            json.setObjectField("currency", std::make_shared<STCurrency>(sfGeneric, cur));
            Serializer s;
            json.add(s);
            auto const parsed = STJson::fromBlob(s.peekData().data(), s.peekData().size());
            auto currency = parsed->getObjectField("currency");
            BEAST_EXPECT(currency.has_value());
            if (!currency)
                return;
            auto const parsedCur = std::dynamic_pointer_cast<STCurrency>(*currency);
            BEAST_EXPECT(parsedCur->value() == cur);
        }

        // STI_JSON (STJson) Nested JSON
        {
            STJson innerJson;
            // XRP amount
            STAmount const xrp(sfAmount, static_cast<std::int64_t>(123456789u));
            innerJson.setObjectField("amount", std::make_shared<STAmount>(xrp));

            STJson json;
            json.setObjectField("nested", std::make_shared<STJson>(innerJson));
            Serializer s;
            json.add(s);

            auto const parsed = STJson::fromBlob(s.peekData().data(), s.peekData().size());
            auto nested = parsed->getObjectField("nested");
            BEAST_EXPECT(nested.has_value());
            if (!nested)
                return;
            auto const parsedNested = std::dynamic_pointer_cast<STJson>(*nested);
            auto amount = parsedNested->getObjectField("amount");
            BEAST_EXPECT(amount.has_value());
            if (!amount)
                return;
            BEAST_EXPECT(std::dynamic_pointer_cast<STAmount>(*amount)->mantissa() == 123456789u);
        }
    }

    void
    testEdgeCases()
    {
        testcase("Edge cases");

        // Empty object
        {
            STJson const json;
            Serializer s;
            json.add(s);
            auto const parsed = STJson::fromBlob(s.peekData().data(), s.peekData().size());
            BEAST_EXPECT(parsed->isObject());
            BEAST_EXPECT(parsed->getMap().empty());
        }

        // Empty array
        {
            STJson const json(STJson::Array{});
            Serializer s;
            json.add(s);
            auto const parsed = STJson::fromBlob(s.peekData().data(), s.peekData().size());
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
            if (!elem0)
                return;
            BEAST_EXPECT(*elem0 == nullptr);

            auto elem1 = json.getArrayElement(1);
            BEAST_EXPECT(elem1.has_value());
            if (!elem1)
                return;
            BEAST_EXPECT(*elem1 != nullptr);
        }

        // Object with null value - getObjectField treats null as absent
        {
            STJson json;
            json.setObjectField("null_field", nullptr);

            auto const val = json.getObjectField("null_field");
            BEAST_EXPECT(!val.has_value());
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
        testFromSField();
        testGetJson();
        testGetJsonArray();
        testMakeValueFromVLWithType();
        testMixedStructures();
        testSTTypes();
        testEdgeCases();
        testMalformedBytes();
        testNestingFromBytes();
        testRoundTripEveryType();
        testEquivalence();
        testWrongContainer();
    }
};

Blob const STJson_test::kObject{static_cast<std::uint8_t>(STJson::JsonType::Object)};
Blob const STJson_test::kArray{static_cast<std::uint8_t>(STJson::JsonType::Array)};

BEAST_DEFINE_TESTSUITE(STJson, protocol, xrpl);

}  // namespace xrpl
