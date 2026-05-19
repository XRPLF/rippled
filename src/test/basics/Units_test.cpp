#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/SystemParameters.h>
#include <xrpl/protocol/Units.h>
#include <xrpl/protocol/XRPAmount.h>

#include <cstdint>
#include <limits>
#include <type_traits>

namespace xrpl::test {

class units_test : public beast::unit_test::Suite
{
private:
    void
    testTypes()
    {
        using FeeLevel32 = FeeLevel<std::uint32_t>;

        {
            XRPAmount const x{100};
            BEAST_EXPECT(x.drops() == 100);
            BEAST_EXPECT((std::is_same_v<decltype(x)::unit_type, unit::dropTag>));
            auto y = 4u * x;
            BEAST_EXPECT(y.value() == 400);
            BEAST_EXPECT((std::is_same_v<decltype(y)::unit_type, unit::dropTag>));

            auto z = 4 * y;
            BEAST_EXPECT(z.value() == 1600);
            BEAST_EXPECT((std::is_same_v<decltype(z)::unit_type, unit::dropTag>));

            FeeLevel32 const f{10};
            FeeLevel32 const baseFee{100};

            auto drops = mulDiv(baseFee, x, f);

            BEAST_EXPECT(drops);
            BEAST_EXPECT(drops.value() == 1000);  // NOLINT(bugprone-unchecked-optional-access)
            BEAST_EXPECT((std::is_same_v<
                          std::remove_reference_t<decltype(*drops)>::unit_type,
                          unit::dropTag>));

            BEAST_EXPECT((std::is_same_v<std::remove_reference_t<decltype(*drops)>, XRPAmount>));
        }
        {
            XRPAmount const x{100};
            BEAST_EXPECT(x.value() == 100);
            BEAST_EXPECT((std::is_same_v<decltype(x)::unit_type, unit::dropTag>));
            auto y = 4u * x;
            BEAST_EXPECT(y.value() == 400);
            BEAST_EXPECT((std::is_same_v<decltype(y)::unit_type, unit::dropTag>));

            FeeLevel64 const f{10};
            FeeLevel64 const baseFee{100};

            auto drops = mulDiv(baseFee, x, f);

            BEAST_EXPECT(drops);
            BEAST_EXPECT(drops.value() == 1000);  // NOLINT(bugprone-unchecked-optional-access)
            BEAST_EXPECT((std::is_same_v<
                          std::remove_reference_t<decltype(*drops)>::unit_type,
                          unit::dropTag>));
            BEAST_EXPECT((std::is_same_v<std::remove_reference_t<decltype(*drops)>, XRPAmount>));
        }
        {
            FeeLevel64 const x{1024};
            BEAST_EXPECT(x.value() == 1024);
            BEAST_EXPECT((std::is_same_v<decltype(x)::unit_type, unit::feelevelTag>));
            std::uint64_t const m = 4;
            auto y = m * x;
            BEAST_EXPECT(y.value() == 4096);
            BEAST_EXPECT((std::is_same_v<decltype(y)::unit_type, unit::feelevelTag>));

            XRPAmount const basefee{10};
            FeeLevel64 const referencefee{256};

            auto drops = mulDiv(x, basefee, referencefee);

            BEAST_EXPECT(drops);
            BEAST_EXPECT(drops.value() == 40);  // NOLINT(bugprone-unchecked-optional-access)
            BEAST_EXPECT((std::is_same_v<
                          std::remove_reference_t<decltype(*drops)>::unit_type,
                          unit::dropTag>));
            BEAST_EXPECT((std::is_same_v<std::remove_reference_t<decltype(*drops)>, XRPAmount>));
        }
    }

