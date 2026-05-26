//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2025 Ripple Labs Inc.

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

#include <xrpl/basics/Slice.h>
#include <xrpl/protocol/digest.h>

#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <iostream>
#include <random>
#include <vector>

using namespace ripple;

static std::vector<char> const data = []() {
    std::vector<char> strV(pow(10, 5));
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dis(32, 127);
    for (size_t index = 0; index < strV.size(); ++index)
    {
        strV[index] = static_cast<char>(dis(gen));
    }
    return strV;
}();

TEST_SUITE_BEGIN("OpenSSL");

TEST_CASE("SingleHashFullSlice")
{
    Slice const s{data.data(), data.size()};
    auto hash = sha512Half(s);
}

TEST_CASE("MultihashAllSlices")
{
    for (std::size_t i = 0; i < data.size(); ++i)
    {
        Slice s(&data[i], data.size() - i);
        auto hash = sha512Half(s);
    }
}

TEST_SUITE_END();
