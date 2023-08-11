//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2019 Ripple Labs Inc.

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

#include <ripple/basics/FeeUnits.h>
#include <ripple/beast/unit_test.h>
#include <ripple/protocol/SystemParameters.h>
#include <type_traits>

namespace ripple {
namespace test {

class feeunits_test : public beast::unit_test::suite
{
private:
    void
    testTypes()
    {
        using FeeLevel32 = FeeLevel<std::uint32_t>;

        {
            XRPAmount x{100};
            BEAST_EXPECT(x.drops() == 100);
            BEAST_EXPECT(
                (std::is_same_v<decltype(x)::unit_type, feeunit::dropTag>));
            auto y = 4u * x;
            BEAST_EXPECT(y.value() == 400);
            BEAST_EXPECT(
                (std::is_same_v<decltype(y)::unit_type, feeunit::dropTag>));

            auto z = 4 * y;
            BEAST_EXPECT(z.value() == 1600);
            BEAST_EXPECT(
                (std::is_same_v<decltype(z)::unit_type, feeunit::dropTag>));

            FeeLevel32 f{10};
            FeeLevel32 baseFee{100};

            auto drops = mulDiv(baseFee, x, f);

            BEAST_EXPECT(drops);
            BEAST_EXPECT(drops.value() == 1000);
            BEAST_EXPECT((std::is_same_v<
                          std::remove_reference_t<decltype(*drops)>::unit_type,
                          feeunit::dropTag>));

            BEAST_EXPECT((std::is_same_v<
                          std::remove_reference_t<decltype(*drops)>,
                          XRPAmount>));
        }
        {
            XRPAmount x{100};
            BEAST_EXPECT(x.value() == 100);
            BEAST_EXPECT(
                (std::is_same_v<decltype(x)::unit_type, feeunit::dropTag>));
            auto y = 4u * x;
            BEAST_EXPECT(y.value() == 400);
            BEAST_EXPECT(
                (std::is_same_v<decltype(y)::unit_type, feeunit::dropTag>));

            FeeLevel64 f{10};
            FeeLevel64 baseFee{100};

            auto drops = mulDiv(baseFee, x, f);

            BEAST_EXPECT(drops);
            BEAST_EXPECT(drops.value() == 1000);
            BEAST_EXPECT((std::is_same_v<
                          std::remove_reference_t<decltype(*drops)>::unit_type,
                          feeunit::dropTag>));
            BEAST_EXPECT((std::is_same_v<
                          std::remove_reference_t<decltype(*drops)>,
                          XRPAmount>));
        }
        {
            FeeLevel64 x{1024};
            BEAST_EXPECT(x.value() == 1024);
            BEAST_EXPECT(
                (std::is_same_v<decltype(x)::unit_type, feeunit::feelevelTag>));
            std::uint64_t m = 4;
            auto y = m * x;
            BEAST_EXPECT(y.value() == 4096);
            BEAST_EXPECT(
                (std::is_same_v<decltype(y)::unit_type, feeunit::feelevelTag>));

            XRPAmount basefee{10};
            FeeLevel64 referencefee{256};

            auto drops = mulDiv(x, basefee, referencefee);

            BEAST_EXPECT(drops);
            BEAST_EXPECT(drops.value() == 40);
            BEAST_EXPECT((std::is_same_v<
                          std::remove_reference_t<decltype(*drops)>::unit_type,
                          feeunit::dropTag>));
            BEAST_EXPECT((std::is_same_v<
                          std::remove_reference_t<decltype(*drops)>,
                          XRPAmount>));
        }
    }

    void
    testJson()
    {
        // Json value functionality
        using FeeLevel32 = FeeLevel<std::uint32_t>;

        {
            FeeLevel32 x{std::numeric_limits<std::uint32_t>::max()};
            auto y = x.jsonClipped();
            BEAST_EXPECT(y.type() == Json::uintValue);
            BEAST_EXPECT(y == Json::Value{x.fee()});
        }

        {
            FeeLevel32 x{std::numeric_limits<std::uint32_t>::min()};
            auto y = x.jsonClipped();
            BEAST_EXPECT(y.type() == Json::uintValue);
            BEAST_EXPECT(y == Json::Value{x.fee()});
        }

        {
            FeeLevel64 x{std::numeric_limits<std::uint64_t>::max()};
            auto y = x.jsonClipped();
            BEAST_EXPECT(y.type() == Json::uintValue);
            BEAST_EXPECT(
                y == Json::Value{std::numeric_limits<std::uint32_t>::max()});
        }

        {
            FeeLevel64 x{std::numeric_limits<std::uint64_t>::min()};
            auto y = x.jsonClipped();
            BEAST_EXPECT(y.type() == Json::uintValue);
            BEAST_EXPECT(y == Json::Value{0});
        }

        {
            FeeLevelDouble x{std::numeric_limits<double>::max()};
            auto y = x.jsonClipped();
            BEAST_EXPECT(y.type() == Json::realValue);
            BEAST_EXPECT(y == Json::Value{std::numeric_limits<double>::max()});
        }

        {
            FeeLevelDouble x{std::numeric_limits<double>::min()};
            auto y = x.jsonClipped();
            BEAST_EXPECT(y.type() == Json::realValue);
            BEAST_EXPECT(y == Json::Value{std::numeric_limits<double>::min()});
        }

        {
            XRPAmount x{std::numeric_limits<std::int64_t>::max()};
            auto y = x.jsonClipped();
            BEAST_EXPECT(y.type() == Json::intValue);
            BEAST_EXPECT(
                y == Json::Value{std::numeric_limits<std::int32_t>::max()});
        }

        {
            XRPAmount x{std::numeric_limits<std::int64_t>::min()};
            auto y = x.jsonClipped();
            BEAST_EXPECT(y.type() == Json::intValue);
            BEAST_EXPECT(
                y == Json::Value{std::numeric_limits<std::int32_t>::min()});
        }
    }

    void
    testFunctions()
    {
        // Explicitly test every defined function for the TaggedFee class
        // since some of them are templated, but not used anywhere else.
        using FeeLevel32 = FeeLevel<std::uint32_t>;

        {
            auto make = [&](auto x) -> FeeLevel64 { return x; };
            auto explicitmake = [&](auto x) -> FeeLevel64 {
                return FeeLevel64{x};
            };

            FeeLevel64 defaulted;
            (void)defaulted;
            FeeLevel64 test{0};
            BEAST_EXPECT(test.fee() == 0);

            test = explicitmake(beast::zero);
            BEAST_EXPECT(test.fee() == 0);

            test = beast::zero;
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
            auto explicitmake = [&](auto x) -> FeeLevelDouble {
                return FeeLevelDouble{x};
            };

            FeeLevelDouble defaulted;
            (void)defaulted;
            FeeLevelDouble test{0};
            BEAST_EXPECT(test.fee() == 0);

            test = explicitmake(beast::zero);
            BEAST_EXPECT(test.fee() == 0);

            test = beast::zero;
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
        BEAST_EXPECT(INITIAL_XRP.drops() == 100'000'000'000'000'000);
        BEAST_EXPECT(INITIAL_XRP == XRPAmount{100'000'000'000'000'000});

        testTypes();
        testJson();
        testFunctions();
    }
};

BEAST_DEFINE_TESTSUITE(feeunits, ripple_basics, ripple);

}  // namespace test
}  // namespace ripple
