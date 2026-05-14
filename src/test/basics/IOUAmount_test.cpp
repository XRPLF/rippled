#include <xrpl/basics/Number.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/protocol/IOUAmount.h>

#include <cstdint>
#include <limits>
#include <sstream>

namespace xrpl {

class IOUAmount_test : public beast::unit_test::Suite
{
public:
    void
    testZero()
    {
        testcase("zero");

        IOUAmount const z(0, 0);

        BEAST_EXPECT(z.mantissa() == 0);
        BEAST_EXPECT(z.exponent() == -100);
        BEAST_EXPECT(!z);
        BEAST_EXPECT(z.signum() == 0);
        BEAST_EXPECT(z == beast::kZero);

        BEAST_EXPECT((z + z) == z);
        BEAST_EXPECT((z - z) == z);
        BEAST_EXPECT(z == -z);

        IOUAmount const zz(beast::kZero);
        BEAST_EXPECT(z == zz);

        // https://github.com/XRPLF/rippled/issues/5170
        IOUAmount const zzz{};
        BEAST_EXPECT(zzz == beast::kZero);
        // BEAST_EXPECT(zzz == zz);
    }

    void
    testSigNum()
    {
        testcase("signum");

        IOUAmount const neg(-1, 0);
        BEAST_EXPECT(neg.signum() < 0);

        IOUAmount const zer(0, 0);
        BEAST_EXPECT(zer.signum() == 0);

        IOUAmount const pos(1, 0);
        BEAST_EXPECT(pos.signum() > 0);
    }

    void
    testBeastZero()
    {
        testcase("beast::Zero Comparisons");

        using beast::kZero;

        {
            IOUAmount const z(kZero);
            BEAST_EXPECT(z == kZero);
            BEAST_EXPECT(z >= kZero);
            BEAST_EXPECT(z <= kZero);
            unexpected(z != kZero);
            unexpected(z > kZero);
            unexpected(z < kZero);
        }

        {
            IOUAmount const neg(-2, 0);
            BEAST_EXPECT(neg < kZero);
            BEAST_EXPECT(neg <= kZero);
            BEAST_EXPECT(neg != kZero);
            unexpected(neg == kZero);
        }

        {
            IOUAmount const pos(2, 0);
            BEAST_EXPECT(pos > kZero);
            BEAST_EXPECT(pos >= kZero);
            BEAST_EXPECT(pos != kZero);
            unexpected(pos == kZero);
        }
    }

    void
    testComparisons()
    {
        testcase("IOU Comparisons");

        IOUAmount const n(-2, 0);
        IOUAmount const z(0, 0);
        IOUAmount const p(2, 0);

        BEAST_EXPECT(z == z);
        BEAST_EXPECT(z >= z);
        BEAST_EXPECT(z <= z);
        BEAST_EXPECT(z == -z);
        // NOLINTBEGIN(misc-redundant-expression)
        unexpected(z > z);
        unexpected(z < z);
        unexpected(z != z);
        // NOLINTEND(misc-redundant-expression)
        unexpected(z != -z);

        BEAST_EXPECT(n < z);
        BEAST_EXPECT(n <= z);
        BEAST_EXPECT(n != z);
        unexpected(n > z);
        unexpected(n >= z);
        unexpected(n == z);

        BEAST_EXPECT(p > z);
        BEAST_EXPECT(p >= z);
        BEAST_EXPECT(p != z);
        unexpected(p < z);
        unexpected(p <= z);
        unexpected(p == z);

        BEAST_EXPECT(n < p);
        BEAST_EXPECT(n <= p);
        BEAST_EXPECT(n != p);
        unexpected(n > p);
        unexpected(n >= p);
        unexpected(n == p);

        BEAST_EXPECT(p > n);
        BEAST_EXPECT(p >= n);
        BEAST_EXPECT(p != n);
        unexpected(p < n);
        unexpected(p <= n);
        unexpected(p == n);

        BEAST_EXPECT(p > -p);
        BEAST_EXPECT(p >= -p);
        BEAST_EXPECT(p != -p);

        BEAST_EXPECT(n < -n);
        BEAST_EXPECT(n <= -n);
        BEAST_EXPECT(n != -n);
    }

    void
    testToString()
    {
        testcase("IOU strings");

        auto test = [this](IOUAmount const& n, std::string const& expected) {
            auto const result = to_string(n);
            std::stringstream ss;
            ss << "to_string(" << result << "). Expected: " << expected;
            BEAST_EXPECTS(result == expected, ss.str());
        };

        for (auto const mantissaSize :
             {MantissaRange::MantissaScale::Small, MantissaRange::MantissaScale::Large})
        {
            NumberMantissaScaleGuard const mg(mantissaSize);

            test(IOUAmount(-2, 0), "-2");
            test(IOUAmount(0, 0), "0");
            test(IOUAmount(2, 0), "2");
            test(IOUAmount(25, -3), "0.025");
            test(IOUAmount(-25, -3), "-0.025");
            test(IOUAmount(25, 1), "250");
            test(IOUAmount(-25, 1), "-250");
            test(IOUAmount(2, 20), "2e20");
            test(IOUAmount(-2, -20), "-2e-20");
        }
    }

