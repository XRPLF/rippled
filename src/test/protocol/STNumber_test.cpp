//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2024 Ripple Labs Inc.

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

#include <xrpl/beast/unit_test.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_forwards.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STNumber.h>

#include <limits>
#include <ostream>
#include <stdexcept>

namespace ripple {

struct STNumber_test : public beast::unit_test::suite
{
    void
    testCombo(Number number)
    {
        STNumber const before{sfNumber, number};
        BEAST_EXPECT(number == before);
        Serializer s;
        before.add(s);
        BEAST_EXPECT(s.size() == 12);
        SerialIter sit(s.slice());
        STNumber const after{sit, sfNumber};
        BEAST_EXPECT(after.isEquivalent(before));
        BEAST_EXPECT(number == after);
    }

    void
    run() override
    {
        static_assert(!std::is_convertible_v<STNumber*, Number*>);

        {
            STNumber const stnum{sfNumber};
            BEAST_EXPECT(stnum.getSType() == STI_NUMBER);
            BEAST_EXPECT(stnum.getText() == "0");
            BEAST_EXPECT(stnum.isDefault() == true);
            BEAST_EXPECT(stnum.value() == Number{0});
        }

        std::initializer_list<std::int64_t> const mantissas = {
            std::numeric_limits<std::int64_t>::min(),
            -1,
            0,
            1,
            std::numeric_limits<std::int64_t>::max()};
        for (std::int64_t mantissa : mantissas)
            testCombo(Number{mantissa});

        std::initializer_list<std::int32_t> const exponents = {
            Number::minExponent, -1, 0, 1, Number::maxExponent - 1};
        for (std::int32_t exponent : exponents)
            testCombo(Number{123, exponent});

        {
            STAmount const strikePrice{noIssue(), 100};
            STNumber const factor{sfNumber, 100};
            auto const iouValue = strikePrice.iou();
            IOUAmount totalValue{iouValue * factor};
            STAmount const totalAmount{totalValue, strikePrice.issue()};
            BEAST_EXPECT(totalAmount == Number{10'000});
        }

        {
            BEAST_EXPECT(
                numberFromJson(sfNumber, Json::Value(42)) ==
                STNumber(sfNumber, 42));
            BEAST_EXPECT(
                numberFromJson(sfNumber, Json::Value(-42)) ==
                STNumber(sfNumber, -42));

            BEAST_EXPECT(
                numberFromJson(sfNumber, Json::UInt(42)) ==
                STNumber(sfNumber, 42));

            BEAST_EXPECT(
                numberFromJson(sfNumber, "-123") == STNumber(sfNumber, -123));

            BEAST_EXPECT(
                numberFromJson(sfNumber, "123") == STNumber(sfNumber, 123));
            BEAST_EXPECT(
                numberFromJson(sfNumber, "-123") == STNumber(sfNumber, -123));

            BEAST_EXPECT(
                numberFromJson(sfNumber, "3.14") ==
                STNumber(sfNumber, Number(314, -2)));
            BEAST_EXPECT(
                numberFromJson(sfNumber, "-3.14") ==
                STNumber(sfNumber, -Number(314, -2)));
            BEAST_EXPECT(
                numberFromJson(sfNumber, "3.14e2") == STNumber(sfNumber, 314));
            BEAST_EXPECT(
                numberFromJson(sfNumber, "-3.14e2") ==
                STNumber(sfNumber, -314));

            BEAST_EXPECT(
                numberFromJson(sfNumber, "1000e-2") == STNumber(sfNumber, 10));
            BEAST_EXPECT(
                numberFromJson(sfNumber, "-1000e-2") ==
                STNumber(sfNumber, -10));

            BEAST_EXPECT(
                numberFromJson(sfNumber, "0") == STNumber(sfNumber, 0));
            BEAST_EXPECT(
                numberFromJson(sfNumber, "0.0") == STNumber(sfNumber, 0));
            BEAST_EXPECT(
                numberFromJson(sfNumber, "0.000") == STNumber(sfNumber, 0));
            BEAST_EXPECT(
                numberFromJson(sfNumber, "-0") == STNumber(sfNumber, 0));
            BEAST_EXPECT(
                numberFromJson(sfNumber, "-0.0") == STNumber(sfNumber, 0));
            BEAST_EXPECT(
                numberFromJson(sfNumber, "-0.000") == STNumber(sfNumber, 0));
            BEAST_EXPECT(
                numberFromJson(sfNumber, "0e6") == STNumber(sfNumber, 0));
            BEAST_EXPECT(
                numberFromJson(sfNumber, "0.0e6") == STNumber(sfNumber, 0));
            BEAST_EXPECT(
                numberFromJson(sfNumber, "0.000e6") == STNumber(sfNumber, 0));
            BEAST_EXPECT(
                numberFromJson(sfNumber, "-0e6") == STNumber(sfNumber, 0));
            BEAST_EXPECT(
                numberFromJson(sfNumber, "-0.0e6") == STNumber(sfNumber, 0));
            BEAST_EXPECT(
                numberFromJson(sfNumber, "-0.000e6") == STNumber(sfNumber, 0));

            // Obvious non-numbers tested here
            try
            {
                auto _ = numberFromJson(sfNumber, "");
                BEAST_EXPECT(false);
            }
            catch (std::runtime_error const& e)
            {
                std::string const expected = "'' is not a number";
                BEAST_EXPECT(e.what() == expected);
            }

            try
            {
                auto _ = numberFromJson(sfNumber, "e");
                BEAST_EXPECT(false);
            }
            catch (std::runtime_error const& e)
            {
                std::string const expected = "'e' is not a number";
                BEAST_EXPECT(e.what() == expected);
            }

            try
            {
                auto _ = numberFromJson(sfNumber, "1e");
                BEAST_EXPECT(false);
            }
            catch (std::runtime_error const& e)
            {
                std::string const expected = "'1e' is not a number";
                BEAST_EXPECT(e.what() == expected);
            }

            try
            {
                auto _ = numberFromJson(sfNumber, "e2");
                BEAST_EXPECT(false);
            }
            catch (std::runtime_error const& e)
            {
                std::string const expected = "'e2' is not a number";
                BEAST_EXPECT(e.what() == expected);
            }

            try
            {
                auto _ = numberFromJson(sfNumber, Json::Value());
                BEAST_EXPECT(false);
            }
            catch (std::runtime_error const& e)
            {
                std::string const expected = "not a number";
                BEAST_EXPECT(e.what() == expected);
            }

            try
            {
                auto _ = numberFromJson(
                    sfNumber,
                    "1234567890123456789012345678901234567890123456789012345678"
                    "9012345678901234567890123456789012345678901234567890123456"
                    "78901234567890123456789012345678901234567890");
                BEAST_EXPECT(false);
            }
            catch (std::bad_cast const& e)
            {
                BEAST_EXPECT(true);
            }

            // We do not handle leading zeros
            try
            {
                auto _ = numberFromJson(sfNumber, "001");
                BEAST_EXPECT(false);
            }
            catch (std::runtime_error const& e)
            {
                std::string const expected = "'001' is not a number";
                BEAST_EXPECT(e.what() == expected);
            }

            try
            {
                auto _ = numberFromJson(sfNumber, "000.0");
                BEAST_EXPECT(false);
            }
            catch (std::runtime_error const& e)
            {
                std::string const expected = "'000.0' is not a number";
                BEAST_EXPECT(e.what() == expected);
            }

            // We do not handle dangling dot
            try
            {
                auto _ = numberFromJson(sfNumber, ".1");
                BEAST_EXPECT(false);
            }
            catch (std::runtime_error const& e)
            {
                std::string const expected = "'.1' is not a number";
                BEAST_EXPECT(e.what() == expected);
            }

            try
            {
                auto _ = numberFromJson(sfNumber, "1.");
                BEAST_EXPECT(false);
            }
            catch (std::runtime_error const& e)
            {
                std::string const expected = "'1.' is not a number";
                BEAST_EXPECT(e.what() == expected);
            }

            try
            {
                auto _ = numberFromJson(sfNumber, "1.e3");
                BEAST_EXPECT(false);
            }
            catch (std::runtime_error const& e)
            {
                std::string const expected = "'1.e3' is not a number";
                BEAST_EXPECT(e.what() == expected);
            }
        }
    }
};

BEAST_DEFINE_TESTSUITE(STNumber, protocol, ripple);

void
testCompile(std::ostream& out)
{
    STNumber number{sfNumber, 42};
    out << number;
}

}  // namespace ripple
