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

TEST(UnitsTest, functions)
{
    // Explicitly test every defined function for the ValueUnit class
    // since some of them are templated, but not used anywhere else.
    using FeeLevel32 = FeeLevel<std::uint32_t>;

    {
        auto make = [&](auto x) -> FeeLevel64 { return x; };
        auto explicitmake = [&](auto x) -> FeeLevel64 { return FeeLevel64{x}; };

        [[maybe_unused]]
        FeeLevel64 const defaulted{};
        FeeLevel64 test{0};
        EXPECT_EQ(test.fee(), 0);

        test = explicitmake(beast::kZero);
        EXPECT_EQ(test.fee(), 0);

        test = beast::kZero;
        EXPECT_EQ(test.fee(), 0);

        test = explicitmake(100u);
        EXPECT_EQ(test.fee(), 100);

        FeeLevel64 const targetSame{200u};
        FeeLevel32 const targetOther{300u};
        test = make(targetSame);
        EXPECT_EQ(test.fee(), 200);
        EXPECT_EQ(test, targetSame);
        EXPECT_TRUE(test < FeeLevel64{1000});
        EXPECT_TRUE(test > FeeLevel64{100});
        test = make(targetOther);
        EXPECT_EQ(test.fee(), 300);
        EXPECT_EQ(test, targetOther);

        test = std::uint64_t(200);
        EXPECT_EQ(test.fee(), 200);
        test = std::uint32_t(300);
        EXPECT_EQ(test.fee(), 300);

        test = targetSame;
        EXPECT_EQ(test.fee(), 200);
        test = targetOther.fee();
        EXPECT_EQ(test.fee(), 300);
        EXPECT_EQ(test, targetOther);

        test = targetSame * 2;
        EXPECT_EQ(test.fee(), 400);
        test = 3 * targetSame;
        EXPECT_EQ(test.fee(), 600);
        test = targetSame / 10;
        EXPECT_EQ(test.fee(), 20);

        test += targetSame;
        EXPECT_EQ(test.fee(), 220);

        test -= targetSame;
        EXPECT_EQ(test.fee(), 20);

        test++;
        EXPECT_EQ(test.fee(), 21);
        ++test;
        EXPECT_EQ(test.fee(), 22);
        test--;
        EXPECT_EQ(test.fee(), 21);
        --test;
        EXPECT_EQ(test.fee(), 20);

        test *= 5;
        EXPECT_EQ(test.fee(), 100);
        test /= 2;
        EXPECT_EQ(test.fee(), 50);
        test %= 13;
        EXPECT_EQ(test.fee(), 11);

        /*
        // illegal with unsigned
        test = -test;
        EXPECT_EQ(test.fee(), -11);
        EXPECT_EQ(test.signum(), -1);
        EXPECT_EQ(to_string(test), "-11");
        */

        EXPECT_TRUE(test);
        test = 0;
        EXPECT_FALSE(test);
        EXPECT_EQ(test.signum(), 0);
        test = targetSame;
        EXPECT_EQ(test.signum(), 1);
        EXPECT_EQ(to_string(test), "200");
    }
    {
        auto make = [&](auto x) -> FeeLevelDouble { return x; };
        auto explicitmake = [&](auto x) -> FeeLevelDouble { return FeeLevelDouble{x}; };

        [[maybe_unused]]
        FeeLevelDouble const defaulted{};
        FeeLevelDouble test{0};
        EXPECT_EQ(test.fee(), 0);

        test = explicitmake(beast::kZero);
        EXPECT_EQ(test.fee(), 0);

        test = beast::kZero;
        EXPECT_EQ(test.fee(), 0);

        test = explicitmake(100.0);
        EXPECT_EQ(test.fee(), 100);

        FeeLevelDouble const targetSame{200.0};
        FeeLevel64 const targetOther{300};
        test = make(targetSame);
        EXPECT_EQ(test.fee(), 200);
        EXPECT_EQ(test, targetSame);
        EXPECT_TRUE(test < FeeLevelDouble{1000.0});
        EXPECT_TRUE(test > FeeLevelDouble{100.0});
        test = targetOther.fee();
        EXPECT_EQ(test.fee(), 300);
        EXPECT_EQ(test, targetOther);

        test = 200.0;
        EXPECT_EQ(test.fee(), 200);
        test = std::uint64_t(300);
        EXPECT_EQ(test.fee(), 300);

        test = targetSame;
        EXPECT_EQ(test.fee(), 200);

        test = targetSame * 2;
        EXPECT_EQ(test.fee(), 400);
        test = 3 * targetSame;
        EXPECT_EQ(test.fee(), 600);
        test = targetSame / 10;
        EXPECT_EQ(test.fee(), 20);

        test += targetSame;
        EXPECT_EQ(test.fee(), 220);

        test -= targetSame;
        EXPECT_EQ(test.fee(), 20);

        test++;
        EXPECT_EQ(test.fee(), 21);
        ++test;
        EXPECT_EQ(test.fee(), 22);
        test--;
        EXPECT_EQ(test.fee(), 21);
        --test;
        EXPECT_EQ(test.fee(), 20);

        test *= 5;
        EXPECT_EQ(test.fee(), 100);
        test /= 2;
        EXPECT_EQ(test.fee(), 50);
        /* illegal with floating
        test %= 13;
        EXPECT_EQ(test.fee(), 11);
        */

        // legal with signed
        test = -test;
        EXPECT_EQ(test.fee(), -50);
        EXPECT_EQ(test.signum(), -1);
        EXPECT_EQ(to_string(test), "-50.000000");

        EXPECT_TRUE(test);
        test = 0;
        EXPECT_FALSE(test);
        EXPECT_EQ(test.signum(), 0);
        test = targetSame;
        EXPECT_EQ(test.signum(), 1);
        EXPECT_EQ(to_string(test), "200.000000");
    }
}

TEST(UnitsTest, initial_xrp)
{
    EXPECT_EQ(kInitialXrp.drops(), 100'000'000'000'000'000);
    EXPECT_EQ(kInitialXrp, XRPAmount{100'000'000'000'000'000});
}

}  // namespace xrpl::test
