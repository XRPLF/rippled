#include <xrpl/basics/Number.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/IOUAmount.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/SystemParameters.h>
#include <xrpl/protocol/XRPAmount.h>

#include <boost/multiprecision/number.hpp>

#include <array>
#include <cctype>
#include <cstdint>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>

namespace xrpl {

class Number_test : public beast::unit_test::Suite
{
    using BigInt = boost::multiprecision::cpp_int;

    static std::string
    fmt(BigInt const& value)
    {
        auto s = to_string(value);
        std::string out;
        int count = 0;
        for (auto it = s.rbegin(); it != s.rend(); ++it)
        {
            if (count != 0 && count % 3 == 0 && (isdigit(*it) != 0))
                out.insert(out.begin(), '_');
            out.insert(out.begin(), *it);
            ++count;
        }
        return out;
    }

public:
    void
    testZero()
    {
        testcase << "zero " << to_string(Number::getMantissaScale());

        for (Number const& z : {Number{0, 0}, Number{0}})
        {
            BEAST_EXPECT(z.mantissa() == 0);
            BEAST_EXPECT(z.exponent() == Number{}.exponent());

            BEAST_EXPECT((z + z) == z);
            BEAST_EXPECT((z - z) == z);
            BEAST_EXPECT(z == -z);
        }
    }

