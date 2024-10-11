//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2024 Ripple Labs Inc.

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

#ifndef RIPPLE_TEST_JTX_SUBCASES_H_INCLUDED
#define RIPPLE_TEST_JTX_SUBCASES_H_INCLUDED

#include <cstdint>
#include <functional>

namespace subcases {

constexpr std::size_t MAXIMUM_SUBCASE_DEPTH = 10;

/**
 * This short library implements a pattern found in doctest and Catch:
 *
 * TEST_CASE(testName) {
 *     // setup
 *     SUBCASE("one") {
 *         // actions and assertions
 *     }
 *     SUBCASE("two") {
 *         // actions and assertions
 *     }
 *     SUBCASE("three") {
 *         // actions and assertions
 *     }
 *     // assertions before teardown
 * }
 *
 * EXECUTE(testName);
 *
 * In short:
 *
 * - Top-level test cases are declared with `TEST_CASE(name)`.
 *   The name must be a legal identifier.
 *   It will become the name of a function.
 * - Subcases are declared with `SUBCASE("description")`.
 *   Descriptions do not need to be unique.
 * - Test cases are executed with `EXECUTE(name)`,
 *   where `name` is the one that was passed to `TEST_CASE`.
 *   When executing a test case, it will loop,
 *   executing exactly one leaf subcase in each pass,
 *   until all subcases have executed.
 *   The top-level test case is considered a subcase too.
 *
 * This lets test authors easily share common setup among multiple subcases.
 * Subcases can be nested up to `MAXIMUM_SUBCASE_DEPTH`.
 */

struct Context
{
    // The number of subcases to skip at each level to reach the next subcase.
    std::uint8_t skip[MAXIMUM_SUBCASE_DEPTH] = {0};
    // The current level.
    std::uint8_t level = 0;
    // The maximum depth at which we entered a subcase.
    std::uint8_t entered = 0;
    // The number of subcases we skipped on this or deeper levels
    // since entering a subcase.
    std::uint8_t skipped = 0;

    void
    lap()
    {
        level = 0;
        entered = 0;
        skipped = 0;
    }
};

struct Subcase
{
    Context& _;
    char const* name_;
    Subcase(Context& context, char const* name);
    ~Subcase();
    /** Return true if we should enter this subcase. */
    operator bool() const;
};

using Supercase = std::function<void(Context&)>;

void
execute(Supercase supercase);

}  // namespace subcases

#define TEST_CASE(name) void name(subcases::Context& _09876)
#define SUBCASE(name) if (subcases::Subcase _54321{_09876, name})
#define EXECUTE(name) subcases::execute([&](auto& ctx) { name(ctx); })

#endif
