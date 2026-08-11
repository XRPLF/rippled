#include <xrpl/basics/Number.h>

#include <xrpl/protocol/IOUAmount.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/SystemParameters.h>
#include <xrpl/protocol/XRPAmount.h>

// NOLINTNEXTLINE(misc-include-cleaner)
#include <boost/multiprecision/cpp_dec_float.hpp>
#include <boost/multiprecision/number.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <map>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace xrpl {

using BigInt = boost::multiprecision::cpp_int;
using Dec = boost::multiprecision::cpp_dec_float_50;

static std::string
fmt(BigInt const& value)
{
    auto s = to_string(value);
    std::string out;
    int count = 0;
    for (char const& ch : std::views::reverse(s))
    {
        if (count != 0 && count % 3 == 0 && (isdigit(ch) != 0))
            out.insert(out.begin(), '_');
        out.insert(out.begin(), ch);
        ++count;
    }
    return out;
}

BigInt
toBigInt(Number const& n)
{
    BigInt v = n.mantissa();
    auto e = n.exponent();

    for (; e > 0; --e)
        v *= 10;
    for (; e < 0; ++e)
    {
        EXPECT_EQ(v % 10, 0);
        v /= 10;
    }
    return v;
}

template <class T = Dec>
static T
pow10(int n)
{
    if (n == 0)
        return 1;
    if (n == 1)
        return 10;

    if (n > 1)
    {
        auto r = pow10<T>(n / 2);
        r *= r;
        if (n % 2 != 0)
            r *= 10;
        return r;
    }

    T p = 1;
    p /= pow10<T>(-n);
    return p;
}

static std::string
fmt(Dec const& value)
{
    std::ostringstream os;
    os << std::setprecision(40) << value;
    return os.str();
}

TEST(NumberTest, zero)
{
    for (auto const mantissaScale : MantissaRange::getAllScales())
    {
        NumberMantissaScaleGuard const sg(mantissaScale);

        for (Number const& z : {Number{0, 0}, Number{0}})
        {
            EXPECT_EQ(z.mantissa(), 0);
            EXPECT_EQ(z.exponent(), Number{}.exponent());

            EXPECT_EQ((z + z), z);
            EXPECT_EQ((z - z), z);
            EXPECT_EQ(z, -z);
        }
    }
}

