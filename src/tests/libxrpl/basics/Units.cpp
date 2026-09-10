#include <xrpl/protocol/Units.h>

#include <xrpl/beast/utility/Zero.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/SystemParameters.h>
#include <xrpl/protocol/XRPAmount.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <type_traits>

namespace xrpl::test {

TEST(UnitsTest, types)
{
    using FeeLevel32 = FeeLevel<std::uint32_t>;

    {
        XRPAmount const x{100};
        EXPECT_EQ(x.drops(), 100);
        EXPECT_TRUE((std::is_same_v<decltype(x)::unit_type, unit::dropTag>));
        auto y = 4u * x;
        EXPECT_EQ(y.value(), 400);
        EXPECT_TRUE((std::is_same_v<decltype(y)::unit_type, unit::dropTag>));

        auto z = 4 * y;
        EXPECT_EQ(z.value(), 1600);
        EXPECT_TRUE((std::is_same_v<decltype(z)::unit_type, unit::dropTag>));

        FeeLevel32 const f{10};
        FeeLevel32 const baseFee{100};

        auto drops = mulDiv(baseFee, x, f);

        EXPECT_TRUE(drops);
        EXPECT_EQ(drops.value(), 1000);  // NOLINT(bugprone-unchecked-optional-access)
        EXPECT_TRUE(
            (std::is_same_v<std::remove_reference_t<decltype(*drops)>::unit_type, unit::dropTag>));

        EXPECT_TRUE((std::is_same_v<std::remove_reference_t<decltype(*drops)>, XRPAmount>));
    }
    {
        XRPAmount const x{100};
        EXPECT_EQ(x.value(), 100);
        EXPECT_TRUE((std::is_same_v<decltype(x)::unit_type, unit::dropTag>));
        auto y = 4u * x;
        EXPECT_EQ(y.value(), 400);
        EXPECT_TRUE((std::is_same_v<decltype(y)::unit_type, unit::dropTag>));

        FeeLevel64 const f{10};
        FeeLevel64 const baseFee{100};

        auto drops = mulDiv(baseFee, x, f);

        EXPECT_TRUE(drops);
        EXPECT_EQ(drops.value(), 1000);  // NOLINT(bugprone-unchecked-optional-access)
        EXPECT_TRUE(
            (std::is_same_v<std::remove_reference_t<decltype(*drops)>::unit_type, unit::dropTag>));
        EXPECT_TRUE((std::is_same_v<std::remove_reference_t<decltype(*drops)>, XRPAmount>));
    }
    {
        FeeLevel64 const x{1024};
        EXPECT_EQ(x.value(), 1024);
        EXPECT_TRUE((std::is_same_v<decltype(x)::unit_type, unit::feelevelTag>));
        std::uint64_t const m = 4;
        auto y = m * x;
        EXPECT_EQ(y.value(), 4096);
        EXPECT_TRUE((std::is_same_v<decltype(y)::unit_type, unit::feelevelTag>));

        XRPAmount const basefee{10};
        FeeLevel64 const referencefee{256};

        auto drops = mulDiv(x, basefee, referencefee);

        EXPECT_TRUE(drops);
        EXPECT_EQ(drops.value(), 40);  // NOLINT(bugprone-unchecked-optional-access)
        EXPECT_TRUE(
            (std::is_same_v<std::remove_reference_t<decltype(*drops)>::unit_type, unit::dropTag>));
        EXPECT_TRUE((std::is_same_v<std::remove_reference_t<decltype(*drops)>, XRPAmount>));
    }
}

TEST(UnitsTest, json)
{
    // Json value functionality
    using FeeLevel32 = FeeLevel<std::uint32_t>;

    {
        FeeLevel32 const x{std::numeric_limits<std::uint32_t>::max()};
        auto y = x.jsonClipped();
        EXPECT_EQ(y.type(), json::ValueType::UInt);
        EXPECT_EQ(y, json::Value{x.fee()});
    }

    {
        FeeLevel32 const x{std::numeric_limits<std::uint32_t>::min()};
        auto y = x.jsonClipped();
        EXPECT_EQ(y.type(), json::ValueType::UInt);
        EXPECT_EQ(y, json::Value{x.fee()});
    }

    {
        FeeLevel64 const x{std::numeric_limits<std::uint64_t>::max()};
        auto y = x.jsonClipped();
        EXPECT_EQ(y.type(), json::ValueType::UInt);
        EXPECT_EQ(y, json::Value{std::numeric_limits<std::uint32_t>::max()});
    }

    {
        FeeLevel64 const x{std::numeric_limits<std::uint64_t>::min()};
        auto y = x.jsonClipped();
        EXPECT_EQ(y.type(), json::ValueType::UInt);
        EXPECT_EQ(y, json::Value{0});
    }

    {
        FeeLevelDouble const x{std::numeric_limits<double>::max()};
        auto y = x.jsonClipped();
        EXPECT_EQ(y.type(), json::ValueType::Real);
        EXPECT_EQ(y, json::Value{std::numeric_limits<double>::max()});
    }

    {
        FeeLevelDouble const x{std::numeric_limits<double>::min()};
        auto y = x.jsonClipped();
        EXPECT_EQ(y.type(), json::ValueType::Real);
        EXPECT_EQ(y, json::Value{std::numeric_limits<double>::min()});
    }

    {
        XRPAmount const x{std::numeric_limits<std::int64_t>::max()};
        auto y = x.jsonClipped();
        EXPECT_EQ(y.type(), json::ValueType::Int);
        EXPECT_EQ(y, json::Value{std::numeric_limits<std::int32_t>::max()});
    }

    {
        XRPAmount const x{std::numeric_limits<std::int64_t>::min()};
        auto y = x.jsonClipped();
        EXPECT_EQ(y.type(), json::ValueType::Int);
        EXPECT_EQ(y, json::Value{std::numeric_limits<std::int32_t>::min()});
    }
}

// The ValueUnit operators are templated and mostly unused elsewhere, so each is
// exercised explicitly. FeeLevel64 is the unsigned-integral instantiation;
// FeeLevelDouble below repeats the set for the floating-point one, where the
// modulo and unary-minus cases differ.
struct FeeLevel64Test : public ::testing::Test
{
    using FeeLevel32 = FeeLevel<std::uint32_t>;

