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

#include <BeastConfig.h>
#include <ripple/basics/Log.h>
#include <ripple/protocol/STBase.h>
#include <ripple/protocol/STAccount.h>
#include <ripple/protocol/STArray.h>
#include <ripple/protocol/STObject.h>
#include <ripple/protocol/STParsedJSON.h>
#include <ripple/json/json_reader.h>
#include <ripple/json/to_string.h>
#include <beast/unit_test/suite.h>
#include <beast/cxx14/memory.h> // <memory>

namespace ripple {

class SerializedObject_test : public beast::unit_test::suite
{
public:
    void run()
    {
        testSerialization();
        testParseJSONArray();
        testParseJSONArrayWithInvalidChildrenObjects();
    }

    bool parseJSONString (std::string const& json, Json::Value& to)
    {
        Json::Reader reader;
        return (reader.parse(json, to) &&
               !to.isNull() &&
                to.isObject());
    }

    void testParseJSONArrayWithInvalidChildrenObjects ()
    {
        testcase ("parse json array invalid children");
        try
        {
            /*

            STArray/STObject constructs don't really map perfectly to json
            arrays/objects.

            STObject is an associative container, mapping fields to value, but
            an STObject may also have a Field as it's name, stored outside the
            associative structure. The name is important, so to maintain
            fidelity, it will take TWO json objects to represent them.

            */
            std::string faulty ("{\"Template\":[{"
                                    "\"ModifiedNode\":{\"Sequence\":1}, "
                                    "\"DeletedNode\":{\"Sequence\":1}"
                                "}]}");

            std::unique_ptr<STObject> so;
            Json::Value faultyJson;
            bool parsedOK (parseJSONString(faulty, faultyJson));
            unexpected(!parsedOK, "failed to parse");
            STParsedJSONObject parsed ("test", faultyJson);
            expect (parsed.object.get() == nullptr,
                "It should have thrown. "
                  "Immediate children of STArray encoded as json must "
                  "have one key only.");
        }
        catch(std::runtime_error& e)
        {
            std::string what(e.what());
            unexpected (what.find("First level children of `Template`") != 0);
        }
    }

    void testParseJSONArray ()
    {
        testcase ("parse json array");
        std::string const json (
            "{\"Template\":[{\"ModifiedNode\":{\"Sequence\":1}}]}\n");

        Json::Value jsonObject;
        bool parsedOK (parseJSONString(json, jsonObject));
        if (parsedOK)
        {
            STParsedJSONObject parsed ("test", jsonObject);
            std::string const& serialized (
                to_string (parsed.object->getJson(0)));
            expect (serialized == json, serialized + " should equal: " + json);
        }
        else
        {
            fail ("Couldn't parse json: " + json);
        }
    }

    void testSerialization ()
    {
        testcase ("serialization");

        unexpected (sfGeneric.isUseful (), "sfGeneric must not be useful");

        SField const& sfTestVL = SField::getField (STI_VL, 255);
        SField const& sfTestH256 = SField::getField (STI_HASH256, 255);
        SField const& sfTestU32 = SField::getField (STI_UINT32, 255);
        SField const& sfTestObject = SField::getField (STI_OBJECT, 255);

        SOTemplate elements;
        elements.push_back (SOElement (sfFlags, SOE_REQUIRED));
        elements.push_back (SOElement (sfTestVL, SOE_REQUIRED));
        elements.push_back (SOElement (sfTestH256, SOE_OPTIONAL));
        elements.push_back (SOElement (sfTestU32, SOE_REQUIRED));

        STObject object1 (elements, sfTestObject);
        STObject object2 (object1);

        unexpected (object1.getSerializer () != object2.getSerializer (),
            "STObject error 1");

        unexpected (object1.isFieldPresent (sfTestH256) ||
            !object1.isFieldPresent (sfTestVL), "STObject error");

        object1.makeFieldPresent (sfTestH256);

        unexpected (!object1.isFieldPresent (sfTestH256), "STObject Error 2");

        unexpected (object1.getFieldH256 (sfTestH256) != uint256 (),
            "STObject error 3");

        if (object1.getSerializer () == object2.getSerializer ())
        {
            WriteLog (lsINFO, STObject) << "O1: " << object1.getJson (0);
            WriteLog (lsINFO, STObject) << "O2: " << object2.getJson (0);
            fail ("STObject error 4");
        }
        else
        {
            pass ();
        }

        object1.makeFieldAbsent (sfTestH256);

        unexpected (object1.isFieldPresent (sfTestH256), "STObject error 5");

        unexpected (object1.getFlags () != 0, "STObject error 6");

        unexpected (object1.getSerializer () != object2.getSerializer (),
            "STObject error 7");

        STObject copy (object1);

        unexpected (object1.isFieldPresent (sfTestH256), "STObject error 8");

        unexpected (copy.isFieldPresent (sfTestH256), "STObject error 9");

        unexpected (object1.getSerializer () != copy.getSerializer (),
            "STObject error 10");

        copy.setFieldU32 (sfTestU32, 1);

        unexpected (object1.getSerializer () == copy.getSerializer (),
            "STObject error 11");

        for (int i = 0; i < 1000; i++)
        {
            Blob j (i, 2);

            object1.setFieldVL (sfTestVL, j);

            Serializer s;
            object1.add (s);
            SerializerIterator it (s);

            STObject object3 (elements, it, sfTestObject);

            unexpected (object1.getFieldVL (sfTestVL) != j, "STObject error");

            unexpected (object3.getFieldVL (sfTestVL) != j, "STObject error");
        }
    }
};

BEAST_DEFINE_TESTSUITE(SerializedObject,ripple_data,ripple);

} // ripple