TEST(NumberTest, limits)
{
    for (auto const mantissaScale : MantissaRange::getAllScales())
    {
        NumberMantissaScaleGuard const sg(mantissaScale);

        auto const scale = Number::getMantissaScale();
        bool caught = false;
        auto const minMantissa = Number::minMantissa();
        try
        {
            [[maybe_unused]] Number const x =
                Number{false, minMantissa * 10, 32768, Number::Normalized{}};
        }
        catch (std::overflow_error const&)
        {
            caught = true;
        }
        EXPECT_TRUE(caught);

        auto test = [](auto const& x, auto const& y, int line) {
            auto const result = x == y;
            std::stringstream ss;
            ss << x << " == " << y << " -> " << (result ? "true" : "false");
            EXPECT_TRUE(result) << ss.str() << " (" << __FILE__ << ":" << line << ")";
        };

        test(
            Number{false, minMantissa * 10, 32767, Number::Normalized{}},
            Number{false, minMantissa, 32768, Number::Normalized{}},
            __LINE__);
        test(Number{false, minMantissa, -32769, Number::Normalized{}}, Number{}, __LINE__);
        test(
            Number{false, minMantissa, 32000, Number::Normalized{}} * 1'000 +
                Number{false, 1'500, 32000, Number::Normalized{}},
            Number{false, minMantissa + 2, 32003, Number::Normalized{}},
            __LINE__);
        // 9,223,372,036,854,775,808

        test(
            Number{std::numeric_limits<std::int64_t>::min()},
            scale == MantissaRange::MantissaScale::Small
                ? Number{-9'223'372'036'854'776, 3}
                : Number{true, 9'223'372'036'854'775'808ULL, 0, Number::Normalized{}},
            __LINE__);
        test(
            Number{std::numeric_limits<std::int64_t>::min() + 1},
            scale == MantissaRange::MantissaScale::Small ? Number{-9'223'372'036'854'776, 3}
                                                         : Number{-9'223'372'036'854'775'807},
            __LINE__);
        test(
            Number{std::numeric_limits<std::int64_t>::max()},
            Number{
                scale == MantissaRange::MantissaScale::Small
                    ? 9'223'372'036'854'776
                    : std::numeric_limits<std::int64_t>::max(),
                18 - Number::mantissaLog()},
            __LINE__);
        caught = false;
        try
        {
            [[maybe_unused]]
            Number const q = Number{false, minMantissa, 32767, Number::Normalized{}} * 100;
        }
        catch (std::overflow_error const&)
        {
            caught = true;
        }
        EXPECT_TRUE(caught);

        if (scale == MantissaRange::MantissaScale::Large330)
        {
            // Normalization with the other scales, including the older large mantissa scales, will
            // overflow.
            Number const bigNum{Number::kMaxRepUp, Number::kMaxExponent, Number::Normalized{}};
            // The display of large exponents won't go above kMaxExponent
            EXPECT_EQ(to_string(bigNum), "9223372036854775810e32768") << bigNum;
            // Perhaps surprisingly, this is ok, because the exponent range is related to when the
            // number is _normalized_, and for mantissas > kMaxRep, the accessors return values that
            // are not normalized.
            EXPECT_EQ(bigNum.mantissa(), 922337203685477581ULL) << bigNum.mantissa();
            EXPECT_EQ(bigNum.exponent(), 32769) << bigNum.exponent();
        }
        else
        {
            try
            {
                Number{Number::kMaxRepUp, Number::kMaxExponent, Number::Normalized{}};
                ADD_FAILURE();
            }
            catch (std::overflow_error const& e)
            {
                std::string const expected =
                    (scale == MantissaRange::MantissaScale::Small ? "Number::normalize 1"
                                                                  : "Number::normalize 1.5");
                EXPECT_EQ(e.what(), expected) << e.what();
            }
        }
    }
}

TEST(NumberTest, add)
{
    for (auto const mantissaScale : MantissaRange::getAllScales())
    {
        NumberMantissaScaleGuard const sg(mantissaScale);

        auto const scale = Number::getMantissaScale();

        EXPECT_EQ(Number::getround(), Number::RoundingMode::ToNearest)
            << to_string(Number::getround());

        using Case = std::tuple<Number, Number, Number, int>;
        // TODO: Move these to the blocks where they're used
        auto const cSmall = std::to_array<Case>({
            {Number{1'000'000'000'000'000, -15},
             Number{6'555'555'555'555'555, -29},
             Number{1'000'000'000'000'066, -15},
             __LINE__},
            {Number{-1'000'000'000'000'000, -15},
             Number{-6'555'555'555'555'555, -29},
             Number{-1'000'000'000'000'066, -15},
             __LINE__},
            {Number{-1'000'000'000'000'000, -15},
             Number{6'555'555'555'555'555, -29},
             Number{-9'999'999'999'999'344, -16},
             __LINE__},
            {Number{-6'555'555'555'555'555, -29},
             Number{1'000'000'000'000'000, -15},
             Number{9'999'999'999'999'344, -16},
             __LINE__},
            {Number{}, Number{5}, Number{5}, __LINE__},
            {Number{5}, Number{}, Number{5}, __LINE__},
            {Number{5'555'555'555'555'555, -32768},
             Number{-5'555'555'555'555'554, -32768},
             Number{0},
             __LINE__},
            {Number{-9'999'999'999'999'999, -31},
             Number{1'000'000'000'000'000, -15},
             Number{9'999'999'999'999'990, -16},
             __LINE__},
        });
        auto const cLarge = std::to_array<Case>(
            // Note that items with extremely large mantissas need to be
            // calculated, because otherwise they overflow uint64. Items from C
            // with larger mantissa
            {
                {Number{1'000'000'000'000'000, -15},
                 Number{6'555'555'555'555'555, -29},
                 Number{1'000'000'000'000'065'556, -18},
                 __LINE__},
                {Number{-1'000'000'000'000'000, -15},
                 Number{-6'555'555'555'555'555, -29},
                 Number{-1'000'000'000'000'065'556, -18},
                 __LINE__},
                {Number{-1'000'000'000'000'000, -15},
                 Number{6'555'555'555'555'555, -29},
                 Number{true, 9'999'999'999'999'344'444ULL, -19, Number::Normalized{}},
                 __LINE__},
                {Number{-6'555'555'555'555'555, -29},
                 Number{1'000'000'000'000'000, -15},
                 Number{false, 9'999'999'999'999'344'444ULL, -19, Number::Normalized{}},
                 __LINE__},
                {Number{}, Number{5}, Number{5}, __LINE__},
                {Number{5}, Number{}, Number{5}, __LINE__},
                {Number{5'555'555'555'555'555'000, -32768},
                 Number{-5'555'555'555'555'554'000, -32768},
                 Number{0},
                 __LINE__},
                {Number{-9'999'999'999'999'999, -31},
                 Number{1'000'000'000'000'000, -15},
                 Number{9'999'999'999'999'990, -16},
                 __LINE__},
                // Items from cSmall expanded for the larger mantissa
                {Number{1'000'000'000'000'000'000, -18},
                 Number{6'555'555'555'555'555'555, -35},
                 Number{1'000'000'000'000'000'066, -18},
                 __LINE__},
                {Number{-1'000'000'000'000'000'000, -18},
                 Number{-6'555'555'555'555'555'555, -35},
                 Number{-1'000'000'000'000'000'066, -18},
                 __LINE__},
                {Number{-1'000'000'000'000'000'000, -18},
                 Number{6'555'555'555'555'555'555, -35},
                 Number{true, 9'999'999'999'999'999'344ULL, -19, Number::Normalized{}},
                 __LINE__},
                {Number{-6'555'555'555'555'555'555, -35},
                 Number{1'000'000'000'000'000'000, -18},
                 Number{false, 9'999'999'999'999'999'344ULL, -19, Number::Normalized{}},
                 __LINE__},
                {Number{}, Number{5}, Number{5}, __LINE__},
                {Number{5'555'555'555'555'555'555, -32768},
                 Number{-5'555'555'555'555'555'554, -32768},
                 Number{0},
                 __LINE__},
                {Number{true, 9'999'999'999'999'999'999ULL, -37, Number::Normalized{}},
                 Number{1'000'000'000'000'000'000, -18},
                 Number{false, 9'999'999'999'999'999'990ULL, -19, Number::Normalized{}},
                 __LINE__},
                {Number{Number::kMaxRep - 1}, Number{1, 0}, Number{Number::kMaxRep}, __LINE__},
                // Test extremes
                {
                    // Each Number operand rounds up, so the actual mantissa is
                    // minMantissa
                    Number{false, 9'999'999'999'999'999'999ULL, 0, Number::Normalized{}},
                    Number{false, 9'999'999'999'999'999'999ULL, 0, Number::Normalized{}},
                    Number{2, 19},
                    __LINE__,
                },
                {
                    // Does not round. Mantissas are going to be > kMaxRep, so if
                    // added together as uint64_t's, the result will overflow.
                    // With addition using uint128_t, there's no problem. After
                    // normalizing, the resulting mantissa ends up less than
                    // kMaxRep.
                    Number{false, 9'999'999'999'999'999'990ULL, 0, Number::Normalized{}},
                    Number{false, 9'999'999'999'999'999'990ULL, 0, Number::Normalized{}},
                    Number{false, 1'999'999'999'999'999'998ULL, 1, Number::Normalized{}},
                    __LINE__,
                },
            });
        auto const cLargeLegacy = std::to_array<Case>({
            {Number{Number::kMaxRep}, Number{6, -1}, Number{Number::kMaxRep / 10, 1}, __LINE__},
        });
        auto const cLarge320 = std::to_array<Case>({
            {Number{Number::kMaxRep},
             Number{6, -1},
             Number{(Number::kMaxRep / 10) + 1, 1},
             __LINE__},
        });
        auto const cLargeCorrected = std::to_array<Case>({
            {Number{Number::kMaxRep}, Number{6, -1}, Number{Number::kMaxRep}, __LINE__},
        });
        auto test = [](auto const& c) {
            for (auto const& [x, y, z, line] : c)
            {
                auto const result = x + y;
                std::stringstream ss;
                ss << x << " + " << y << " = " << result << ". Expected: " << z;
                EXPECT_EQ(result, z) << ss.str() << " Line: " << line;
            }
        };
        if (scale == MantissaRange::MantissaScale::Small)
        {
            test(cSmall);
        }
        else
        {
            test(cLarge);
            if (scale == MantissaRange::MantissaScale::LargeLegacy)
            {
                test(cLargeLegacy);
            }
            else if (scale == MantissaRange::MantissaScale::Large320)
            {
                test(cLarge320);
            }
            else
            {
                test(cLargeCorrected);

                // This has to be created in this block, because normalization with the other
                // scales, including the older large mantissa scales, will overflow.
                Number const bigResult{
                    Number::kMaxRepUp, Number::kMaxExponent, Number::Normalized{}};
                auto const cBigNums = std::to_array<Case>({
                    {
                        // Add 3 to the mantissa to avoid rounding
                        Number::max(),
                        Number{3, Number::kMaxExponent},
                        bigResult,
                        __LINE__,
                    },
                });
                test(cBigNums);
            }
        }
        {
            bool caught = false;
            try
            {
                Number{false, Number::maxMantissa(), 32768, Number::Normalized{}} +
                    Number{false, Number::minMantissa(), 32767, Number::Normalized{}} * 5;
            }
            catch (std::overflow_error const&)
            {
                caught = true;
            }
            EXPECT_TRUE(caught);
        }
    }
}

TEST(NumberTest, sub)
{
    for (auto const mantissaScale : MantissaRange::getAllScales())
    {
        NumberMantissaScaleGuard const sg(mantissaScale);

        auto const scale = Number::getMantissaScale();

        using Case = std::tuple<Number, Number, Number, int>;
        auto const cSmall = std::to_array<Case>(
            {{Number{1'000'000'000'000'000, -15},
              Number{6'555'555'555'555'555, -29},
              Number{9'999'999'999'999'344, -16},
              __LINE__},
             {Number{6'555'555'555'555'555, -29},
              Number{1'000'000'000'000'000, -15},
              Number{-9'999'999'999'999'344, -16},
              __LINE__},
             {Number{1'000'000'000'000'000, -15},
              Number{1'000'000'000'000'000, -15},
              Number{0},
              __LINE__},
             {Number{1'000'000'000'000'000, -15},
              Number{1'000'000'000'000'001, -15},
              Number{-1'000'000'000'000'000, -30},
              __LINE__},
             {Number{1'000'000'000'000'001, -15},
              Number{1'000'000'000'000'000, -15},
              Number{1'000'000'000'000'000, -30},
              __LINE__}});
        auto const cLargeAll = std::to_array<Case>(
            // Note that items with extremely large mantissas need to be
            // calculated, because otherwise they overflow uint64. Items from C
            // with larger mantissa
            {
                {Number{1'000'000'000'000'000, -15},
                 Number{6'555'555'555'555'555, -29},
                 Number{false, 9'999'999'999'999'344'444ULL, -19, Number::Normalized{}},
                 __LINE__},
                {Number{6'555'555'555'555'555, -29},
                 Number{1'000'000'000'000'000, -15},
                 Number{true, 9'999'999'999'999'344'444ULL, -19, Number::Normalized{}},
                 __LINE__},
                {Number{1'000'000'000'000'000, -15},
                 Number{1'000'000'000'000'000, -15},
                 Number{0},
                 __LINE__},
                {Number{1'000'000'000'000'000, -15},
                 Number{1'000'000'000'000'001, -15},
                 Number{-1'000'000'000'000'000, -30},
                 __LINE__},
                {Number{1'000'000'000'000'001, -15},
                 Number{1'000'000'000'000'000, -15},
                 Number{1'000'000'000'000'000, -30},
                 __LINE__},
                // Items from cSmall expanded for the larger mantissa
                {Number{1'000'000'000'000'000'000, -18},
                 Number{6'555'555'555'555'555'555, -32},
                 Number{false, 9'999'999'999'999'344'444ULL, -19, Number::Normalized{}},
                 __LINE__},
                {Number{6'555'555'555'555'555'555, -32},
                 Number{1'000'000'000'000'000'000, -18},
                 Number{true, 9'999'999'999'999'344'444ULL, -19, Number::Normalized{}},
                 __LINE__},
                {Number{1'000'000'000'000'000'000, -18},
                 Number{1'000'000'000'000'000'000, -18},
                 Number{0},
                 __LINE__},
                {Number{1'000'000'000'000'000'000, -18},
                 Number{1'000'000'000'000'000'001, -18},
                 Number{-1'000'000'000'000'000'000, -36},
                 __LINE__},
                {Number{1'000'000'000'000'000'001, -18},
                 Number{1'000'000'000'000'000'000, -18},
                 Number{1'000'000'000'000'000'000, -36},
                 __LINE__},
                {Number{Number::kMaxRep}, Number{6, -1}, Number{Number::kMaxRep - 1}, __LINE__},
            });
        // Note that items with extremely large mantissas need to be
        // calculated, because otherwise they overflow uint64. Items from C
        // with larger mantissa
        auto const cLarge = std::to_array<Case>({
            // Anything larger than kMaxRep rounds up
            {Number{false, Number::kMaxRep + 1, 0, Number::Normalized{}},
             Number{1, 0},
             Number{(Number::kMaxRep / 10) + 1, 1},
             __LINE__},
            {Number{false, Number::kMaxRep + 1, 0, Number::Normalized{}},
             Number{3, 0},
             Number{Number::kMaxRep},
             __LINE__},
            {Number{false, Number::kMaxRep + 2, 0, Number::Normalized{}},
             Number{1, 0},
             Number{(Number::kMaxRep / 10) + 1, 1},
             __LINE__},
            {Number{false, Number::kMaxRep + 2, 0, Number::Normalized{}},
             Number{3, 0},
             Number{Number::kMaxRep},
             __LINE__},
            {power(2, 63), Number{3, 0}, Number{Number::kMaxRep}, __LINE__},
        });
        auto const cLarge330 = std::to_array<Case>({
            // kMaxRep + 1 is below the half-way point, so it rounds down to kMaxRep when the Number
            // is created.
            {Number{false, Number::kMaxRep + 1, 0, Number::Normalized{}},
             Number{1, 0},
             Number{Number::kMaxRep - 1},
             __LINE__},
            {Number{false, Number::kMaxRep + 1, 0, Number::Normalized{}},
             Number{3, 0},
             Number{Number::kMaxRep - 3},
             __LINE__},
            // kMaxRepUp -1 is above the half-way point, so it rounds up to kMaxRepUp when the
            // Number is created. Subtracting 1 from that rounds up again. A little non-intuitive.
            {Number{false, Number::kMaxRepUp - 1, 0, Number::Normalized{}},
             Number{1, 0},
             Number{(Number::kMaxRep / 10) + 1, 1},
             __LINE__},
            // Subtracting 3 gets back down to kMaxRep
            {Number{false, Number::kMaxRepUp - 1, 0, Number::Normalized{}},
             Number{3, 0},
             Number{Number::kMaxRep},
             __LINE__},
            // 2^63 is the same as kMaxRep+1
            {power(2, 63), Number{3, 0}, Number{Number::kMaxRep - 3}, __LINE__},
        });
        auto test = [](auto const& c) {
            for (auto const& [x, y, z, line] : c)
            {
                auto const result = x - y;
                std::stringstream ss;
                ss << x << " - " << y << " = " << result << ". Expected: " << z;
                EXPECT_EQ(result, z) << ss.str() << " Line: " << line;
            }
        };
        switch (scale)
        {
            case MantissaRange::MantissaScale::Small:
                test(cSmall);
                break;
            case MantissaRange::MantissaScale::LargeLegacy:
            case MantissaRange::MantissaScale::Large320:
                test(cLargeAll);
                test(cLarge);
                break;
            case MantissaRange::MantissaScale::Large330:
                test(cLargeAll);
                test(cLarge330);
                break;
            default:
                ADD_FAILURE();
                break;
        }
    }
}

TEST(NumberTest, mul)
{
    for (auto const mantissaScale : MantissaRange::getAllScales())
    {
        NumberMantissaScaleGuard const sg(mantissaScale);

        auto const scale = Number::getMantissaScale();

        using Case = std::tuple<Number, Number, Number>;
        auto test = [](auto const& c) {
            for (auto const& [x, y, z] : c)
            {
                auto const result = x * y;
                std::stringstream ss;
                ss << x << " * " << y << " = " << result << ". Expected: " << z;
                EXPECT_EQ(result, z) << ss.str();
            }
        };
        auto tests = [&](auto const& cSmall, auto const& cLarge) {
            if (scale == MantissaRange::MantissaScale::Small)
            {
                test(cSmall);
            }
            else
            {
                test(cLarge);
            }
        };
        auto const maxMantissa = Number::maxMantissa();

        SaveNumberRoundMode const save{Number::setround(Number::RoundingMode::ToNearest)};
        {
            auto const cSmall = std::to_array<Case>({
                {Number{7}, Number{8}, Number{56}},
                {Number{1414213562373095, -15},
                 Number{1414213562373095, -15},
                 Number{2000000000000000, -15}},
                {Number{-1414213562373095, -15},
                 Number{1414213562373095, -15},
                 Number{-2000000000000000, -15}},
                {Number{-1414213562373095, -15},
                 Number{-1414213562373095, -15},
                 Number{2000000000000000, -15}},
                {Number{3214285714285706, -15},
                 Number{3111111111111119, -15},
                 Number{1000000000000000, -14}},
                {Number{1000000000000000, -32768}, Number{1000000000000000, -32768}, Number{0}},
                // Maximum mantissa range
                {Number{9'999'999'999'999'999, 0},
                 Number{9'999'999'999'999'999, 0},
                 Number{9'999'999'999'999'998, 16}},
            });
            auto const cLarge = std::to_array<Case>({
                // Note that items with extremely large mantissas need to be
                // calculated, because otherwise they overflow uint64. Items
                // from C with larger mantissa
                {Number{7}, Number{8}, Number{56}},
                {Number{1414213562373095, -15},
                 Number{1414213562373095, -15},
                 Number{1999999999999999862, -18}},
                {Number{-1414213562373095, -15},
                 Number{1414213562373095, -15},
                 Number{-1999999999999999862, -18}},
                {Number{-1414213562373095, -15},
                 Number{-1414213562373095, -15},
                 Number{1999999999999999862, -18}},
                {Number{3214285714285706, -15},
                 Number{3111111111111119, -15},
                 Number{false, 9'999'999'999'999'999'579ULL, -18, Number::Normalized{}}},
                {Number{1000000000000000000, -32768},
                 Number{1000000000000000000, -32768},
                 Number{0}},
                // Items from cSmall expanded for the larger mantissa,
                // except duplicates. Sadly, it looks like sqrt(2)^2 != 2
                // with higher precision
                {Number{1414213562373095049, -18},
                 Number{1414213562373095049, -18},
                 Number{2000000000000000001, -18}},
                {Number{-1414213562373095048, -18},
                 Number{1414213562373095048, -18},
                 Number{-1999999999999999998, -18}},
                {Number{-1414213562373095048, -18},
                 Number{-1414213562373095049, -18},
                 Number{1999999999999999999, -18}},
                {Number{3214285714285714278, -18}, Number{3111111111111111119, -18}, Number{10, 0}},
                // Maximum mantissa range - rounds up to 1e19
                {Number{false, maxMantissa, 0, Number::Normalized{}},
                 Number{false, maxMantissa, 0, Number::Normalized{}},
                 Number{1, 38}},
                // Maximum int64 range
                {Number{Number::kMaxRep, 0},
                 Number{Number::kMaxRep, 0},
                 Number{85'070'591'730'234'615'85, 19}},
            });
            tests(cSmall, cLarge);
        }
        Number::setround(Number::RoundingMode::TowardsZero);
        {
            auto const cSmall = std::to_array<Case>(
                {{Number{7}, Number{8}, Number{56}},
                 {Number{1414213562373095, -15},
                  Number{1414213562373095, -15},
                  Number{1999999999999999, -15}},
                 {Number{-1414213562373095, -15},
                  Number{1414213562373095, -15},
                  Number{-1999999999999999, -15}},
                 {Number{-1414213562373095, -15},
                  Number{-1414213562373095, -15},
                  Number{1999999999999999, -15}},
                 {Number{3214285714285706, -15},
                  Number{3111111111111119, -15},
                  Number{9999999999999999, -15}},
                 {Number{1000000000000000, -32768}, Number{1000000000000000, -32768}, Number{0}}});
            auto const cLarge = std::to_array<Case>(
                // Note that items with extremely large mantissas need to be
                // calculated, because otherwise they overflow uint64. Items
                // from C with larger mantissa
                {
                    {Number{7}, Number{8}, Number{56}},
                    {Number{1414213562373095, -15},
                     Number{1414213562373095, -15},
                     Number{1999999999999999861, -18}},
                    {Number{-1414213562373095, -15},
                     Number{1414213562373095, -15},
                     Number{-1999999999999999861, -18}},
                    {Number{-1414213562373095, -15},
                     Number{-1414213562373095, -15},
                     Number{1999999999999999861, -18}},
                    {Number{3214285714285706, -15},
                     Number{3111111111111119, -15},
                     Number{false, 9999999999999999579ULL, -18, Number::Normalized{}}},
                    {Number{1000000000000000000, -32768},
                     Number{1000000000000000000, -32768},
                     Number{0}},
                    // Items from cSmall expanded for the larger mantissa,
                    // except duplicates. Sadly, it looks like sqrt(2)^2 != 2
                    // with higher precision
                    {Number{1414213562373095049, -18},
                     Number{1414213562373095049, -18},
                     Number{2, 0}},
                    {Number{-1414213562373095048, -18},
                     Number{1414213562373095048, -18},
                     Number{-1999999999999999997, -18}},
                    {Number{-1414213562373095048, -18},
                     Number{-1414213562373095049, -18},
                     Number{1999999999999999999, -18}},
                    {Number{3214285714285714278, -18},
                     Number{3111111111111111119, -18},
                     Number{10, 0}},
                    // Maximum mantissa range - rounds down to maxMantissa/10e1
                    // 99'999'999'999'999'999'800'000'000'000'000'000'100
                    {Number{false, maxMantissa, 0, Number::Normalized{}},
                     Number{false, maxMantissa, 0, Number::Normalized{}},
                     Number{false, (maxMantissa / 10) - 1, 20, Number::Normalized{}}},
                    // Maximum int64 range
                    // 85'070'591'730'234'615'847'396'907'784'232'501'249
                    {Number{Number::kMaxRep, 0},
                     Number{Number::kMaxRep, 0},
                     Number{85'070'591'730'234'615'84, 19}},
                });
            tests(cSmall, cLarge);
        }
        Number::setround(Number::RoundingMode::Downward);
        {
            auto const cSmall = std::to_array<Case>(
                {{Number{7}, Number{8}, Number{56}},
                 {Number{1414213562373095, -15},
                  Number{1414213562373095, -15},
                  Number{1999999999999999, -15}},
                 {Number{-1414213562373095, -15},
                  Number{1414213562373095, -15},
                  Number{-2000000000000000, -15}},
                 {Number{-1414213562373095, -15},
                  Number{-1414213562373095, -15},
                  Number{1999999999999999, -15}},
                 {Number{3214285714285706, -15},
                  Number{3111111111111119, -15},
                  Number{9999999999999999, -15}},
                 {Number{1000000000000000, -32768}, Number{1000000000000000, -32768}, Number{0}}});
            auto const cLarge = std::to_array<Case>(
                // Note that items with extremely large mantissas need to be
                // calculated, because otherwise they overflow uint64. Items
                // from C with larger mantissa
                {
                    {Number{7}, Number{8}, Number{56}},
                    {Number{1414213562373095, -15},
                     Number{1414213562373095, -15},
                     Number{1999999999999999861, -18}},
                    {Number{-1414213562373095, -15},
                     Number{1414213562373095, -15},
                     Number{-1999999999999999862, -18}},
                    {Number{-1414213562373095, -15},
                     Number{-1414213562373095, -15},
                     Number{1999999999999999861, -18}},
                    {Number{3214285714285706, -15},
                     Number{3111111111111119, -15},
                     Number{false, 9'999'999'999'999'999'579ULL, -18, Number::Normalized{}}},
                    {Number{1000000000000000000, -32768},
                     Number{1000000000000000000, -32768},
                     Number{0}},
                    // Items from cSmall expanded for the larger mantissa,
                    // except duplicates. Sadly, it looks like sqrt(2)^2 != 2
                    // with higher precision
                    {Number{1414213562373095049, -18},
                     Number{1414213562373095049, -18},
                     Number{2, 0}},
                    {Number{-1414213562373095048, -18},
                     Number{1414213562373095048, -18},
                     Number{-1999999999999999998, -18}},
                    {Number{-1414213562373095048, -18},
                     Number{-1414213562373095049, -18},
                     Number{1999999999999999999, -18}},
                    {Number{3214285714285714278, -18},
                     Number{3111111111111111119, -18},
                     Number{10, 0}},
                    // Maximum mantissa range - rounds down to maxMantissa/10e1
                    // 99'999'999'999'999'999'800'000'000'000'000'000'100
                    {Number{false, maxMantissa, 0, Number::Normalized{}},
                     Number{false, maxMantissa, 0, Number::Normalized{}},
                     Number{false, (maxMantissa / 10) - 1, 20, Number::Normalized{}}},
                    // Maximum int64 range
                    // 85'070'591'730'234'615'847'396'907'784'232'501'249
                    {Number{Number::kMaxRep, 0},
                     Number{Number::kMaxRep, 0},
                     Number{85'070'591'730'234'615'84, 19}},
                });
            tests(cSmall, cLarge);
        }
        Number::setround(Number::RoundingMode::Upward);
        {
            auto const cSmall = std::to_array<Case>(
                {{Number{7}, Number{8}, Number{56}},
                 {Number{1414213562373095, -15},
                  Number{1414213562373095, -15},
                  Number{2000000000000000, -15}},
                 {Number{-1414213562373095, -15},
                  Number{1414213562373095, -15},
                  Number{-1999999999999999, -15}},
                 {Number{-1414213562373095, -15},
                  Number{-1414213562373095, -15},
                  Number{2000000000000000, -15}},
                 {Number{3214285714285706, -15},
                  Number{3111111111111119, -15},
                  Number{1000000000000000, -14}},
                 {Number{1000000000000000, -32768}, Number{1000000000000000, -32768}, Number{0}}});
            auto const cLarge = std::to_array<Case>(
                // Note that items with extremely large mantissas need to be
                // calculated, because otherwise they overflow uint64. Items
                // from C with larger mantissa
                {
                    {Number{7}, Number{8}, Number{56}},
                    {Number{1414213562373095, -15},
                     Number{1414213562373095, -15},
                     Number{1999999999999999862, -18}},
                    {Number{-1414213562373095, -15},
                     Number{1414213562373095, -15},
                     Number{-1999999999999999861, -18}},
                    {Number{-1414213562373095, -15},
                     Number{-1414213562373095, -15},
                     Number{1999999999999999862, -18}},
                    {Number{3214285714285706, -15},
                     Number{3111111111111119, -15},
                     Number{999999999999999958, -17}},
                    {Number{1000000000000000000, -32768},
                     Number{1000000000000000000, -32768},
                     Number{0}},
                    // Items from cSmall expanded for the larger mantissa,
                    // except duplicates. Sadly, it looks like sqrt(2)^2 != 2
                    // with higher precision
                    {Number{1414213562373095049, -18},
                     Number{1414213562373095049, -18},
                     Number{2000000000000000001, -18}},
                    {Number{-1414213562373095048, -18},
                     Number{1414213562373095048, -18},
                     Number{-1999999999999999997, -18}},
                    {Number{-1414213562373095048, -18},
                     Number{-1414213562373095049, -18},
                     Number{2, 0}},
                    {Number{3214285714285714278, -18},
                     Number{3111111111111111119, -18},
                     Number{1000000000000000001, -17}},
                    // Maximum mantissa range - rounds up to minMantissa*10
                    // 1e19*1e19=1e38
                    {Number{false, maxMantissa, 0, Number::Normalized{}},
                     Number{false, maxMantissa, 0, Number::Normalized{}},
                     Number{1, 38}},
                    // Maximum int64 range
                    // 85'070'591'730'234'615'847'396'907'784'232'501'249
                    {Number{Number::kMaxRep, 0},
                     Number{Number::kMaxRep, 0},
                     Number{85'070'591'730'234'615'85, 19}},
                });
            tests(cSmall, cLarge);
        }
        {
            bool caught = false;
            try
            {
                Number{false, maxMantissa, 32768, Number::Normalized{}} *
                    Number{false, Number::minMantissa() * 5, 32767, Number::Normalized{}};
            }
            catch (std::overflow_error const&)
            {
                caught = true;
            }
            EXPECT_TRUE(caught);
        }
    }
}

TEST(NumberTest, div)
{
    for (auto const mantissaScale : MantissaRange::getAllScales())
    {
        NumberMantissaScaleGuard const sg(mantissaScale);

        auto const scale = Number::getMantissaScale();

        using Case = std::tuple<Number, Number, Number>;
        auto test = [](auto const& c) {
            for (auto const& [x, y, z] : c)
            {
                auto const result = x / y;
                std::stringstream ss;
                ss << x << " / " << y << " = " << result << ". Expected: " << z;
                EXPECT_EQ(result, z) << ss.str();
            }
        };
        auto const maxMantissa = Number::maxMantissa();
        auto tests = [&](auto const& cSmall, auto const& cLarge) {
            if (scale == MantissaRange::MantissaScale::Small)
            {
                test(cSmall);
            }
            else
            {
                test(cLarge);
            }
        };
        SaveNumberRoundMode const save{Number::setround(Number::RoundingMode::ToNearest)};
        {
            auto const cSmall = std::to_array<Case>(
                {{Number{1}, Number{2}, Number{5, -1}},
                 {Number{1}, Number{10}, Number{1, -1}},
                 {Number{1}, Number{-10}, Number{-1, -1}},
                 {Number{0}, Number{100}, Number{0}},
                 {Number{1414213562373095, -10}, Number{1414213562373095, -10}, Number{1}},
                 {Number{9'999'999'999'999'999},
                  Number{1'000'000'000'000'000},
                  Number{9'999'999'999'999'999, -15}},
                 {Number{2}, Number{3}, Number{6'666'666'666'666'667, -16}},
                 {Number{-2}, Number{3}, Number{-6'666'666'666'666'667, -16}},
                 {Number{1}, Number{7}, Number{1'428'571'428'571'428, -16}}});
            auto const cLarge = std::to_array<Case>(
                // Note that items with extremely large mantissas need to be
                // calculated, because otherwise they overflow uint64. Items
                // from C with larger mantissa
                {{Number{1}, Number{2}, Number{5, -1}},
                 {Number{1}, Number{10}, Number{1, -1}},
                 {Number{1}, Number{-10}, Number{-1, -1}},
                 {Number{0}, Number{100}, Number{0}},
                 {Number{1414213562373095, -10}, Number{1414213562373095, -10}, Number{1}},
                 {Number{9'999'999'999'999'999},
                  Number{1'000'000'000'000'000},
                  Number{9'999'999'999'999'999, -15}},
                 {Number{2}, Number{3}, Number{6'666'666'666'666'666'667, -19}},
                 {Number{-2}, Number{3}, Number{-6'666'666'666'666'666'667, -19}},
                 {Number{1}, Number{7}, Number{1'428'571'428'571'428'571, -19}},
                 // Items from cSmall expanded for the larger mantissa, except
                 // duplicates.
                 {Number{1414213562373095049, -13}, Number{1414213562373095049, -13}, Number{1}},
                 {Number{false, maxMantissa, 0, Number::Normalized{}},
                  Number{1'000'000'000'000'000'000},
                  Number{false, maxMantissa, -18, Number::Normalized{}}}});
            tests(cSmall, cLarge);
        }
        Number::setround(Number::RoundingMode::TowardsZero);
        {
            auto const cSmall = std::to_array<Case>(
                {{Number{1}, Number{2}, Number{5, -1}},
                 {Number{1}, Number{10}, Number{1, -1}},
                 {Number{1}, Number{-10}, Number{-1, -1}},
                 {Number{0}, Number{100}, Number{0}},
                 {Number{1414213562373095, -10}, Number{1414213562373095, -10}, Number{1}},
                 {Number{9'999'999'999'999'999},
                  Number{1'000'000'000'000'000},
                  Number{9'999'999'999'999'999, -15}},
                 {Number{2}, Number{3}, Number{6'666'666'666'666'666, -16}},
                 {Number{-2}, Number{3}, Number{-6'666'666'666'666'666, -16}},
                 {Number{1}, Number{7}, Number{1'428'571'428'571'428, -16}}});
            auto const cLarge = std::to_array<Case>(
                // Note that items with extremely large mantissas need to be
                // calculated, because otherwise they overflow uint64. Items
                // from C with larger mantissa
                {{Number{1}, Number{2}, Number{5, -1}},
                 {Number{1}, Number{10}, Number{1, -1}},
                 {Number{1}, Number{-10}, Number{-1, -1}},
                 {Number{0}, Number{100}, Number{0}},
                 {Number{1414213562373095, -10}, Number{1414213562373095, -10}, Number{1}},
                 {Number{9'999'999'999'999'999},
                  Number{1'000'000'000'000'000},
                  Number{9'999'999'999'999'999, -15}},
                 {Number{2}, Number{3}, Number{6'666'666'666'666'666'666, -19}},
                 {Number{-2}, Number{3}, Number{-6'666'666'666'666'666'666, -19}},
                 {Number{1}, Number{7}, Number{1'428'571'428'571'428'571, -19}},
                 // Items from cSmall expanded for the larger mantissa, except
                 // duplicates.
                 {Number{1414213562373095049, -13}, Number{1414213562373095049, -13}, Number{1}},
                 {Number{false, maxMantissa, 0, Number::Normalized{}},
                  Number{1'000'000'000'000'000'000},
                  Number{false, maxMantissa, -18, Number::Normalized{}}}});
            tests(cSmall, cLarge);
        }
        Number::setround(Number::RoundingMode::Downward);
        {
            auto const cSmall = std::to_array<Case>(
                {{Number{1}, Number{2}, Number{5, -1}},
                 {Number{1}, Number{10}, Number{1, -1}},
                 {Number{1}, Number{-10}, Number{-1, -1}},
                 {Number{0}, Number{100}, Number{0}},
                 {Number{1414213562373095, -10}, Number{1414213562373095, -10}, Number{1}},
                 {Number{9'999'999'999'999'999},
                  Number{1'000'000'000'000'000},
                  Number{9'999'999'999'999'999, -15}},
                 {Number{2}, Number{3}, Number{6'666'666'666'666'666, -16}},
                 {Number{-2}, Number{3}, Number{-6'666'666'666'666'667, -16}},
                 {Number{1}, Number{7}, Number{1'428'571'428'571'428, -16}}});
            auto const cLarge = std::to_array<Case>(
                // Note that items with extremely large mantissas need to be
                // calculated, because otherwise they overflow uint64. Items
                // from C with larger mantissa
                {{Number{1}, Number{2}, Number{5, -1}},
                 {Number{1}, Number{10}, Number{1, -1}},
                 {Number{1}, Number{-10}, Number{-1, -1}},
                 {Number{0}, Number{100}, Number{0}},
                 {Number{1414213562373095, -10}, Number{1414213562373095, -10}, Number{1}},
                 {Number{9'999'999'999'999'999},
                  Number{1'000'000'000'000'000},
                  Number{9'999'999'999'999'999, -15}},
                 {Number{2}, Number{3}, Number{6'666'666'666'666'666'666, -19}},
                 {Number{-2}, Number{3}, Number{-6'666'666'666'666'666'667, -19}},
                 {Number{1}, Number{7}, Number{1'428'571'428'571'428'571, -19}},
                 // Items from cSmall expanded for the larger mantissa, except
                 // duplicates.
                 {Number{1414213562373095049, -13}, Number{1414213562373095049, -13}, Number{1}},
                 {Number{false, maxMantissa, 0, Number::Normalized{}},
                  Number{1'000'000'000'000'000'000},
                  Number{false, maxMantissa, -18, Number::Normalized{}}}});
            tests(cSmall, cLarge);
        }
        Number::setround(Number::RoundingMode::Upward);
        {
            auto const cSmall = std::to_array<Case>(
                {{Number{1}, Number{2}, Number{5, -1}},
                 {Number{1}, Number{10}, Number{1, -1}},
                 {Number{1}, Number{-10}, Number{-1, -1}},
                 {Number{0}, Number{100}, Number{0}},
                 {Number{1414213562373095, -10}, Number{1414213562373095, -10}, Number{1}},
                 {Number{9'999'999'999'999'999},
                  Number{1'000'000'000'000'000},
                  Number{9'999'999'999'999'999, -15}},
                 {Number{2}, Number{3}, Number{6'666'666'666'666'667, -16}},
                 {Number{-2}, Number{3}, Number{-6'666'666'666'666'666, -16}},
                 {Number{1}, Number{7}, Number{1'428'571'428'571'429, -16}}});
            auto const cLarge = std::to_array<Case>(
                // Note that items with extremely large mantissas need to be
                // calculated, because otherwise they overflow uint64. Items
                // from C with larger mantissa
                {{Number{1}, Number{2}, Number{5, -1}},
                 {Number{1}, Number{10}, Number{1, -1}},
                 {Number{1}, Number{-10}, Number{-1, -1}},
                 {Number{0}, Number{100}, Number{0}},
                 {Number{1414213562373095, -10}, Number{1414213562373095, -10}, Number{1}},
                 {Number{9'999'999'999'999'999},
                  Number{1'000'000'000'000'000},
                  Number{9'999'999'999'999'999, -15}},
                 {Number{2}, Number{3}, Number{6'666'666'666'666'666'667, -19}},
                 {Number{-2}, Number{3}, Number{-6'666'666'666'666'666'666, -19}},
                 {Number{1}, Number{7}, Number{1'428'571'428'571'428'572, -19}},
                 // Items from cSmall expanded for the larger mantissa, except
                 // duplicates.
                 {Number{1414213562373095049, -13}, Number{1414213562373095049, -13}, Number{1}},
                 {Number{false, maxMantissa, 0, Number::Normalized{}},
                  Number{1'000'000'000'000'000'000},
                  Number{false, maxMantissa, -18, Number::Normalized{}}}});
            tests(cSmall, cLarge);
        }
        bool caught = false;
        try
        {
            Number{1000000000000000, -15} / Number{0};
        }
        catch (std::overflow_error const&)
        {
            caught = true;
        }
        EXPECT_TRUE(caught);
    }
}

TEST(NumberTest, root)
{
    for (auto const mantissaScale : MantissaRange::getAllScales())
    {
        NumberMantissaScaleGuard const sg(mantissaScale);

        using Case = std::tuple<Number, unsigned, Number>;
        auto test = [](auto const& c) {
            for (auto const& [x, y, z] : c)
            {
                auto const result = root(x, y);
                std::stringstream ss;
                ss << "root(" << x << ", " << y << ") = " << result << ". Expected: " << z;
                EXPECT_EQ(result, z) << ss.str();
            }
        };
        auto const cSmall = std::to_array<Case>(
            {{Number{2}, 2, Number{1414213562373095049, -18}},
             {Number{2'000'000}, 2, Number{1414213562373095049, -15}},
             {Number{2, -30}, 2, Number{1414213562373095049, -33}},
             {Number{-27}, 3, Number{-3}},
             {Number{1}, 5, Number{1}},
             {Number{-1}, 0, Number{1}},
             {Number{5, -1}, 0, Number{0}},
             {Number{0}, 5, Number{0}},
             {Number{5625, -4}, 2, Number{75, -2}}});
        auto const cLarge = std::to_array<Case>({
            {Number{false, Number::maxMantissa() - 9, -1, Number::Normalized{}},
             2,
             Number{false, 999'999'999'999'999'999, -9, Number::Normalized{}}},
            {Number{false, Number::maxMantissa() - 9, 0, Number::Normalized{}},
             2,
             Number{false, 3'162'277'660'168'379'330, -9, Number::Normalized{}}},
            {Number{Number::kMaxRep},
             2,
             Number{false, 3'037'000'499'976049692, -9, Number::Normalized{}}},
            {Number{Number::kMaxRep},
             4,
             Number{false, 55'108'98747006743627, -14, Number::Normalized{}}},
        });
        test(cSmall);
        if (Number::getMantissaScale() != MantissaRange::MantissaScale::Small)
        {
            NumberRoundModeGuard const mg(Number::RoundingMode::TowardsZero);
            test(cLarge);
        }
        bool caught = false;
        try
        {
            (void)root(Number{-2}, 0);
        }
        catch (std::overflow_error const&)
        {
            caught = true;
        }
        EXPECT_TRUE(caught);
        caught = false;
        try
        {
            (void)root(Number{-2}, 4);
        }
        catch (std::overflow_error const&)
        {
            caught = true;
        }
        EXPECT_TRUE(caught);
    }
}

TEST(NumberTest, root2)
{
    for (auto const mantissaScale : MantissaRange::getAllScales())
    {
        NumberMantissaScaleGuard const sg(mantissaScale);

        auto test = [](auto const& c) {
            for (auto const& x : c)
            {
                auto const expected = root(x, 2);
                auto const result = root2(x);
                std::stringstream ss;
                ss << "root2(" << x << ") = " << result << ". Expected: " << expected;
                EXPECT_EQ(result, expected) << ss.str();
            }
        };

        auto const cSmall = std::to_array<Number>({
            Number{2},
            Number{2'000'000},
            Number{2, -30},
            Number{27},
            Number{1},
            Number{5, -1},
            Number{0},
            Number{5625, -4},
            Number{Number::kMaxRep},
        });
        test(cSmall);
        bool caught = false;
        try
        {
            (void)root2(Number{-2});
        }
        catch (std::overflow_error const&)
        {
            caught = true;
        }
        EXPECT_TRUE(caught);
    }
}

TEST(NumberTest, power1)
{
    for (auto const mantissaScale : MantissaRange::getAllScales())
    {
        NumberMantissaScaleGuard const sg(mantissaScale);

        using Case = std::tuple<Number, unsigned, Number>;
        Case const c[]{
            {Number{64}, 0, Number{1}},
            {Number{64}, 1, Number{64}},
            {Number{64}, 2, Number{4096}},
            {Number{-64}, 2, Number{4096}},
            {Number{64}, 3, Number{262144}},
            {Number{-64}, 3, Number{-262144}},
            {Number{64}, 11, Number{false, 7378697629483820646ULL, 1, Number::Normalized{}}},
            {Number{-64}, 11, Number{true, 7378697629483820646ULL, 1, Number::Normalized{}}}};
        for (auto const& [x, y, z] : c)
            EXPECT_EQ(power(x, y), z);
    }
}

TEST(NumberTest, power2)
{
    for (auto const mantissaScale : MantissaRange::getAllScales())
    {
        NumberMantissaScaleGuard const sg(mantissaScale);

        using Case = std::tuple<Number, unsigned, unsigned, Number>;
        Case const c[]{
            {Number{1}, 3, 7, Number{1}},
            {Number{-1}, 1, 0, Number{1}},
            {Number{-1, -1}, 1, 0, Number{0}},
            {Number{16}, 0, 5, Number{1}},
            {Number{34}, 3, 3, Number{34}},
            {Number{4}, 3, 2, Number{8}}};
        for (auto const& [x, n, d, z] : c)
            EXPECT_EQ(power(x, n, d), z);
        bool caught = false;
        try
        {
            (void)power(Number{7}, 0, 0);
        }
        catch (std::overflow_error const&)
        {
            caught = true;
        }
        EXPECT_TRUE(caught);
        caught = false;
        try
        {
            (void)power(Number{7}, 1, 0);
        }
        catch (std::overflow_error const&)
        {
            caught = true;
        }
        EXPECT_TRUE(caught);
        caught = false;
        try
        {
            (void)power(Number{-1, -1}, 3, 2);
        }
        catch (std::overflow_error const&)
        {
            caught = true;
        }
        EXPECT_TRUE(caught);
    }
}

TEST(NumberTest, conversions)
{
    for (auto const mantissaScale : MantissaRange::getAllScales())
    {
        NumberMantissaScaleGuard const sg(mantissaScale);

        IOUAmount const x{5, 6};
        Number const y = x;
        EXPECT_EQ(y, (Number{5, 6}));
        IOUAmount const z{y};
        EXPECT_EQ(x, z);
        XRPAmount const xrp{500};
        STAmount const st = xrp;
        Number const n = st;
        EXPECT_EQ(XRPAmount{n}, xrp);
        IOUAmount const x0{0, 0};
        Number const y0 = x0;
        EXPECT_EQ(y0, Number{0});
        IOUAmount const z0{y0};
        EXPECT_EQ(x0, z0);
        XRPAmount const xrp0{0};
        Number const n0 = xrp0;
        EXPECT_EQ(n0, Number{0});
        XRPAmount const xrp1{n0};  // NOLINT misc-confusable-identifiers
        EXPECT_EQ(xrp1, xrp0);
    }
}

TEST(NumberTest, to_integer)
{
    for (auto const mantissaScale : MantissaRange::getAllScales())
    {
        NumberMantissaScaleGuard const sg(mantissaScale);

        using Case = std::tuple<Number, std::int64_t>;
        SaveNumberRoundMode const save{Number::setround(Number::RoundingMode::ToNearest)};
        {
            Case const c[]{
                {Number{0}, 0},
                {Number{1}, 1},
                {Number{2}, 2},
                {Number{3}, 3},
                {Number{-1}, -1},
                {Number{-2}, -2},
                {Number{-3}, -3},
                {Number{10}, 10},
                {Number{99}, 99},
                {Number{1155}, 1155},
                {Number{9'999'999'999'999'999, 0}, 9'999'999'999'999'999},
                {Number{9'999'999'999'999'999, 1}, 99'999'999'999'999'990},
                {Number{9'999'999'999'999'999, 2}, 999'999'999'999'999'900},
                {Number{-9'999'999'999'999'999, 2}, -999'999'999'999'999'900},
                {Number{15, -1}, 2},
                {Number{14, -1}, 1},
                {Number{16, -1}, 2},
                {Number{25, -1}, 2},
                {Number{6, -1}, 1},
                {Number{5, -1}, 0},
                {Number{4, -1}, 0},
                {Number{-15, -1}, -2},
                {Number{-14, -1}, -1},
                {Number{-16, -1}, -2},
                {Number{-25, -1}, -2},
                {Number{-6, -1}, -1},
                {Number{-5, -1}, 0},
                {Number{-4, -1}, 0}};
            for (auto const& [x, y] : c)
            {
                auto j = static_cast<std::int64_t>(x);
                EXPECT_EQ(j, y);
            }
        }
        auto prevMode = Number::setround(Number::RoundingMode::TowardsZero);
        EXPECT_EQ(prevMode, Number::RoundingMode::ToNearest);
        {
            Case const c[]{
                {Number{0}, 0},
                {Number{1}, 1},
                {Number{2}, 2},
                {Number{3}, 3},
                {Number{-1}, -1},
                {Number{-2}, -2},
                {Number{-3}, -3},
                {Number{10}, 10},
                {Number{99}, 99},
                {Number{1155}, 1155},
                {Number{9'999'999'999'999'999, 0}, 9'999'999'999'999'999},
                {Number{9'999'999'999'999'999, 1}, 99'999'999'999'999'990},
                {Number{9'999'999'999'999'999, 2}, 999'999'999'999'999'900},
                {Number{-9'999'999'999'999'999, 2}, -999'999'999'999'999'900},
                {Number{15, -1}, 1},
                {Number{14, -1}, 1},
                {Number{16, -1}, 1},
                {Number{25, -1}, 2},
                {Number{6, -1}, 0},
                {Number{5, -1}, 0},
                {Number{4, -1}, 0},
                {Number{-15, -1}, -1},
                {Number{-14, -1}, -1},
                {Number{-16, -1}, -1},
                {Number{-25, -1}, -2},
                {Number{-6, -1}, 0},
                {Number{-5, -1}, 0},
                {Number{-4, -1}, 0}};
            for (auto const& [x, y] : c)
            {
                auto j = static_cast<std::int64_t>(x);
                EXPECT_EQ(j, y);
            }
        }
        prevMode = Number::setround(Number::RoundingMode::Downward);
        EXPECT_EQ(prevMode, Number::RoundingMode::TowardsZero);
        {
            Case const c[]{
                {Number{0}, 0},
                {Number{1}, 1},
                {Number{2}, 2},
                {Number{3}, 3},
                {Number{-1}, -1},
                {Number{-2}, -2},
                {Number{-3}, -3},
                {Number{10}, 10},
                {Number{99}, 99},
                {Number{1155}, 1155},
                {Number{9'999'999'999'999'999, 0}, 9'999'999'999'999'999},
                {Number{9'999'999'999'999'999, 1}, 99'999'999'999'999'990},
                {Number{9'999'999'999'999'999, 2}, 999'999'999'999'999'900},
                {Number{-9'999'999'999'999'999, 2}, -999'999'999'999'999'900},
                {Number{15, -1}, 1},
                {Number{14, -1}, 1},
                {Number{16, -1}, 1},
                {Number{25, -1}, 2},
                {Number{6, -1}, 0},
                {Number{5, -1}, 0},
                {Number{4, -1}, 0},
                {Number{-15, -1}, -2},
                {Number{-14, -1}, -2},
                {Number{-16, -1}, -2},
                {Number{-25, -1}, -3},
                {Number{-6, -1}, -1},
                {Number{-5, -1}, -1},
                {Number{-4, -1}, -1}};
            for (auto const& [x, y] : c)
            {
                auto j = static_cast<std::int64_t>(x);
                EXPECT_EQ(j, y);
            }
        }
        prevMode = Number::setround(Number::RoundingMode::Upward);
        EXPECT_EQ(prevMode, Number::RoundingMode::Downward);
        {
            Case const c[]{
                {Number{0}, 0},
                {Number{1}, 1},
                {Number{2}, 2},
                {Number{3}, 3},
                {Number{-1}, -1},
                {Number{-2}, -2},
                {Number{-3}, -3},
                {Number{10}, 10},
                {Number{99}, 99},
                {Number{1155}, 1155},
                {Number{9'999'999'999'999'999, 0}, 9'999'999'999'999'999},
                {Number{9'999'999'999'999'999, 1}, 99'999'999'999'999'990},
                {Number{9'999'999'999'999'999, 2}, 999'999'999'999'999'900},
                {Number{-9'999'999'999'999'999, 2}, -999'999'999'999'999'900},
                {Number{15, -1}, 2},
                {Number{14, -1}, 2},
                {Number{16, -1}, 2},
                {Number{25, -1}, 3},
                {Number{6, -1}, 1},
                {Number{5, -1}, 1},
                {Number{4, -1}, 1},
                {Number{-15, -1}, -1},
                {Number{-14, -1}, -1},
                {Number{-16, -1}, -1},
                {Number{-25, -1}, -2},
                {Number{-6, -1}, 0},
                {Number{-5, -1}, 0},
                {Number{-4, -1}, 0}};
            for (auto const& [x, y] : c)
            {
                auto j = static_cast<std::int64_t>(x);
                EXPECT_EQ(j, y);
            }
        }
        bool caught = false;
        try
        {
            (void)static_cast<std::int64_t>(Number{9223372036854776, 3});
        }
        catch (std::overflow_error const&)
        {
            caught = true;
        }
        EXPECT_TRUE(caught);
    }
}

TEST(NumberTest, squelch)
{
    for (auto const mantissaScale : MantissaRange::getAllScales())
    {
        NumberMantissaScaleGuard const sg(mantissaScale);

        Number const limit{1, -6};
        EXPECT_EQ(squelch(Number{2, -6}, limit), (Number{2, -6}));
        EXPECT_EQ(squelch(Number{1, -6}, limit), (Number{1, -6}));
        EXPECT_EQ(squelch(Number{9, -7}, limit), Number{0});
        EXPECT_EQ(squelch(Number{-2, -6}, limit), (Number{-2, -6}));
        EXPECT_EQ(squelch(Number{-1, -6}, limit), (Number{-1, -6}));
        EXPECT_EQ(squelch(Number{-9, -7}, limit), Number{0});
    }
}

TEST(NumberTest, to_string)
{
    for (auto const mantissaScale : MantissaRange::getAllScales())
    {
        NumberMantissaScaleGuard const sg(mantissaScale);

        auto const scale = Number::getMantissaScale();

        auto test = [](Number const& n, std::string const& expected, int line) {
            auto const result = to_string(n);
            std::stringstream ss;
            ss << "to_string(" << result << "). Expected: " << expected;
            EXPECT_EQ(result, expected) << ss.str() << " Line: " << line;
        };

        test(Number(-2, 0), "-2", __LINE__);
        test(Number(0, 0), "0", __LINE__);
        test(Number(2, 0), "2", __LINE__);
        test(Number(25, -3), "0.025", __LINE__);
        test(Number(-25, -3), "-0.025", __LINE__);
        test(Number(25, 1), "250", __LINE__);
        test(Number(-25, 1), "-250", __LINE__);
        test(Number(2, 20), "2e20", __LINE__);
        test(Number(-2, -20), "-2e-20", __LINE__);
        // Test the edges
        // ((exponent < -(25)) || (exponent > -(5)))))
        // or ((exponent < -(28)) || (exponent > -(8)))))
        test(Number(2, -10), "0.0000000002", __LINE__);
        test(Number(2, -11), "2e-11", __LINE__);

        test(Number(-2, 10), "-20000000000", __LINE__);
        test(Number(-2, 11), "-2e11", __LINE__);
        test(Number(-2, 11) - 1, "-200000000001", __LINE__);

        switch (scale)
        {
            case MantissaRange::MantissaScale::Small:

                test(Number::min(), "1e-32753", __LINE__);
                test(Number::max(), "9999999999999999e32768", __LINE__);
                test(Number::lowest(), "-9999999999999999e32768", __LINE__);
                {
                    NumberRoundModeGuard const mg(Number::RoundingMode::TowardsZero);

                    auto const maxMantissa = Number::maxMantissa();
                    EXPECT_EQ(maxMantissa, 9'999'999'999'999'999);
                    test(
                        Number{false, (maxMantissa * 1000) + 999, -3, Number::Normalized()},
                        "9999999999999999",
                        __LINE__);
                    test(
                        Number{true, (maxMantissa * 1000) + 999, -3, Number::Normalized()},
                        "-9999999999999999",
                        __LINE__);

                    test(
                        Number{std::numeric_limits<std::int64_t>::max(), -3},
                        "9223372036854775",
                        __LINE__);
                    test(
                        -(Number{std::numeric_limits<std::int64_t>::max(), -3}),
                        "-9223372036854775",
                        __LINE__);

                    test(
                        Number{std::numeric_limits<std::int64_t>::min(), 0},
                        "-9223372036854775e3",
                        __LINE__);
                    test(
                        -(Number{std::numeric_limits<std::int64_t>::min(), 0}),
                        "9223372036854775e3",
                        __LINE__);
                }
                break;
            default:
                // Test the edges
                // ((exponent < -(28)) || (exponent > -(8)))))
                test(Number::min(), "1e-32750", __LINE__);
                test(Number::max(), "9223372036854775807e32768", __LINE__);
                test(Number::lowest(), "-9223372036854775807e32768", __LINE__);
                {
                    NumberRoundModeGuard const mg(Number::RoundingMode::TowardsZero);

                    auto const maxMantissa = Number::maxMantissa();
                    EXPECT_EQ(maxMantissa, 9'999'999'999'999'999'999ULL);
                    test(
                        Number{false, maxMantissa, 0, Number::Normalized{}},
                        "9999999999999999990",
                        __LINE__);
                    test(
                        Number{true, maxMantissa, 0, Number::Normalized{}},
                        "-9999999999999999990",
                        __LINE__);

                    test(
                        Number{std::numeric_limits<std::int64_t>::max(), 0},
                        "9223372036854775807",
                        __LINE__);
                    test(
                        -(Number{std::numeric_limits<std::int64_t>::max(), 0}),
                        "-9223372036854775807",
                        __LINE__);

                    switch (scale)
                    {
                        case MantissaRange::MantissaScale::Large330:
                            // Because the absolute value of min() is larger than max(), it
                            // will be rounded down toward max()
                            test(
                                Number{std::numeric_limits<std::int64_t>::min(), 0},
                                "-9223372036854775807",
                                __LINE__);
                            test(
                                -(Number{std::numeric_limits<std::int64_t>::min(), 0}),
                                "9223372036854775807",
                                __LINE__);
                            break;
                        default:
                            // Because the absolute value of min() is larger than max(), it
                            // will be scaled down to fit under max(). Since we're
                            // rounding towards zero, the 8 at the end is dropped.
                            test(
                                Number{std::numeric_limits<std::int64_t>::min(), 0},
                                "-9223372036854775800",
                                __LINE__);
                            test(
                                -(Number{std::numeric_limits<std::int64_t>::min(), 0}),
                                "9223372036854775800",
                                __LINE__);
                            break;
                    }
                }

                switch (scale)
                {
                    case MantissaRange::MantissaScale::Large330:
                        // Rounding to nearest, since the mantissa is below the halfway point from
                        // kMaxRep to kMaxRepUp, it will be rounded down to kMaxRep
                        test(
                            Number{std::numeric_limits<std::int64_t>::max(), 0} + 1,
                            "9223372036854775807",
                            __LINE__);
                        test(
                            -(Number{std::numeric_limits<std::int64_t>::max(), 0} + 1),
                            "-9223372036854775807",
                            __LINE__);
                        break;
                    default:
                        // Rounding to nearest, since the mantissa is bigger than kMaxRep, the 8
                        // will be dropped, and since that is bigger than 5, the result will be
                        // rounded up from 0 to 1.
                        test(
                            Number{std::numeric_limits<std::int64_t>::max(), 0} + 1,
                            "9223372036854775810",
                            __LINE__);
                        test(
                            -(Number{std::numeric_limits<std::int64_t>::max(), 0} + 1),
                            "-9223372036854775810",
                            __LINE__);
                        break;
                }
                // Rounding to nearest, will be rounded up to kMaxRepUp, but for different reasons
                // depending on the scale. If older than "Large", it rounds up for the same reason
                // "+1" rounds up. For "Large", since the mantissa is above the halfway point from
                // kMaxRep to kMaxRepUp, it will be rounded up to kMaxRepUp.
                test(
                    Number{std::numeric_limits<std::int64_t>::max(), 0} + 2,
                    "9223372036854775810",
                    __LINE__);
                test(
                    -(Number{std::numeric_limits<std::int64_t>::max(), 0} + 2),
                    "-9223372036854775810",
                    __LINE__);
                break;
        }
    }
}

TEST(NumberTest, relationals)
{
    for (auto const mantissaScale : MantissaRange::getAllScales())
    {
        NumberMantissaScaleGuard const sg(mantissaScale);

        {
            auto test = [](auto const& nums) {
                EXPECT_TRUE(std::ranges::is_sorted(nums));

                for (auto iter1 = nums.begin(); iter1 != nums.end(); ++iter1)
                {
                    auto iter2 = iter1;
                    for (++iter2; iter2 != nums.end(); ++iter2)
                    {
                        Number const& smaller = *iter1;
                        Number const& larger = *iter2;
                        std::stringstream ss;
                        ss << smaller << " < " << larger;
                        auto const str = ss.str();

                        // The ==/!= operators use a completely different code path than <, etc.
                        // This helps detect a breakage in one but not the other. It also helps
                        // verify that the values are being ordered correctly.
                        EXPECT_TRUE(smaller != larger) << str << " (!=)";
                        EXPECT_FALSE(smaller == larger) << str << " (==)";

                        // true results using operator< and derived operators
                        EXPECT_TRUE(smaller < larger) << str << " (<)";
                        EXPECT_TRUE(larger > smaller) << str << " (>)";
                        EXPECT_TRUE(larger >= smaller) << str << " (>=)";
                        EXPECT_TRUE(smaller <= larger) << str << " (<=)";

                        // false results using operator< and derived operators
                        EXPECT_FALSE(larger < smaller) << str << " (! <)";
                        EXPECT_FALSE(smaller > larger) << str << " (! >)";
                        EXPECT_FALSE(smaller >= larger) << str << " (! >=)";
                        EXPECT_FALSE(larger <= smaller) << str << " (! <=)";
                    }
                }
            };

            auto const intNums = []() {
                // Inequality test cases are built from a list of sorted integers
                auto const values =
                    std::to_array<int>({-100, -50, -20, -10, -1, 0, 1, 10, 20, 50, 100});
                // Check this list is sorted before converting it to Numbers.
                // That way if any of the other tests fail, we know it's because of code and not the
                // source data.
                EXPECT_TRUE(std::ranges::is_sorted(values));

                std::vector<Number> result;
                result.reserve(values.size());
                for (auto const v : values)
                    result.emplace_back(v);
                return result;
            }();

            auto const otherNums = std::to_array<Number>({
                Number{-5, 100},
                Number{-1, 100},
                Number{-7, -10},
                Number{-2, -10},
                Number{0},
                Number{2, -10},
                Number{7, -10},
                Number{1, 100},
                Number{5, 100},
            });

            test(intNums);
            test(otherNums);
        }

        {
            // Equality test cases are <Number, __LINE__>. Number will be compared against itself
            using Case = std::pair<Number, int>;
            auto const c = std::to_array<Case>({
                {700, __LINE__},
                {50, __LINE__},
                {1, __LINE__},
                {0, __LINE__},
                {-1, __LINE__},
                {-30, __LINE__},
                {-600, __LINE__},
            });
            for (auto const& [n, line] : c)
            {
                auto const str = to_string(n);
                auto const location =
                    std::string{" ("} + __FILE__ + ":" + std::to_string(line) + ")";

                // NOLINTBEGIN(misc-redundant-expression) Explicitly testing operators with
                // equivalent values
                EXPECT_TRUE(n == n) << str << " ==" << location;
                EXPECT_FALSE(n != n) << str << " !=" << location;

                EXPECT_FALSE(n < n) << str << " <" << location;
                EXPECT_FALSE(n > n) << str << " >" << location;
                EXPECT_TRUE(n >= n) << str << " >=" << location;
                EXPECT_TRUE(n <= n) << str << " <=" << location;
                // NOLINTEND(misc-redundant-expression)
            }
        }
    }
}

TEST(NumberTest, stream)
{
    for (auto const mantissaScale : MantissaRange::getAllScales())
    {
        NumberMantissaScaleGuard const sg(mantissaScale);

        Number const x{100};
        std::ostringstream os;
        os << x;
        EXPECT_EQ((os.str()), (to_string(x)));
    }
}

TEST(NumberTest, inc_dec)
{
    for (auto const mantissaScale : MantissaRange::getAllScales())
    {
        NumberMantissaScaleGuard const sg(mantissaScale);

        Number x{100};
        Number const y = +x;
        EXPECT_EQ((x), (y));
        EXPECT_EQ((x++), (y));
        EXPECT_EQ((x), (Number{101}));
        EXPECT_EQ((x--), (Number{101}));
        EXPECT_EQ((x), (y));
    }
}

TEST(NumberTest, to_st_amount)
{
    for (auto const mantissaScale : MantissaRange::getAllScales())
    {
        NumberMantissaScaleGuard const sg(mantissaScale);

        Issue const issue;
        Number const n{7'518'783'80596, -5};
        SaveNumberRoundMode const save{Number::setround(Number::RoundingMode::ToNearest)};
        auto res2 = STAmount{issue, n};
        EXPECT_EQ((res2), (STAmount{7518784}));

        Number::setround(Number::RoundingMode::TowardsZero);
        res2 = STAmount{issue, n};
        EXPECT_EQ((res2), (STAmount{7518783}));

        Number::setround(Number::RoundingMode::Downward);
        res2 = STAmount{issue, n};
        EXPECT_EQ((res2), (STAmount{7518783}));

        Number::setround(Number::RoundingMode::Upward);
        res2 = STAmount{issue, n};
        EXPECT_EQ((res2), (STAmount{7518784}));
    }
}

TEST(NumberTest, truncate)
{
    for (auto const mantissaScale : MantissaRange::getAllScales())
    {
        NumberMantissaScaleGuard const sg(mantissaScale);

        EXPECT_EQ((Number(25, +1).truncate()), (Number(250, 0)));
        EXPECT_EQ((Number(25, 0).truncate()), (Number(25, 0)));
        EXPECT_EQ((Number(25, -1).truncate()), (Number(2, 0)));
        EXPECT_EQ((Number(25, -2).truncate()), (Number(0, 0)));
        EXPECT_EQ((Number(99, -2).truncate()), (Number(0, 0)));

        EXPECT_EQ((Number(-25, +1).truncate()), (Number(-250, 0)));
        EXPECT_EQ((Number(-25, 0).truncate()), (Number(-25, 0)));
        EXPECT_EQ((Number(-25, -1).truncate()), (Number(-2, 0)));
        EXPECT_EQ((Number(-25, -2).truncate()), (Number(0, 0)));
        EXPECT_EQ((Number(-99, -2).truncate()), (Number(0, 0)));

        EXPECT_EQ((Number(0, 0).truncate()), (Number(0, 0)));
        EXPECT_EQ((Number(0, 30000).truncate()), (Number(0, 0)));
        EXPECT_EQ((Number(0, -30000).truncate()), (Number(0, 0)));
        EXPECT_EQ((Number(100, -30000).truncate()), (Number(0, 0)));
        EXPECT_EQ((Number(100, -30000).truncate()), (Number(0, 0)));
        EXPECT_EQ((Number(-100, -30000).truncate()), (Number(0, 0)));
        EXPECT_EQ((Number(-100, -30000).truncate()), (Number(0, 0)));
    }
}

TEST(NumberTest, rounding)
{
    for (auto const mantissaScale : MantissaRange::getAllScales())
    {
        NumberMantissaScaleGuard const sg(mantissaScale);

        // Test that rounding works as expected.

        using NumberRoundings = std::map<Number::RoundingMode, std::int64_t>;

        std::map<Number, NumberRoundings> const expected{
            // Positive numbers
            {Number{13, -1},
             {{Number::RoundingMode::ToNearest, 1},
              {Number::RoundingMode::TowardsZero, 1},
              {Number::RoundingMode::Downward, 1},
              {Number::RoundingMode::Upward, 2}}},
            {Number{23, -1},
             {{Number::RoundingMode::ToNearest, 2},
              {Number::RoundingMode::TowardsZero, 2},
              {Number::RoundingMode::Downward, 2},
              {Number::RoundingMode::Upward, 3}}},
            {Number{15, -1},
             {{Number::RoundingMode::ToNearest, 2},
              {Number::RoundingMode::TowardsZero, 1},
              {Number::RoundingMode::Downward, 1},
              {Number::RoundingMode::Upward, 2}}},
            {Number{25, -1},
             {{Number::RoundingMode::ToNearest, 2},
              {Number::RoundingMode::TowardsZero, 2},
              {Number::RoundingMode::Downward, 2},
              {Number::RoundingMode::Upward, 3}}},
            {Number{152, -2},
             {{Number::RoundingMode::ToNearest, 2},
              {Number::RoundingMode::TowardsZero, 1},
              {Number::RoundingMode::Downward, 1},
              {Number::RoundingMode::Upward, 2}}},
            {Number{252, -2},
             {{Number::RoundingMode::ToNearest, 3},
              {Number::RoundingMode::TowardsZero, 2},
              {Number::RoundingMode::Downward, 2},
              {Number::RoundingMode::Upward, 3}}},
            {Number{17, -1},
             {{Number::RoundingMode::ToNearest, 2},
              {Number::RoundingMode::TowardsZero, 1},
              {Number::RoundingMode::Downward, 1},
              {Number::RoundingMode::Upward, 2}}},
            {Number{27, -1},
             {{Number::RoundingMode::ToNearest, 3},
              {Number::RoundingMode::TowardsZero, 2},
              {Number::RoundingMode::Downward, 2},
              {Number::RoundingMode::Upward, 3}}},

            // Negative numbers
            {Number{-13, -1},
             {{Number::RoundingMode::ToNearest, -1},
              {Number::RoundingMode::TowardsZero, -1},
              {Number::RoundingMode::Downward, -2},
              {Number::RoundingMode::Upward, -1}}},
            {Number{-23, -1},
             {{Number::RoundingMode::ToNearest, -2},
              {Number::RoundingMode::TowardsZero, -2},
              {Number::RoundingMode::Downward, -3},
              {Number::RoundingMode::Upward, -2}}},
            {Number{-15, -1},
             {{Number::RoundingMode::ToNearest, -2},
              {Number::RoundingMode::TowardsZero, -1},
              {Number::RoundingMode::Downward, -2},
              {Number::RoundingMode::Upward, -1}}},
            {Number{-25, -1},
             {{Number::RoundingMode::ToNearest, -2},
              {Number::RoundingMode::TowardsZero, -2},
              {Number::RoundingMode::Downward, -3},
              {Number::RoundingMode::Upward, -2}}},
            {Number{-152, -2},
             {{Number::RoundingMode::ToNearest, -2},
              {Number::RoundingMode::TowardsZero, -1},
              {Number::RoundingMode::Downward, -2},
              {Number::RoundingMode::Upward, -1}}},
            {Number{-252, -2},
             {{Number::RoundingMode::ToNearest, -3},
              {Number::RoundingMode::TowardsZero, -2},
              {Number::RoundingMode::Downward, -3},
              {Number::RoundingMode::Upward, -2}}},
            {Number{-17, -1},
             {{Number::RoundingMode::ToNearest, -2},
              {Number::RoundingMode::TowardsZero, -1},
              {Number::RoundingMode::Downward, -2},
              {Number::RoundingMode::Upward, -1}}},
            {Number{-27, -1},
             {{Number::RoundingMode::ToNearest, -3},
              {Number::RoundingMode::TowardsZero, -2},
              {Number::RoundingMode::Downward, -3},
              {Number::RoundingMode::Upward, -2}}},
        };

        for (auto const& [num, roundings] : expected)
        {
            for (auto const& [mode, val] : roundings)
            {
                NumberRoundModeGuard const g{mode};
                auto const res = static_cast<std::int64_t>(num);
                EXPECT_EQ((res), (val)) << to_string(num) + " with mode " +
                        std::to_string(static_cast<int>(mode)) + " expected " +
                        std::to_string(val) + " got " + std::to_string(res);
            }
        }
    }
}

TEST(NumberTest, int64)
{
    for (auto const mantissaScale : MantissaRange::getAllScales())
    {
        NumberMantissaScaleGuard const sg(mantissaScale);

        auto const scale = Number::getMantissaScale();

        // Control case
        EXPECT_GT((Number::maxMantissa()), (10));
        Number const ten{10};
        EXPECT_LE((ten.exponent()), (0));

        if (scale == MantissaRange::MantissaScale::Small)
        {
            EXPECT_GT((std::numeric_limits<std::int64_t>::max()), (kInitialXrp.drops()));
            EXPECT_LT((Number::maxMantissa()), (kInitialXrp.drops()));
            Number const initalXrp{kInitialXrp};
            EXPECT_GT((initalXrp.exponent()), (0));

            Number const maxInt64{Number::kMaxRep};
            EXPECT_GT((maxInt64.exponent()), (0));
            // 85'070'591'730'234'615'865'843'651'857'942'052'864 - 38 digits
            EXPECT_EQ((power(maxInt64, 2)), (Number{85'070'591'730'234'62, 22}));

            Number const max = Number{false, Number::maxMantissa(), 0, Number::Normalized{}};
            EXPECT_LE(max.exponent(), 0);
            // 99'999'999'999'999'980'000'000'000'000'001 - 32 digits
            EXPECT_EQ(power(max, 2), (Number{99'999'999'999'999'98, 16}));
        }
        else
        {
            EXPECT_GT((std::numeric_limits<std::int64_t>::max()), (kInitialXrp.drops()));
            EXPECT_GT((Number::maxMantissa()), (kInitialXrp.drops()));
            Number const initalXrp{kInitialXrp};
            EXPECT_LE((initalXrp.exponent()), (0));

            Number const maxInt64{Number::kMaxRep};
            EXPECT_LE((maxInt64.exponent()), (0));
            // 85'070'591'730'234'615'847'396'907'784'232'501'249 - 38 digits
            EXPECT_EQ((power(maxInt64, 2)), (Number{85'070'591'730'234'615'85, 19}));

            NumberRoundModeGuard const mg(Number::RoundingMode::TowardsZero);

            auto const maxMantissa = Number::maxMantissa();
            Number const max = Number{false, maxMantissa, 0, Number::Normalized{}};
            EXPECT_EQ((max.mantissa()), (maxMantissa / 10));
            EXPECT_EQ((max.exponent()), (1));
            // 99'999'999'999'999'999'800'000'000'000'000'000'100 - also 38
            // digits
            EXPECT_EQ(
                (power(max, 2)), (Number{false, (maxMantissa / 10) - 1, 20, Number::Normalized{}}));
        }
    }
}

TEST(NumberTest, upward_rounding_produces_value_not_below_exact_at_k_max_rep_cusp)
{
    for (auto const mantissaScale : MantissaRange::getAllScales())
    {
        NumberMantissaScaleGuard const mg{mantissaScale};
        NumberRoundModeGuard const rg{Number::RoundingMode::Upward};

        auto const scale = Number::getMantissaScale();

        constexpr std::int64_t kAValue = 1'000'000'000'000'049'863LL;
        constexpr std::int64_t kBValue = 9'223'372'036'854'315'903LL;

        Number const a = kAValue;
        Number const b = kBValue;
        Number const product = a * b;

        // Exact reference in BigInt.
        BigInt const exactProduct = BigInt(kAValue) * BigInt(kBValue);

        // What Number actually stored.
        BigInt const storedValue = toBigInt(product);

        BigInt const signedDifference = storedValue - exactProduct;

        auto const message = [&] {
            std::ostringstream os;
            os << "  a              = " << fmt(BigInt(kAValue)) << "\n"
               << "  b              = " << fmt(BigInt(kBValue)) << "\n"
               << "  exact a*b      = " << fmt(exactProduct) << "\n"
               << "  stored         = " << fmt(storedValue) << "\n"
               << "  stored - exact = " << fmt(signedDifference) << "\n"
               << "  upward         = " << (signedDifference >= 0 ? "held" : "VIOLATED") << "\n"
               << " stored.mantissa = " << product.mantissa() << "\n"
               << " stored.exponent = " << product.exponent() << "\n\n";
            return os.str();
        };

        switch (scale)
        {
            case MantissaRange::MantissaScale::Large320:
            case MantissaRange::MantissaScale::Large330:
                EXPECT_TRUE(signedDifference >= 0) << message();
                EXPECT_TRUE(signedDifference < pow10<BigInt>(product.exponent())) << message();
                EXPECT_EQ(product.mantissa(), (std::numeric_limits<std::int64_t>::max() / 10) + 1);
                EXPECT_EQ(product.exponent(), 19);
                break;

            case MantissaRange::MantissaScale::LargeLegacy:
                EXPECT_TRUE(signedDifference < 0) << message();
                EXPECT_EQ(
                    product.mantissa(), (std::numeric_limits<std::int64_t>::max() / 100) * 100);
                EXPECT_EQ(product.exponent(), 18);
                break;

            case MantissaRange::MantissaScale::Small:
                // The seemingly weird rounding here is because a & b are both
                // normalized, and both round up when being converted to Number,
                // so you're really getting
                // 1_000_000_000_000_050 * 9_223_372_036_854_316.
                EXPECT_TRUE(signedDifference >= 0) << message();
                EXPECT_EQ(
                    product.mantissa(), (std::numeric_limits<std::int64_t>::max() / 1000) + 3);
                EXPECT_EQ(product.exponent(), 21);
                break;
        }
    }
}

/*
 * Companion regression for the kMaxRep cusp behavior, but for `operator/=` on
 * the cusp-fix-ENABLED `Large` scale.
 *
 * Before the dropped-remainder fix, `operator/=` with Upward rounding could
 * return a value STRICTLY LESS than the exact quotient, violating Upward's
 * directional invariant.
 *
 * Mechanism (fix-enabled path):
 *   1. `operator/=` computes `numerator = nm * 10^17` and
 *      `zm = numerator / dm` (integer division, truncates remainder).
 *   2. If `remainder != 0`, the correction block runs:
 *        zm *= 100000
 *        correction = (remainder * 100000) / dm  // also truncates
 *        zm += correction
 *        ze -= 5
 *      The truncation in `correction` discards a sub-1/100000 residual.
 *   3. `normalize`'s shift loop reduces zm to fit, but the discarded residual
 *      is BELOW the Guard's visibility, so the Guard sees fraction = 0.
 *   4. Under Upward + positive, `round()` returns -1 (no round-up), and the
 *      algorithm returns the truncated zm.
 */
TEST(NumberTest, upward_division_returns_value_not_below_exact_on_large_scale)
{
    for (auto const mantissaScale : MantissaRange::getAllScales())
    {
        NumberMantissaScaleGuard const mg{mantissaScale};
        NumberRoundModeGuard const rg{Number::RoundingMode::Upward};

        auto const scale = Number::getMantissaScale();

        constexpr std::int64_t kAValue = 2LL;
        constexpr std::int64_t kBValue = 1'000'000'000'000'000'007LL;
        // kBValue = 10^18 + 7 (prime, in [minMantissa, kMaxRep]).

        Number const a{kAValue, 0};
        Number const b{kBValue, 0};
        Number const quotient = a / b;

        Dec const exact = Dec(kAValue) / Dec(kBValue);
        Dec const stored = Dec(quotient.mantissa()) * pow10(quotient.exponent());
        Dec const diff = stored - exact;

        auto const message = [&] {
            std::ostringstream os;
            os << "  a                 = " << kAValue << "\n"
               << "  b                 = " << kBValue << "\n"
               << "  exact a/b         = " << fmt(exact) << "\n"
               << "  stored a/b        = " << fmt(stored) << "\n"
               << "  stored - exact    = " << fmt(diff)
               << "    (negative => Upward gave value BELOW truth)\n"
               << "  quotient.mantissa = " << quotient.mantissa() << "\n"
               << "  quotient.exponent = " << quotient.exponent() << "\n\n";
            return os.str();
        };

        // Upward invariant: stored >= exact. Bug: stored < exact.
        switch (scale)
        {
            case MantissaRange::MantissaScale::Large320:
            case MantissaRange::MantissaScale::Large330:
                EXPECT_TRUE(stored >= exact) << message();
                EXPECT_TRUE(diff < pow10(quotient.exponent())) << message();
                break;

            case MantissaRange::MantissaScale::LargeLegacy:
                EXPECT_TRUE(stored < exact) << message();
                EXPECT_TRUE(diff >= -pow10(quotient.exponent())) << message();
                break;

            case MantissaRange::MantissaScale::Small:
                // Small mantissa doesn't have the correction for dropped remainders.
                EXPECT_TRUE(stored < exact) << message();
                break;
        }
    }
}

// Companion test case for Upward positive operator/=: Downward negative.
TEST(NumberTest, downward_division_returns_value_not_above_exact_on_large_scale)
{
    for (auto const mantissaScale : MantissaRange::getAllScales())
    {
        NumberMantissaScaleGuard const mg{mantissaScale};
        NumberRoundModeGuard const rg{Number::RoundingMode::Downward};

        auto const scale = Number::getMantissaScale();

        constexpr std::int64_t kAValue = -2LL;
        constexpr std::int64_t kBValue = 1'000'000'000'000'000'007LL;
        // kBValue = 10^18 + 7 (prime, in [minMantissa, kMaxRep]).

        Number const a{kAValue, 0};
        Number const b{kBValue, 0};
        Number const quotient = a / b;

        Dec const exact = Dec(kAValue) / Dec(kBValue);
        Dec const stored = Dec(quotient.mantissa()) * pow10(quotient.exponent());
        Dec const diff = stored - exact;

        auto const message = [&] {
            std::ostringstream os;
            os << "  a                 = " << kAValue << "\n"
               << "  b                 = " << kBValue << "\n"
               << "  exact a/b         = " << fmt(exact) << "\n"
               << "  stored a/b        = " << fmt(stored) << "\n"
               << "  stored - exact    = " << fmt(diff)
               << "    (positive => Downward gave value ABOVE truth)\n"
               << "  quotient.mantissa = " << quotient.mantissa() << "\n"
               << "  quotient.exponent = " << quotient.exponent() << "\n\n";
            return os.str();
        };

        // invariant: stored <= exact. Bug: stored > exact.
        switch (scale)
        {
            case MantissaRange::MantissaScale::Large320:
            case MantissaRange::MantissaScale::Large330:
                EXPECT_TRUE(stored <= exact) << message();
                EXPECT_TRUE(diff > -pow10(quotient.exponent())) << message();
                break;

            case MantissaRange::MantissaScale::LargeLegacy:
                EXPECT_TRUE(stored > exact) << message();
                EXPECT_TRUE(diff <= pow10(quotient.exponent())) << message();
                break;

            case MantissaRange::MantissaScale::Small:
                // Small mantissa doesn't have the correction for dropped remainders.
                EXPECT_TRUE(stored < exact) << message();
                break;
        }
    }
}

/*
 * Companion test case for Upward positive operator/=: ToNearest.
 *
 * With ToNearest, if the dropped digits are exactly "5", then the mantissa will
 * be rounded to even. The numbers below result in a value where the unrounded
 * mantissa ends in an even digit, and "infinite precision" would drop
 * "500000000000000000145...", but doNormalize only sees "5". Without the
 * rounding fix, doNormalize rounds down to the even value. With the rounding
 * fix, doNormalize knows there are more digits beyond "5", and so rounds _up_
 * to the odd value.
 */
TEST(NumberTest, to_nearest_division_uses_dropped_digits_on_large_scale)
{
    for (auto const mantissaScale : MantissaRange::getAllScales())
    {
        NumberMantissaScaleGuard const mg{mantissaScale};
        NumberRoundModeGuard const rg{Number::RoundingMode::ToNearest};

        auto const scale = Number::getMantissaScale();

        constexpr std::int64_t kAValue = 1'269'917'268'816'087'809LL;
        constexpr std::int64_t kBValue = 3'458'525'013'821'685'511LL;
        // kBValue is prime and in [minMantissa, kMaxRep].

        Number const a{kAValue, 0};
        Number const b{kBValue, 0};
        Number const quotient = a / b;

        Dec const exact = Dec(kAValue) / Dec(kBValue);
        Dec const stored = Dec(quotient.mantissa()) * pow10(quotient.exponent());
        Dec const diff = stored - exact;

        auto const message = [&] {
            std::ostringstream os;
            os << "  a                 = " << kAValue << "\n"
               << "  b                 = " << kBValue << "\n"
               << "  exact a/b         = " << fmt(exact) << "\n"
               << "  stored a/b        = " << fmt(stored) << "\n"
               << "  stored - exact    = " << fmt(diff)
               << "    (negative => ToNearest gave value BELOW truth)\n"
               << "  quotient.mantissa = " << quotient.mantissa() << "\n"
               << "  quotient.exponent = " << quotient.exponent() << "\n\n";
            return os.str();
        };

        // invariant: stored >= exact. Bug: stored < exact.
        switch (scale)
        {
            case MantissaRange::MantissaScale::Large320:
            case MantissaRange::MantissaScale::Large330:
                EXPECT_TRUE(stored >= exact) << message();
                EXPECT_TRUE(diff < pow10(quotient.exponent())) << message();
                break;

            case MantissaRange::MantissaScale::LargeLegacy:
                EXPECT_TRUE(stored < exact) << message();
                EXPECT_TRUE(diff >= -pow10(quotient.exponent())) << message();
                break;

            case MantissaRange::MantissaScale::Small:
                // Small mantissa doesn't have the correction for dropped remainders.
                EXPECT_TRUE(stored < exact) << message();
                break;
        }
    }
}

TEST(NumberTest, subtraction_rounding)
{
    for (auto const mantissaScale : MantissaRange::getAllScales())
    {
        NumberMantissaScaleGuard const mg{mantissaScale};
        NumberRoundModeGuard const rg{Number::RoundingMode::ToNearest};

        auto const scale = Number::getMantissaScale();

        auto const exp = Number::mantissaLog();
        // SubCase is <offset, extraB, aString, bString>
        //  * offset: offset from exp
        //  * extraB: whether to include 1e"exp" in "b"
        //  * aString: expected string value for "a"
        //  * bString: expected string value for "b"
        // There aren't too many valid combinations for test cases here. If extraB is true,
        // offset can really only be 2, because any larger and the mantissa can't be represented
        // without loss. Offset can't be less than 2, or there's no error.
        using SubCase = std::tuple<int, bool, std::string, std::string>;
        auto const c = std::to_array<SubCase>({
            {2,
             true,
             scale == MantissaRange::MantissaScale::Small ? "100000000000000000"
                                                          : "100000000000000000000",
             scale == MantissaRange::MantissaScale::Small ? "-1000000000000001"
                                                          : "-1000000000000000001"},
            {2,
             false,
             scale == MantissaRange::MantissaScale::Small ? "100000000000000000"
                                                          : "100000000000000000000",
             "-1"},
            {30,
             false,
             scale == MantissaRange::MantissaScale::Small
                 ? "1000000000000000000000000000000000000000000000"
                 : "1000000000000000000000000000000000000000000000000",
             "-1"},
        });

        for (auto const& [offset, extraB, aString, bString] : c)
        {
            Number const a{1LL, exp + offset};
            Number const b{-((extraB ? Number{1, exp} : kNumZero) + 1)};

            auto const bigA = toBigInt(a);
            auto const bigB = toBigInt(b);

            EXPECT_EQ(bigA, BigInt{aString});
            EXPECT_EQ(bigB, BigInt{bString});

            auto construct = [&a, &b](Number::RoundingMode r) {
                NumberRoundModeGuard const roundGuard{r};
                auto const sum = a + b;
                BigInt const stored = toBigInt(sum);
                return std::make_pair(r, std::make_pair(stored, sum));
            };

            BigInt const exact = bigA + bigB;

            auto const sums = [&]() {
                std::map<Number::RoundingMode, std::pair<BigInt, Number>> r;
                r.emplace(construct(Number::RoundingMode::TowardsZero));
                r.emplace(construct(Number::RoundingMode::Upward));
                r.emplace(construct(Number::RoundingMode::Downward));
                r.emplace(construct(Number::RoundingMode::ToNearest));
                return r;
            }();

            auto const message = [&](auto const& r, auto const& sum) {
                std::ostringstream os;
                os << "              a = " << a << " (" << fmt(bigA) << ")\n              b = " << b
                   << " (" << fmt(bigB) << ")\n    exact a + b = " << fmt(exact) << "\n";

                auto const diff = sum.first - exact;
                auto const rLabel = to_string(r);
                os << std::string(15 - rLabel.length(), ' ') << rLabel << " = " << fmt(sum.first)
                   << "\n     difference = " << fmt(diff) << "\n\n";

                return os.str();
            };

            auto const expectedExponent =
                offset - (scale == MantissaRange::MantissaScale::Small && extraB ? 1 : 0);
            auto const epsilon = pow10<BigInt>(expectedExponent);
            for (auto const& [r, sum] : sums)
            {
                auto diff = sum.first - exact;
                switch (scale)
                {
                    case MantissaRange::MantissaScale::Small:
                    case MantissaRange::MantissaScale::LargeLegacy:
                    case MantissaRange::MantissaScale::Large320: {
                        // Without the fix, all the results but one round up
                        if (r == Number::RoundingMode::Downward)
                        {
                            // Downward works because the Guard sign is negative, and Downward
                            // returns Up instead of Down if negative and there's a remainder,
                            // whereas TowardsZero always returns Down.
                            EXPECT_LT(sum.first, exact) << message(r, sum);
                            EXPECT_EQ(diff, -(epsilon - 1)) << message(r, sum);
                        }
                        else
                        {
                            EXPECT_GT(sum.first, exact) << message(r, sum);
                            EXPECT_EQ(diff, 1) << message(r, sum);
                        }
                        break;
                    }
                    default: {
                        EXPECT_LE(sum.second.exponent(), expectedExponent) << message(r, sum);
                        switch (r)
                        {
                            case Number::RoundingMode::Upward:
                            case Number::RoundingMode::ToNearest:
                                EXPECT_GT(sum.first, exact) << message(r, sum);
                                EXPECT_EQ(diff, 1) << message(r, sum);
                                break;
                            default:
                                EXPECT_LT(sum.first, exact) << message(r, sum);
                                EXPECT_EQ(diff, -(epsilon - 1)) << message(r, sum);
                        }
                    }
                }
            }
        }
    }
}

TEST(NumberTest, normalization_cusp_tonearest_and_downward)
{
    for (auto const mantissaScale : MantissaRange::getAllScales())
    {
        NumberMantissaScaleGuard const mg{mantissaScale};
        NumberRoundModeGuard const rg{Number::RoundingMode::ToNearest};

        auto const scale = Number::getMantissaScale();

        constexpr auto kMaxRep = Number::kMaxRep;

        // Both ToNearest and Downward should round to `below`
        auto constexpr actual = static_cast<std::uint64_t>(kMaxRep) + 1;
        Number const below{static_cast<std::int64_t>(kMaxRep), 0};
        Number const above{false, static_cast<std::uint64_t>(kMaxRep) + 3, 0, Number::Normalized{}};

        auto construct = [](Number::RoundingMode mode) {
            NumberRoundModeGuard const roundGuard{mode};
            return Number(false, actual, 0, Number::Normalized{});
        };
        Number const upward = construct(Number::RoundingMode::Upward);

        Number const toNearest = construct(Number::RoundingMode::ToNearest);

        Number const downward = construct(Number::RoundingMode::Downward);

        auto message = [&] {
            std::ostringstream log;
            log << "  actual     = " << actual << "  (kMaxRep + 1)\n"
                << "  below      = " << below << "  (kMaxRep, distance 1)\n"
                << "  above      = " << above << "  (kMaxRep + 3, distance 2)\n"
                << "  Upward     = " << upward << "\n"
                << "  ToNearest  = " << toNearest << "\n"
                << "  Downward   = " << downward << "\n\n";
            return log.str();
        };

        switch (scale)
        {
            case MantissaRange::MantissaScale::Small:
                // With the small mantissa, everything but Downward rounds UP, including the
                // reference values, "above" and "below"

                EXPECT_EQ(below, above) << message();
                EXPECT_EQ(upward, above) << message();
                EXPECT_EQ(toNearest, above) << message();

                EXPECT_LT(downward, below) << message();

                break;

            case MantissaRange::MantissaScale::LargeLegacy:
            case MantissaRange::MantissaScale::Large320:
                // Upward round UP
                EXPECT_EQ(upward, above) << message();

                // ToNearest rounds UP when the DOWN neighbor is strictly closer
                EXPECT_EQ(toNearest, above) << message();
                EXPECT_GT(toNearest, below) << message();

                // Downward undershoots: it returns a value below `below`
                EXPECT_LT(downward, below) << message();

                // Both should have given the same answer, but they differ
                EXPECT_GT(toNearest, downward) << message();

                break;
            default:
                // Covers "Large" and any newly added scales

                // Upward round UP
                EXPECT_EQ(upward, above) << message();

                // ToNearest rounds to the strictly closer DOWN neighbor
                EXPECT_NE(toNearest, above) << message();
                EXPECT_EQ(toNearest, below) << message();

                // Downward also rounds to `below`
                EXPECT_EQ(downward, below) << message();

                // ToNearest rounds to downward
                EXPECT_EQ(toNearest, downward) << message();
                break;
        }
    }
}

TEST(NumberTest, number_add_directed_sign_wrong)
{
    for (auto const mantissaScale : MantissaRange::getAllScales())
    {
        NumberMantissaScaleGuard const mg{mantissaScale};
        NumberRoundModeGuard const rg{Number::RoundingMode::ToNearest};

        auto const scale = Number::getMantissaScale();
        {
            // Two negative numbers with the same exponent
            Number const a{-6, Number::mantissaLog()};
            Number const b{a - 3};
            EXPECT_TRUE(a.exponent() == b.exponent() && abs(b) > abs(a));

            BigInt const exact = toBigInt(a) + toBigInt(b);
            if (scale == MantissaRange::MantissaScale::Small)
            {
                EXPECT_EQ(exact, BigInt{"-12000000000000003"});
            }
            else
            {
                EXPECT_EQ(exact, BigInt{"-12000000000000000003"});
            }

            Number down, up;
            {
                NumberRoundModeGuard const g{Number::RoundingMode::Downward};
                down = a + b;
            }
            {
                NumberRoundModeGuard const g{Number::RoundingMode::Upward};
                up = a + b;
            }

            auto const valueDown = toBigInt(down);
            auto const valueUp = toBigInt(up);
            auto message = [&] {
                std::ostringstream log;
                log << "    exact    = " << fmt(exact) << "\n    downward = " << fmt(valueDown)
                    << "   (correct rounding: <= exact)"
                    << "\n    upward   = " << fmt(valueUp) << "   (correct rounding: >= exact)\n\n";
                return log.str();
            };

            if (scale == MantissaRange::MantissaScale::Large330)
            {
                EXPECT_LE(valueDown, exact) << message();  // Downward should round away from zero
                EXPECT_GE(valueUp, exact) << message();    // Upward should round toward 0
            }
            else
            {
                EXPECT_GT(valueDown, exact)
                    << message();                        // Downward rounded toward zero (too high)
                EXPECT_LT(valueUp, exact) << message();  // Upward rounded toward -inf (too low)
            }
        }

        {
            // Positive control: the same magnitudes with a positive result round
            Number const pa{6, Number::mantissaLog()};
            Number const pb{pa + 3};
            EXPECT_TRUE(pa.exponent() == pb.exponent() && abs(pb) > abs(pa));
            BigInt const pexact = toBigInt(pa) + toBigInt(pb);  // 12'000'000'000'000'000'003

            Number pdown, pup;
            {
                NumberRoundModeGuard const g{Number::RoundingMode::Downward};
                pdown = pa + pb;
            }
            {
                NumberRoundModeGuard const g{Number::RoundingMode::Upward};
                pup = pa + pb;
            }
            auto const valuePDown = toBigInt(pdown);
            auto const valuePUp = toBigInt(pup);
            auto message = [&] {
                std::ostringstream log;
                log << "    exact    = " << fmt(pexact) << "\n    downward = " << fmt(valuePDown)
                    << "   (correct rounding: <= exact)"
                    << "\n    upward   = " << fmt(valuePUp)
                    << "   (correct rounding: >= exact)\n\n";
                return log.str();
            };

            EXPECT_LE(valuePDown, pexact) << message();  // correct for positive results
            EXPECT_GE(valuePUp, pexact) << message();
        }

        {
            // Mixed sign numbers with the same exponent: negative second value
            Number const a{1, Number::mantissaLog()};
            Number const b{Number{-9, Number::mantissaLog()} - 3};
            EXPECT_TRUE(a.exponent() == b.exponent() && abs(b) > abs(a));

            BigInt const exact = toBigInt(a) + toBigInt(b);
            if (scale == MantissaRange::MantissaScale::Small)
            {
                EXPECT_EQ(exact, BigInt{"-8000000000000003"});
            }
            else
            {
                EXPECT_EQ(exact, BigInt{"-8000000000000000003"});
            }

            Number down, up;
            {
                NumberRoundModeGuard const g{Number::RoundingMode::Downward};
                down = a + b;
            }
            {
                NumberRoundModeGuard const g{Number::RoundingMode::Upward};
                up = a + b;
            }

            auto const valueDown = toBigInt(down);
            auto const valueUp = toBigInt(up);
            auto message = [&] {
                std::ostringstream log;
                log << "    exact    = " << fmt(exact) << "\n    downward = " << fmt(valueDown)
                    << "   (correct rounding: <= exact)"
                    << "\n    upward   = " << fmt(valueUp) << "   (correct rounding: >= exact)\n\n";
                return log.str();
            };

            EXPECT_LE(valueDown, exact) << message();  // Downward should round away from zero
            EXPECT_GE(valueUp, exact) << message();    // Upward should round toward 0
        }

        {
            // Mixed sign numbers with the same exponent: negative first value
            Number const a{-1, Number::mantissaLog()};
            Number const b{Number{9, Number::mantissaLog()} + 3};
            EXPECT_TRUE(a.exponent() == b.exponent() && abs(b) > abs(a));

            BigInt const exact = toBigInt(a) + toBigInt(b);
            if (scale == MantissaRange::MantissaScale::Small)
            {
                EXPECT_EQ(exact, BigInt{"8000000000000003"});
            }
            else
            {
                EXPECT_EQ(exact, BigInt{"8000000000000000003"});
            }

            Number down, up;
            {
                NumberRoundModeGuard const g{Number::RoundingMode::Downward};
                down = a + b;
            }
            {
                NumberRoundModeGuard const g{Number::RoundingMode::Upward};
                up = a + b;
            }

            auto const valueDown = toBigInt(down);
            auto const valueUp = toBigInt(up);
            auto message = [&] {
                std::ostringstream log;
                log << "    exact    = " << fmt(exact) << "\n    downward = " << fmt(valueDown)
                    << "   (correct rounding: <= exact)"
                    << "\n    upward   = " << fmt(valueUp) << "   (correct rounding: >= exact)\n\n";
                return log.str();
            };

            EXPECT_LE(valueDown, exact) << message();  // Downward should round away from zero
            EXPECT_GE(valueUp, exact) << message();    // Upward should round toward 0
        }
    }
}

TEST(NumberTest, number_add_to_nearest_picks_farther)
{
    for (auto const mantissaScale : MantissaRange::getAllScales())
    {
        NumberMantissaScaleGuard const mg{mantissaScale};
        NumberRoundModeGuard const rg{Number::RoundingMode::ToNearest};

        auto const scale = Number::getMantissaScale();

        // Case is <y, expected q>
        using Case = std::pair<Number, std::int64_t>;

        auto const c = std::to_array<Case>({
            {Number{5'175'909'259'972'499'745LL, 22}, -1'074'951'375'311'646'003},
            {Number{1}, -1'074'956'551'220'905'975},
            {Number{1, 10}, -1'074'956'551'220'905'975},
            {Number{1, 20}, -1'074'956'551'220'905'975},
            {Number{1, 27}, -1'074'956'551'220'905'975},
            {Number{1, 28}, -1'074'956'551'220'905'974},
            {Number{1, 31}, -1'074'956'551'220'904'975},
        });

        for (auto const& [y, expectedQ] : c)
        {
            Number const x{-1'074'956'551'220'905'975LL, 28};
            Number const res = x + y;

            BigInt const exact = toBigInt(x) + toBigInt(y);
            BigInt const vres = toBigInt(res);

            BigInt ulp = 1;
            for (int i = 0; i < res.exponent(); ++i)
                ulp *= 10;

            BigInt const q = (exact - ulp / 2) / ulp;
            Number const normalizedExact{static_cast<std::int64_t>(q), res.exponent()};
            BigInt const norm = toBigInt(normalizedExact);

            auto message = [&](auto const& comp) {
                std::ostringstream log;
                log << fmt(q) + " != " + fmt(comp) << "\n"
                    << "    x                = " << x << "\n    y                = " << y
                    << "\n    exact            = " << fmt(exact)
                    << "\n    result (x + y)   = " << fmt(vres)
                    << "\n    normalize(exact) = " << fmt(norm) << "\n\n";
                return log.str();
            };

            if (scale == MantissaRange::MantissaScale::Small)
            {
                auto const comp = toBigInt(Number{expectedQ, -3});
                EXPECT_EQ(q, comp) << message(comp);
            }
            else
            {
                EXPECT_EQ(q, expectedQ) << message(BigInt(expectedQ));
            }
            EXPECT_EQ(normalizedExact, res);
        }
    }
}

TEST(NumberTest, number_cusp_rounding_with_fractional_parts)
{
    for (auto const mantissaScale : MantissaRange::getAllScales())
    {
        NumberMantissaScaleGuard const mg{mantissaScale};

        auto const scale = Number::getMantissaScale();

        Number const below{static_cast<std::int64_t>(Number::kMaxRep), 0};
        Number const above{false, Number::kMaxRepUp, 0, Number::Normalized{}};

        auto header = [&] {
            std::ostringstream log;
            log << "Scale: " << to_string(mantissaScale) << ", Below: " << below
                << ", Above: " << above << "\n";
            return log.str();
        };

        auto const zeroPointFour = Number(4, -1);
        auto const zeroPointFive = Number(5, -1);
        auto const zeroPointSix = Number(6, -1);
        auto const onePointFour = Number(14, -1);
        auto const onePointFive = Number(15, -1);
        auto const onePointSix = Number(16, -1);
        auto const twoPointFour = Number(24, -1);
        auto const twoPointFive = Number(25, -1);
        auto const twoPointSix = Number(26, -1);

        auto const operands = std::to_array<Number>({
            zeroPointFour,
            zeroPointFive,
            zeroPointSix,
            onePointFour,
            onePointFive,
            onePointSix,
            twoPointFour,
            twoPointFive,
            twoPointSix,
        });

        auto const modes = std::to_array<Number::RoundingMode>({
            Number::RoundingMode::ToNearest,
            Number::RoundingMode::TowardsZero,
            Number::RoundingMode::Downward,
            Number::RoundingMode::Upward,
        });

        // Addition cases test kMaxRep + Operand
        for (auto const& mode : modes)
        {
            for (auto const& operand : operands)
            {
                NumberRoundModeGuard const rg{mode};

                auto const expectedValue = [&]() {
                    // Returns "above" by default. The checks here are for exceptions.
                    if (scale >= MantissaRange::MantissaScale::Large330)
                    {
                        if (mode == Number::RoundingMode::ToNearest && operand < onePointFive)
                            return below;
                        if (mode == Number::RoundingMode::TowardsZero ||
                            mode == Number::RoundingMode::Downward)
                            return below;
                    }
                    if (scale == MantissaRange::MantissaScale::Large320)
                    {
                        if (mode == Number::RoundingMode::ToNearest)
                        {
                            if (operand < zeroPointFive)
                                return below;
                        }
                        if (mode == Number::RoundingMode::TowardsZero ||
                            mode == Number::RoundingMode::Downward)
                        {
                            if (operand >= onePointFour)
                                return below - 7;
                            return below;
                        }
                    }
                    if (scale == MantissaRange::MantissaScale::LargeLegacy)
                    {
                        if (mode == Number::RoundingMode::ToNearest)
                        {
                            if (operand < zeroPointFive)
                                return below;
                            if (operand <= zeroPointSix)
                                return below - 7;
                        }
                        if (mode == Number::RoundingMode::TowardsZero ||
                            mode == Number::RoundingMode::Downward)
                        {
                            if (operand >= onePointFour)
                                return below - 7;
                            return below;
                        }
                        if (mode == Number::RoundingMode::Upward && operand <= zeroPointSix)
                            return below - 7;
                    }
                    if (scale == MantissaRange::MantissaScale::Small &&
                        mode == Number::RoundingMode::Upward)
                        return above + 1000;
                    return above;
                }();

                Number const actual = below + operand;

                auto message = [&] {
                    std::stringstream ss;
                    ss << header() << "kMaxRep + " << operand << " rounded " << to_string(mode)
                       << " to " << actual << ". Expected: " << expectedValue;
                    return ss.str();
                };
                EXPECT_EQ(actual, expectedValue) << message();
            }
        }

        // Subtraction cases test kMaxRepUp - Operand
        for (auto const& mode : modes)
        {
            for (auto const& operand : operands)
            {
                NumberRoundModeGuard const rg{mode};

                auto const expectedValue = [&]() {
                    if (scale >= MantissaRange::MantissaScale::Large330)
                    {
                        if (mode == Number::RoundingMode::ToNearest && operand > onePointFive)
                            return below;
                        if (mode == Number::RoundingMode::TowardsZero ||
                            mode == Number::RoundingMode::Downward)
                            return below;
                    }
                    if (scale == MantissaRange::MantissaScale::LargeLegacy ||
                        scale == MantissaRange::MantissaScale::Large320)
                    {
                        if (mode == Number::RoundingMode::ToNearest)
                        {
                            if (operand >= twoPointSix)
                                return below;
                        }
                        if (mode == Number::RoundingMode::TowardsZero)
                        {
                            if (operand >= onePointFour)
                                return below - 7;
                        }
                        if (mode == Number::RoundingMode::Downward)
                        {
                            if (operand <= onePointSix)
                                return below - 7;
                            return below;
                        }
                    }
                    if (scale == MantissaRange::MantissaScale::Small)
                    {
                        if (mode == Number::RoundingMode::Downward)
                            return below - 1000;
                        if (mode == Number::RoundingMode::Upward)
                            return below;
                    }
                    return above;
                }();

                Number const actual = above - operand;

                auto message = [&] {
                    std::stringstream ss;
                    ss << header() << "kMaxRepUp - " << operand << " rounded " << to_string(mode)
                       << " to " << actual << ". Expected: " << expectedValue;
                    return ss.str();
                };
                EXPECT_EQ(actual, expectedValue) << message();
            }
        }
    }
}

}  // namespace xrpl
