//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#include <ripple/beast/unit_test.h>
#include <ripple/json/json_reader.h>
#include <ripple/json/to_string.h>
#include <ripple/protocol/SecretKey.h>
#include <ripple/protocol/jss.h>
#include <ripple/protocol/st.h>
#include <test/jtx.h>

#include <array>
#include <memory>
#include <type_traits>

namespace ripple {

class STObject_test : public beast::unit_test::suite
{
public:
    bool
    parseJSONString(std::string const& json, Json::Value& to)
    {
        Json::Reader reader;
        return reader.parse(json, to) && to.isObject();
    }

    void
    testParseJSONArrayWithInvalidChildrenObjects()
    {
        testcase("parse json array invalid children");
        try
        {
            /*

            STArray/STObject constructs don't really map perfectly to json
            arrays/objects.

            STObject is an associative container, mapping fields to value, but
            an STObject may also have a Field as its name, stored outside the
            associative structure. The name is important, so to maintain
            fidelity, it will take TWO json objects to represent them.

            */
            std::string faulty(
                "{\"Template\":[{"
                "\"ModifiedNode\":{\"Sequence\":1}, "
                "\"DeletedNode\":{\"Sequence\":1}"
                "}]}");

            std::unique_ptr<STObject> so;
            Json::Value faultyJson;
            bool parsedOK(parseJSONString(faulty, faultyJson));
            unexpected(!parsedOK, "failed to parse");
            STParsedJSONObject parsed("test", faultyJson);
            BEAST_EXPECT(!parsed.object);
        }
        catch (std::runtime_error& e)
        {
            std::string what(e.what());
            unexpected(what.find("First level children of `Template`") != 0);
        }
    }

    void
    testParseJSONArray()
    {
        testcase("parse json array");
        std::string const json(
            "{\"Template\":[{\"ModifiedNode\":{\"Sequence\":1}}]}");

        Json::Value jsonObject;
        bool parsedOK(parseJSONString(json, jsonObject));
        if (parsedOK)
        {
            STParsedJSONObject parsed("test", jsonObject);
            BEAST_EXPECT(parsed.object);
            std::string const& serialized(
                to_string(parsed.object->getJson(JsonOptions::none)));
            BEAST_EXPECT(serialized == json);
        }
        else
        {
            fail("Couldn't parse json: " + json);
        }
    }