    void
    testLimits()
    {
        auto const scale = Number::getMantissaScale();
        testcase << "test_limits " << to_string(scale);
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
        BEAST_EXPECT(caught);

        auto test = [this](auto const& x, auto const& y, int line) {
            auto const result = x == y;
            std::stringstream ss;
            ss << x << " == " << y << " -> " << (result ? "true" : "false");
            expect(result, ss.str(), __FILE__, line);
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
        BEAST_EXPECT(caught);
    }

    void
    testAdd()
    {
        auto const scale = Number::getMantissaScale();
        testcase << "test_add " << to_string(scale);

        using Case = std::tuple<Number, Number, Number>;
        auto const cSmall = std::to_array<Case>(
            {{Number{1'000'000'000'000'000, -15},
              Number{6'555'555'555'555'555, -29},
              Number{1'000'000'000'000'066, -15}},
             {Number{-1'000'000'000'000'000, -15},
              Number{-6'555'555'555'555'555, -29},
              Number{-1'000'000'000'000'066, -15}},
             {Number{-1'000'000'000'000'000, -15},
              Number{6'555'555'555'555'555, -29},
              Number{-9'999'999'999'999'344, -16}},
             {Number{-6'555'555'555'555'555, -29},
              Number{1'000'000'000'000'000, -15},
              Number{9'999'999'999'999'344, -16}},
             {Number{}, Number{5}, Number{5}},
             {Number{5}, Number{}, Number{5}},
             {Number{5'555'555'555'555'555, -32768},
              Number{-5'555'555'555'555'554, -32768},
              Number{0}},
             {Number{-9'999'999'999'999'999, -31},
              Number{1'000'000'000'000'000, -15},
              Number{9'999'999'999'999'990, -16}}});
        auto const cLarge = std::to_array<Case>(
            // Note that items with extremely large mantissas need to be
            // calculated, because otherwise they overflow uint64. Items from C
            // with larger mantissa
            {
                {Number{1'000'000'000'000'000, -15},
                 Number{6'555'555'555'555'555, -29},
                 Number{1'000'000'000'000'065'556, -18}},
                {Number{-1'000'000'000'000'000, -15},
                 Number{-6'555'555'555'555'555, -29},
                 Number{-1'000'000'000'000'065'556, -18}},
                {Number{-1'000'000'000'000'000, -15},
                 Number{6'555'555'555'555'555, -29},
                 Number{true, 9'999'999'999'999'344'444ULL, -19, Number::Normalized{}}},
                {Number{-6'555'555'555'555'555, -29},
                 Number{1'000'000'000'000'000, -15},
                 Number{false, 9'999'999'999'999'344'444ULL, -19, Number::Normalized{}}},
                {Number{}, Number{5}, Number{5}},
                {Number{5}, Number{}, Number{5}},
                {Number{5'555'555'555'555'555'000, -32768},
                 Number{-5'555'555'555'555'554'000, -32768},
                 Number{0}},
                {Number{-9'999'999'999'999'999, -31},
                 Number{1'000'000'000'000'000, -15},
                 Number{9'999'999'999'999'990, -16}},
                // Items from cSmall expanded for the larger mantissa
                {Number{1'000'000'000'000'000'000, -18},
                 Number{6'555'555'555'555'555'555, -35},
                 Number{1'000'000'000'000'000'066, -18}},
                {Number{-1'000'000'000'000'000'000, -18},
                 Number{-6'555'555'555'555'555'555, -35},
                 Number{-1'000'000'000'000'000'066, -18}},
                {Number{-1'000'000'000'000'000'000, -18},
                 Number{6'555'555'555'555'555'555, -35},
                 Number{true, 9'999'999'999'999'999'344ULL, -19, Number::Normalized{}}},
                {Number{-6'555'555'555'555'555'555, -35},
                 Number{1'000'000'000'000'000'000, -18},
                 Number{false, 9'999'999'999'999'999'344ULL, -19, Number::Normalized{}}},
                {Number{}, Number{5}, Number{5}},
                {Number{5'555'555'555'555'555'555, -32768},
                 Number{-5'555'555'555'555'555'554, -32768},
                 Number{0}},
                {Number{true, 9'999'999'999'999'999'999ULL, -37, Number::Normalized{}},
                 Number{1'000'000'000'000'000'000, -18},
                 Number{false, 9'999'999'999'999'999'990ULL, -19, Number::Normalized{}}},
                {Number{Number::kMaxRep - 1}, Number{1, 0}, Number{Number::kMaxRep}},
                // Test extremes
                {
                    // Each Number operand rounds up, so the actual mantissa is
                    // minMantissa
                    Number{false, 9'999'999'999'999'999'999ULL, 0, Number::Normalized{}},
                    Number{false, 9'999'999'999'999'999'999ULL, 0, Number::Normalized{}},
                    Number{2, 19},
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
                },
            });
        auto const cLargeLegacy = std::to_array<Case>({
            {Number{Number::kMaxRep}, Number{6, -1}, Number{Number::kMaxRep / 10, 1}},
        });
        auto const cLargeCorrected = std::to_array<Case>({
            {Number{Number::kMaxRep}, Number{6, -1}, Number{(Number::kMaxRep / 10) + 1, 1}},
        });
        auto test = [this](auto const& c) {
            for (auto const& [x, y, z] : c)
            {
                auto const result = x + y;
                std::stringstream ss;
                ss << x << " + " << y << " = " << result << ". Expected: " << z;
                BEAST_EXPECTS(result == z, ss.str());
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
            else
            {
                test(cLargeCorrected);
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
            BEAST_EXPECT(caught);
        }
    }

    void
    testSub()
    {
        auto const scale = Number::getMantissaScale();
        testcase << "test_sub " << to_string(scale);

        using Case = std::tuple<Number, Number, Number>;
        auto const cSmall = std::to_array<Case>(
            {{Number{1'000'000'000'000'000, -15},
              Number{6'555'555'555'555'555, -29},
              Number{9'999'999'999'999'344, -16}},
             {Number{6'555'555'555'555'555, -29},
              Number{1'000'000'000'000'000, -15},
              Number{-9'999'999'999'999'344, -16}},
             {Number{1'000'000'000'000'000, -15}, Number{1'000'000'000'000'000, -15}, Number{0}},
             {Number{1'000'000'000'000'000, -15},
              Number{1'000'000'000'000'001, -15},
              Number{-1'000'000'000'000'000, -30}},
             {Number{1'000'000'000'000'001, -15},
              Number{1'000'000'000'000'000, -15},
              Number{1'000'000'000'000'000, -30}}});
        auto const cLarge = std::to_array<Case>(
            // Note that items with extremely large mantissas need to be
            // calculated, because otherwise they overflow uint64. Items from C
            // with larger mantissa
            {
                {Number{1'000'000'000'000'000, -15},
                 Number{6'555'555'555'555'555, -29},
                 Number{false, 9'999'999'999'999'344'444ULL, -19, Number::Normalized{}}},
                {Number{6'555'555'555'555'555, -29},
                 Number{1'000'000'000'000'000, -15},
                 Number{true, 9'999'999'999'999'344'444ULL, -19, Number::Normalized{}}},
                {Number{1'000'000'000'000'000, -15}, Number{1'000'000'000'000'000, -15}, Number{0}},
                {Number{1'000'000'000'000'000, -15},
                 Number{1'000'000'000'000'001, -15},
                 Number{-1'000'000'000'000'000, -30}},
                {Number{1'000'000'000'000'001, -15},
                 Number{1'000'000'000'000'000, -15},
                 Number{1'000'000'000'000'000, -30}},
                // Items from cSmall expanded for the larger mantissa
                {Number{1'000'000'000'000'000'000, -18},
                 Number{6'555'555'555'555'555'555, -32},
                 Number{false, 9'999'999'999'999'344'444ULL, -19, Number::Normalized{}}},
                {Number{6'555'555'555'555'555'555, -32},
                 Number{1'000'000'000'000'000'000, -18},
                 Number{true, 9'999'999'999'999'344'444ULL, -19, Number::Normalized{}}},
                {Number{1'000'000'000'000'000'000, -18},
                 Number{1'000'000'000'000'000'000, -18},
                 Number{0}},
                {Number{1'000'000'000'000'000'000, -18},
                 Number{1'000'000'000'000'000'001, -18},
                 Number{-1'000'000'000'000'000'000, -36}},
                {Number{1'000'000'000'000'000'001, -18},
                 Number{1'000'000'000'000'000'000, -18},
                 Number{1'000'000'000'000'000'000, -36}},
                {Number{Number::kMaxRep}, Number{6, -1}, Number{Number::kMaxRep - 1}},
                {Number{false, Number::kMaxRep + 1, 0, Number::Normalized{}},
                 Number{1, 0},
                 Number{(Number::kMaxRep / 10) + 1, 1}},
                {Number{false, Number::kMaxRep + 1, 0, Number::Normalized{}},
                 Number{3, 0},
                 Number{Number::kMaxRep}},
                {power(2, 63), Number{3, 0}, Number{Number::kMaxRep}},
            });
        auto test = [this](auto const& c) {
            for (auto const& [x, y, z] : c)
            {
                auto const result = x - y;
                std::stringstream ss;
                ss << x << " - " << y << " = " << result << ". Expected: " << z;
                BEAST_EXPECTS(result == z, ss.str());
            }
        };
        if (scale == MantissaRange::MantissaScale::Small)
        {
            test(cSmall);
        }
        else
        {
            test(cLarge);
        }
    }

    void
    testMul()
    {
        auto const scale = Number::getMantissaScale();
        testcase << "test_mul " << to_string(scale);

        using Case = std::tuple<Number, Number, Number>;
        auto test = [this](auto const& c) {
            for (auto const& [x, y, z] : c)
            {
                auto const result = x * y;
                std::stringstream ss;
                ss << x << " * " << y << " = " << result << ". Expected: " << z;
                BEAST_EXPECTS(result == z, ss.str());
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
        testcase << "test_mul " << to_string(Number::getMantissaScale()) << " towards_zero";
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
        testcase << "test_mul " << to_string(Number::getMantissaScale()) << " downward";
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
        testcase << "test_mul " << to_string(Number::getMantissaScale()) << " upward";
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
        testcase << "test_mul " << to_string(Number::getMantissaScale()) << " overflow";
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
            BEAST_EXPECT(caught);
        }
    }

    void
    testDiv()
    {
        auto const scale = Number::getMantissaScale();
        testcase << "test_div " << to_string(scale);

        using Case = std::tuple<Number, Number, Number>;
        auto test = [this](auto const& c) {
            for (auto const& [x, y, z] : c)
            {
                auto const result = x / y;
                std::stringstream ss;
                ss << x << " / " << y << " = " << result << ". Expected: " << z;
                BEAST_EXPECTS(result == z, ss.str());
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
        testcase << "test_div " << to_string(Number::getMantissaScale()) << " towards_zero";
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
        testcase << "test_div " << to_string(Number::getMantissaScale()) << " downward";
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
        testcase << "test_div " << to_string(Number::getMantissaScale()) << " upward";
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
        testcase << "test_div " << to_string(Number::getMantissaScale()) << " overflow";
        bool caught = false;
        try
        {
            Number{1000000000000000, -15} / Number{0};
        }
        catch (std::overflow_error const&)
        {
            caught = true;
        }
        BEAST_EXPECT(caught);
    }

    void
    testRoot()
    {
        auto const scale = Number::getMantissaScale();
        testcase << "test_root " << to_string(scale);

        using Case = std::tuple<Number, unsigned, Number>;
        auto test = [this](auto const& c) {
            for (auto const& [x, y, z] : c)
            {
                auto const result = root(x, y);
                std::stringstream ss;
                ss << "root(" << x << ", " << y << ") = " << result << ". Expected: " << z;
                BEAST_EXPECTS(result == z, ss.str());
            }
        };
        /*
        auto tests = [&](auto const& cSmall, auto const& cLarge) {
            test(cSmall);
            if (scale != MantissaRange::MantissaScale::Small)
                test(cLarge);
        };
        */

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
        BEAST_EXPECT(caught);
        caught = false;
        try
        {
            (void)root(Number{-2}, 4);
        }
        catch (std::overflow_error const&)
        {
            caught = true;
        }
        BEAST_EXPECT(caught);
    }

    void
    testRoot2()
    {
        auto const scale = Number::getMantissaScale();
        testcase << "test_root2 " << to_string(scale);

        auto test = [this](auto const& c) {
            for (auto const& x : c)
            {
                auto const expected = root(x, 2);
                auto const result = root2(x);
                std::stringstream ss;
                ss << "root2(" << x << ") = " << result << ". Expected: " << expected;
                BEAST_EXPECTS(result == expected, ss.str());
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
        BEAST_EXPECT(caught);
    }

    void
    testPower1()
    {
        testcase << "test_power1 " << to_string(Number::getMantissaScale());
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
            BEAST_EXPECT((power(x, y) == z));
    }

    void
    testPower2()
    {
        testcase << "test_power2 " << to_string(Number::getMantissaScale());
        using Case = std::tuple<Number, unsigned, unsigned, Number>;
        Case const c[]{
            {Number{1}, 3, 7, Number{1}},
            {Number{-1}, 1, 0, Number{1}},
            {Number{-1, -1}, 1, 0, Number{0}},
            {Number{16}, 0, 5, Number{1}},
            {Number{34}, 3, 3, Number{34}},
            {Number{4}, 3, 2, Number{8}}};
        for (auto const& [x, n, d, z] : c)
            BEAST_EXPECT((power(x, n, d) == z));
        bool caught = false;
        try
        {
            (void)power(Number{7}, 0, 0);
        }
        catch (std::overflow_error const&)
        {
            caught = true;
        }
        BEAST_EXPECT(caught);
        caught = false;
        try
        {
            (void)power(Number{7}, 1, 0);
        }
        catch (std::overflow_error const&)
        {
            caught = true;
        }
        BEAST_EXPECT(caught);
        caught = false;
        try
        {
            (void)power(Number{-1, -1}, 3, 2);
        }
        catch (std::overflow_error const&)
        {
            caught = true;
        }
        BEAST_EXPECT(caught);
    }

    void
    testConversions()
    {
        testcase << "testConversions " << to_string(Number::getMantissaScale());

        IOUAmount const x{5, 6};
        Number const y = x;
        BEAST_EXPECT((y == Number{5, 6}));
        IOUAmount const z{y};
        BEAST_EXPECT(x == z);
        XRPAmount const xrp{500};
        STAmount const st = xrp;
        Number const n = st;
        BEAST_EXPECT(XRPAmount{n} == xrp);
        IOUAmount const x0{0, 0};
        Number const y0 = x0;
        BEAST_EXPECT((y0 == Number{0}));
        IOUAmount const z0{y0};
        BEAST_EXPECT(x0 == z0);
        XRPAmount const xrp0{0};
        Number const n0 = xrp0;
        BEAST_EXPECT(n0 == Number{0});
        XRPAmount const xrp1{n0};
        BEAST_EXPECT(xrp1 == xrp0);
    }

    void
    testToInteger()
    {
        testcase << "test_to_integer " << to_string(Number::getMantissaScale());
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
                BEAST_EXPECT(j == y);
            }
        }
        auto prevMode = Number::setround(Number::RoundingMode::TowardsZero);
        BEAST_EXPECT(prevMode == Number::RoundingMode::ToNearest);
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
                BEAST_EXPECT(j == y);
            }
        }
        prevMode = Number::setround(Number::RoundingMode::Downward);
        BEAST_EXPECT(prevMode == Number::RoundingMode::TowardsZero);
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
                BEAST_EXPECT(j == y);
            }
        }
        prevMode = Number::setround(Number::RoundingMode::Upward);
        BEAST_EXPECT(prevMode == Number::RoundingMode::Downward);
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
                BEAST_EXPECT(j == y);
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
        BEAST_EXPECT(caught);
    }

    void
    testSquelch()
    {
        testcase << "test_squelch " << to_string(Number::getMantissaScale());
        Number const limit{1, -6};
        BEAST_EXPECT((squelch(Number{2, -6}, limit) == Number{2, -6}));
        BEAST_EXPECT((squelch(Number{1, -6}, limit) == Number{1, -6}));
        BEAST_EXPECT((squelch(Number{9, -7}, limit) == Number{0}));
        BEAST_EXPECT((squelch(Number{-2, -6}, limit) == Number{-2, -6}));
        BEAST_EXPECT((squelch(Number{-1, -6}, limit) == Number{-1, -6}));
        BEAST_EXPECT((squelch(Number{-9, -7}, limit) == Number{0}));
    }

    void
    testToString()
    {
        auto const scale = Number::getMantissaScale();
        testcase << "testToString " << to_string(scale);

        auto test = [this](Number const& n, std::string const& expected) {
            auto const result = to_string(n);
            std::stringstream ss;
            ss << "to_string(" << result << "). Expected: " << expected;
            BEAST_EXPECTS(result == expected, ss.str());
        };

        test(Number(-2, 0), "-2");
        test(Number(0, 0), "0");
        test(Number(2, 0), "2");
        test(Number(25, -3), "0.025");
        test(Number(-25, -3), "-0.025");
        test(Number(25, 1), "250");
        test(Number(-25, 1), "-250");
        test(Number(2, 20), "2e20");
        test(Number(-2, -20), "-2e-20");
        // Test the edges
        // ((exponent < -(25)) || (exponent > -(5)))))
        // or ((exponent < -(28)) || (exponent > -(8)))))
        test(Number(2, -10), "0.0000000002");
        test(Number(2, -11), "2e-11");

        test(Number(-2, 10), "-20000000000");
        test(Number(-2, 11), "-2e11");

        switch (scale)
        {
            case MantissaRange::MantissaScale::Small:

                test(Number::min(), "1e-32753");
                test(Number::max(), "9999999999999999e32768");
                test(Number::lowest(), "-9999999999999999e32768");
                {
                    NumberRoundModeGuard const mg(Number::RoundingMode::TowardsZero);

                    auto const maxMantissa = Number::maxMantissa();
                    BEAST_EXPECT(maxMantissa == 9'999'999'999'999'999);
                    test(
                        Number{false, (maxMantissa * 1000) + 999, -3, Number::Normalized()},
                        "9999999999999999");
                    test(
                        Number{true, (maxMantissa * 1000) + 999, -3, Number::Normalized()},
                        "-9999999999999999");

                    test(Number{std::numeric_limits<std::int64_t>::max(), -3}, "9223372036854775");
                    test(
                        -(Number{std::numeric_limits<std::int64_t>::max(), -3}),
                        "-9223372036854775");

                    test(
                        Number{std::numeric_limits<std::int64_t>::min(), 0}, "-9223372036854775e3");
                    test(
                        -(Number{std::numeric_limits<std::int64_t>::min(), 0}),
                        "9223372036854775e3");
                }
                break;
            case MantissaRange::MantissaScale::LargeLegacy:
            case MantissaRange::MantissaScale::Large:
                // Test the edges
                // ((exponent < -(28)) || (exponent > -(8)))))
                test(Number::min(), "1e-32750");
                test(Number::max(), "9223372036854775807e32768");
                test(Number::lowest(), "-9223372036854775807e32768");
                {
                    NumberRoundModeGuard const mg(Number::RoundingMode::TowardsZero);

                    auto const maxMantissa = Number::maxMantissa();
                    BEAST_EXPECT(maxMantissa == 9'999'999'999'999'999'999ULL);
                    test(
                        Number{false, maxMantissa, 0, Number::Normalized{}}, "9999999999999999990");
                    test(
                        Number{true, maxMantissa, 0, Number::Normalized{}}, "-9999999999999999990");

                    test(
                        Number{std::numeric_limits<std::int64_t>::max(), 0}, "9223372036854775807");
                    test(
                        -(Number{std::numeric_limits<std::int64_t>::max(), 0}),
                        "-9223372036854775807");

                    // Because the absolute value of min is larger than max, it
                    // will be scaled down to fit under max. Since we're
                    // rounding towards zero, the 8 at the end is dropped.
                    test(
                        Number{std::numeric_limits<std::int64_t>::min(), 0},
                        "-9223372036854775800");
                    test(
                        -(Number{std::numeric_limits<std::int64_t>::min(), 0}),
                        "9223372036854775800");
                }

                test(
                    Number{std::numeric_limits<std::int64_t>::max(), 0} + 1, "9223372036854775810");
                test(
                    -(Number{std::numeric_limits<std::int64_t>::max(), 0} + 1),
                    "-9223372036854775810");
                break;
            default:
                BEAST_EXPECT(false);
        }
    }

    void
    testRelationals()
    {
        testcase << "test_relationals " << to_string(Number::getMantissaScale());
        BEAST_EXPECT(!(Number{100} < Number{10}));
        BEAST_EXPECT(Number{100} > Number{10});
        BEAST_EXPECT(Number{100} >= Number{10});
        BEAST_EXPECT(!(Number{100} <= Number{10}));
    }

    void
    testStream()
    {
        testcase << "test_stream " << to_string(Number::getMantissaScale());
        Number const x{100};
        std::ostringstream os;
        os << x;
        BEAST_EXPECT(os.str() == to_string(x));
    }

    void
    testIncDec()
    {
        testcase << "test_inc_dec " << to_string(Number::getMantissaScale());
        Number x{100};
        Number const y = +x;
        BEAST_EXPECT(x == y);
        BEAST_EXPECT(x++ == y);
        BEAST_EXPECT(x == Number{101});
        BEAST_EXPECT(x-- == Number{101});
        BEAST_EXPECT(x == y);
    }

    void
    testToStAmount()
    {
        NumberSO const stNumberSO{true};
        Issue const issue;
        Number const n{7'518'783'80596, -5};
        SaveNumberRoundMode const save{Number::setround(Number::RoundingMode::ToNearest)};
        auto res2 = STAmount{issue, n};
        BEAST_EXPECT(res2 == STAmount{7518784});

        Number::setround(Number::RoundingMode::TowardsZero);
        res2 = STAmount{issue, n};
        BEAST_EXPECT(res2 == STAmount{7518783});

        Number::setround(Number::RoundingMode::Downward);
        res2 = STAmount{issue, n};
        BEAST_EXPECT(res2 == STAmount{7518783});

        Number::setround(Number::RoundingMode::Upward);
        res2 = STAmount{issue, n};
        BEAST_EXPECT(res2 == STAmount{7518784});
    }

    void
    testTruncate()
    {
        BEAST_EXPECT(Number(25, +1).truncate() == Number(250, 0));
        BEAST_EXPECT(Number(25, 0).truncate() == Number(25, 0));
        BEAST_EXPECT(Number(25, -1).truncate() == Number(2, 0));
        BEAST_EXPECT(Number(25, -2).truncate() == Number(0, 0));
        BEAST_EXPECT(Number(99, -2).truncate() == Number(0, 0));

        BEAST_EXPECT(Number(-25, +1).truncate() == Number(-250, 0));
        BEAST_EXPECT(Number(-25, 0).truncate() == Number(-25, 0));
        BEAST_EXPECT(Number(-25, -1).truncate() == Number(-2, 0));
        BEAST_EXPECT(Number(-25, -2).truncate() == Number(0, 0));
        BEAST_EXPECT(Number(-99, -2).truncate() == Number(0, 0));

        BEAST_EXPECT(Number(0, 0).truncate() == Number(0, 0));
        BEAST_EXPECT(Number(0, 30000).truncate() == Number(0, 0));
        BEAST_EXPECT(Number(0, -30000).truncate() == Number(0, 0));
        BEAST_EXPECT(Number(100, -30000).truncate() == Number(0, 0));
        BEAST_EXPECT(Number(100, -30000).truncate() == Number(0, 0));
        BEAST_EXPECT(Number(-100, -30000).truncate() == Number(0, 0));
        BEAST_EXPECT(Number(-100, -30000).truncate() == Number(0, 0));
    }

    void
    testRounding()
    {
        // Test that rounding works as expected.
        testcase("Rounding");

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
                BEAST_EXPECTS(
                    res == val,
                    to_string(num) + " with mode " + std::to_string(static_cast<int>(mode)) +
                        " expected " + std::to_string(val) + " got " + std::to_string(res));
            }
        }
    }

    void
    testInt64()
    {
        auto const scale = Number::getMantissaScale();
        testcase << "std::int64_t " << to_string(scale);

        // Control case
        BEAST_EXPECT(Number::maxMantissa() > 10);
        Number const ten{10};
        BEAST_EXPECT(ten.exponent() <= 0);

        if (scale == MantissaRange::MantissaScale::Small)
        {
            BEAST_EXPECT(std::numeric_limits<std::int64_t>::max() > kInitialXrp.drops());
            BEAST_EXPECT(Number::maxMantissa() < kInitialXrp.drops());
            Number const initalXrp{kInitialXrp};
            BEAST_EXPECT(initalXrp.exponent() > 0);

            Number const maxInt64{Number::kMaxRep};
            BEAST_EXPECT(maxInt64.exponent() > 0);
            // 85'070'591'730'234'615'865'843'651'857'942'052'864 - 38 digits
            BEAST_EXPECT((power(maxInt64, 2) == Number{85'070'591'730'234'62, 22}));

            Number const max = Number{false, Number::maxMantissa(), 0, Number::Normalized{}};
            BEAST_EXPECT(max.exponent() <= 0);
            // 99'999'999'999'999'980'000'000'000'000'001 - 32 digits
            BEAST_EXPECT((power(max, 2) == Number{99'999'999'999'999'98, 16}));
        }
        else
        {
            BEAST_EXPECT(std::numeric_limits<std::int64_t>::max() > kInitialXrp.drops());
            BEAST_EXPECT(Number::maxMantissa() > kInitialXrp.drops());
            Number const initalXrp{kInitialXrp};
            BEAST_EXPECT(initalXrp.exponent() <= 0);

            Number const maxInt64{Number::kMaxRep};
            BEAST_EXPECT(maxInt64.exponent() <= 0);
            // 85'070'591'730'234'615'847'396'907'784'232'501'249 - 38 digits
            BEAST_EXPECT((power(maxInt64, 2) == Number{85'070'591'730'234'615'85, 19}));

            NumberRoundModeGuard const mg(Number::RoundingMode::TowardsZero);

            auto const maxMantissa = Number::maxMantissa();
            Number const max = Number{false, maxMantissa, 0, Number::Normalized{}};
            BEAST_EXPECT(max.mantissa() == maxMantissa / 10);
            BEAST_EXPECT(max.exponent() == 1);
            // 99'999'999'999'999'999'800'000'000'000'000'000'100 - also 38
            // digits
            BEAST_EXPECT(
                (power(max, 2) == Number{false, (maxMantissa / 10) - 1, 20, Number::Normalized{}}));
        }
    }

    void
    testUpwardRoundsDown()
    {
        testcase << "upward rounding produces a value below exact at kMaxRep cusp";

        NumberMantissaScaleGuard const mg{MantissaRange::MantissaScale::Large};
        NumberRoundModeGuard const rg{Number::RoundingMode::Upward};

        constexpr std::int64_t kAValue = 1'000'000'000'000'049'863LL;
        constexpr std::int64_t kBValue = 9'223'372'036'854'315'903LL;

        Number const a = kAValue;
        Number const b = kBValue;
        Number const product = a * b;

        // Exact reference in BigInt.
        BigInt const exactProduct = BigInt(kAValue) * BigInt(kBValue);

        // What Number actually stored.
        BigInt storedValue = BigInt(product.mantissa());
        for (int i = 0; i < product.exponent(); ++i)
            storedValue *= 10;

        BigInt const signedDifference = storedValue - exactProduct;

        log << "\n"
            << "  a              = " << fmt(BigInt(kAValue)) << "\n"
            << "  b              = " << fmt(BigInt(kBValue)) << "\n"
            << "  exact a*b      = " << fmt(exactProduct) << "\n"
            << "  stored         = " << fmt(storedValue) << "\n"
            << "  stored - exact = " << fmt(signedDifference) << "\n"
            << "  upward         = " << (signedDifference >= 0 ? "held" : "VIOLATED") << "\n";

        BEAST_EXPECT(signedDifference >= 0);
        BEAST_EXPECT(product.mantissa() == (std::numeric_limits<std::int64_t>::max() / 10) + 1);
        BEAST_EXPECT(product.exponent() == 19);
    }

    void
    run() override
    {
        for (auto const scale : MantissaRange::getAllScales())
        {
            NumberMantissaScaleGuard const sg(scale);
            testZero();
            testLimits();
            testToString();
            testAdd();
            testSub();
            testMul();
            testDiv();
            testRoot();
            testRoot2();
            testPower1();
            testPower2();
            testConversions();
            testToInteger();
            testSquelch();
            testRelationals();
            testStream();
            testIncDec();
            testToStAmount();
            testTruncate();
            testRounding();
            testInt64();
        }
        // This test sets its own number range
        testUpwardRoundsDown();
    }
};

BEAST_DEFINE_TESTSUITE(Number, basics, xrpl);

}  // namespace xrpl
