#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/detail/token_errors.h>

#include <boost/multiprecision/cpp_int.hpp>  // IWYU pragma: keep

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
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
    for (auto i = in.rbegin(); i != in.rend(); ++i)
    {
        mbp <<= 64;
        mbp += *i;
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
    for (int i = 0; i < numCoeff; ++i)
    {
        coeffs.push_back(dist(eng));
    }
    return coeffs;
}
}  // namespace multiprecision_utils

class base58_test : public beast::unit_test::Suite
{
    void
    testMultiprecision()
    {
        testcase("b58_multiprecision");

        using namespace boost::multiprecision;

        static constexpr std::size_t kIters = 100000;
        auto eng = randEngine();
        std::uniform_int_distribution<std::uint64_t> dist;
        std::uniform_int_distribution<std::uint64_t> dist1(1);
        for (int i = 0; i < kIters; ++i)
        {
            std::uint64_t const d = dist(eng);
            if (d == 0u)
                continue;
            auto bigInt = multiprecision_utils::randomBigInt();
            auto const boostBigInt = multiprecision_utils::toBoostMP(
                std::span<std::uint64_t>(bigInt.data(), bigInt.size()));

            auto const refDiv = boostBigInt / d;
            auto const refMod = boostBigInt % d;

            auto const mod = b58_fast::detail::inplaceBigintDivRem(
                std::span<uint64_t>(bigInt.data(), bigInt.size()), d);
            auto const foundDiv = multiprecision_utils::toBoostMP(bigInt);
            BEAST_EXPECT(refMod.convert_to<std::uint64_t>() == mod);
            BEAST_EXPECT(foundDiv == refDiv);
        }
        for (int i = 0; i < kIters; ++i)
        {
            std::uint64_t const d = dist(eng);
            auto bigInt = multiprecision_utils::randomBigInt(/*minSize*/ 2);
            if (bigInt[bigInt.size() - 1] == std::numeric_limits<std::uint64_t>::max())
            {
                bigInt[bigInt.size() - 1] -= 1;  // Prevent overflow
            }
            auto const boostBigInt = multiprecision_utils::toBoostMP(
                std::span<std::uint64_t>(bigInt.data(), bigInt.size()));

            auto const refAdd = boostBigInt + d;

            auto const result = b58_fast::detail::inplaceBigintAdd(
                std::span<uint64_t>(bigInt.data(), bigInt.size()), d);
            BEAST_EXPECT(result == TokenCodecErrc::Success);
            auto const foundAdd = multiprecision_utils::toBoostMP(bigInt);
            BEAST_EXPECT(refAdd == foundAdd);
        }
        for (int i = 0; i < kIters; ++i)
        {
            std::uint64_t const d = dist1(eng);
            // Force overflow
            std::vector<std::uint64_t> bigInt(5, std::numeric_limits<std::uint64_t>::max());

            auto const boostBigInt = multiprecision_utils::toBoostMP(
                std::span<std::uint64_t>(bigInt.data(), bigInt.size()));

            auto const refAdd = boostBigInt + d;

            auto const result = b58_fast::detail::inplaceBigintAdd(
                std::span<uint64_t>(bigInt.data(), bigInt.size()), d);
            BEAST_EXPECT(result == TokenCodecErrc::OverflowAdd);
            auto const foundAdd = multiprecision_utils::toBoostMP(bigInt);
            BEAST_EXPECT(refAdd != foundAdd);
        }
        for (int i = 0; i < kIters; ++i)
        {
            std::uint64_t const d = dist(eng);
            auto bigInt = multiprecision_utils::randomBigInt(/* minSize */ 2);
            // inplace mul requires the most significant coeff to be zero to
            // hold the result.
            bigInt[bigInt.size() - 1] = 0;
            auto const boostBigInt = multiprecision_utils::toBoostMP(
                std::span<std::uint64_t>(bigInt.data(), bigInt.size()));

            auto const refMul = boostBigInt * d;

            auto const result = b58_fast::detail::inplaceBigintMul(
                std::span<uint64_t>(bigInt.data(), bigInt.size()), d);
            BEAST_EXPECT(result == TokenCodecErrc::Success);
            auto const foundMul = multiprecision_utils::toBoostMP(bigInt);
            BEAST_EXPECT(refMul == foundMul);
        }
        for (int i = 0; i < kIters; ++i)
        {
            std::uint64_t const d = dist1(eng);
            // Force overflow
            std::vector<std::uint64_t> bigInt(5, std::numeric_limits<std::uint64_t>::max());
            auto const boostBigInt = multiprecision_utils::toBoostMP(
                std::span<std::uint64_t>(bigInt.data(), bigInt.size()));

            auto const refMul = boostBigInt * d;

            auto const result = b58_fast::detail::inplaceBigintMul(
                std::span<uint64_t>(bigInt.data(), bigInt.size()), d);
            BEAST_EXPECT(result == TokenCodecErrc::InputTooLarge);
            auto const foundMul = multiprecision_utils::toBoostMP(bigInt);
            BEAST_EXPECT(refMul != foundMul);
        }
    }

