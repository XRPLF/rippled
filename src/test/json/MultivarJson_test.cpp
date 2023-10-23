//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/XRPLF/rippled/
    Copyright (c) 2023 Ripple Labs Inc.

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

#include <ripple/json/MultivarJson.h>
#include <ripple/rpc/impl/RPCHelpers.h>

#include <ripple/beast/unit_test.h>
#include "ripple/beast/unit_test/suite.hpp"
#include "ripple/json/json_value.h"
#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>

namespace ripple {
namespace test {

struct MultivarJson_test : beast::unit_test::suite
{
    void
    run() override
    {
        constexpr static Json::StaticString string1("string1");
        static Json::Value const str1{string1};

        static Json::Value const obj1{[]() {
            Json::Value obj1(Json::objectValue);
            obj1["one"] = 1;
            return obj1;
        }()};

        static Json::Value const jsonNull{};

        MultivarJson<3> const subject({str1, obj1});
        static_assert(sizeof(subject) == sizeof(subject.val));
        static_assert(subject.size == subject.val.size());
        static_assert(
            std::is_same_v<decltype(subject.val), std::array<Json::Value, 3>>);

        BEAST_EXPECT(subject.val.size() == 3);
        BEAST_EXPECT(
            (subject.val == std::array<Json::Value, 3>{str1, obj1, jsonNull}));
        BEAST_EXPECT(
            (MultivarJson<3>({obj1, str1}).val ==
             std::array<Json::Value, 3>{obj1, str1, jsonNull}));
        BEAST_EXPECT(
            (MultivarJson<3>({jsonNull, obj1, str1}).val ==
             std::array<Json::Value, 3>{jsonNull, obj1, str1}));

        {
            testcase("default copy construction / assignment");

            MultivarJson<3> x{subject};

            BEAST_EXPECT(x.val.size() == subject.val.size());
            BEAST_EXPECT(x.val[0] == subject.val[0]);
            BEAST_EXPECT(x.val[1] == subject.val[1]);
            BEAST_EXPECT(x.val[2] == subject.val[2]);
            BEAST_EXPECT(x.val == subject.val);
            BEAST_EXPECT(&x.val[0] != &subject.val[0]);
            BEAST_EXPECT(&x.val[1] != &subject.val[1]);
            BEAST_EXPECT(&x.val[2] != &subject.val[2]);

            MultivarJson<3> y;
            BEAST_EXPECT((y.val == std::array<Json::Value, 3>{}));
            y = subject;
            BEAST_EXPECT(y.val == subject.val);
            BEAST_EXPECT(&y.val[0] != &subject.val[0]);
            BEAST_EXPECT(&y.val[1] != &subject.val[1]);
            BEAST_EXPECT(&y.val[2] != &subject.val[2]);

            y = std::move(x);
            BEAST_EXPECT(y.val == subject.val);
            BEAST_EXPECT(&y.val[0] != &subject.val[0]);
            BEAST_EXPECT(&y.val[1] != &subject.val[1]);
            BEAST_EXPECT(&y.val[2] != &subject.val[2]);
        }

        {
            testcase("select");

            BEAST_EXPECT(
                subject.select([]() -> std::size_t { return 0; }) == str1);
            BEAST_EXPECT(
                subject.select([]() -> std::size_t { return 1; }) == obj1);
            BEAST_EXPECT(
                subject.select([]() -> std::size_t { return 2; }) == jsonNull);

            // Tests of requires clause - these are expected to match
            static_assert([](auto&& v) {
                return requires
                {
                    v.select([]() -> std::size_t { return 0; });
                };
            }(subject));
            static_assert([](auto&& v) {
                return requires
                {
                    v.select([]() constexpr->std::size_t { return 0; });
                };
            }(subject));
            static_assert([](auto&& v) {
                return requires
                {
                    v.select([]() mutable -> std::size_t { return 0; });
                };
            }(subject));

            // Tests of requires clause - these are expected NOT to match
            static_assert([](auto&& a) {
                return !requires
                {
                    subject.select([]() -> int { return 0; });
                };
            }(subject));
            static_assert([](auto&& v) {
                return !requires
                {
                    v.select([]() -> void {});
                };
            }(subject));
            static_assert([](auto&& v) {
                return !requires
                {
                    v.select([]() -> bool { return false; });
                };
            }(subject));
        }

        {
            struct foo_t final
            {
            };
            testcase("set");

            auto x = MultivarJson<2>{{Json::objectValue, Json::objectValue}};
            x.set("name1", 42);
            BEAST_EXPECT(x.val[0].isMember("name1"));
            BEAST_EXPECT(x.val[1].isMember("name1"));
            BEAST_EXPECT(x.val[0]["name1"].isInt());
            BEAST_EXPECT(x.val[1]["name1"].isInt());
            BEAST_EXPECT(x.val[0]["name1"].asInt() == 42);
            BEAST_EXPECT(x.val[1]["name1"].asInt() == 42);

            x.set("name2", "bar");
            BEAST_EXPECT(x.val[0].isMember("name2"));
            BEAST_EXPECT(x.val[1].isMember("name2"));
            BEAST_EXPECT(x.val[0]["name2"].isString());
            BEAST_EXPECT(x.val[1]["name2"].isString());
            BEAST_EXPECT(x.val[0]["name2"].asString() == "bar");
            BEAST_EXPECT(x.val[1]["name2"].asString() == "bar");

            // Tests of requires clause - these are expected to match
            static_assert([](auto&& v) {
                return requires
                {
                    v.set("name", Json::nullValue);
                };
            }(x));
            static_assert([](auto&& v) {
                return requires
                {
                    v.set("name", "value");
                };
            }(x));
            static_assert([](auto&& v) {
                return requires
                {
                    v.set("name", true);
                };
            }(x));
            static_assert([](auto&& v) {
                return requires
                {
                    v.set("name", 42);
                };
            }(x));

            // Tests of requires clause - these are expected NOT to match
            static_assert([](auto&& v) {
                return !requires
                {
                    v.set("name", foo_t{});
                };
            }(x));
            static_assert([](auto&& v) {
                return !requires
                {
                    v.set("name", std::nullopt);
                };
            }(x));
        }

        {
            testcase("isMember");

            // Well defined behaviour even if we have different types of members
            BEAST_EXPECT(subject.isMember("foo") == decltype(subject)::none);

            auto const makeJson = [](const char* key, int val) {
                Json::Value obj1(Json::objectValue);
                obj1[key] = val;
                return obj1;
            };

            {
                // All variants have element "One", none have element "Two"
                MultivarJson<2> const s1{
                    {makeJson("One", 12), makeJson("One", 42)}};
                BEAST_EXPECT(s1.isMember("One") == decltype(s1)::all);
                BEAST_EXPECT(s1.isMember("Two") == decltype(s1)::none);
            }

            {
                // Some variants have element "One" and some have "Two"
                MultivarJson<2> const s2{
                    {makeJson("One", 12), makeJson("Two", 42)}};
                BEAST_EXPECT(s2.isMember("One") == decltype(s2)::some);
                BEAST_EXPECT(s2.isMember("Two") == decltype(s2)::some);
            }

            {
                // Not all variants have element "One", because last one is null
                MultivarJson<3> const s3{
                    {makeJson("One", 12), makeJson("One", 42), {}}};
                BEAST_EXPECT(s3.isMember("One") == decltype(s3)::some);
                BEAST_EXPECT(s3.isMember("Two") == decltype(s3)::none);
            }
        }

        {
            // NOTE It's fine to change this test when we change API versions
            testcase("apiVersionSelector");

            static_assert(MultiApiJson::size == 2);
            static MultiApiJson x{{obj1, str1}};

            static_assert(
                std::is_same_v<decltype(apiVersionSelector(1)()), std::size_t>);
            static_assert([](auto&& v) {
                return requires
                {
                    v.select(apiVersionSelector(1));
                };
            }(x));

            BEAST_EXPECT(x.select(apiVersionSelector(0)) == obj1);
            BEAST_EXPECT(x.select(apiVersionSelector(2)) == str1);

            static_assert(apiVersionSelector(0)() == 0);
            static_assert(apiVersionSelector(1)() == 0);
            static_assert(apiVersionSelector(2)() == 1);
            static_assert(apiVersionSelector(3)() == 1);
            static_assert(
                apiVersionSelector(
                    std::numeric_limits<unsigned int>::max())() == 1);
        }

        {
            // There should be no reson to change this test
            testcase("apiVersionSelector invariants");

            static_assert(
                apiVersionSelector(RPC::apiMinimumSupportedVersion)() == 0);
            static_assert(
                apiVersionSelector(RPC::apiBetaVersion)() + 1  //
                == MultiApiJson::size);

            BEAST_EXPECT(MultiApiJson::size >= 1);
        }
    }
};

BEAST_DEFINE_TESTSUITE(MultivarJson, ripple_basics, ripple);

}  // namespace test
}  // namespace ripple
