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

#include <xrpl/protocol/ApiVersion.h>

#include <doctest/doctest.h>

using namespace ripple;

TEST_SUITE_BEGIN("ApiVersion");

TEST_CASE("API versions invariants")
{
    static_assert(
        RPC::apiMinimumSupportedVersion <= RPC::apiMaximumSupportedVersion);
    static_assert(
        RPC::apiMinimumSupportedVersion <= RPC::apiMaximumValidVersion);
    static_assert(
        RPC::apiMaximumSupportedVersion <= RPC::apiMaximumValidVersion);
    static_assert(RPC::apiBetaVersion <= RPC::apiMaximumValidVersion);

    CHECK(true);
}

TEST_CASE("API versions")
{
    // Update when we change versions
    static_assert(RPC::apiMinimumSupportedVersion >= 1);
    static_assert(RPC::apiMinimumSupportedVersion < 2);
    static_assert(RPC::apiMaximumSupportedVersion >= 2);
    static_assert(RPC::apiMaximumSupportedVersion < 3);
    static_assert(RPC::apiMaximumValidVersion >= 3);
    static_assert(RPC::apiMaximumValidVersion < 4);
    static_assert(RPC::apiBetaVersion >= 3);
    static_assert(RPC::apiBetaVersion < 4);

    CHECK(true);
}

TEST_SUITE_END();