    void
    testFastMatchesRef()
    {
        testcase("fast_matches_ref");
        auto testRawEncode = [&](std::span<std::uint8_t> const& b256Data) {
            std::array<std::uint8_t, 64> b58ResultBuf[2];
            std::array<std::span<std::uint8_t>, 2> b58Result;

            std::array<std::uint8_t, 64> b256ResultBuf[2];
            std::array<std::span<std::uint8_t>, 2> b256Result;
            for (int i = 0; i < 2; ++i)
            {
                std::span const outBuf{b58ResultBuf[i]};
                if (i == 0)
                {
                    auto const r = xrpl::b58_fast::detail::b256ToB58Be(b256Data, outBuf);
                    BEAST_EXPECT(r);
                    b58Result[i] = r.value();
                }
                else
                {
                    std::array<std::uint8_t, 128> tmpBuf{};
                    std::string const s = xrpl::b58_ref::detail::encodeBase58(
                        b256Data.data(), b256Data.size(), tmpBuf.data(), tmpBuf.size());
                    BEAST_EXPECT(s.size());
                    b58Result[i] = outBuf.subspan(0, s.size());
                    std::ranges::copy(s, b58Result[i].begin());
                }
            }
            if (BEAST_EXPECT(b58Result[0].size() == b58Result[1].size()))
            {
                if (!BEAST_EXPECT(
                        memcmp(b58Result[0].data(), b58Result[1].data(), b58Result[0].size()) == 0))
                {
                    printAsChar(b58Result[0], b58Result[1]);
                }
            }

            for (int i = 0; i < 2; ++i)
            {
                std::span const outBuf{b256ResultBuf[i].data(), b256ResultBuf[i].size()};
                if (i == 0)
                {
                    std::string const in(
                        b58Result[i].data(), b58Result[i].data() + b58Result[i].size());
                    auto const r = xrpl::b58_fast::detail::b58ToB256Be(in, outBuf);
                    BEAST_EXPECT(r);
                    b256Result[i] = r.value();
                }
                else
                {
                    std::string const st(b58Result[i].begin(), b58Result[i].end());
                    std::string const s = xrpl::b58_ref::detail::decodeBase58(st);
                    BEAST_EXPECT(s.size());
                    b256Result[i] = outBuf.subspan(0, s.size());
                    std::ranges::copy(s, b256Result[i].begin());
                }
            }

            if (BEAST_EXPECT(b256Result[0].size() == b256Result[1].size()))
            {
                if (!BEAST_EXPECT(
                        memcmp(b256Result[0].data(), b256Result[1].data(), b256Result[0].size()) ==
                        0))
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
            for (int i = 0; i < 2; ++i)
            {
                std::span const outBuf{b58ResultBuf[i].data(), b58ResultBuf[i].size()};
                if (i == 0)
                {
                    auto const r = xrpl::b58_fast::encodeBase58Token(tokType, b256Data, outBuf);
                    BEAST_EXPECT(r);
                    b58Result[i] = r.value();
                }
                else
                {
                    std::string const s =
                        xrpl::b58_ref::encodeBase58Token(tokType, b256Data.data(), b256Data.size());
                    BEAST_EXPECT(s.size());
                    b58Result[i] = outBuf.subspan(0, s.size());
                    std::ranges::copy(s, b58Result[i].begin());
                }
            }
            if (BEAST_EXPECT(b58Result[0].size() == b58Result[1].size()))
            {
                if (!BEAST_EXPECT(
                        memcmp(b58Result[0].data(), b58Result[1].data(), b58Result[0].size()) == 0))
                {
                    printAsChar(b58Result[0], b58Result[1]);
                }
            }

            for (int i = 0; i < 2; ++i)
            {
                std::span const outBuf{b256ResultBuf[i].data(), b256ResultBuf[i].size()};
                if (i == 0)
                {
                    std::string const in(
                        b58Result[i].data(), b58Result[i].data() + b58Result[i].size());
                    auto const r = xrpl::b58_fast::decodeBase58Token(tokType, in, outBuf);
                    BEAST_EXPECT(r);
                    b256Result[i] = r.value();
                }
                else
                {
                    std::string const st(b58Result[i].begin(), b58Result[i].end());
                    std::string const s = xrpl::b58_ref::decodeBase58Token(st, tokType);
                    BEAST_EXPECT(s.size());
                    b256Result[i] = outBuf.subspan(0, s.size());
                    std::ranges::copy(s, b256Result[i].begin());
                }
            }

            if (BEAST_EXPECT(b256Result[0].size() == b256Result[1].size()))
            {
                if (!BEAST_EXPECT(
                        memcmp(b256Result[0].data(), b256Result[1].data(), b256Result[0].size()) ==
                        0))
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
        static constexpr std::size_t kIters = 100000;
        for (int i = 0; i < kIters; ++i)
        {
            std::array<std::uint8_t, 128> b256DataBuf{};
            auto const [tokType, b256Data] = randomB256TestData(b256DataBuf);
            testIt(tokType, b256Data);
        }
    }

    void
    run() override
    {
        testMultiprecision();
        testFastMatchesRef();
    }
};

BEAST_DEFINE_TESTSUITE(base58, basics, xrpl);

}  // namespace xrpl::test

#endif  // _MSC_VER
