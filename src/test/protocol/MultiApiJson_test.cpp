#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ApiVersion.h>
#include <xrpl/protocol/MultiApiJson.h>

#include <array>
#include <iterator>
#include <limits>
#include <optional>
#include <type_traits>
#include <utility>

namespace xrpl::test {

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

struct MultiApiJson_test : beast::unit_test::Suite
{
    static auto
    makeJson(char const* key, int val)
    {
        json::Value obj1(json::ValueType::Object);
        obj1[key] = val;
        return obj1;
    }

    void
    run() override
    {
        using xrpl::detail::MultiApiJson;

        json::Value const obj1 = makeJson("value", 1);
        json::Value const obj2 = makeJson("value", 2);
        json::Value const obj3 = makeJson("value", 3);
        json::Value const jsonNull{};

        MultiApiJson<1, 3> subject{};
        static_assert(sizeof(subject) == sizeof(subject.val));
        static_assert(subject.kSize == subject.val.size());
        static_assert(std::is_same_v<decltype(subject.val), std::array<json::Value, 3>>);

        BEAST_EXPECT(subject.val.size() == 3);
        BEAST_EXPECT((subject.val == std::array<json::Value, 3>{jsonNull, jsonNull, jsonNull}));

        subject.val[0] = obj1;
        subject.val[1] = obj2;

        {
            testcase("forApiVersions, forAllApiVersions");

            // Some static data for test inputs
            static int const kPrimes[] = {2,  3,  5,  7,  11, 13, 17, 19, 23, 29, 31, 37, 41,
                                          43, 47, 53, 59, 61, 67, 71, 73, 79, 83, 89, 97};
            static_assert(std::size(kPrimes) > RPC::kApiMaximumValidVersion);

            MultiApiJson<1, 3> s1{};
            static_assert(
                s1.kSize == RPC::kApiMaximumValidVersion + 1 - RPC::kApiMinimumSupportedVersion);

            int productAllVersions = 1;
            for (unsigned i = RPC::kApiMinimumSupportedVersion; i <= RPC::kApiMaximumValidVersion;
                 ++i)
            {
                auto const index = i - RPC::kApiMinimumSupportedVersion;
                BEAST_EXPECT(index == s1.index(i));
                BEAST_EXPECT(s1.valid(i));
                s1.val[index] = makeJson("value", kPrimes[i]);
                productAllVersions *= kPrimes[i];
            }
            BEAST_EXPECT(!s1.valid(0));
            BEAST_EXPECT(!s1.valid(RPC::kApiMaximumValidVersion + 1));
            BEAST_EXPECT(!s1.valid(
                std::numeric_limits<decltype(RPC::kApiMaximumValidVersion.value)>::max()));

            int result = 1;
            static_assert(RPC::kApiMinimumSupportedVersion + 1 <= RPC::kApiMaximumValidVersion);
            forApiVersions<RPC::kApiMinimumSupportedVersion, RPC::kApiMinimumSupportedVersion + 1>(
                std::as_const(s1).visit(),
                [this](json::Value const& json, unsigned int version, int* result) {
                    BEAST_EXPECT(
                        version >= RPC::kApiMinimumSupportedVersion &&
                        version <= RPC::kApiMinimumSupportedVersion + 1);
                    if (BEAST_EXPECT(json.isMember("value")))
                    {
                        *result *= json["value"].asInt();
                    }
                },
                &result);
            BEAST_EXPECT(
                result ==
                kPrimes[RPC::kApiMinimumSupportedVersion] *
                    kPrimes[RPC::kApiMinimumSupportedVersion + 1]);

            // Check all the values with mutable data
            forAllApiVersions(s1.visit(), [&s1, this](json::Value& json, auto version) {
                BEAST_EXPECT(s1.val[s1.index(version)] == json);
                if (BEAST_EXPECT(json.isMember("value")))
                {
                    BEAST_EXPECT(json["value"].asInt() == kPrimes[version]);
                }
            });

            result = 1;
            forAllApiVersions(
                std::as_const(s1).visit(),
                [this](json::Value const& json, unsigned int version, int* result) {
                    BEAST_EXPECT(
                        version >= RPC::kApiMinimumSupportedVersion &&
                        version <= RPC::kApiMaximumValidVersion);
                    if (BEAST_EXPECT(json.isMember("value")))
                    {
                        *result *= json["value"].asInt();
                    }
                },
                &result);

            BEAST_EXPECT(result == productAllVersions);

            // Several overloads we want to fail
            static_assert([](auto&& v) {
                return !requires {
                    forAllApiVersions(
                        std::forward<decltype(v)>(v).visit(),  //
                        [](json::Value&, auto) {});            // missing const
                };
            }(std::as_const(s1)));
            static_assert([](auto&& v) {
                return !requires {
                    forAllApiVersions(
                        std::forward<decltype(v)>(v).visit(),  //
                        [](json::Value&) {});                  // missing const
                };
            }(std::as_const(s1)));
            static_assert([](auto&& v) {
                return !requires {
                    forAllApiVersions(
                        std::forward<decltype(v)>(v).visit(),  //
                        []() {});                              // missing parameters
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
                        [](json::Value const&) {});
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
                        [](json::Value const&, auto...) {});
                };
            }(std::as_const(s1)));
            static_assert([](auto&& v) {
                return requires {
                    forAllApiVersions(
                        std::forward<decltype(v)>(v).visit(),  //
                        [](json::Value&, auto, auto, auto...) {},
                        0,
                        "");
                };
            }(s1));
            static_assert([](auto&& v) {
                return requires {
                    forAllApiVersions(
                        std::forward<decltype(v)>(v).visit(),  //
                        []<unsigned int Version>(
                            json::Value const&,
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
            }(std::move(std::as_const(s1))));  // NOLINT(performance-move-const-arg)
        }

        {
            testcase("default copy construction / assignment");

            MultiApiJson<1, 3> x{subject};

            BEAST_EXPECT(x.val.size() == subject.val.size());
            BEAST_EXPECT(x.val[0] == subject.val[0]);
            BEAST_EXPECT(x.val[1] == subject.val[1]);
            BEAST_EXPECT(x.val[2] == subject.val[2]);
            BEAST_EXPECT(x.val == subject.val);
            BEAST_EXPECT(&x.val[0] != &subject.val[0]);
            BEAST_EXPECT(&x.val[1] != &subject.val[1]);
            BEAST_EXPECT(&x.val[2] != &subject.val[2]);

            MultiApiJson<1, 3> y;
            BEAST_EXPECT((y.val == std::array<json::Value, 3>{}));
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
            testcase("set");

            auto x = MultiApiJson<1, 2>{json::ValueType::Object};
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
            static_assert(
                [](auto&& v) { return requires { v.set("name", json::ValueType::Null); }; }(x));
            static_assert([](auto&& v) { return requires { v.set("name", "value"); }; }(x));
            static_assert([](auto&& v) { return requires { v.set("name", true); }; }(x));
            static_assert([](auto&& v) { return requires { v.set("name", 42); }; }(x));

            // Tests of requires clause - these are expected NOT to match
            struct FooT final
            {
            };
            static_assert([](auto&& v) { return !requires { v.set("name", FooT{}); }; }(x));
            static_assert([](auto&& v) { return !requires { v.set("name", std::nullopt); }; }(x));
        }

        {
            testcase("isMember");

            // Well defined behaviour even if we have different types of members
            BEAST_EXPECT(subject.isMember("foo") == decltype(subject)::IsMemberResult::None);

            {
                // All variants have element "One", none have element "Two"
                MultiApiJson<1, 2> s1{};
                s1.val[0] = makeJson("One", 12);
                s1.val[1] = makeJson("One", 42);
                BEAST_EXPECT(s1.isMember("One") == decltype(s1)::IsMemberResult::All);
                BEAST_EXPECT(s1.isMember("Two") == decltype(s1)::IsMemberResult::None);
            }

            {
                // Some variants have element "One" and some have "Two"
                MultiApiJson<1, 2> s2{};
                s2.val[0] = makeJson("One", 12);
                s2.val[1] = makeJson("Two", 42);
                BEAST_EXPECT(s2.isMember("One") == decltype(s2)::IsMemberResult::Some);
                BEAST_EXPECT(s2.isMember("Two") == decltype(s2)::IsMemberResult::Some);
            }

            {
                // Not all variants have element "One", because last one is null
                MultiApiJson<1, 3> s3{};
                s3.val[0] = makeJson("One", 12);
                s3.val[1] = makeJson("One", 42);
                BEAST_EXPECT(s3.isMember("One") == decltype(s3)::IsMemberResult::Some);
                BEAST_EXPECT(s3.isMember("Two") == decltype(s3)::IsMemberResult::None);
            }
        }

        {
            testcase("visitor");

            MultiApiJson<1, 3> s1{};
            s1.val[0] = makeJson("value", 2);
            s1.val[1] = makeJson("value", 3);
            s1.val[2] = makeJson("value", 5);

            BEAST_EXPECT(not s1.valid(0));
            BEAST_EXPECT(s1.index(0) == 0);

            BEAST_EXPECT(s1.valid(1));
            BEAST_EXPECT(s1.index(1) == 0);

            BEAST_EXPECT(not s1.valid(4));

            // Test different overloads
            static_assert([](auto&& v) {
                return requires {
                    v.kVisitor(
                        v,
                        std::integral_constant<unsigned, 1>{},
                        [](json::Value&, std::integral_constant<unsigned, 1>) {});
                };
            }(s1));
            BEAST_EXPECT(
                s1.kVisitor(
                    s1,
                    std::integral_constant<unsigned, 1>{},
                    Overload{
                        [](json::Value& v, std::integral_constant<unsigned, 1>) {
                            return v["value"].asInt();
                        },
                        [](json::Value const&, auto) { return 0; },
                        [](auto, auto) { return 0; }}) == 2);

            static_assert([](auto&& v) {
                return requires {
                    v.kVisitor(v, std::integral_constant<unsigned, 1>{}, [](json::Value&) {});
                };
            }(s1));
            BEAST_EXPECT(
                s1.kVisitor(
                    s1,
                    std::integral_constant<unsigned, 1>{},
                    Overload{
                        [](json::Value& v) { return v["value"].asInt(); },
                        [](json::Value const&) { return 0; },
                        [](auto...) { return 0; }}) == 2);

            static_assert([](auto&& v) {
                return requires {
                    v.kVisitor(
                        v,
                        std::integral_constant<unsigned, 1>{},
                        [](json::Value const&, std::integral_constant<unsigned, 1>) {});
                };
            }(std::as_const(s1)));
            BEAST_EXPECT(
                s1.kVisitor(
                    std::as_const(s1),
                    std::integral_constant<unsigned, 2>{},
                    Overload{
                        [](json::Value const& v, std::integral_constant<unsigned, 2>) {
                            return v["value"].asInt();
                        },
                        [](json::Value&, auto) { return 0; },
                        [](auto, auto) { return 0; }}) == 3);

            static_assert([](auto&& v) {
                return requires {
                    v.kVisitor(v, std::integral_constant<unsigned, 1>{}, [](json::Value const&) {});
                };
            }(std::as_const(s1)));
            BEAST_EXPECT(
                s1.kVisitor(
                    std::as_const(s1),
                    std::integral_constant<unsigned, 2>{},
                    Overload{
                        [](json::Value const& v) { return v["value"].asInt(); },
                        [](json::Value&) { return 0; },
                        [](auto...) { return 0; }}) == 3);

            static_assert([](auto&& v) {
                return requires { v.kVisitor(v, 1, [](json::Value&, unsigned) {}); };
            }(s1));
            BEAST_EXPECT(
                s1.kVisitor(
                    s1,  //
                    3u,
                    Overload{
                        [](json::Value& v, unsigned) { return v["value"].asInt(); },
                        [](json::Value const&, unsigned) { return 0; },
                        [](auto, auto) { return 0; }}) == 5);

            static_assert(
                [](auto&& v) { return requires { v.kVisitor(v, 1, [](json::Value&) {}); }; }(s1));
            BEAST_EXPECT(
                s1.kVisitor(
                    s1,  //
                    3,
                    Overload{
                        [](json::Value& v) { return v["value"].asInt(); },
                        [](json::Value const&) { return 0; },
                        [](auto...) { return 0; }}) == 5);

            static_assert([](auto&& v) {
                return requires { v.kVisitor(v, 1, [](json::Value const&, unsigned) {}); };
            }(std::as_const(s1)));
            BEAST_EXPECT(
                s1.kVisitor(
                    std::as_const(s1),  //
                    2u,
                    Overload{
                        [](json::Value const& v, unsigned) { return v["value"].asInt(); },
                        [](json::Value const&, auto) { return 0; },
                        [](auto, auto) { return 0; }}) == 3);

            static_assert([](auto&& v) {
                return requires { v.kVisitor(v, 1, [](json::Value const&) {}); };
            }(std::as_const(s1)));
            BEAST_EXPECT(
                s1.kVisitor(
                    std::as_const(s1),  //
                    2,
                    Overload{
                        [](json::Value const& v) { return v["value"].asInt(); },
                        [](json::Value&) { return 0; },
                        [](auto...) { return 0; }}) == 3);

            // Test type conversions
            BEAST_EXPECT(
                s1.kVisitor(
                    s1,
                    std::integral_constant<unsigned, 1>{},  // to unsigned
                    [](json::Value& v, unsigned) { return v["value"].asInt(); }) == 2);
            BEAST_EXPECT(
                s1.kVisitor(
                    std::as_const(s1),
                    std::integral_constant<unsigned, 2>{},  // to unsigned
                    [](json::Value const& v, unsigned) { return v["value"].asInt(); }) == 3);
            BEAST_EXPECT(
                s1.kVisitor(
                    s1,  // to const
                    std::integral_constant<unsigned, 3>{},
                    [](json::Value const& v, auto) { return v["value"].asInt(); }) == 5);
            BEAST_EXPECT(
                s1.kVisitor(
                    s1,  // to const
                    std::integral_constant<unsigned, 3>{},
                    [](json::Value const& v) { return v["value"].asInt(); }) == 5);
            BEAST_EXPECT(
                s1.kVisitor(
                    s1,
                    3,  // to long
                    [](json::Value& v, long) { return v["value"].asInt(); }) == 5);
            BEAST_EXPECT(
                s1.kVisitor(
                    std::as_const(s1),
                    1,  // to long
                    [](json::Value const& v, long) { return v["value"].asInt(); }) == 2);
            BEAST_EXPECT(
                s1.kVisitor(
                    s1,  // to const
                    2,
                    [](json::Value const& v, auto) { return v["value"].asInt(); }) == 3);
            BEAST_EXPECT(
                s1.kVisitor(
                    s1,  // type deduction
                    2,
                    [](auto& v, auto) { return v["value"].asInt(); }) == 3);
            BEAST_EXPECT(
                s1.kVisitor(
                    s1,  // to const, type deduction
                    2,
                    [](auto const& v, auto) { return v["value"].asInt(); }) == 3);
            BEAST_EXPECT(
                s1.kVisitor(
                    s1,  // type deduction
                    2,
                    [](auto& v) { return v["value"].asInt(); }) == 3);
            BEAST_EXPECT(
                s1.kVisitor(
                    s1,  // to const, type deduction
                    2,
                    [](auto const& v) { return v["value"].asInt(); }) == 3);

            // Test passing of additional arguments
            BEAST_EXPECT(
                s1.kVisitor(
                    s1,
                    std::integral_constant<unsigned, 2>{},
                    [](json::Value& v, auto ver, auto a1, auto a2) {
                        return ver * a1 * a2 * v["value"].asInt();
                    },
                    5,
                    7) == 2 * 5 * 7 * 3);
            BEAST_EXPECT(
                s1.kVisitor(
                    s1,
                    std::integral_constant<unsigned, 2>{},
                    [](json::Value& v, auto ver, auto... args) {
                        return ver * (1 * ... * args) * v["value"].asInt();
                    },
                    5,
                    7) == 2 * 5 * 7 * 3);

            // Several overloads we want to fail
            static_assert([](auto&& v) {
                return !requires {
                    v.kVisitor(
                        v,
                        1,                           //
                        [](json::Value&, auto) {});  // missing const
                };
            }(std::as_const(s1)));

            static_assert([](auto&& v) {
                return !requires {
                    v.kVisitor(
                        decltype(v){},  // cannot bind rvalue
                        1,
                        [](json::Value&, auto) {});
                };
            }(s1));

            static_assert([](auto&& v) {
                return !requires {
                    v.kVisitor(
                        v,
                        1,         //
                        []() {});  // missing parameter
                };
            }(s1));

            static_assert([](auto&& v) {
                return !requires {
                    v.kVisitor(
                        v,
                        1,                               //
                        [](json::Value&, int, int) {});  // too many parameters
                };
            }(s1));

            // Want these to be unambiguous
            static_assert([](auto&& v) { return requires { v.kVisitor(v, 1, [](auto) {}); }; }(s1));

            static_assert(
                [](auto&& v) { return requires { v.kVisitor(v, 1, [](json::Value&) {}); }; }(s1));

            static_assert([](auto&& v) {
                return requires { v.kVisitor(v, 1, [](json::Value&, auto...) {}); };
            }(s1));

            static_assert([](auto&& v) {
                return requires { v.kVisitor(v, 1, [](json::Value const&) {}); };
            }(s1));

            static_assert([](auto&& v) {
                return requires { v.kVisitor(v, 1, [](json::Value const&, auto...) {}); };
            }(s1));

            static_assert(
                [](auto&& v) { return requires { v.kVisitor(v, 1, [](auto...) {}); }; }(s1));

            static_assert(
                [](auto&& v) { return requires { v.kVisitor(v, 1, [](auto, auto...) {}); }; }(s1));

            static_assert([](auto&& v) {
                return requires { v.kVisitor(v, 1, [](auto, auto, auto...) {}); };
            }(s1));

            static_assert([](auto&& v) {
                return requires { v.kVisitor(v, 1, [](auto, auto, auto...) {}, ""); };
            }(s1));

            static_assert([](auto&& v) {
                return requires { v.kVisitor(v, 1, [](auto, auto, auto, auto...) {}, ""); };
            }(s1));
        }

        {
            testcase("visit");

            MultiApiJson<1, 3> s1{};
            s1.val[0] = makeJson("value", 2);
            s1.val[1] = makeJson("value", 3);
            s1.val[2] = makeJson("value", 5);

            // Test different overloads
            static_assert([](auto&& v) {
                return requires {
                    v.visit(
                        std::integral_constant<unsigned, 1>{},
                        [](json::Value&, std::integral_constant<unsigned, 1>) {});
                };
            }(s1));
            BEAST_EXPECT(
                s1.visit(
                    std::integral_constant<unsigned, 1>{},
                    Overload{
                        [](json::Value& v, std::integral_constant<unsigned, 1>) {
                            return v["value"].asInt();
                        },
                        [](json::Value const&, auto) { return 0; },
                        [](auto, auto) { return 0; }}) == 2);
            static_assert([](auto&& v) {
                return requires {
                    v.visit()(
                        std::integral_constant<unsigned, 1>{},
                        [](json::Value&, std::integral_constant<unsigned, 1>) {});
                };
            }(s1));
            BEAST_EXPECT(
                s1.visit()(
                    std::integral_constant<unsigned, 1>{},
                    Overload{
                        [](json::Value& v, std::integral_constant<unsigned, 1>) {
                            return v["value"].asInt();
                        },
                        [](json::Value const&, auto) { return 0; },
                        [](auto, auto) { return 0; }}) == 2);

            static_assert([](auto&& v) {
                return requires {
                    v.visit(std::integral_constant<unsigned, 1>{}, [](json::Value&) {});
                };
            }(s1));
            BEAST_EXPECT(
                s1.visit(
                    std::integral_constant<unsigned, 1>{},
                    Overload{
                        [](json::Value& v) { return v["value"].asInt(); },
                        [](json::Value const&) { return 0; },
                        [](auto...) { return 0; }}) == 2);
            static_assert([](auto&& v) {
                return requires {
                    v.visit()(std::integral_constant<unsigned, 1>{}, [](json::Value&) {});
                };
            }(s1));
            BEAST_EXPECT(
                s1.visit()(
                    std::integral_constant<unsigned, 1>{},
                    Overload{
                        [](json::Value& v) { return v["value"].asInt(); },
                        [](json::Value const&) { return 0; },
                        [](auto...) { return 0; }}) == 2);

            static_assert([](auto&& v) {
                return requires {
                    v.visit(
                        std::integral_constant<unsigned, 1>{},
                        [](json::Value const&, std::integral_constant<unsigned, 1>) {});
                };
            }(std::as_const(s1)));
            BEAST_EXPECT(
                std::as_const(s1).visit(
                    std::integral_constant<unsigned, 2>{},
                    Overload{
                        [](json::Value const& v, std::integral_constant<unsigned, 2>) {
                            return v["value"].asInt();
                        },
                        [](json::Value&, auto) { return 0; },
                        [](auto, auto) { return 0; }}) == 3);
            static_assert([](auto&& v) {
                return requires {
                    v.visit()(
                        std::integral_constant<unsigned, 1>{},
                        [](json::Value const&, std::integral_constant<unsigned, 1>) {});
                };
            }(std::as_const(s1)));
            BEAST_EXPECT(
                std::as_const(s1).visit()(
                    std::integral_constant<unsigned, 2>{},
                    Overload{
                        [](json::Value const& v, std::integral_constant<unsigned, 2>) {
                            return v["value"].asInt();
                        },
                        [](json::Value&, auto) { return 0; },
                        [](auto, auto) { return 0; }}) == 3);

            static_assert([](auto&& v) {
                return requires {
                    v.visit(std::integral_constant<unsigned, 1>{}, [](json::Value const&) {});
                };
            }(std::as_const(s1)));
            BEAST_EXPECT(
                std::as_const(s1).visit(
                    std::integral_constant<unsigned, 2>{},
                    Overload{
                        [](json::Value const& v) { return v["value"].asInt(); },
                        [](json::Value&) { return 0; },
                        [](auto...) { return 0; }}) == 3);
            static_assert([](auto&& v) {
                return requires {
                    v.visit()(std::integral_constant<unsigned, 1>{}, [](json::Value const&) {});
                };
            }(std::as_const(s1)));
            BEAST_EXPECT(
                std::as_const(s1).visit()(
                    std::integral_constant<unsigned, 2>{},
                    Overload{
                        [](json::Value const& v) { return v["value"].asInt(); },
                        [](json::Value&) { return 0; },
                        [](auto...) { return 0; }}) == 3);

            static_assert([](auto&& v) {
                return requires { v.visit(1, [](json::Value&, unsigned) {}); };
            }(s1));
            BEAST_EXPECT(
                s1.visit(
                    3u,
                    Overload{
                        [](json::Value& v, unsigned) { return v["value"].asInt(); },
                        [](json::Value const&, unsigned) { return 0; },
                        [](json::Value&, auto) { return 0; },
                        [](auto, auto) { return 0; }}) == 5);
            static_assert([](auto&& v) {
                return requires { v.visit()(1, [](json::Value&, unsigned) {}); };
            }(s1));
            BEAST_EXPECT(
                s1.visit()(
                    3u,
                    Overload{
                        [](json::Value& v, unsigned) { return v["value"].asInt(); },
                        [](json::Value const&, unsigned) { return 0; },
                        [](json::Value&, auto) { return 0; },
                        [](auto, auto) { return 0; }}) == 5);

            static_assert(
                [](auto&& v) { return requires { v.visit(1, [](json::Value&) {}); }; }(s1));
            BEAST_EXPECT(
                s1.visit(
                    3,
                    Overload{
                        [](json::Value& v) { return v["value"].asInt(); },
                        [](json::Value const&) { return 0; },
                        [](auto...) { return 0; }}) == 5);
            static_assert(
                [](auto&& v) { return requires { v.visit()(1, [](json::Value&) {}); }; }(s1));
            BEAST_EXPECT(
                s1.visit()(
                    3,
                    Overload{
                        [](json::Value& v) { return v["value"].asInt(); },
                        [](json::Value const&) { return 0; },
                        [](auto...) { return 0; }}) == 5);

            static_assert([](auto&& v) {
                return requires { v.visit(1, [](json::Value const&, unsigned) {}); };
            }(std::as_const(s1)));
            BEAST_EXPECT(
                std::as_const(s1).visit(
                    2u,
                    Overload{
                        [](json::Value const& v, unsigned) { return v["value"].asInt(); },
                        [](json::Value const&, auto) { return 0; },
                        [](json::Value&, unsigned) { return 0; },
                        [](auto, auto) { return 0; }}) == 3);
            static_assert([](auto&& v) {
                return requires { v.visit()(1, [](json::Value const&, unsigned) {}); };
            }(std::as_const(s1)));
            BEAST_EXPECT(
                std::as_const(s1).visit()(
                    2u,
                    Overload{
                        [](json::Value const& v, unsigned) { return v["value"].asInt(); },
                        [](json::Value const&, auto) { return 0; },
                        [](json::Value&, unsigned) { return 0; },
                        [](auto, auto) { return 0; }}) == 3);

            static_assert([](auto&& v) {
                return requires { v.visit(1, [](json::Value const&) {}); };
            }(std::as_const(s1)));
            BEAST_EXPECT(
                std::as_const(s1).visit(
                    2,
                    Overload{
                        [](json::Value const& v) { return v["value"].asInt(); },
                        [](json::Value&) { return 0; },
                        [](auto...) { return 0; }}) == 3);
            static_assert([](auto&& v) {
                return requires { v.visit()(1, [](json::Value const&) {}); };
            }(std::as_const(s1)));
            BEAST_EXPECT(
                std::as_const(s1).visit()(
                    2,
                    Overload{
                        [](json::Value const& v) { return v["value"].asInt(); },
                        [](json::Value&) { return 0; },
                        [](auto...) { return 0; }}) == 3);

            // Rvalue MultivarJson visitor only binds to regular reference
            static_assert([](auto&& v) {
                // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
                return !requires { std::forward<decltype(v)>(v).visit(1, [](json::Value&&) {}); };
            }(std::move(s1)));
            static_assert([](auto&& v) {
                return !requires {
                    std::forward<decltype(v)>(v).visit(1, [](json::Value const&&) {});
                };
            }(std::move(s1)));
            static_assert([](auto&& v) {
                return requires { std::forward<decltype(v)>(v).visit(1, [](json::Value&) {}); };
            }(std::move(s1)));
            static_assert([](auto&& v) {
                return requires {
                    std::forward<decltype(v)>(v).visit(1, [](json::Value const&) {});
                };
            }(std::move(s1)));
            static_assert([](auto&& v) {
                // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
                return !requires { std::forward<decltype(v)>(v).visit()(1, [](json::Value&&) {}); };
            }(std::move(s1)));
            static_assert([](auto&& v) {
                return !requires {
                    std::forward<decltype(v)>(v).visit()(1, [](json::Value const&&) {});
                };
            }(std::move(s1)));
            static_assert([](auto&& v) {
                return requires { std::forward<decltype(v)>(v).visit()(1, [](json::Value&) {}); };
            }(std::move(s1)));
            static_assert([](auto&& v) {
                return requires {
                    std::forward<decltype(v)>(v).visit()(1, [](json::Value const&) {});
                };
            }(std::move(s1)));
            static_assert([](auto&& v) {
                return !requires {
                    std::forward<decltype(v)>(v).visit(1, [](json::Value const&&) {});
                };
            }(std::move(std::as_const(s1))));  // NOLINT(performance-move-const-arg)
            static_assert([](auto&& v) {
                return requires {
                    std::forward<decltype(v)>(v).visit(1, [](json::Value const&) {});
                };
            }(std::move(std::as_const(s1))));  // NOLINT(performance-move-const-arg)
            static_assert([](auto&& v) {
                return !requires {
                    std::forward<decltype(v)>(v).visit()(1, [](json::Value const&&) {});
                };
            }(std::move(std::as_const(s1))));  // NOLINT(performance-move-const-arg)
            static_assert([](auto&& v) {
                return requires {
                    std::forward<decltype(v)>(v).visit()(1, [](json::Value const&) {});
                };
            }(std::move(std::as_const(s1))));  // NOLINT(performance-move-const-arg)

            // Missing const
            static_assert([](auto&& v) {
                return !requires {
                    std::forward<decltype(v)>(v).visit(1, [](json::Value&, auto) {});
                };
            }(std::as_const(s1)));
            static_assert([](auto&& v) {
                return !requires {
                    std::forward<decltype(v)>(v).visit()(1, [](json::Value&, auto) {});
                };
            }(std::as_const(s1)));

            // Missing parameter
            static_assert([](auto&& v) {
                return !requires { std::forward<decltype(v)>(v).visit(1, []() {}); };
            }(s1));
            static_assert([](auto&& v) {
                return !requires { std::forward<decltype(v)>(v).visit()(1, []() {}); };
            }(s1));

            // Sanity checks
            static_assert([](auto&& v) {
                return requires { std::forward<decltype(v)>(v).visit(1, [](auto...) {}); };
            }(std::as_const(s1)));
            static_assert([](auto&& v) {
                return requires { std::forward<decltype(v)>(v).visit()(1, [](auto...) {}); };
            }(std::as_const(s1)));
        }
    }
};

BEAST_DEFINE_TESTSUITE(MultiApiJson, protocol, xrpl);

}  // namespace xrpl::test
