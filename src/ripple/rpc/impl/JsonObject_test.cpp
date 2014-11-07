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

#include <ripple/rpc/impl/JsonObject.h>
#include <ripple/rpc/impl/TestOutputSuite.h>
#include <beast/unit_test/suite.h>

namespace ripple {
namespace RPC {
namespace New {

class JsonObject_test : public TestOutputSuite
{
public:
    void testTrivial ()
    {
        setup ("trivial");

        {
            Object::Root root (*writer_);
            (void) root;
        }
        expectResult ("{}");
    }

    void testSimple ()
    {
        setup ("simple");
        {
            Object::Root root (*writer_);
            root["hello"] = "world";
            root["skidoo"] = 23;
            root["awake"] = false;
            root["temperature"] = 98.6;
        }

        expectResult (
            "{\"hello\":\"world\","
            "\"skidoo\":23,"
            "\"awake\":false,"
            "\"temperature\":98.6}");
    }

    void testSimpleShort ()
    {
        setup ("simpleShort");
        Object::Root (*writer_)
                .set ("hello", "world")
                .set ("skidoo", 23)
                .set ("awake", false)
                .set ("temperature", 98.6);

        expectResult (
            "{\"hello\":\"world\","
            "\"skidoo\":23,"
            "\"awake\":false,"
            "\"temperature\":98.6}");
    }

    void testOneSub ()
    {
        setup ("oneSub");
        {
            Object::Root root (*writer_);
            root.makeArray ("ar");
        }
        expectResult ("{\"ar\":[]}");
    }

    void testSubs ()
    {
        setup ("subs");
        {
            Object::Root root (*writer_);

            {
                // Add an array with three entries.
                auto array = root.makeArray ("ar");
                array.append (23);
                array.append (false);
                array.append (23.5);
            }

            {
                // Add an object with one entry.
                auto obj = root.makeObject ("obj");
                obj["hello"] = "world";
            }

            {
                // Add another object with two entries.
                auto obj = root.makeObject ("obj2");
                obj["h"] = "w";
                obj["f"] = false;
            }
        }

        expectResult (
            "{\"ar\":[23,false,23.5],"
            "\"obj\":{\"hello\":\"world\"},"
            "\"obj2\":{\"h\":\"w\",\"f\":false}}");
    }

    void testSubsShort ()
    {
        setup ("subsShort");

        {
            Object::Root root (*writer_);

            // Add an array with three entries.
            root.makeArray ("ar")
                    .append (23)
                    .append (false)
                    .append (23.5);

            // Add an object with one entry.
            root.makeObject ("obj")["hello"] = "world";

            // Add another object with two entries.
            root.makeObject ("obj2")
                    .set("h", "w")
                    .set("f", false);
        }

        expectResult (
            "{\"ar\":[23,false,23.5],"
            "\"obj\":{\"hello\":\"world\"},"
            "\"obj2\":{\"h\":\"w\",\"f\":false}}");
    }

    template <typename Functor>
    void expectException (Functor f)
    {
        bool success = true;
        try
        {
            f();
            success = false;
        } catch (std::exception)
        {
        }
        expect (success, "no exception thrown");
    }

    void testFailureObject()
    {
        {
            setup ("object failure assign");
            Object::Root root (*writer_);
            auto obj = root.makeObject ("o1");
            expectException ([&]() { root["fail"] = "complete"; });
        }
        {
            setup ("object failure object");
            Object::Root root (*writer_);
            auto obj = root.makeObject ("o1");
            expectException ([&] () { root.makeObject ("o2"); });
        }
        {
            setup ("object failure Array");
            Object::Root root (*writer_);
            auto obj = root.makeArray ("o1");
            expectException ([&] () { root.makeArray ("o2"); });
        }
    }

    void testFailureArray()
    {
        {
            setup ("array failure append");
            Object::Root root (*writer_);
            auto array = root.makeArray ("array");
            auto subarray = array.makeArray ();
            auto fail = [&]() { array.append ("fail"); };
            expectException (fail);
        }
        {
            setup ("array failure makeArray");
            Object::Root root (*writer_);
            auto array = root.makeArray ("array");
            auto subarray = array.makeArray ();
            auto fail = [&]() { array.makeArray (); };
            expectException (fail);
        }
        {
            setup ("array failure makeObject");
            Object::Root root (*writer_);
            auto array = root.makeArray ("array");
            auto subarray = array.makeArray ();
            auto fail = [&]() { array.makeObject (); };
            expectException (fail);
        }
    }

    void testKeyFailure ()
    {
#ifdef DEBUG
        setup ("repeating keys");
        Object::Root root(*writer_);
        root.set ("foo", "bar")
            .set ("baz", 0);
        auto fail = [&]() { root.set ("foo", "bar"); };
        expectException (fail);
#endif
    }

    void run () override
    {
        testSimple ();
        testSimpleShort ();

        testOneSub ();
        testSubs ();
        testSubsShort ();

        testFailureObject ();
        testFailureArray ();
        testKeyFailure ();
    }
};

BEAST_DEFINE_TESTSUITE(JsonObject, ripple_basics, ripple);

} // New
} // RPC
} // ripple