    void
    testJson()
    {
        // Json value functionality
        using FeeLevel32 = FeeLevel<std::uint32_t>;

        {
            FeeLevel32 const x{std::numeric_limits<std::uint32_t>::max()};
            auto y = x.jsonClipped();
            BEAST_EXPECT(y.type() == json::ValueType::UInt);
            BEAST_EXPECT(y == json::Value{x.fee()});
        }

        {
            FeeLevel32 const x{std::numeric_limits<std::uint32_t>::min()};
            auto y = x.jsonClipped();
            BEAST_EXPECT(y.type() == json::ValueType::UInt);
            BEAST_EXPECT(y == json::Value{x.fee()});
        }

        {
            FeeLevel64 const x{std::numeric_limits<std::uint64_t>::max()};
            auto y = x.jsonClipped();
            BEAST_EXPECT(y.type() == json::ValueType::UInt);
            BEAST_EXPECT(y == json::Value{std::numeric_limits<std::uint32_t>::max()});
        }

        {
            FeeLevel64 const x{std::numeric_limits<std::uint64_t>::min()};
            auto y = x.jsonClipped();
            BEAST_EXPECT(y.type() == json::ValueType::UInt);
            BEAST_EXPECT(y == json::Value{0});
        }

        {
            FeeLevelDouble const x{std::numeric_limits<double>::max()};
            auto y = x.jsonClipped();
            BEAST_EXPECT(y.type() == json::ValueType::Real);
            BEAST_EXPECT(y == json::Value{std::numeric_limits<double>::max()});
        }

        {
            FeeLevelDouble const x{std::numeric_limits<double>::min()};
            auto y = x.jsonClipped();
            BEAST_EXPECT(y.type() == json::ValueType::Real);
            BEAST_EXPECT(y == json::Value{std::numeric_limits<double>::min()});
        }

        {
            XRPAmount const x{std::numeric_limits<std::int64_t>::max()};
            auto y = x.jsonClipped();
            BEAST_EXPECT(y.type() == json::ValueType::Int);
            BEAST_EXPECT(y == json::Value{std::numeric_limits<std::int32_t>::max()});
        }

        {
            XRPAmount const x{std::numeric_limits<std::int64_t>::min()};
            auto y = x.jsonClipped();
            BEAST_EXPECT(y.type() == json::ValueType::Int);
            BEAST_EXPECT(y == json::Value{std::numeric_limits<std::int32_t>::min()});
        }
    }

    void
    testFunctions()
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
            BEAST_EXPECT(test.fee() == 0);

            test = explicitmake(beast::kZero);
            BEAST_EXPECT(test.fee() == 0);

            test = beast::kZero;
            BEAST_EXPECT(test.fee() == 0);

            test = explicitmake(100u);
            BEAST_EXPECT(test.fee() == 100);

            FeeLevel64 const targetSame{200u};
            FeeLevel32 const targetOther{300u};
            test = make(targetSame);
            BEAST_EXPECT(test.fee() == 200);
            BEAST_EXPECT(test == targetSame);
            BEAST_EXPECT(test < FeeLevel64{1000});
            BEAST_EXPECT(test > FeeLevel64{100});
            test = make(targetOther);
            BEAST_EXPECT(test.fee() == 300);
            BEAST_EXPECT(test == targetOther);

            test = std::uint64_t(200);
            BEAST_EXPECT(test.fee() == 200);
            test = std::uint32_t(300);
            BEAST_EXPECT(test.fee() == 300);

            test = targetSame;
            BEAST_EXPECT(test.fee() == 200);
            test = targetOther.fee();
            BEAST_EXPECT(test.fee() == 300);
            BEAST_EXPECT(test == targetOther);

            test = targetSame * 2;
            BEAST_EXPECT(test.fee() == 400);
            test = 3 * targetSame;
            BEAST_EXPECT(test.fee() == 600);
            test = targetSame / 10;
            BEAST_EXPECT(test.fee() == 20);

            test += targetSame;
            BEAST_EXPECT(test.fee() == 220);

            test -= targetSame;
            BEAST_EXPECT(test.fee() == 20);

            test++;
            BEAST_EXPECT(test.fee() == 21);
            ++test;
            BEAST_EXPECT(test.fee() == 22);
            test--;
            BEAST_EXPECT(test.fee() == 21);
            --test;
            BEAST_EXPECT(test.fee() == 20);