    void
    testMulRatio()
    {
        testcase("mulRatio");

        /* The range for the mantissa when normalized */
        static constexpr std::int64_t kMinMantissa = 1000000000000000ull;
        static constexpr std::int64_t kMaxMantissa = 9999999999999999ull;
        // log(2,maxMantissa) ~ 53.15
        /* The range for the exponent when normalized */
        static constexpr int kMinExponent = -96;
        static constexpr int kMaxExponent = 80;
        constexpr auto kMaxUInt = std::numeric_limits<std::uint32_t>::max();

        {
            // multiply by a number that would overflow the mantissa, then
            // divide by the same number, and check we didn't lose any value
            IOUAmount const bigMan(kMaxMantissa, 0);
            BEAST_EXPECT(bigMan == mulRatio(bigMan, kMaxUInt, kMaxUInt, true));
            // rounding mode shouldn't matter as the result is exact
            BEAST_EXPECT(bigMan == mulRatio(bigMan, kMaxUInt, kMaxUInt, false));
        }
        {
            // Similar test as above, but for negative values
            IOUAmount const bigMan(-kMaxMantissa, 0);
            BEAST_EXPECT(bigMan == mulRatio(bigMan, kMaxUInt, kMaxUInt, true));
            // rounding mode shouldn't matter as the result is exact
            BEAST_EXPECT(bigMan == mulRatio(bigMan, kMaxUInt, kMaxUInt, false));
        }

        {
            // small amounts
            IOUAmount const tiny(kMinMantissa, kMinExponent);
            // Round up should give the smallest allowable number
            BEAST_EXPECT(tiny == mulRatio(tiny, 1, kMaxUInt, true));
            BEAST_EXPECT(tiny == mulRatio(tiny, kMaxUInt - 1, kMaxUInt, true));
            // rounding down should be zero
            BEAST_EXPECT(beast::kZero == mulRatio(tiny, 1, kMaxUInt, false));
            BEAST_EXPECT(beast::kZero == mulRatio(tiny, kMaxUInt - 1, kMaxUInt, false));

            // tiny negative numbers
            IOUAmount const tinyNeg(-kMinMantissa, kMinExponent);
            // Round up should give zero
            BEAST_EXPECT(beast::kZero == mulRatio(tinyNeg, 1, kMaxUInt, true));
            BEAST_EXPECT(beast::kZero == mulRatio(tinyNeg, kMaxUInt - 1, kMaxUInt, true));
            // rounding down should be tiny
            BEAST_EXPECT(tinyNeg == mulRatio(tinyNeg, 1, kMaxUInt, false));
            BEAST_EXPECT(tinyNeg == mulRatio(tinyNeg, kMaxUInt - 1, kMaxUInt, false));
        }

        {  // rounding
            {
                IOUAmount const one(1, 0);
                auto const rup = mulRatio(one, kMaxUInt - 1, kMaxUInt, true);
                auto const rdown = mulRatio(one, kMaxUInt - 1, kMaxUInt, false);
                BEAST_EXPECT(rup.mantissa() - rdown.mantissa() == 1);
            }
            {
                IOUAmount const big(kMaxMantissa, kMaxExponent);
                auto const rup = mulRatio(big, kMaxUInt - 1, kMaxUInt, true);
                auto const rdown = mulRatio(big, kMaxUInt - 1, kMaxUInt, false);
                BEAST_EXPECT(rup.mantissa() - rdown.mantissa() == 1);
            }

            {
                IOUAmount const negOne(-1, 0);
                auto const rup = mulRatio(negOne, kMaxUInt - 1, kMaxUInt, true);
                auto const rdown = mulRatio(negOne, kMaxUInt - 1, kMaxUInt, false);
                BEAST_EXPECT(rup.mantissa() - rdown.mantissa() == 1);
            }
        }

        {
            // division by zero
            IOUAmount one(1, 0);
            except([&] { mulRatio(one, 1, 0, true); });
        }

        {
            // overflow
            IOUAmount big(kMaxMantissa, kMaxExponent);
            except([&] { mulRatio(big, 2, 0, true); });
        }
    }  // namespace xrpl

    //--------------------------------------------------------------------------

    void
    run() override
    {
        testZero();
        testSigNum();
        testBeastZero();
        testComparisons();
        testToString();
        testMulRatio();
    }
};

BEAST_DEFINE_TESTSUITE(IOUAmount, basics, xrpl);

}  // namespace xrpl
