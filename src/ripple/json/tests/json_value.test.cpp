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
#include <ripple/json/json_value.h>
#include <ripple/json/json_reader.h>
#include <beast/unit_test/suite.h>
#include <beast/utility/type_name.h>

namespace ripple {

struct json_value_test : beast::unit_test::suite
{
    void test_bool()
    {
        expect (! Json::Value());

        expect (! Json::Value(""));

        expect (bool (Json::Value("empty")));
        expect (bool (Json::Value(false)));
        expect (bool (Json::Value(true)));
        expect (bool (Json::Value(0)));
        expect (bool (Json::Value(1)));

        Json::Value array (Json::arrayValue);
        expect (! array);
        array.append(0);
        expect (bool (array));

        Json::Value object (Json::objectValue);
        expect (! object);
        object[""] = false;
        expect (bool (object));
    }

    void test_bad_json ()
    {
        char const* s (
            "{\"method\":\"ledger\",\"params\":[{\"ledger_index\":1e300}]}"
            );

        Json::Value j;
        Json::Reader r;

        r.parse (s, j);
        pass ();
    }

    void test_edge_cases ()
    {
        std::string json;

        std::uint32_t max_uint = std::numeric_limits<std::uint32_t>::max ();
        std::int32_t max_int = std::numeric_limits<std::int32_t>::max ();
        std::int32_t min_int = std::numeric_limits<std::int32_t>::min ();

        std::uint32_t a_uint = max_uint - 1978;
        std::int32_t a_large_int = max_int - 1978;
        std::int32_t a_small_int = min_int + 1978;

        json  = "{\"max_uint\":"    + std::to_string (max_uint);
        json += ",\"max_int\":"     + std::to_string (max_int);
        json += ",\"min_int\":"     + std::to_string (min_int);
        json += ",\"a_uint\":"      + std::to_string (a_uint);
        json += ",\"a_large_int\":" + std::to_string (a_large_int);
        json += ",\"a_small_int\":" + std::to_string (a_small_int);
        json += "}";

        Json::Value j1;
        Json::Reader r1;

        expect (r1.parse (json, j1), "parsing integer edge cases");
        expect (j1["max_uint"].asUInt() == max_uint, "max_uint");
        expect (j1["max_int"].asInt() == max_int, "min_int");
        expect (j1["min_int"].asInt() == min_int, "max_int");
        expect (j1["a_uint"].asUInt() == a_uint, "a_uint");
        expect (j1["a_large_int"].asInt() == a_large_int, "a_large_int");
        expect (j1["a_small_int"].asInt() == a_small_int, "a_large_int");

        json  = "{\"overflow\":";
        json += std::to_string(std::uint64_t(max_uint) + 1);
        json += "}";

        Json::Value j2;
        Json::Reader r2;

        expect (!r2.parse (json, j2), "parsing unsigned integer that overflows");

        json  = "{\"underflow\":";
        json += std::to_string(std::int64_t(min_int) - 1);
        json += "}";

        Json::Value j3;
        Json::Reader r3;

        expect (!r3.parse (json, j3), "parsing signed integer that underflows");

        pass ();
    }

    void
    test_copy ()
    {
        Json::Value v1{2.5};
        expect (v1.isDouble ());
        expect (v1.asDouble () == 2.5);

        Json::Value v2 = v1;
        expect (v1.isDouble ());
        expect (v1.asDouble () == 2.5);
        expect (v2.isDouble ());
        expect (v2.asDouble () == 2.5);
        expect (v1 == v2);

        v1 = v2;
        expect (v1.isDouble ());
        expect (v1.asDouble () == 2.5);
        expect (v2.isDouble ());
        expect (v2.asDouble () == 2.5);
        expect (v1 == v2);

        pass ();
    }

    void
    test_move ()
    {
        Json::Value v1{2.5};
        expect (v1.isDouble ());
        expect (v1.asDouble () == 2.5);

        Json::Value v2 = std::move(v1);
        expect (!v1);
        expect (v2.isDouble ());
        expect (v2.asDouble () == 2.5);
        expect (v1 != v2);

        v1 = std::move(v2);
        expect (v1.isDouble ());
        expect (v1.asDouble () == 2.5);
        expect (! v2);
        expect (v1 != v2);

        pass ();
    }

    void
    test_comparisons()
    {
        Json::Value a, b;
        auto testEquals = [&] (std::string const& name) {
            expect (a == b, "a == b " + name);
            expect (a <= b, "a <= b " + name);
            expect (a >= b, "a >= b " + name);

            expect (! (a != b), "! (a != b) " + name);
            expect (! (a < b), "! (a < b) " + name);
            expect (! (a > b), "! (a > b) " + name);

            expect (b == a, "b == a " + name);
            expect (b <= a, "b <= a " + name);
            expect (b >= a, "b >= a " + name);

            expect (! (b != a), "! (b != a) " + name);
            expect (! (b < a), "! (b < a) " + name);
            expect (! (b > a), "! (b > a) " + name);
        };

        auto testGreaterThan = [&] (std::string const& name) {
            expect (! (a == b), "! (a == b) " + name);
            expect (! (a <= b), "! (a <= b) " + name);
            expect (a >= b, "a >= b " + name);

            expect (a != b, "a != b " + name);
            expect (! (a < b), "! (a < b) " + name);
            expect (a > b, "a > b " + name);

            expect (! (b == a), "! (b == a) " + name);
            expect (b <= a, "b <= a " + name);
            expect (! (b >= a), "! (b >= a) " + name);

            expect (b != a, "b != a " + name);
            expect (b < a, "b < a " + name);
            expect (! (b > a), "! (b > a) " + name);
        };

        a["a"] = Json::UInt (0);
        b["a"] = Json::Int (0);
        testEquals ("zero");

        b["a"] = Json::Int (-1);
        testGreaterThan ("negative");

        Json::Int big = std::numeric_limits<int>::max();
        Json::UInt bigger = big;
        bigger++;

        a["a"] = bigger;
        b["a"] = big;
        testGreaterThan ("big");
    }

    void run ()
    {
        test_bool ();
        test_bad_json ();
        test_edge_cases ();
        test_copy ();
        test_move ();
        test_comparisons ();
    }
};

BEAST_DEFINE_TESTSUITE(json_value, json, ripple);

} // ripple
