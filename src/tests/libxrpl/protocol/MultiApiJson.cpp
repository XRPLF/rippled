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

#include <doctest/doctest.h>
#include <xrpl/protocol/MultiApiJson.h>

#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>
#include <utility>

namespace ripple {
namespace test {

namespace {

// This needs to be in a namespace because of deduction guide
template <typename... Ts>
struct Overload : Ts...
{
    using Ts::operator()...;
};
template <typename... Ts>
Overload(Ts...) -> Overload<Ts...>;

}  // namespace

static auto
makeJson(char const* key, int val)
{
    Json::Value obj(Json::objectValue);
    obj[key] = val;
    return obj;
}

struct MultiApiJsonFixture
{
    using MultiApiJson13 = ripple::detail::MultiApiJson<1, 3>;

    Json::Value const obj1{makeJson("value", 1)};
    Json::Value const obj2{makeJson("value", 2)};
    Json::Value const obj3{makeJson("value", 3)};
    Json::Value const jsonNull{};

    MultiApiJsonFixture() = default;
};
TEST_SUITE_BEGIN("MultiApiJson");

TEST_CASE_METHOD(
    MultiApiJsonFixture,
    "forApiVersions, forAllApiVersions",
    "[MultiApiJson]")
{
    using ripple::detail::MultiApiJson;

    MultiApiJson13 subject{};
    static_assert(sizeof(subject) == sizeof(subject.val));
    static_assert(subject.size == subject.val.size());
    static_assert(
        std::is_same_v<decltype(subject.val), std::array<Json::Value, 3>>);

    CHECK(subject.val.size() == 3);
    CHECK(subject.val == std::array<Json::Value, 3>{jsonNull, jsonNull, jsonNull});

    subject.val[0] = obj1;
    subject.val[1] = obj2;

    // Some static data for test inputs
    static int const primes[] = {2,  3,  5,  7,  11, 13, 17, 19, 23,
                                 29, 31, 37, 41, 43, 47, 53, 59, 61,
                                 67, 71, 73, 79, 83, 89, 97};
    static_assert(std::size(primes) > RPC::apiMaximumValidVersion);

    MultiApiJson13 s1{};
    static_assert(
        s1.size ==
        RPC::apiMaximumValidVersion + 1 - RPC::apiMinimumSupportedVersion);

    int productAllVersions = 1;
    for (unsigned i = RPC::apiMinimumSupportedVersion;
         i <= RPC::apiMaximumValidVersion;
         ++i)
    {
        auto const index = i - RPC::apiMinimumSupportedVersion;
        CHECK(index == s1.index(i));
        CHECK(s1.valid(i));
        s1.val[index] = makeJson("value", primes[i]);
        productAllVersions *= primes[i];
    }
    CHECK(!s1.valid(0));
    CHECK(!s1.valid(RPC::apiMaximumValidVersion + 1));
    CHECK(
        !s1.valid(std::numeric_limits<
                  decltype(RPC::apiMaximumValidVersion.value)>::max()));

    int result = 1;
    static_assert(
        RPC::apiMinimumSupportedVersion + 1 <= RPC::apiMaximumValidVersion);
    forApiVersions<
        RPC::apiMinimumSupportedVersion,
        RPC::apiMinimumSupportedVersion + 1>(
        std::as_const(s1).visit(),
        [](
            Json::Value const& json,
            unsigned int version,
            int* result) {
            CHECK(
                version >= RPC::apiMinimumSupportedVersion &&
                version <= RPC::apiMinimumSupportedVersion + 1);
            if (CHECK(json.isMember("value")))
            {
                *result *= json["value"].asInt();
            }
        },
        &result);
    CHECK(
        result ==
        primes[RPC::apiMinimumSupportedVersion] *
            primes[RPC::apiMinimumSupportedVersion + 1]);

    // Check all the values with mutable data
    forAllApiVersions(
        s1.visit(), [&s1](Json::Value& json, auto version) {
            CHECK(s1.val[s1.index(version)] == json);
            if (CHECK(json.isMember("value")))
            {
                CHECK(json["value"].asInt() == primes[version]);
            }
        });

    result = 1;
    forAllApiVersions(
        std::as_const(s1).visit(),
        [](
            Json::Value const& json,
            unsigned int version,
            int* result) {
            CHECK(
                version >= RPC::apiMinimumSupportedVersion &&
                version <= RPC::apiMaximumValidVersion);
            if (CHECK(json.isMember("value")))
            {
                *result *= json["value"].asInt();
            }
        },
        &result);

    CHECK(result == productAllVersions);

    // Several overloads we want to fail
    static_assert([](auto&& v) {
        return !requires {
            forAllApiVersions(
                std::forward<decltype(v)>(v).visit(),  //
                [](Json::Value&, auto) {});            // missing const
        };
    }(std::as_const(s1)));
    static_assert([](auto&& v) {
        return !requires {
            forAllApiVersions(
                std::forward<decltype(v)>(v).visit(),  //
                [](Json::Value&) {});                  // missing const
        };
    }(std::as_const(s1)));
    static_assert([](auto&& v) {
        return !requires {
            forAllApiVersions(
                std::forward<decltype(v)>(v).visit(),  //
                []() {});  // missing parameters
        };
    }(std::as_const(s1)));
    static_assert([](auto&& v) {
        return !requires {
            forAllApiVersions(
                std::forward<decltype(v)>(v).visit(),  //
                [](auto) {},
                1);  // missing parameters
        };
    }(std::as_const(s1)));
    static_assert([](auto&& v) {
        return !requires {
            forAllApiVersions(
                std::forward<decltype(v)>(v).visit(),  //
                [](auto, auto) {},
                1);  // missing parameters
        };
    }(std::as_const(s1)));
    static_assert([](auto&& v) {
        return !requires {
            forAllApiVersions(
                std::forward<decltype(v)>(v).visit(),  //
                [](auto, auto, char const*) {},
                1);  // parameter type mismatch
        };
    }(std::as_const(s1)));

    // Sanity checks
    static_assert([](auto&& v) {
        return requires {
            forAllApiVersions(
                std::forward<decltype(v)>(v).visit(),  //
                [](auto) {});
        };
    }(s1));
    static_assert([](auto&& v) {
        return requires {
            forAllApiVersions(
                std::forward<decltype(v)>(v).visit(),  //
                [](Json::Value const&) {});
        };
    }(std::as_const(s1)));
    static_assert([](auto&& v) {
        return requires {
            forAllApiVersions(
                std::forward<decltype(v)>(v).visit(),  //
                [](auto...) {});
        };
    }(s1));
    static_assert([](auto&& v) {
        return requires {
            forAllApiVersions(
                std::forward<decltype(v)>(v).visit(),  //
                [](Json::Value const&, auto...) {});
        };
    }(std::as_const(s1)));
    static_assert([](auto&& v) {
        return requires {
            forAllApiVersions(
                std::forward<decltype(v)>(v).visit(),  //
                [](Json::Value&, auto, auto, auto...) {},
                0,
                "");
        };
    }(s1));
    static_assert([](auto&& v) {
        return requires {
            forAllApiVersions(
                std::forward<decltype(v)>(v).visit(),  //
                []<unsigned int Version>(
                    Json::Value const&,
                    std::integral_constant<unsigned int, Version>,
                    int,
                    char const*) {},
                0,
                "");
        };
    }(std::as_const(s1)));
    static_assert([](auto&& v) {
        return requires {
            forAllApiVersions(
                std::forward<decltype(v)>(v).visit(),  //
                [](auto...) {});
        };
    }(std::move(s1)));
    static_assert([](auto&& v) {
        return requires {
            forAllApiVersions(
                std::forward<decltype(v)>(v).visit(),  //
                [](auto...) {});
        };
    }(std::move(std::as_const(s1))));
}

TEST_CASE_METHOD(
    MultiApiJsonFixture,
    "default copy construction / assignment",
    "[MultiApiJson]")
{
    using ripple::detail::MultiApiJson;

    MultiApiJson13 subject{};
    subject.val[0] = obj1;
    subject.val[1] = obj2;

    MultiApiJson13 x{subject};

    CHECK(x.val.size() == subject.val.size());
    CHECK(x.val[0] == subject.val[0]);
    CHECK(x.val[1] == subject.val[1]);
    CHECK(x.val[2] == subject.val[2]);
    CHECK(x.val == subject.val);
    CHECK(&x.val[0] != &subject.val[0]);
    CHECK(&x.val[1] != &subject.val[1]);
    CHECK(&x.val[2] != &subject.val[2]);

    MultiApiJson13 y;
    CHECK((y.val == std::array<Json::Value, 3>{}));
    y = subject;
    CHECK(y.val == subject.val);
    CHECK(&y.val[0] != &subject.val[0]);
    CHECK(&y.val[1] != &subject.val[1]);
    CHECK(&y.val[2] != &subject.val[2]);

    y = std::move(x);
    CHECK(y.val == subject.val);
    CHECK(&y.val[0] != &subject.val[0]);
    CHECK(&y.val[1] != &subject.val[1]);
    CHECK(&y.val[2] != &subject.val[2]);
}

TEST_CASE_METHOD(MultiApiJsonFixture, "set", "[MultiApiJson]")
{
    using ripple::detail::MultiApiJson;

    auto x = MultiApiJson<1, 2>{Json::objectValue};
    x.set("name1", 42);
    CHECK(x.val[0].isMember("name1"));
    CHECK(x.val[1].isMember("name1"));
    CHECK(x.val[0]["name1"].isInt());
    CHECK(x.val[1]["name1"].isInt());
    CHECK(x.val[0]["name1"].asInt() == 42);
    CHECK(x.val[1]["name1"].asInt() == 42);

    x.set("name2", "bar");
    CHECK(x.val[0].isMember("name2"));
    CHECK(x.val[1].isMember("name2"));
    CHECK(x.val[0]["name2"].isString());
    CHECK(x.val[1]["name2"].isString());
    CHECK(x.val[0]["name2"].asString() == "bar");
    CHECK(x.val[1]["name2"].asString() == "bar");

    // Tests of requires clause - these are expected to match
    static_assert([](auto&& v) {
        return requires { v.set("name", Json::nullValue); };
    }(x));
    static_assert([](auto&& v) {
        return requires { v.set("name", "value"); };
    }(x));
    static_assert(
        [](auto&& v) { return requires { v.set("name", true); }; }(x));
    static_assert(
        [](auto&& v) { return requires { v.set("name", 42); }; }(x));

    // Tests of requires clause - these are expected NOT to match
    struct foo_t final {};
    static_assert([](auto&& v) {
        return !requires { v.set("name", foo_t{}); };
    }(x));
    static_assert([](auto&& v) {
        return !requires { v.set("name", std::nullopt); };
    }(x));
}

TEST_CASE_METHOD(MultiApiJsonFixture, "isMember", "[MultiApiJson]")
{
    using ripple::detail::MultiApiJson;

    MultiApiJson13 subject{};
    subject.val[0] = obj1;
    subject.val[1] = obj2;

    // Well defined behaviour even if we have different types of members
    CHECK(subject.isMember("foo") == decltype(subject)::none);

    {
        // All variants have element "One", none have element "Two"
        MultiApiJson<1, 2> s1{};
        s1.val[0] = makeJson("One", 12);
        s1.val[1] = makeJson("One", 42);
        CHECK(s1.isMember("One") == decltype(s1)::all);
        CHECK(s1.isMember("Two") == decltype(s1)::none);
    }

    {
        // Some variants have element "One" and some have "Two"
        MultiApiJson<1, 2> s2{};
        s2.val[0] = makeJson("One", 12);
        s2.val[1] = makeJson("Two", 42);
        CHECK(s2.isMember("One") == decltype(s2)::some);
        CHECK(s2.isMember("Two") == decltype(s2)::some);
    }

    {
        // Not all variants have element "One", because last one is null
        MultiApiJson<1, 3> s3{};
        s3.val[0] = makeJson("One", 12);
        s3.val[1] = makeJson("One", 42);
        CHECK(s3.isMember("One") == decltype(s3)::some);
        CHECK(s3.isMember("Two") == decltype(s3)::none);
    }
}

TEST_CASE_METHOD(MultiApiJsonFixture, "visitor", "[MultiApiJson]")
{
    using ripple::detail::MultiApiJson;

    MultiApiJson13 s1{};
    s1.val[0] = makeJson("value", 2);
    s1.val[1] = makeJson("value", 3);
    s1.val[2] = makeJson("value", 5);

    CHECK(not s1.valid(0));
    CHECK(s1.index(0) == 0);

    CHECK(s1.valid(1));
    CHECK(s1.index(1) == 0);

    CHECK(not s1.valid(4));

    // Test different overloads
    static_assert([](auto&& v) {
        return requires {
            v.visitor(
                v,
                std::integral_constant<unsigned, 1>{},
                [](Json::Value&, std::integral_constant<unsigned, 1>) {});
        };
    }(s1));
    CHECK(
        s1.visitor(
            s1,
            std::integral_constant<unsigned, 1>{},
            Overload{
                [](Json::Value& v,
                   std::integral_constant<unsigned, 1>) {
                    return v["value"].asInt();
                },
                [](Json::Value const&, auto) { return 0; },
                [](auto, auto) { return 0; }}) == 2);

    static_assert([](auto&& v) {
        return requires {
            v.visitor(
                v,
                std::integral_constant<unsigned, 1>{},
                [](Json::Value&) {});
        };
    }(s1));
    CHECK(
        s1.visitor(
            s1,
            std::integral_constant<unsigned, 1>{},
            Overload{
                [](Json::Value& v) { return v["value"].asInt(); },
                [](Json::Value const&) { return 0; },
                [](auto...) { return 0; }}) == 2);

    static_assert([](auto&& v) {
        return requires {
            v.visitor(
                v,
                std::integral_constant<unsigned, 1>{},
                [](Json::Value const&,
                   std::integral_constant<unsigned, 1>) {});
        };
    }(std::as_const(s1)));
    CHECK(
        s1.visitor(
            std::as_const(s1),
            std::integral_constant<unsigned, 2>{},
            Overload{
                [](Json::Value const& v,
                   std::integral_constant<unsigned, 2>) {
                    return v["value"].asInt();
                },
                [](Json::Value&, auto) { return 0; },
                [](auto, auto) { return 0; }}) == 3);

    static_assert([](auto&& v) {
        return requires {
            v.visitor(
                v,
                std::integral_constant<unsigned, 1>{},
                [](auto, auto, auto...) {});
        };
    }(s1));

    static_assert([](auto&& v) {
        return requires {
            v.visitor(v, 1, [](auto, auto, auto...) {});
        };
    }(s1));

    static_assert([](auto&& v) {
        return requires {
            v.visitor(v, 1, [](auto, auto, auto...) {}, "");
        };
    }(s1));

    static_assert([](auto&& v) {
        return requires {
            v.visitor(v, 1, [](auto, auto, auto, auto...) {}, "");
        };
    }(s1));
}

TEST_CASE_METHOD(MultiApiJsonFixture, "visit", "[MultiApiJson]")
{
    using ripple::detail::MultiApiJson;

    MultiApiJson13 s1{};
    s1.val[0] = makeJson("value", 2);
    s1.val[1] = makeJson("value", 3);
    s1.val[2] = makeJson("value", 5);

    // Test different overloads
    static_assert([](auto&& v) {
        return requires {
            v.visit(
                std::integral_constant<unsigned, 1>{},
                [](Json::Value&, std::integral_constant<unsigned, 1>) {});
        };
    }(s1));
    CHECK(
        s1.visit(
            std::integral_constant<unsigned, 1>{},
            Overload{
                [](Json::Value& v,
                   std::integral_constant<unsigned, 1>) {
                    return v["value"].asInt();
                },
                [](Json::Value const&, auto) { return 0; },
                [](auto, auto) { return 0; }}) == 2);
    static_assert([](auto&& v) {
        return requires {
            v.visit()(
                std::integral_constant<unsigned, 1>{},
                [](Json::Value&, std::integral_constant<unsigned, 1>) {});
        };
    }(s1));
    CHECK(
        s1.visit()(std::integral_constant<unsigned, 1>{},
                   Overload{[](Json::Value& v,
                              std::integral_constant<unsigned, 1>) {
                                return v["value"].asInt();
                            },
                            [](Json::Value const&, auto) { return 0; },
                            [](auto, auto) { return 0; }}) == 2);
    static_assert([](auto&& v) {
        return requires {
            v.visit(
                std::integral_constant<unsigned, 1>{},
                [](Json::Value&) {});
        };
    }(s1));
    CHECK(
        s1.visit(
            std::integral_constant<unsigned, 1>{},
            Overload{
                [](Json::Value& v) { return v["value"].asInt(); },
                [](Json::Value const&) { return 0; },
                [](auto...) { return 0; }}) == 2);
    static_assert([](auto&& v) {
        return requires {
            v.visit()(std::integral_constant<unsigned, 1>{}, [](Json::Value&) {});
        };
    }(s1));
    CHECK(
        s1.visit()(std::integral_constant<unsigned, 1>{},
                   Overload{[](Json::Value& v) { return v["value"].asInt(); },
                            [](Json::Value const&) { return 0; },
                            [](auto...) { return 0; }}) == 2);
    static_assert([](auto&& v) {
        return requires {
            v.visit(
                std::integral_constant<unsigned, 1>{},
                [](Json::Value const&,
                   std::integral_constant<unsigned, 1>) {});
        };
    }(std::as_const(s1)));
    CHECK(
        std::as_const(s1).visit(
            std::integral_constant<unsigned, 2>{},
            Overload{
                [](Json::Value const& v,
                   std::integral_constant<unsigned, 2>) {
                    return v["value"].asInt();
                },
                [](Json::Value&, auto) { return 0; },
                [](auto, auto) { return 0; }}) == 3);
    static_assert([](auto&& v) {
        return requires {
            v.visit()(std::integral_constant<unsigned, 1>{},
                      [](Json::Value const&,
                         std::integral_constant<unsigned, 1>) {});
        };
    }(std::as_const(s1)));
    CHECK(
        std::as_const(s1).visit()(std::integral_constant<unsigned, 2>{},
                                  Overload{[](Json::Value const& v,
                                             std::integral_constant<unsigned,
                                                            2>) {
                                                return v["value"].asInt();
                                            },
                                            [](Json::Value&, auto) { return 0; },
                                            [](auto, auto) { return 0; }}) == 3);
    static_assert([](auto&& v) {
        return requires {
            v.visit(
                std::integral_constant<unsigned, 1>{}, [](Json::Value const&) {});
        };
    }(std::as_const(s1)));
    CHECK(
        std::as_const(s1).visit(
            std::integral_constant<unsigned, 2>{},
            Overload{
                [](Json::Value const& v) { return v["value"].asInt(); },
                [](Json::Value&) { return 0; },
                [](auto...) { return 0; }}) == 3);
    static_assert([](auto&& v) {
        return requires {
            v.visit()(std::integral_constant<unsigned, 1>{},
                      [](Json::Value const&) {});
        };
    }(std::as_const(s1)));
    CHECK(
        std::as_const(s1).visit()(std::integral_constant<unsigned, 2>{},
                                  Overload{[](Json::Value const& v) {
                                               return v["value"].asInt();
                                           },
                                           [](Json::Value&) { return 0; },
                                           [](auto...) { return 0; }}) == 3);

    static_assert([](auto&& v) {
        return requires { v.visit(v, 1, [](auto, auto, auto...) {}); };
    }(s1));

    static_assert([](auto&& v) {
        return requires { v.visit(v, 1, [](auto, auto, auto...) {}, ""); };
    }(s1));

    static_assert([](auto&& v) {
        return requires {
            v.visit(v, 1, [](auto, auto, auto, auto...) {}, "");
        };
    }(s1));
}

TEST_SUITE_END();