    void
    testParseJSONEdgeCases()
    {
        testcase("parse json object");

        {
            std::string const goodJson(R"({"CloseResolution":19,"Method":250,)"
                                       R"("TransactionResult":"tecFROZEN"})");

            Json::Value jv;
            if (BEAST_EXPECT(parseJSONString(goodJson, jv)))
            {
                STParsedJSONObject parsed("test", jv);
                if (BEAST_EXPECT(parsed.object))
                {
                    std::string const& serialized(
                        to_string(parsed.object->getJson(JsonOptions::none)));
                    BEAST_EXPECT(serialized == goodJson);
                }
            }
        }

        {
            std::string const goodJson(
                R"({"CloseResolution":19,"Method":"250",)"
                R"("TransactionResult":"tecFROZEN"})");
            std::string const expectedJson(
                R"({"CloseResolution":19,"Method":250,)"
                R"("TransactionResult":"tecFROZEN"})");

            Json::Value jv;
            if (BEAST_EXPECT(parseJSONString(goodJson, jv)))
            {
                // Integer values are always parsed as int,
                // unless they're too big. We want a small uint.
                jv["CloseResolution"] = Json::UInt(19);
                STParsedJSONObject parsed("test", jv);
                if (BEAST_EXPECT(parsed.object))
                {
                    std::string const& serialized(
                        to_string(parsed.object->getJson(JsonOptions::none)));
                    BEAST_EXPECT(serialized == expectedJson);
                }
            }
        }

        {
            std::string const json(R"({"CloseResolution":19,"Method":250,)"
                                   R"("TransactionResult":"terQUEUED"})");

            Json::Value jv;
            if (BEAST_EXPECT(parseJSONString(json, jv)))
            {
                STParsedJSONObject parsed("test", jv);
                BEAST_EXPECT(!parsed.object);
                BEAST_EXPECT(parsed.error);
                BEAST_EXPECT(parsed.error[jss::error] == "invalidParams");
                BEAST_EXPECT(
                    parsed.error[jss::error_message] ==
                    "Field 'test.TransactionResult' is out of range.");
            }
        }

        {
            std::string const json(R"({"CloseResolution":19,"Method":"pony",)"
                                   R"("TransactionResult":"tesSUCCESS"})");

            Json::Value jv;
            if (BEAST_EXPECT(parseJSONString(json, jv)))
            {
                STParsedJSONObject parsed("test", jv);
                BEAST_EXPECT(!parsed.object);
                BEAST_EXPECT(parsed.error);
                BEAST_EXPECT(parsed.error[jss::error] == "invalidParams");
                BEAST_EXPECT(
                    parsed.error[jss::error_message] ==
                    "Field 'test.Method' has bad type.");
            }
        }

        {
            std::string const json(
                R"({"CloseResolution":19,"Method":3294967296,)"
                R"("TransactionResult":"tesSUCCESS"})");

            Json::Value jv;
            if (BEAST_EXPECT(parseJSONString(json, jv)))
            {
                STParsedJSONObject parsed("test", jv);
                BEAST_EXPECT(!parsed.object);
                BEAST_EXPECT(parsed.error);
                BEAST_EXPECT(parsed.error[jss::error] == "invalidParams");
                BEAST_EXPECT(
                    parsed.error[jss::error_message] ==
                    "Field 'test.Method' is out of range.");
            }
        }

        {
            std::string const json(R"({"CloseResolution":-10,"Method":42,)"
                                   R"("TransactionResult":"tesSUCCESS"})");

            Json::Value jv;
            if (BEAST_EXPECT(parseJSONString(json, jv)))
            {
                STParsedJSONObject parsed("test", jv);
                BEAST_EXPECT(!parsed.object);
                BEAST_EXPECT(parsed.error);
                BEAST_EXPECT(parsed.error[jss::error] == "invalidParams");
                BEAST_EXPECT(
                    parsed.error[jss::error_message] ==
                    "Field 'test.CloseResolution' is out of range.");
            }
        }

        {
            std::string const json(
                R"({"CloseResolution":19,"Method":3.141592653,)"
                R"("TransactionResult":"tesSUCCESS"})");

            Json::Value jv;
            if (BEAST_EXPECT(parseJSONString(json, jv)))
            {
                STParsedJSONObject parsed("test", jv);
                BEAST_EXPECT(!parsed.object);
                BEAST_EXPECT(parsed.error);
                BEAST_EXPECT(parsed.error[jss::error] == "invalidParams");
                BEAST_EXPECT(
                    parsed.error[jss::error_message] ==
                    "Field 'test.Method' has bad type.");
            }
        }
    }

