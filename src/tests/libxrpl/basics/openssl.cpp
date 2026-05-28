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

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <random>
#include <vector>

using namespace xrpl;

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

// Fold 8 bytes of a uint256 into a sink so the optimizer cannot
// dead-code-eliminate the hash call. Each test CHECKs the sink to
// keep it observable.
static inline std::uint64_t
absorb(uint256 const& h)
{
    std::uint64_t word;
    std::memcpy(&word, h.data(), sizeof(word));
    return word;
}

TEST(OpenSSL, SingleHashFullSlice)
{
    constexpr int kIterations = 10'000;
    Slice const s{data.data(), data.size()};
    std::uint64_t sink = 0;
    for (int i = 0; i < kIterations; ++i)
        sink ^= absorb(sha512Half(s));
    EXPECT_NE(sink, 0xDEADBEEFDEADBEEFull);
}

TEST(OpenSSL, MultihashAllSlices)
{
    std::uint64_t sink = 0;
    for (std::size_t i = 0; i < data.size(); ++i)
    {
        Slice s(&data[i], data.size() - i);
        sink ^= absorb(sha512Half(s));
    }
    EXPECT_NE(sink, 0xDEADBEEFDEADBEEFull);
}