            test *= 5;
            BEAST_EXPECT(test.fee() == 100);
            test /= 2;
            BEAST_EXPECT(test.fee() == 50);
            test %= 13;
            BEAST_EXPECT(test.fee() == 11);

            /*
            // illegal with unsigned
            test = -test;
            BEAST_EXPECT(test.fee() == -11);
            BEAST_EXPECT(test.signum() == -1);
            BEAST_EXPECT(to_string(test) == "-11");
            */

            BEAST_EXPECT(test);
            test = 0;
            BEAST_EXPECT(!test);
            BEAST_EXPECT(test.signum() == 0);
            test = targetSame;
            BEAST_EXPECT(test.signum() == 1);
            BEAST_EXPECT(to_string(test) == "200");
        }
        {
            auto make = [&](auto x) -> FeeLevelDouble { return x; };
            auto explicitmake = [&](auto x) -> FeeLevelDouble { return FeeLevelDouble{x}; };

            [[maybe_unused]]
            FeeLevelDouble const defaulted{};
            FeeLevelDouble test{0};
            BEAST_EXPECT(test.fee() == 0);

            test = explicitmake(beast::kZero);
            BEAST_EXPECT(test.fee() == 0);

            test = beast::kZero;
            BEAST_EXPECT(test.fee() == 0);

            test = explicitmake(100.0);
            BEAST_EXPECT(test.fee() == 100);

            FeeLevelDouble const targetSame{200.0};
            FeeLevel64 const targetOther{300};
            test = make(targetSame);
            BEAST_EXPECT(test.fee() == 200);
            BEAST_EXPECT(test == targetSame);
            BEAST_EXPECT(test < FeeLevelDouble{1000.0});
            BEAST_EXPECT(test > FeeLevelDouble{100.0});
            test = targetOther.fee();
            BEAST_EXPECT(test.fee() == 300);
            BEAST_EXPECT(test == targetOther);

            test = 200.0;
            BEAST_EXPECT(test.fee() == 200);
            test = std::uint64_t(300);
            BEAST_EXPECT(test.fee() == 300);

            test = targetSame;
            BEAST_EXPECT(test.fee() == 200);

            test = targetSame * 2;
            BEAST_EXPECT(test.fee() == 400);
            test = 3 * targetSame;
            BEAST_EXPECT(test.fee() == 600);
            test = targetSame / 10;
            BEAST_EXPECT(test.fee() == 20);

            test += targetSame;
            BEAST_EXPECT(test.fee() == 220);

            test -= targetSame;
            BEAST_EXPECT(test.fee() == 20);

            test++;
            BEAST_EXPECT(test.fee() == 21);
            ++test;
            BEAST_EXPECT(test.fee() == 22);
            test--;
            BEAST_EXPECT(test.fee() == 21);
            --test;
            BEAST_EXPECT(test.fee() == 20);

            test *= 5;
            BEAST_EXPECT(test.fee() == 100);
            test /= 2;
            BEAST_EXPECT(test.fee() == 50);
            /* illegal with floating
            test %= 13;
            BEAST_EXPECT(test.fee() == 11);
            */

            // legal with signed
            test = -test;
            BEAST_EXPECT(test.fee() == -50);
            BEAST_EXPECT(test.signum() == -1);
            BEAST_EXPECT(to_string(test) == "-50.000000");

            BEAST_EXPECT(test);
            test = 0;
            BEAST_EXPECT(!test);
            BEAST_EXPECT(test.signum() == 0);
            test = targetSame;
            BEAST_EXPECT(test.signum() == 1);
            BEAST_EXPECT(to_string(test) == "200.000000");
        }
    }

public:
    void
    run() override
    {
        BEAST_EXPECT(kInitialXrp.drops() == 100'000'000'000'000'000);
        BEAST_EXPECT(kInitialXrp == XRPAmount{100'000'000'000'000'000});

        testTypes();
        testJson();
        testFunctions();
    }
};

BEAST_DEFINE_TESTSUITE(units, basics, xrpl);

}  // namespace xrpl::test