    void
    testSerialization()
    {
        testcase("serialization");

        unexpected(sfGeneric.isUseful(), "sfGeneric must not be useful");
        {
            // Try to put sfGeneric in an SOTemplate.
            except<std::runtime_error>([&]() {
                SOTemplate elements{{sfGeneric, soeREQUIRED}};
            });
        }

        unexpected(sfInvalid.isUseful(), "sfInvalid must not be useful");
        {
            // Test return of sfInvalid.
            auto testInvalid = [this](SerializedTypeID tid, int fv) {
                SField const& shouldBeInvalid{SField::getField(tid, fv)};
                BEAST_EXPECT(shouldBeInvalid == sfInvalid);
            };
            testInvalid(STI_VL, 255);
            testInvalid(STI_UINT256, 255);
            testInvalid(STI_UINT32, 255);
            testInvalid(STI_VECTOR256, 255);
            testInvalid(STI_OBJECT, 255);
        }
        {
            // Try to put sfInvalid in an SOTemplate.
            except<std::runtime_error>([&]() {
                SOTemplate elements{{sfInvalid, soeREQUIRED}};
            });
        }
        {
            // Try to put the same SField into an SOTemplate twice.
            except<std::runtime_error>([&]() {
                SOTemplate elements{
                    {sfAccount, soeREQUIRED},
                    {sfAccount, soeREQUIRED},
                };
            });
        }

        // Put a variety of SFields of different types in an SOTemplate.
        SField const& sfTestVL = sfMasterSignature;
        SField const& sfTestH256 = sfCheckID;
        SField const& sfTestU32 = sfSettleDelay;
        SField const& sfTestV256 = sfAmendments;
        SField const& sfTestObject = sfMajority;

        SOTemplate const elements{
            {sfFlags, soeREQUIRED},
            {sfTestVL, soeREQUIRED},
            {sfTestH256, soeOPTIONAL},
            {sfTestU32, soeREQUIRED},
            {sfTestV256, soeOPTIONAL},
        };

        STObject object1(elements, sfTestObject);
        STObject object2(object1);

        unexpected(
            object1.getSerializer() != object2.getSerializer(),
            "STObject error 1");

        unexpected(
            object1.isFieldPresent(sfTestH256) ||
                !object1.isFieldPresent(sfTestVL),
            "STObject error");

        object1.makeFieldPresent(sfTestH256);

        unexpected(!object1.isFieldPresent(sfTestH256), "STObject Error 2");

        unexpected(
            object1.getFieldH256(sfTestH256) != uint256(), "STObject error 3");

        if (object1.getSerializer() == object2.getSerializer())
        {
            log << "O1: " << object1.getJson(JsonOptions::none) << '\n'
                << "O2: " << object2.getJson(JsonOptions::none) << std::endl;
            fail("STObject error 4");
        }
        else
        {
            pass();
        }

        object1.makeFieldAbsent(sfTestH256);

        unexpected(object1.isFieldPresent(sfTestH256), "STObject error 5");

        unexpected(object1.getFlags() != 0, "STObject error 6");

        unexpected(
            object1.getSerializer() != object2.getSerializer(),
            "STObject error 7");

        STObject copy(object1);

        unexpected(object1.isFieldPresent(sfTestH256), "STObject error 8");

        unexpected(copy.isFieldPresent(sfTestH256), "STObject error 9");

        unexpected(
            object1.getSerializer() != copy.getSerializer(),
            "STObject error 10");

        copy.setFieldU32(sfTestU32, 1);

        unexpected(
            object1.getSerializer() == copy.getSerializer(),
            "STObject error 11");

        for (int i = 0; i < 1000; i++)
        {
            Blob j(i, 2);

            object1.setFieldVL(sfTestVL, j);

            Serializer s;
            object1.add(s);
            SerialIter it(s.slice());

            STObject object3(elements, it, sfTestObject);

            unexpected(object1.getFieldVL(sfTestVL) != j, "STObject error");

            unexpected(object3.getFieldVL(sfTestVL) != j, "STObject error");
        }

        {
            std::vector<uint256> uints;
            uints.reserve(5);
            for (int i = 0; i < uints.capacity(); ++i)
            {
                uints.emplace_back(i);
            }
            object1.setFieldV256(sfTestV256, STVector256(uints));

            Serializer s;
            object1.add(s);
            SerialIter it(s.slice());

            STObject object3(elements, it, sfTestObject);

            auto const& uints1 = object1.getFieldV256(sfTestV256);
            auto const& uints3 = object3.getFieldV256(sfTestV256);

            BEAST_EXPECT(uints1 == uints3);
        }
    }

