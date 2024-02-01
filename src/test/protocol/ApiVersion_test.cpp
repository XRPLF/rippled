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

#include <ripple/beast/unit_test.h>
#include <ripple/beast/unit_test/suite.hpp>
#include <ripple/protocol/ApiVersion.h>

#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>
#include <utility>

namespace ripple {
namespace test {
struct ApiVersion_test : beast::unit_test::suite
{
    static auto
    makeJson(const char* key, int val)
    {
        Json::Value obj1(Json::objectValue);
        obj1[key] = val;
        return obj1;
    }

    void
    run() override
    {
        {
            testcase("API versions invariants");

            static_assert(
                RPC::apiMinimumSupportedVersion <=
                RPC::apiMaximumSupportedVersion);
            static_assert(
                RPC::apiMinimumSupportedVersion <= RPC::apiMaximumValidVersion);
            static_assert(
                RPC::apiMaximumSupportedVersion <= RPC::apiMaximumValidVersion);
            static_assert(RPC::apiBetaVersion <= RPC::apiMaximumValidVersion);

            BEAST_EXPECT(true);
        }

        {
            // Update when we change versions
            testcase("API versions");

            static_assert(RPC::apiMinimumSupportedVersion >= 1);
            static_assert(RPC::apiMinimumSupportedVersion < 2);
            static_assert(RPC::apiMaximumSupportedVersion >= 2);
            static_assert(RPC::apiMaximumSupportedVersion < 3);
            static_assert(RPC::apiMaximumValidVersion >= 3);
            static_assert(RPC::apiMaximumValidVersion < 4);
            static_assert(RPC::apiBetaVersion >= 3);
            static_assert(RPC::apiBetaVersion < 4);

            BEAST_EXPECT(true);
        }

        {
            testcase("forApiVersions, forAllApiVersions");

            // Some static data for test inputs
            static const int primes[] = {2,  3,  5,  7,  11, 13, 17, 19, 23,
                                         29, 31, 37, 41, 43, 47, 53, 59, 61,
                                         67, 71, 73, 79, 83, 89, 97};
            static_assert(std::size(primes) > RPC::apiMaximumValidVersion);

            MultiApiJson s1{};
            static_assert(
                s1.size ==
                RPC::apiMaximumValidVersion + 1 -
                    RPC::apiMinimumSupportedVersion);

            int productAllVersions = 1;
            for (unsigned i = RPC::apiMinimumSupportedVersion;
                 i <= RPC::apiMaximumValidVersion;
                 ++i)
            {
                auto const index = i - RPC::apiMinimumSupportedVersion;
                BEAST_EXPECT(index == s1.index(i));
                BEAST_EXPECT(s1.valid(i));
                s1.val[index] = makeJson("value", primes[i]);
                productAllVersions *= primes[i];
            }
            BEAST_EXPECT(!s1.valid(0));
            BEAST_EXPECT(!s1.valid(RPC::apiMaximumValidVersion + 1));
            BEAST_EXPECT(
                !s1.valid(std::numeric_limits<decltype(
                              RPC::apiMaximumValidVersion.value)>::max()));

            int result = 1;
            static_assert(
                RPC::apiMinimumSupportedVersion + 1 <=
                RPC::apiMaximumValidVersion);
            forApiVersions<
                RPC::apiMinimumSupportedVersion,
                RPC::apiMinimumSupportedVersion + 1>(
                std::as_const(s1).visit(),
                [this](
                    Json::Value const& json,
                    unsigned int version,
                    int* result) {
                    BEAST_EXPECT(
                        version >= RPC::apiMinimumSupportedVersion &&
                        version <= RPC::apiMinimumSupportedVersion + 1);
                    if (BEAST_EXPECT(json.isMember("value")))
                    {
                        *result *= json["value"].asInt();
                    }
                },
                &result);
            BEAST_EXPECT(
                result ==
                primes[RPC::apiMinimumSupportedVersion] *
                    primes[RPC::apiMinimumSupportedVersion + 1]);

            // Check all the values with mutable data
            forAllApiVersions(
                s1.visit(), [&s1, this](Json::Value& json, auto version) {
                    BEAST_EXPECT(s1.val[s1.index(version)] == json);
                    if (BEAST_EXPECT(json.isMember("value")))
                    {
                        BEAST_EXPECT(json["value"].asInt() == primes[version]);
                    }
                });

            result = 1;
            forAllApiVersions(
                std::as_const(s1).visit(),
                [this](
                    Json::Value const& json,
                    unsigned int version,
                    int* result) {
                    BEAST_EXPECT(
                        version >= RPC::apiMinimumSupportedVersion &&
                        version <= RPC::apiMaximumValidVersion);
                    if (BEAST_EXPECT(json.isMember("value")))
                    {
                        *result *= json["value"].asInt();
                    }
                },
                &result);

            BEAST_EXPECT(result == productAllVersions);

            // Several overloads we want to fail
            static_assert([](auto&& v) {
                return !requires
                {
                    forAllApiVersions(
                        std::forward<decltype(v)>(v).visit(),  //
                        [](Json::Value&, auto) {});            // missing const
                };
            }(std::as_const(s1)));
            static_assert([](auto&& v) {
                return !requires
                {
                    forAllApiVersions(
                        std::forward<decltype(v)>(v).visit(),  //
                        [](Json::Value&) {});                  // missing const
                };
            }(std::as_const(s1)));
            static_assert([](auto&& v) {
                return !requires
                {
                    forAllApiVersions(
                        std::forward<decltype(v)>(v).visit(),  //
                        []() {});  // missing parameters
                };
            }(std::as_const(s1)));
            static_assert([](auto&& v) {
                return !requires
                {
                    forAllApiVersions(
                        std::forward<decltype(v)>(v).visit(),  //
                        [](auto) {},
                        1);  // missing parameters
                };
            }(std::as_const(s1)));
            static_assert([](auto&& v) {
                return !requires
                {
                    forAllApiVersions(
                        std::forward<decltype(v)>(v).visit(),  //
                        [](auto, auto) {},
                        1);  // missing parameters
                };
            }(std::as_const(s1)));
            static_assert([](auto&& v) {
                return !requires
                {
                    forAllApiVersions(
                        std::forward<decltype(v)>(v).visit(),  //
                        [](auto, auto, const char*) {},
                        1);  // parameter type mismatch
                };
            }(std::as_const(s1)));

            // Sanity checks
            static_assert([](auto&& v) {
                return requires
                {
                    forAllApiVersions(
                        std::forward<decltype(v)>(v).visit(),  //
                        [](auto) {});
                };
            }(s1));
            static_assert([](auto&& v) {
                return requires
                {
                    forAllApiVersions(
                        std::forward<decltype(v)>(v).visit(),  //
                        [](Json::Value const&) {});
                };
            }(std::as_const(s1)));
            static_assert([](auto&& v) {
                return requires
                {
                    forAllApiVersions(
                        std::forward<decltype(v)>(v).visit(),  //
                        [](auto...) {});
                };
            }(s1));
            static_assert([](auto&& v) {
                return requires
                {
                    forAllApiVersions(
                        std::forward<decltype(v)>(v).visit(),  //
                        [](Json::Value const&, auto...) {});
                };
            }(std::as_const(s1)));
            static_assert([](auto&& v) {
                return requires
                {
                    forAllApiVersions(
                        std::forward<decltype(v)>(v).visit(),  //
                        [](Json::Value&, auto, auto, auto...) {},
                        0,
                        "");
                };
            }(s1));
            static_assert([](auto&& v) {
                return requires
                {
                    forAllApiVersions(
                        std::forward<decltype(v)>(v).visit(),  //
                        []<unsigned int Version>(
                            Json::Value const&,
                            std::integral_constant<unsigned int, Version>,
                            int,
                            const char*) {},
                        0,
                        "");
                };
            }(std::as_const(s1)));
            static_assert([](auto&& v) {
                return requires
                {
                    forAllApiVersions(
                        std::forward<decltype(v)>(v).visit(),  //
                        [](auto...) {});
                };
            }(std::move(s1)));
            static_assert([](auto&& v) {
                return requires
                {
                    forAllApiVersions(
                        std::forward<decltype(v)>(v).visit(),  //
                        [](auto...) {});
                };
            }(std::move(std::as_const(s1))));
        }
    }
};

BEAST_DEFINE_TESTSUITE(ApiVersion, protocol, ripple);

}  // namespace test
}  // namespace ripple