    static FeeLevel64
    make(auto x)
    {
        return x;
    }

    static FeeLevel64
    explicitMake(auto x)
    {
        return FeeLevel64{x};
    }

    FeeLevel64 const targetSame{200u};
    FeeLevel32 const targetOther{300u};
};

TEST_F(FeeLevel64Test, construction)
{
    [[maybe_unused]]
    FeeLevel64 const defaulted{};

    FeeLevel64 const test{0};
    EXPECT_EQ(test.fee(), 0);
}

TEST_F(FeeLevel64Test, construction_from_zero)
{
    FeeLevel64 test{0};

    test = explicitMake(beast::kZero);
    EXPECT_EQ(test.fee(), 0);

    test = beast::kZero;
    EXPECT_EQ(test.fee(), 0);
}

TEST_F(FeeLevel64Test, construction_from_an_unsigned_literal)
{
    FeeLevel64 const test = explicitMake(100u);
    EXPECT_EQ(test.fee(), 100);
}

TEST_F(FeeLevel64Test, conversion_from_the_same_unit)
{
    FeeLevel64 const test = make(targetSame);

    EXPECT_EQ(test.fee(), 200);
    EXPECT_EQ(test, targetSame);
    EXPECT_TRUE(test < FeeLevel64{1000});
    EXPECT_TRUE(test > FeeLevel64{100});
}

TEST_F(FeeLevel64Test, conversion_from_a_narrower_unit)
{
    FeeLevel64 const test = make(targetOther);

    EXPECT_EQ(test.fee(), 300);
    EXPECT_EQ(test, targetOther);
}

TEST_F(FeeLevel64Test, assignment_from_raw_integers)
{
    FeeLevel64 test{0};

    test = std::uint64_t(200);
    EXPECT_EQ(test.fee(), 200);

    test = std::uint32_t(300);
    EXPECT_EQ(test.fee(), 300);
}

TEST_F(FeeLevel64Test, assignment_from_units)
{
    FeeLevel64 test{0};

    test = targetSame;
    EXPECT_EQ(test.fee(), 200);

    test = targetOther.fee();
    EXPECT_EQ(test.fee(), 300);
    EXPECT_EQ(test, targetOther);
}

TEST_F(FeeLevel64Test, multiplication_and_division)
{
    FeeLevel64 test{0};

    test = targetSame * 2;
    EXPECT_EQ(test.fee(), 400);

    test = 3 * targetSame;
    EXPECT_EQ(test.fee(), 600);

    test = targetSame / 10;
    EXPECT_EQ(test.fee(), 20);
}

TEST_F(FeeLevel64Test, compound_addition_and_subtraction)
{
    FeeLevel64 test{20u};

    test += targetSame;
    EXPECT_EQ(test.fee(), 220);

    test -= targetSame;
    EXPECT_EQ(test.fee(), 20);
}

TEST_F(FeeLevel64Test, increment_and_decrement)
{
    FeeLevel64 test{20u};

    test++;
    EXPECT_EQ(test.fee(), 21);
    ++test;
    EXPECT_EQ(test.fee(), 22);
    test--;
    EXPECT_EQ(test.fee(), 21);
    --test;
    EXPECT_EQ(test.fee(), 20);
}

TEST_F(FeeLevel64Test, compound_multiplication_division_and_modulo)
{
    FeeLevel64 test{20u};

    test *= 5;
    EXPECT_EQ(test.fee(), 100);

    test /= 2;
    EXPECT_EQ(test.fee(), 50);

    // Modulo is only available on the integral instantiation.
    test %= 13;
    EXPECT_EQ(test.fee(), 11);
}

TEST_F(FeeLevel64Test, truthiness_signum_and_to_string)
{
    FeeLevel64 test{11u};
    EXPECT_TRUE(test);

    test = 0;
    EXPECT_FALSE(test);
    EXPECT_EQ(test.signum(), 0);

    test = targetSame;
    EXPECT_EQ(test.signum(), 1);
    EXPECT_EQ(to_string(test), "200");
}

struct FeeLevelDoubleTest : public ::testing::Test
{
    static FeeLevelDouble
    make(auto x)
    {
        return x;
    }