    // Exercise field accessors
    void
    testFields()
    {
        testcase("fields");

        auto const& sf1Outer = sfSequence;
        auto const& sf2Outer = sfExpiration;
        auto const& sf3Outer = sfQualityIn;
        auto const& sf4 = sfSignature;
        auto const& sf5 = sfPublicKey;

        // read free object

        {
            auto const st = [&]() {
                STObject s(sfGeneric);
                s.setFieldU32(sf1Outer, 1);
                s.setFieldU32(sf2Outer, 2);
                return s;
            }();

            BEAST_EXPECT(st[sf1Outer] == 1);
            BEAST_EXPECT(st[sf2Outer] == 2);
            except<STObject::FieldErr>([&]() { st[sf3Outer]; });
            BEAST_EXPECT(*st[~sf1Outer] == 1);
            BEAST_EXPECT(*st[~sf2Outer] == 2);
            BEAST_EXPECT(st[~sf3Outer] == std::nullopt);
            BEAST_EXPECT(!!st[~sf1Outer]);
            BEAST_EXPECT(!!st[~sf2Outer]);
            BEAST_EXPECT(!st[~sf3Outer]);
            BEAST_EXPECT(st[sf1Outer] != st[sf2Outer]);
            BEAST_EXPECT(st[~sf1Outer] != st[~sf2Outer]);
        }

        // read templated object
        SOTemplate const sotOuter{
            {sf1Outer, soeREQUIRED},
            {sf2Outer, soeOPTIONAL},
            {sf3Outer, soeDEFAULT},
            {sf4, soeOPTIONAL},
            {sf5, soeDEFAULT},
        };

        {
            auto const st = [&]() {
                STObject s(sotOuter, sfGeneric);
                s.setFieldU32(sf1Outer, 1);
                s.setFieldU32(sf2Outer, 2);
                return s;
            }();

            BEAST_EXPECT(st[sf1Outer] == 1);
            BEAST_EXPECT(st[sf2Outer] == 2);
            BEAST_EXPECT(st[sf3Outer] == 0);
            BEAST_EXPECT(*st[~sf1Outer] == 1);
            BEAST_EXPECT(*st[~sf2Outer] == 2);
            BEAST_EXPECT(*st[~sf3Outer] == 0);
            BEAST_EXPECT(!!st[~sf1Outer]);
            BEAST_EXPECT(!!st[~sf2Outer]);
            BEAST_EXPECT(!!st[~sf3Outer]);
        }

        // write free object

        {
            STObject st(sfGeneric);
            unexcept([&]() { st[sf1Outer]; });
            except([&]() { return st[sf1Outer] == 0; });
            BEAST_EXPECT(st[~sf1Outer] == std::nullopt);
            BEAST_EXPECT(st[~sf1Outer] == std::optional<std::uint32_t>{});
            BEAST_EXPECT(st[~sf1Outer] != std::optional<std::uint32_t>(1));
            BEAST_EXPECT(!st[~sf1Outer]);
            st[sf1Outer] = 2;
            BEAST_EXPECT(st[sf1Outer] == 2);
            BEAST_EXPECT(st[~sf1Outer] != std::nullopt);
            BEAST_EXPECT(st[~sf1Outer] == std::optional<std::uint32_t>(2));
            BEAST_EXPECT(!!st[~sf1Outer]);
            st[sf1Outer] = 1;
            BEAST_EXPECT(st[sf1Outer] == 1);
            BEAST_EXPECT(!!st[sf1Outer]);
            BEAST_EXPECT(!!st[~sf1Outer]);
            st[sf1Outer] = 0;
            BEAST_EXPECT(!st[sf1Outer]);
            BEAST_EXPECT(!!st[~sf1Outer]);
            st[~sf1Outer] = std::nullopt;
            BEAST_EXPECT(!st[~sf1Outer]);
            BEAST_EXPECT(st[~sf1Outer] == std::nullopt);
            BEAST_EXPECT(st[~sf1Outer] == std::optional<std::uint32_t>{});
            st[~sf1Outer] = std::nullopt;
            BEAST_EXPECT(!st[~sf1Outer]);
            except([&]() { return st[sf1Outer] == 0; });
            except([&]() { return *st[~sf1Outer]; });
            st[sf1Outer] = 1;
            BEAST_EXPECT(st[sf1Outer] == 1);
            BEAST_EXPECT(!!st[sf1Outer]);
            BEAST_EXPECT(!!st[~sf1Outer]);
            st[sf1Outer] = 3;
            st[sf2Outer] = st[sf1Outer];
            BEAST_EXPECT(st[sf1Outer] == 3);
            BEAST_EXPECT(st[sf2Outer] == 3);
            BEAST_EXPECT(st[sf2Outer] == st[sf1Outer]);
            st[sf1Outer] = 4;
            st[sf2Outer] = st[sf1Outer];
            BEAST_EXPECT(st[sf1Outer] == 4);
            BEAST_EXPECT(st[sf2Outer] == 4);
            BEAST_EXPECT(st[sf2Outer] == st[sf1Outer]);
        }

        // Write templated object

        {
            STObject st(sotOuter, sfGeneric);
            BEAST_EXPECT(!!st[~sf1Outer]);
            BEAST_EXPECT(st[~sf1Outer] != std::nullopt);
            BEAST_EXPECT(st[sf1Outer] == 0);
            BEAST_EXPECT(*st[~sf1Outer] == 0);
            BEAST_EXPECT(!st[~sf2Outer]);
            BEAST_EXPECT(st[~sf2Outer] == std::nullopt);
            except([&]() { return st[sf2Outer] == 0; });
            BEAST_EXPECT(!!st[~sf3Outer]);
            BEAST_EXPECT(st[~sf3Outer] != std::nullopt);
            BEAST_EXPECT(st[sf3Outer] == 0);
            except([&]() { st[~sf1Outer] = std::nullopt; });
            st[sf1Outer] = 1;
            BEAST_EXPECT(st[sf1Outer] == 1);
            BEAST_EXPECT(*st[~sf1Outer] == 1);
            BEAST_EXPECT(!!st[~sf1Outer]);
            st[sf1Outer] = 0;
            BEAST_EXPECT(st[sf1Outer] == 0);
            BEAST_EXPECT(*st[~sf1Outer] == 0);
            BEAST_EXPECT(!!st[~sf1Outer]);
            st[sf2Outer] = 2;
            BEAST_EXPECT(st[sf2Outer] == 2);
            BEAST_EXPECT(*st[~sf2Outer] == 2);
            BEAST_EXPECT(!!st[~sf2Outer]);
            st[~sf2Outer] = std::nullopt;
            except([&]() { return *st[~sf2Outer]; });
            BEAST_EXPECT(!st[~sf2Outer]);
            st[sf3Outer] = 3;
            BEAST_EXPECT(st[sf3Outer] == 3);
            BEAST_EXPECT(*st[~sf3Outer] == 3);
            BEAST_EXPECT(!!st[~sf3Outer]);
            st[sf3Outer] = 2;
            BEAST_EXPECT(st[sf3Outer] == 2);
            BEAST_EXPECT(*st[~sf3Outer] == 2);
            BEAST_EXPECT(!!st[~sf3Outer]);
            st[sf3Outer] = 0;
            BEAST_EXPECT(st[sf3Outer] == 0);
            BEAST_EXPECT(*st[~sf3Outer] == 0);
            BEAST_EXPECT(!!st[~sf3Outer]);
            except([&]() { st[~sf3Outer] = std::nullopt; });
            BEAST_EXPECT(st[sf3Outer] == 0);
            BEAST_EXPECT(*st[~sf3Outer] == 0);
            BEAST_EXPECT(!!st[~sf3Outer]);
        }

        // coercion operator to std::optional

        {
            STObject st(sfGeneric);
            auto const v = ~st[~sf1Outer];
            static_assert(
                std::is_same<
                    std::decay_t<decltype(v)>,
                    std::optional<std::uint32_t>>::value,
                "");
        }

        // UDT scalar fields

        {
            STObject st(sfGeneric);
            st[sfAmount] = STAmount{};
            st[sfAccount] = AccountID{};
            st[sfDigest] = uint256{};
            [&](STAmount) {}(st[sfAmount]);
            [&](AccountID) {}(st[sfAccount]);
            [&](uint256) {}(st[sfDigest]);
        }

        // STBlob and slice

        {{STObject st(sfGeneric);
        Buffer b(1);
        BEAST_EXPECT(!b.empty());
        st[sf4] = std::move(b);
        BEAST_EXPECT(b.empty());
        BEAST_EXPECT(Slice(st[sf4]).size() == 1);
        st[~sf4] = std::nullopt;
        BEAST_EXPECT(!~st[~sf4]);
        b = Buffer{2};
        st[sf4] = Slice(b);
        BEAST_EXPECT(b.size() == 2);
        BEAST_EXPECT(Slice(st[sf4]).size() == 2);
        st[sf5] = st[sf4];
        BEAST_EXPECT(Slice(st[sf4]).size() == 2);
        BEAST_EXPECT(Slice(st[sf5]).size() == 2);
    }
    {
        STObject st(sotOuter, sfGeneric);
        BEAST_EXPECT(st[sf5] == Slice{});
        BEAST_EXPECT(!!st[~sf5]);
        BEAST_EXPECT(!!~st[~sf5]);
        Buffer b(1);
        st[sf5] = std::move(b);
        BEAST_EXPECT(b.empty());
        BEAST_EXPECT(Slice(st[sf5]).size() == 1);
        st[~sf4] = std::nullopt;
        BEAST_EXPECT(!~st[~sf4]);
    }
}

// UDT blobs

{
    STObject st(sfGeneric);
    BEAST_EXPECT(!st[~sf5]);
    auto const kp =
        generateKeyPair(KeyType::secp256k1, generateSeed("masterpassphrase"));
    st[sf5] = kp.first;
    st[~sf5] = std::nullopt;
}

// By reference fields

{
    auto const& sf = sfIndexes;
    STObject st(sfGeneric);
    std::vector<uint256> v;
    v.emplace_back(1);
    v.emplace_back(2);
    st[sf] = v;
    st[sf] = std::move(v);
    auto const& cst = st;
    BEAST_EXPECT(cst[sf].size() == 2);
    BEAST_EXPECT(cst[~sf]->size() == 2);
    BEAST_EXPECT(cst[sf][0] == 1);
    BEAST_EXPECT(cst[sf][1] == 2);
    static_assert(
        std::is_same<decltype(cst[sfIndexes]), std::vector<uint256> const&>::
            value,
        "");
}

// Default by reference field

{
    auto const& sf1 = sfIndexes;
    auto const& sf2 = sfHashes;
    auto const& sf3 = sfAmendments;
    SOTemplate const sot{
        {sf1, soeREQUIRED},
        {sf2, soeOPTIONAL},
        {sf3, soeDEFAULT},
    };

    STObject st(sot, sfGeneric);
    auto const& cst(st);
    BEAST_EXPECT(cst[sf1].size() == 0);
    BEAST_EXPECT(!cst[~sf2]);
    BEAST_EXPECT(cst[sf3].size() == 0);
    std::vector<uint256> v;
    v.emplace_back(1);
    st[sf1] = v;
    BEAST_EXPECT(cst[sf1].size() == 1);
    BEAST_EXPECT(cst[sf1][0] == uint256{1});
    st[sf2] = v;
    BEAST_EXPECT(cst[sf2].size() == 1);
    BEAST_EXPECT(cst[sf2][0] == uint256{1});
    st[~sf2] = std::nullopt;
    BEAST_EXPECT(!st[~sf2]);
    st[sf3] = v;
    BEAST_EXPECT(cst[sf3].size() == 1);
    BEAST_EXPECT(cst[sf3][0] == uint256{1});
    st[sf3] = std::vector<uint256>{};
    BEAST_EXPECT(cst[sf3].size() == 0);
}
}  // namespace ripple

void
testMalformed()
{
    testcase("Malformed serialized forms");

    try
    {
        std::array<std::uint8_t, 7> const payload{
            {0xe9, 0x12, 0xab, 0xcd, 0x12, 0xfe, 0xdc}};
        SerialIter sit{makeSlice(payload)};
        auto obj = std::make_shared<STArray>(sit, sfMetadata);
        BEAST_EXPECT(!obj);
    }
    catch (std::exception const& e)
    {
        BEAST_EXPECT(strcmp(e.what(), "Duplicate field detected") == 0);
    }

    try
    {
        std::array<std::uint8_t, 3> const payload{{0xe2, 0xe1, 0xe2}};
        SerialIter sit{makeSlice(payload)};
        auto obj = std::make_shared<STObject>(sit, sfMetadata);
        BEAST_EXPECT(!obj);
    }
    catch (std::exception const& e)
    {
        BEAST_EXPECT(strcmp(e.what(), "Duplicate field detected") == 0);
    }
}

void
run() override
{
    // Instantiate a jtx::Env so debugLog writes are exercised.
    test::jtx::Env env(*this);

    testFields();
    testSerialization();
    testParseJSONArray();
    testParseJSONArrayWithInvalidChildrenObjects();
    testParseJSONEdgeCases();
    testMalformed();
}
}
;

BEAST_DEFINE_TESTSUITE(STObject, protocol, ripple);

}  // ripple
