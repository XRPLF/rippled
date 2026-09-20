#include <xrpl/protocol/detail/token_errors.h>

#include <boost/multiprecision/cpp_int.hpp>  // IWYU pragma: keep

#include <gtest/gtest.h>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>
#ifndef _MSC_VER

#include <xrpl/protocol/detail/b58_utils.h>
#include <xrpl/protocol/tokens.h>

#include <array>
#include <cstddef>
#include <random>
#include <span>
#include <sstream>

namespace xrpl::test {
namespace {

[[nodiscard]] inline auto
randEngine() -> std::mt19937&
{
    static std::mt19937 kR = [] {
        std::random_device rd;
        return std::mt19937{rd()};
    }();
    return kR;
}

constexpr int kNumTokenTypeIndexes = 9;

[[nodiscard]] inline auto
tokenTypeAndSize(int i) -> std::tuple<xrpl::TokenType, std::size_t>
{
    assert(i < kNumTokenTypeIndexes);

    switch (i)
    {
        using enum xrpl::TokenType;
        case 0:
            return {None, 20};
        case 1:
            return {NodePublic, 32};
        case 2:
            return {NodePublic, 33};
        case 3:
            return {NodePrivate, 32};
        case 4:
            return {AccountID, 20};
        case 5:
            return {AccountPublic, 32};
        case 6:
            return {AccountPublic, 33};
        case 7:
            return {AccountSecret, 32};
        case 8:
            return {FamilySeed, 16};
        default:
            throw std::invalid_argument(
                "Invalid token selection passed to tokenTypeAndSize() "
                "in " __FILE__);
    }
}

[[nodiscard]] inline auto
randomTokenTypeAndSize() -> std::tuple<xrpl::TokenType, std::size_t>
{
    using namespace xrpl;
    auto& rng = randEngine();
    std::uniform_int_distribution<> d(0, 8);
    return tokenTypeAndSize(d(rng));
}

// Return the token type and subspan of `d` to use as test data.
[[nodiscard]] inline auto
randomB256TestData(std::span<std::uint8_t> d)
    -> std::tuple<xrpl::TokenType, std::span<std::uint8_t>>
{
    auto& rng = randEngine();
    std::uniform_int_distribution<std::uint8_t> dist(0, 255);
    auto [tokType, tokSize] = randomTokenTypeAndSize();
    std::generate(d.begin(), d.begin() + tokSize, [&] { return dist(rng); });
    return {tokType, d.subspan(0, tokSize)};
}

inline void
printAsChar(std::span<std::uint8_t> a, std::span<std::uint8_t> b)
{
    auto asString = [](std::span<std::uint8_t> s) {
        std::string r;
        r.resize(s.size());
        std::ranges::copy(s, r.begin());
        return r;
    };
    auto sa = asString(a);
    auto sb = asString(b);
    std::cerr << "\n\n" << sa << "\n" << sb << "\n";
}

inline void
printAsInt(std::span<std::uint8_t> a, std::span<std::uint8_t> b)
{
    auto asString = [](std::span<std::uint8_t> s) -> std::string {
        std::stringstream sstr;
        for (auto i : s)
        {
            sstr << std::setw(3) << int(i) << ',';
        }
        return sstr.str();
    };
    auto sa = asString(a);
    auto sb = asString(b);
    std::cerr << "\n\n" << sa << "\n" << sb << "\n";
}

}  // namespace

namespace multiprecision_utils {

boost::multiprecision::checked_uint512_t
toBoostMP(std::span<std::uint64_t> in)
{
    boost::multiprecision::checked_uint512_t mbp = 0;
    for (auto const& i : std::views::reverse(in))
    {
        mbp <<= 64;
        mbp += i;
    }
    return mbp;
}

std::vector<std::uint64_t>
randomBigInt(std::uint8_t minSize = 1, std::uint8_t maxSize = 5)
{
    auto eng = randEngine();
    std::uniform_int_distribution<std::uint8_t> numCoeffDist(minSize, maxSize);
    std::uniform_int_distribution<std::uint64_t> dist;
    auto const numCoeff = numCoeffDist(eng);
    std::vector<std::uint64_t> coeffs;
    coeffs.reserve(numCoeff);
    for (auto i = 0uz; i < numCoeff; ++i)
    {
        coeffs.push_back(dist(eng));
    }
    return coeffs;
}
}  // namespace multiprecision_utils

TEST(Base58Test, multiprecision)
{
    using namespace boost::multiprecision;

    constexpr std::size_t kIters = 100000;
    auto eng = randEngine();
    std::uniform_int_distribution<std::uint64_t> dist;
    std::uniform_int_distribution<std::uint64_t> dist1(1);
    for (auto i = 0uz; i < kIters; ++i)
    {
        std::uint64_t const d = dist(eng);
        if (d == 0u)
            continue;
        auto bigInt = multiprecision_utils::randomBigInt();
        auto const boostBigInt =
            multiprecision_utils::toBoostMP(std::span<std::uint64_t>(bigInt.data(), bigInt.size()));

        auto const refDiv = boostBigInt / d;
        auto const refMod = boostBigInt % d;

        auto const mod = b58_fast::detail::inplaceBigintDivRem(
            std::span<uint64_t>(bigInt.data(), bigInt.size()), d);
        auto const foundDiv = multiprecision_utils::toBoostMP(bigInt);
        EXPECT_EQ(refMod.convert_to<std::uint64_t>(), mod);
        EXPECT_EQ(foundDiv, refDiv);
    }
    for (auto i = 0uz; i < kIters; ++i)
    {
        std::uint64_t const d = dist(eng);
        auto bigInt = multiprecision_utils::randomBigInt(/*minSize*/ 2);
        if (bigInt[bigInt.size() - 1] == std::numeric_limits<std::uint64_t>::max())
        {
            bigInt[bigInt.size() - 1] -= 1;  // Prevent overflow
        }
        auto const boostBigInt =
            multiprecision_utils::toBoostMP(std::span<std::uint64_t>(bigInt.data(), bigInt.size()));

        auto const refAdd = boostBigInt + d;

        auto const result = b58_fast::detail::inplaceBigintAdd(
            std::span<uint64_t>(bigInt.data(), bigInt.size()), d);
        EXPECT_EQ(result, TokenCodecErrc::Success);
        auto const foundAdd = multiprecision_utils::toBoostMP(bigInt);
        EXPECT_EQ(refAdd, foundAdd);
    }
    for (auto i = 0uz; i < kIters; ++i)
    {
        std::uint64_t const d = dist1(eng);
        // Force overflow
        std::vector<std::uint64_t> bigInt(5, std::numeric_limits<std::uint64_t>::max());

        auto const boostBigInt =
            multiprecision_utils::toBoostMP(std::span<std::uint64_t>(bigInt.data(), bigInt.size()));

        auto const refAdd = boostBigInt + d;

        auto const result = b58_fast::detail::inplaceBigintAdd(
            std::span<uint64_t>(bigInt.data(), bigInt.size()), d);
        EXPECT_EQ(result, TokenCodecErrc::OverflowAdd);
        auto const foundAdd = multiprecision_utils::toBoostMP(bigInt);
        EXPECT_NE(refAdd, foundAdd);
    }
    for (auto i = 0uz; i < kIters; ++i)
    {
        std::uint64_t const d = dist(eng);
        auto bigInt = multiprecision_utils::randomBigInt(/* minSize */ 2);
        // inplace mul requires the most significant coeff to be zero to
        // hold the result.
        bigInt[bigInt.size() - 1] = 0;
        auto const boostBigInt =
            multiprecision_utils::toBoostMP(std::span<std::uint64_t>(bigInt.data(), bigInt.size()));

        auto const refMul = boostBigInt * d;

        auto const result = b58_fast::detail::inplaceBigintMul(
            std::span<uint64_t>(bigInt.data(), bigInt.size()), d);
        EXPECT_EQ(result, TokenCodecErrc::Success);
        auto const foundMul = multiprecision_utils::toBoostMP(bigInt);
        EXPECT_EQ(refMul, foundMul);
    }
    for (auto i = 0uz; i < kIters; ++i)
    {
        std::uint64_t const d = dist1(eng);
        // Force overflow
        std::vector<std::uint64_t> bigInt(5, std::numeric_limits<std::uint64_t>::max());
        auto const boostBigInt =
            multiprecision_utils::toBoostMP(std::span<std::uint64_t>(bigInt.data(), bigInt.size()));

        auto const refMul = boostBigInt * d;

        auto const result = b58_fast::detail::inplaceBigintMul(
            std::span<uint64_t>(bigInt.data(), bigInt.size()), d);
        EXPECT_EQ(result, TokenCodecErrc::InputTooLarge);
        auto const foundMul = multiprecision_utils::toBoostMP(bigInt);
        EXPECT_NE(refMul, foundMul);
    }
}

TEST(Base58Test, fast_matches_ref)
{
    auto testRawEncode = [&](std::span<std::uint8_t> const& b256Data) {
        std::array<std::uint8_t, 64> b58ResultBuf[2];
        std::array<std::span<std::uint8_t>, 2> b58Result;

        std::array<std::uint8_t, 64> b256ResultBuf[2];
        std::array<std::span<std::uint8_t>, 2> b256Result;
        for (auto i = 0uz; i < 2; ++i)
        {
            std::span const outBuf{b58ResultBuf[i]};
            if (i == 0)
            {
                auto const r = xrpl::b58_fast::detail::b256ToB58Be(b256Data, outBuf);
                EXPECT_TRUE(r);
                b58Result[i] = r.value();
            }
            else
            {
                std::array<std::uint8_t, 128> tmpBuf{};
                std::string const s = xrpl::b58_ref::detail::encodeBase58(
                    b256Data.data(), b256Data.size(), tmpBuf.data(), tmpBuf.size());
                EXPECT_TRUE(s.size());
                b58Result[i] = outBuf.subspan(0, s.size());
                std::ranges::copy(s, b58Result[i].begin());
            }
        }
        auto const rawB58SameSize = b58Result[0].size() == b58Result[1].size();
        EXPECT_TRUE(rawB58SameSize);
        if (rawB58SameSize)
        {
            auto const rawB58SameData =
                memcmp(b58Result[0].data(), b58Result[1].data(), b58Result[0].size()) == 0;
            EXPECT_TRUE(rawB58SameData);
            if (!rawB58SameData)
            {
                printAsChar(b58Result[0], b58Result[1]);
            }
        }

        for (auto i = 0uz; i < 2; ++i)
        {
            std::span const outBuf{b256ResultBuf[i].data(), b256ResultBuf[i].size()};
            if (i == 0)
            {
                std::string const in(
                    b58Result[i].data(), b58Result[i].data() + b58Result[i].size());
                auto const r = xrpl::b58_fast::detail::b58ToB256Be(in, outBuf);
                EXPECT_TRUE(r);
                b256Result[i] = r.value();
            }
            else
            {
                std::string const st(b58Result[i].begin(), b58Result[i].end());
                std::string const s = xrpl::b58_ref::detail::decodeBase58(st);
                EXPECT_TRUE(s.size());
                b256Result[i] = outBuf.subspan(0, s.size());
                std::ranges::copy(s, b256Result[i].begin());
            }
        }

        auto const rawB256SameSize = b256Result[0].size() == b256Result[1].size();
        EXPECT_TRUE(rawB256SameSize);
        if (rawB256SameSize)
        {
            auto const rawB256SameData =
                memcmp(b256Result[0].data(), b256Result[1].data(), b256Result[0].size()) == 0;
            EXPECT_TRUE(rawB256SameData);
            if (!rawB256SameData)
            {
                printAsInt(b256Result[0], b256Result[1]);
            }
        }
    };

    auto testTokenEncode = [&](xrpl::TokenType const tokType,
                               std::span<std::uint8_t> const& b256Data) {
        std::array<std::uint8_t, 64> b58ResultBuf[2];
        std::array<std::span<std::uint8_t>, 2> b58Result;

        std::array<std::uint8_t, 64> b256ResultBuf[2];
        std::array<std::span<std::uint8_t>, 2> b256Result;
        for (auto i = 0uz; i < 2; ++i)
        {
            std::span const outBuf{b58ResultBuf[i].data(), b58ResultBuf[i].size()};
            if (i == 0)
            {
                auto const r = xrpl::b58_fast::encodeBase58Token(tokType, b256Data, outBuf);
                EXPECT_TRUE(r);
                b58Result[i] = r.value();
            }
            else
            {
                std::string const s =
                    xrpl::b58_ref::encodeBase58Token(tokType, b256Data.data(), b256Data.size());
                EXPECT_TRUE(s.size());
                b58Result[i] = outBuf.subspan(0, s.size());
                std::ranges::copy(s, b58Result[i].begin());
            }
        }
        auto const tokenB58SameSize = b58Result[0].size() == b58Result[1].size();
        EXPECT_TRUE(tokenB58SameSize);
        if (tokenB58SameSize)
        {
            auto const tokenB58SameData =
                memcmp(b58Result[0].data(), b58Result[1].data(), b58Result[0].size()) == 0;
            EXPECT_TRUE(tokenB58SameData);
            if (!tokenB58SameData)
            {
                printAsChar(b58Result[0], b58Result[1]);
            }
        }

        for (auto i = 0uz; i < 2; ++i)
        {
            std::span const outBuf{b256ResultBuf[i].data(), b256ResultBuf[i].size()};
            if (i == 0)
            {
                std::string const in(
                    b58Result[i].data(), b58Result[i].data() + b58Result[i].size());
                auto const r = xrpl::b58_fast::decodeBase58Token(tokType, in, outBuf);
                EXPECT_TRUE(r);
                b256Result[i] = r.value();
            }
            else
            {
                std::string const st(b58Result[i].begin(), b58Result[i].end());
                std::string const s = xrpl::b58_ref::decodeBase58Token(st, tokType);
                EXPECT_TRUE(s.size());
                b256Result[i] = outBuf.subspan(0, s.size());
                std::ranges::copy(s, b256Result[i].begin());
            }
        }

        auto const tokenB256SameSize = b256Result[0].size() == b256Result[1].size();
        EXPECT_TRUE(tokenB256SameSize);
        if (tokenB256SameSize)
        {
            auto const tokenB256SameData =
                memcmp(b256Result[0].data(), b256Result[1].data(), b256Result[0].size()) == 0;
            EXPECT_TRUE(tokenB256SameData);
            if (!tokenB256SameData)
            {
                printAsInt(b256Result[0], b256Result[1]);
            }
        }
    };

    auto testIt = [&](xrpl::TokenType const tokType, std::span<std::uint8_t> const& b256Data) {
        testRawEncode(b256Data);
        testTokenEncode(tokType, b256Data);
    };

    // test every token type with data where every byte is the same and the
    // bytes range from 0-255
    for (int i = 0; i < kNumTokenTypeIndexes; ++i)
    {
        std::array<std::uint8_t, 128> b256DataBuf{};
        auto const [tokType, tokSize] = tokenTypeAndSize(i);
        for (int d = 0; d <= 255; ++d)
        {
            memset(b256DataBuf.data(), d, tokSize);
            testIt(tokType, std::span(b256DataBuf.data(), tokSize));
        }
    }

    // test with random data
    constexpr std::size_t kIters = 100000;
    for (auto i = 0uz; i < kIters; ++i)
    {
        std::array<std::uint8_t, 128> b256DataBuf{};
        auto const [tokType, b256Data] = randomB256TestData(b256DataBuf);
        testIt(tokType, b256Data);
    }
}

}  // namespace xrpl::test

#endif  // _MSC_VER