    static FeeLevelDouble
    explicitMake(auto x)
    {
        return FeeLevelDouble{x};
    }

    FeeLevelDouble const targetSame{200.0};
    FeeLevel64 const targetOther{300};
};

TEST_F(FeeLevelDoubleTest, construction)
{
    [[maybe_unused]]
    FeeLevelDouble const defaulted{};

    FeeLevelDouble const test{0};
    EXPECT_EQ(test.fee(), 0);
}

TEST_F(FeeLevelDoubleTest, construction_from_zero)
{
    FeeLevelDouble test{0};

    test = explicitMake(beast::kZero);
    EXPECT_EQ(test.fee(), 0);

    test = beast::kZero;
    EXPECT_EQ(test.fee(), 0);
}

TEST_F(FeeLevelDoubleTest, construction_from_a_floating_literal)
{
    FeeLevelDouble const test = explicitMake(100.0);
    EXPECT_EQ(test.fee(), 100);
}

TEST_F(FeeLevelDoubleTest, conversion_from_the_same_unit)
{
    FeeLevelDouble const test = make(targetSame);

    EXPECT_EQ(test.fee(), 200);
    EXPECT_EQ(test, targetSame);
    EXPECT_TRUE(test < FeeLevelDouble{1000.0});
    EXPECT_TRUE(test > FeeLevelDouble{100.0});
}

TEST_F(FeeLevelDoubleTest, assignment_from_an_integral_unit)
{
    FeeLevelDouble test{0};

    test = targetOther.fee();
    EXPECT_EQ(test.fee(), 300);
    EXPECT_EQ(test, targetOther);
}

TEST_F(FeeLevelDoubleTest, assignment_from_raw_numbers)
{
    FeeLevelDouble test{0};

    test = 200.0;
    EXPECT_EQ(test.fee(), 200);

    test = std::uint64_t(300);
    EXPECT_EQ(test.fee(), 300);
}

TEST_F(FeeLevelDoubleTest, assignment_from_units)
{
    FeeLevelDouble test{0};

    test = targetSame;
    EXPECT_EQ(test.fee(), 200);
}

TEST_F(FeeLevelDoubleTest, multiplication_and_division)
{
    FeeLevelDouble test{0};

    test = targetSame * 2;
    EXPECT_EQ(test.fee(), 400);

    test = 3 * targetSame;
    EXPECT_EQ(test.fee(), 600);

    test = targetSame / 10;
    EXPECT_EQ(test.fee(), 20);
}

TEST_F(FeeLevelDoubleTest, compound_addition_and_subtraction)
{
    FeeLevelDouble test{20.0};

    test += targetSame;
    EXPECT_EQ(test.fee(), 220);

    test -= targetSame;
    EXPECT_EQ(test.fee(), 20);
}

TEST_F(FeeLevelDoubleTest, increment_and_decrement)
{
    FeeLevelDouble test{20.0};

    test++;
    EXPECT_EQ(test.fee(), 21);
    ++test;
    EXPECT_EQ(test.fee(), 22);
    test--;
    EXPECT_EQ(test.fee(), 21);
    --test;
    EXPECT_EQ(test.fee(), 20);
}

TEST_F(FeeLevelDoubleTest, compound_multiplication_and_division)
{
    // No %=: modulo is not defined for the floating-point instantiation.
    FeeLevelDouble test{20.0};

    test *= 5;
    EXPECT_EQ(test.fee(), 100);

    test /= 2;
    EXPECT_EQ(test.fee(), 50);
}

TEST_F(FeeLevelDoubleTest, negation)
{
    // Only legal on the signed instantiation.
    FeeLevelDouble test{50.0};

    test = -test;
    EXPECT_EQ(test.fee(), -50);
    EXPECT_EQ(test.signum(), -1);
    EXPECT_EQ(to_string(test), "-50.000000");
}

TEST_F(FeeLevelDoubleTest, truthiness_signum_and_to_string)
{
    FeeLevelDouble test{-50.0};
    EXPECT_TRUE(test);

    test = 0;
    EXPECT_FALSE(test);
    EXPECT_EQ(test.signum(), 0);

    test = targetSame;
    EXPECT_EQ(test.signum(), 1);
    EXPECT_EQ(to_string(test), "200.000000");
}

TEST(UnitsTest, initial_xrp)
{
    EXPECT_EQ(kInitialXrp.drops(), 100'000'000'000'000'000);
    EXPECT_EQ(kInitialXrp, XRPAmount{100'000'000'000'000'000});
}

}  // namespace xrpl::test
