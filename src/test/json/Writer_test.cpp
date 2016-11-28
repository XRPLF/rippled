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
#include <ripple/json/json_writer.h>
#include <ripple/json/Writer.h>
#include <test/json/TestOutputSuite.h>
#include <ripple/beast/unit_test.h>

namespace Json {

class JsonWriter_test : public ripple::test::TestOutputSuite
{
public:
    void testTrivial ()
    {
        setup ("trivial");
        BEAST_EXPECT(output_.empty ());
        expectResult("");
    }

    void testNearTrivial ()
    {
        setup ("near trivial");
        BEAST_EXPECT(output_.empty ());
        writer_->output (0);
        expectResult("0");
    }

    void testPrimitives ()
    {
        setup ("true");
        writer_->output (true);
        expectResult ("true");

        setup ("false");
        writer_->output (false);
        expectResult ("false");

        setup ("23");
        writer_->output (23);
        expectResult ("23");

        setup ("23.0");
        writer_->output (23.0);
        expectResult ("23.0");

        setup ("23.5");
        writer_->output (23.5);
        expectResult ("23.5");

        setup ("a string");
        writer_->output ("a string");
        expectResult ("\"a string\"");

        setup ("nullptr");
        writer_->output (nullptr);
        expectResult ("null");
    }

    void testEmpty ()
    {
        setup ("empty array");
        writer_->startRoot (Writer::array);
        writer_->finish ();
        expectResult ("[]");

        setup ("empty object");
        writer_->startRoot (Writer::object);
        writer_->finish ();
        expectResult ("{}");
    }

    void testEscaping ()
    {
        setup ("backslash");
        writer_->output ("\\");
        expectResult ("\"\\\\\"");

        setup ("quote");
        writer_->output ("\"");
        expectResult ("\"\\\"\"");

        setup ("backslash and quote");
        writer_->output ("\\\"");
        expectResult ("\"\\\\\\\"\"");

        setup ("escape embedded");
        writer_->output ("this contains a \\ in the middle of it.");
        expectResult ("\"this contains a \\\\ in the middle of it.\"");

        setup ("remaining escapes");
        writer_->output ("\b\f\n\r\t");
        expectResult ("\"\\b\\f\\n\\r\\t\"");
    }

    void testArray ()
    {
        setup ("empty array");
        writer_->startRoot (Writer::array);
        writer_->append (12);
        writer_->finish ();
        expectResult ("[12]");
    }

    void testLongArray ()
    {
        setup ("long array");
        writer_->startRoot (Writer::array);
        writer_->append (12);
        writer_->append (true);
        writer_->append ("hello");
        writer_->finish ();
        expectResult ("[12,true,\"hello\"]");
    }

    void testEmbeddedArraySimple ()
    {
        setup ("embedded array simple");
        writer_->startRoot (Writer::array);
        writer_->startAppend (Writer::array);
        writer_->finish ();
        writer_->finish ();
        expectResult ("[[]]");
    }

    void testObject ()
    {
        setup ("object");
        writer_->startRoot (Writer::object);
        writer_->set ("hello", "world");
        writer_->finish ();

        expectResult ("{\"hello\":\"world\"}");
    }

    void testComplexObject ()
    {
        setup ("complex object");
        writer_->startRoot (Writer::object);

        writer_->set ("hello", "world");
        writer_->startSet (Writer::array, "array");

        writer_->append (true);
        writer_->append (12);
        writer_->startAppend (Writer::array);
        writer_->startAppend (Writer::object);
        writer_->set ("goodbye", "cruel world.");
        writer_->startSet (Writer::array, "subarray");
        writer_->append (23.5);
        writer_->finishAll ();

        expectResult ("{\"hello\":\"world\",\"array\":[true,12,"
                      "[{\"goodbye\":\"cruel world.\","
                      "\"subarray\":[23.5]}]]}");
    }

    void testJson ()
    {
        setup ("object");
        Json::Value value (Json::objectValue);
        value["foo"] = 23;
        writer_->startRoot (Writer::object);
        writer_->set ("hello", value);
        writer_->finish ();

        expectResult ("{\"hello\":{\"foo\":23}}");
    }

    void run () override
    {
        testTrivial ();
        testNearTrivial ();
        testPrimitives ();
        testEmpty ();
        testEscaping ();
        testArray ();
        testLongArray ();
        testEmbeddedArraySimple ();
        testObject ();
        testComplexObject ();
        testJson();
    }
};

BEAST_DEFINE_TESTSUITE(JsonWriter, ripple_basics, ripple);

} // Json
