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

#include <test/jtx.h>

#include <xrpl/json/json_reader.h>
#include <xrpl/protocol/st.h>

namespace ripple {

class STParsedJSON_test : public beast::unit_test::suite
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

        {
            std::string const json(R"({"CloseResolution":19,"Method":250,)"
                                   R"("TransferFee":"65536"})");

            Json::Value jv;
            if (BEAST_EXPECT(parseJSONString(json, jv)))
            {
                STParsedJSONObject parsed("test", jv);
                BEAST_EXPECT(!parsed.object);
                BEAST_EXPECT(parsed.error);
                BEAST_EXPECT(parsed.error[jss::error] == "invalidParams");
                BEAST_EXPECT(
                    parsed.error[jss::error_message] ==
                    "Field 'test.TransferFee' has invalid data.");
            }
        }

        {
            std::string const json(R"({"CloseResolution":19,"Method":250,)"
                                   R"("TransferFee":"Payment"})");

            Json::Value jv;
            if (BEAST_EXPECT(parseJSONString(json, jv)))
            {
                STParsedJSONObject parsed("test", jv);
                BEAST_EXPECT(!parsed.object);
                BEAST_EXPECT(parsed.error);
                BEAST_EXPECT(parsed.error[jss::error] == "invalidParams");
                BEAST_EXPECT(
                    parsed.error[jss::error_message] ==
                    "Field 'test.TransferFee' has invalid data.");
            }
        }

        {
            std::string const json(R"({"CloseResolution":19,"Method":250,)"
                                   R"("TransferFee":true})");

            Json::Value jv;
            if (BEAST_EXPECT(parseJSONString(json, jv)))
            {
                STParsedJSONObject parsed("test", jv);
                BEAST_EXPECT(!parsed.object);
                BEAST_EXPECT(parsed.error);
                BEAST_EXPECT(parsed.error[jss::error] == "invalidParams");
                BEAST_EXPECT(
                    parsed.error[jss::error_message] ==
                    "Field 'test.TransferFee' has bad type.");
            }
        }
    }

    void
    run() override
    {
        // Instantiate a jtx::Env so debugLog writes are exercised.
        test::jtx::Env env(*this);
        testParseJSONArrayWithInvalidChildrenObjects();
        testParseJSONArray();
        testParseJSONEdgeCases();
    }
};

BEAST_DEFINE_TESTSUITE(STParsedJSON, protocol, ripple);

}  // namespace ripple
