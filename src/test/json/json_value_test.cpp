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
#include <ripple/json/json_writer.h>
#include <ripple/beast/unit_test.h>
#include <ripple/beast/type_name.h>

#include <algorithm>

namespace ripple {

struct json_value_test : beast::unit_test::suite
{
    void test_bool()
    {
        BEAST_EXPECT(! Json::Value());

        BEAST_EXPECT(! Json::Value(""));

        BEAST_EXPECT(bool (Json::Value("empty")));
        BEAST_EXPECT(bool (Json::Value(false)));
        BEAST_EXPECT(bool (Json::Value(true)));
        BEAST_EXPECT(bool (Json::Value(0)));
        BEAST_EXPECT(bool (Json::Value(1)));

        Json::Value array (Json::arrayValue);
        BEAST_EXPECT(! array);
        array.append(0);
        BEAST_EXPECT(bool (array));

        Json::Value object (Json::objectValue);
        BEAST_EXPECT(! object);
        object[""] = false;
        BEAST_EXPECT(bool (object));
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

        BEAST_EXPECT(r1.parse (json, j1));
        BEAST_EXPECT(j1["max_uint"].asUInt() == max_uint);
        BEAST_EXPECT(j1["max_int"].asInt() == max_int);
        BEAST_EXPECT(j1["min_int"].asInt() == min_int);
        BEAST_EXPECT(j1["a_uint"].asUInt() == a_uint);
        BEAST_EXPECT(j1["a_uint"] > a_large_int);
        BEAST_EXPECT(j1["a_uint"] > a_small_int);
        BEAST_EXPECT(j1["a_large_int"].asInt() == a_large_int);
        BEAST_EXPECT(j1["a_large_int"].asUInt() == a_large_int);
        BEAST_EXPECT(j1["a_large_int"] < a_uint);
        BEAST_EXPECT(j1["a_small_int"].asInt() == a_small_int);
        BEAST_EXPECT(j1["a_small_int"] < a_uint);

        json  = "{\"overflow\":";
        json += std::to_string(std::uint64_t(max_uint) + 1);
        json += "}";

        Json::Value j2;
        Json::Reader r2;

        BEAST_EXPECT(!r2.parse (json, j2));

        json  = "{\"underflow\":";
        json += std::to_string(std::int64_t(min_int) - 1);
        json += "}";

        Json::Value j3;
        Json::Reader r3;

        BEAST_EXPECT(!r3.parse (json, j3));

        pass ();
    }

    void
    test_copy ()
    {
        Json::Value v1{2.5};
        BEAST_EXPECT(v1.isDouble ());
        BEAST_EXPECT(v1.asDouble () == 2.5);

        Json::Value v2 = v1;
        BEAST_EXPECT(v1.isDouble ());
        BEAST_EXPECT(v1.asDouble () == 2.5);
        BEAST_EXPECT(v2.isDouble ());
        BEAST_EXPECT(v2.asDouble () == 2.5);
        BEAST_EXPECT(v1 == v2);

        v1 = v2;
        BEAST_EXPECT(v1.isDouble ());
        BEAST_EXPECT(v1.asDouble () == 2.5);
        BEAST_EXPECT(v2.isDouble ());
        BEAST_EXPECT(v2.asDouble () == 2.5);
        BEAST_EXPECT(v1 == v2);

        pass ();
    }

    void
    test_move ()
    {
        Json::Value v1{2.5};
        BEAST_EXPECT(v1.isDouble ());
        BEAST_EXPECT(v1.asDouble () == 2.5);

        Json::Value v2 = std::move(v1);
        BEAST_EXPECT(!v1);
        BEAST_EXPECT(v2.isDouble ());
        BEAST_EXPECT(v2.asDouble () == 2.5);
        BEAST_EXPECT(v1 != v2);

        v1 = std::move(v2);
        BEAST_EXPECT(v1.isDouble ());
        BEAST_EXPECT(v1.asDouble () == 2.5);
        BEAST_EXPECT(! v2);
        BEAST_EXPECT(v1 != v2);

        pass ();
    }

    void
    test_comparisons()
    {
        Json::Value a, b;
        auto testEquals = [&] (std::string const& name) {
            BEAST_EXPECT(a == b);
            BEAST_EXPECT(a <= b);
            BEAST_EXPECT(a >= b);

            BEAST_EXPECT(! (a != b));
            BEAST_EXPECT(! (a < b));
            BEAST_EXPECT(! (a > b));

            BEAST_EXPECT(b == a);
            BEAST_EXPECT(b <= a);
            BEAST_EXPECT(b >= a);

            BEAST_EXPECT(! (b != a));
            BEAST_EXPECT(! (b < a));
            BEAST_EXPECT(! (b > a));
        };

        auto testGreaterThan = [&] (std::string const& name) {
            BEAST_EXPECT(! (a == b));
            BEAST_EXPECT(! (a <= b));
            BEAST_EXPECT(a >= b);

            BEAST_EXPECT(a != b);
            BEAST_EXPECT(! (a < b));
            BEAST_EXPECT(a > b);

            BEAST_EXPECT(! (b == a));
            BEAST_EXPECT(b <= a);
            BEAST_EXPECT(! (b >= a));

            BEAST_EXPECT(b != a);
            BEAST_EXPECT(b < a);
            BEAST_EXPECT(! (b > a));
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

    void test_compact ()
    {
        Json::Value j;
        Json::Reader r;
        char const* s ("{\"array\":[{\"12\":23},{},null,false,0.5]}");

        auto countLines = [](std::string const & s)
        {
            return 1 + std::count_if(s.begin(), s.end(), [](char c){
                return c == '\n';
            });
        };

        BEAST_EXPECT(r.parse(s,j));
        {
            std::stringstream ss;
            ss << j;
            BEAST_EXPECT(countLines(ss.str()) > 1);
        }
        {
            std::stringstream ss;
            ss << Json::Compact(std::move(j));
            BEAST_EXPECT(countLines(ss.str()) == 1);
        }
    }

    void test_nest_limits ()
    {
        Json::Reader r;
        {
            auto nest = [](std::uint32_t depth)->std::string {
                    std::string s = "{";
                    for (std::uint32_t i{1}; i <= depth; ++i)
                        s += "\"obj\":{";
                    for (std::uint32_t i{1}; i <= depth; ++i)
                        s += "}";
                    s += "}";
                    return s;
                };

            {
                // Within object nest limit
                auto json{nest(std::min(10u, Json::Reader::nest_limit))};
                Json::Value j;
                BEAST_EXPECT(r.parse(json, j));
            }

            {
                // Exceed object nest limit
                auto json{nest(Json::Reader::nest_limit + 1)};
                Json::Value j;
                BEAST_EXPECT(!r.parse(json, j));
            }
        }

        auto nest = [](std::uint32_t depth)->std::string {
            std::string s = "{";
                for (std::uint32_t i{1}; i <= depth; ++i)
                    s += "\"array\":[{";
                for (std::uint32_t i{1}; i <= depth; ++i)
                    s += "]}";
                s += "}";
                return s;
            };
        {
            // Exceed array nest limit
            auto json{nest(Json::Reader::nest_limit + 1)};
            Json::Value j;
            BEAST_EXPECT(!r.parse(json, j));
        }
    }

    void run ()
    {
        test_bool ();
        test_bad_json ();
        test_edge_cases ();
        test_copy ();
        test_move ();
        test_comparisons ();
        test_compact ();
        test_nest_limits ();
    }
};

BEAST_DEFINE_TESTSUITE(json_value, json, ripple);

} // ripple
