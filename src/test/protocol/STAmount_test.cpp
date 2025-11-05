#include <test/jtx.h>

#include <xrpl/basics/random.h>
#include <xrpl/beast/unit_test.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/XRPAmount.h>

namespace ripple {

class STAmount_test : public beast::unit_test::suite
{
public:
    static STAmount
    serializeAndDeserialize(STAmount const& s)
    {
        Serializer ser;
        s.add(ser);

        SerialIter sit(ser.slice());
        return STAmount(sit, sfGeneric);
    }

    //--------------------------------------------------------------------------
    STAmount
    roundSelf(STAmount const& amount)
    {
        if (amount.native())
            return amount;

        std::uint64_t mantissa = amount.mantissa();
        std::uint64_t valueDigits = mantissa % 1000000000;

        if (valueDigits == 1)
        {
            mantissa--;

            if (mantissa < STAmount::cMinValue)
                return {
                    amount.issue(),
                    mantissa,
                    amount.exponent(),
                    amount.negative()};

            return {
                amount.issue(),
                mantissa,
                amount.exponent(),
                amount.negative(),
                STAmount::unchecked{}};
        }

        if (valueDigits == 999999999)
        {
            mantissa++;

            if (mantissa > STAmount::cMaxValue)
                return {
                    amount.issue(),
                    mantissa,
                    amount.exponent(),
                    amount.negative()};

            return {
                amount.issue(),
                mantissa,
                amount.exponent(),
                amount.negative(),
                STAmount::unchecked{}};
        }

        return amount;
    }

    void
    roundTest(int n, int d, int m)
    {
        // check STAmount rounding
        STAmount num(noIssue(), n);
        STAmount den(noIssue(), d);
        STAmount mul(noIssue(), m);
        STAmount quot = divide(STAmount(n), STAmount(d), noIssue());
        STAmount res = roundSelf(multiply(quot, mul, noIssue()));

        BEAST_EXPECT(!res.native());

        STAmount cmp(noIssue(), (n * m) / d);

        BEAST_EXPECT(!cmp.native());

        BEAST_EXPECT(cmp.issue().currency == res.issue().currency);

        if (res != cmp)
        {
            log << "(" << num.getText() << "/" << den.getText() << ") X "
                << mul.getText() << " = " << res.getText() << " not "
                << cmp.getText();
            fail("Rounding");
            return;
        }
    }

    void
    mulTest(int a, int b)
    {
        STAmount aa(noIssue(), a);
        STAmount bb(noIssue(), b);
        STAmount prod1(multiply(aa, bb, noIssue()));

        BEAST_EXPECT(!prod1.native());

        STAmount prod2(
            noIssue(),
            static_cast<std::uint64_t>(a) * static_cast<std::uint64_t>(b));

        if (prod1 != prod2)
        {
            log << "nn(" << aa.getFullText() << " * " << bb.getFullText()
                << ") = " << prod1.getFullText() << " not "
                << prod2.getFullText();
            fail("Multiplication result is not exact");
        }
    }

    //--------------------------------------------------------------------------

    void
    testSetValue(
        std::string const& value,
        Issue const& issue,
        bool success = true)
    {
        try
        {
            STAmount const amount = amountFromString(issue, value);
            BEAST_EXPECT(amount.getText() == value);
        }
        catch (std::exception const&)
        {
            BEAST_EXPECT(!success);
        }
    }

    void
    testSetValue()
    {
        {
            testcase("set value (native)");

            Issue const xrp(xrpIssue());

            // fractional XRP (i.e. drops)
            testSetValue("1", xrp);
            testSetValue("22", xrp);
            testSetValue("333", xrp);
            testSetValue("4444", xrp);
            testSetValue("55555", xrp);
            testSetValue("666666", xrp);

            // 1 XRP up to 100 billion, in powers of 10 (in drops)
            testSetValue("1000000", xrp);
            testSetValue("10000000", xrp);
            testSetValue("100000000", xrp);
            testSetValue("1000000000", xrp);
            testSetValue("10000000000", xrp);
            testSetValue("100000000000", xrp);
            testSetValue("1000000000000", xrp);
            testSetValue("10000000000000", xrp);
            testSetValue("100000000000000", xrp);
            testSetValue("1000000000000000", xrp);
            testSetValue("10000000000000000", xrp);
            testSetValue("100000000000000000", xrp);

            // Invalid native values:
            testSetValue("1.1", xrp, false);
            testSetValue("100000000000000001", xrp, false);
            testSetValue("1000000000000000000", xrp, false);
        }

        {
            testcase("set value (iou)");

            Issue const usd(Currency(0x5553440000000000), AccountID(0x4985601));

            testSetValue("1", usd);
            testSetValue("10", usd);
            testSetValue("100", usd);
            testSetValue("1000", usd);
            testSetValue("10000", usd);
            testSetValue("100000", usd);
            testSetValue("1000000", usd);
            testSetValue("10000000", usd);
            testSetValue("100000000", usd);
            testSetValue("1000000000", usd);
            testSetValue("10000000000", usd);

            testSetValue("1234567.1", usd);
            testSetValue("1234567.12", usd);
            testSetValue("1234567.123", usd);
            testSetValue("1234567.1234", usd);
            testSetValue("1234567.12345", usd);
            testSetValue("1234567.123456", usd);
            testSetValue("1234567.1234567", usd);
            testSetValue("1234567.12345678", usd);
            testSetValue("1234567.123456789", usd);
        }
    }

    //--------------------------------------------------------------------------

    void
    testNativeCurrency()
    {
        testcase("native currency");
        STAmount zeroSt, one(1), hundred(100);
        // VFALCO NOTE Why repeat "STAmount fail" so many times??
        unexpected(serializeAndDeserialize(zeroSt) != zeroSt, "STAmount fail");
        unexpected(serializeAndDeserialize(one) != one, "STAmount fail");
        unexpected(
            serializeAndDeserialize(hundred) != hundred, "STAmount fail");
        unexpected(!zeroSt.native(), "STAmount fail");
        unexpected(!hundred.native(), "STAmount fail");
        unexpected(zeroSt != beast::zero, "STAmount fail");
        unexpected(one == beast::zero, "STAmount fail");
        unexpected(hundred == beast::zero, "STAmount fail");
        unexpected((zeroSt < zeroSt), "STAmount fail");
        unexpected(!(zeroSt < one), "STAmount fail");
        unexpected(!(zeroSt < hundred), "STAmount fail");
        unexpected((one < zeroSt), "STAmount fail");
        unexpected((one < one), "STAmount fail");
        unexpected(!(one < hundred), "STAmount fail");
        unexpected((hundred < zeroSt), "STAmount fail");
        unexpected((hundred < one), "STAmount fail");
        unexpected((hundred < hundred), "STAmount fail");
        unexpected((zeroSt > zeroSt), "STAmount fail");
        unexpected((zeroSt > one), "STAmount fail");
        unexpected((zeroSt > hundred), "STAmount fail");
        unexpected(!(one > zeroSt), "STAmount fail");
        unexpected((one > one), "STAmount fail");
        unexpected((one > hundred), "STAmount fail");
        unexpected(!(hundred > zeroSt), "STAmount fail");
        unexpected(!(hundred > one), "STAmount fail");
        unexpected((hundred > hundred), "STAmount fail");
        unexpected(!(zeroSt <= zeroSt), "STAmount fail");
        unexpected(!(zeroSt <= one), "STAmount fail");
        unexpected(!(zeroSt <= hundred), "STAmount fail");
        unexpected((one <= zeroSt), "STAmount fail");
        unexpected(!(one <= one), "STAmount fail");
        unexpected(!(one <= hundred), "STAmount fail");
        unexpected((hundred <= zeroSt), "STAmount fail");
        unexpected((hundred <= one), "STAmount fail");
        unexpected(!(hundred <= hundred), "STAmount fail");
        unexpected(!(zeroSt >= zeroSt), "STAmount fail");
        unexpected((zeroSt >= one), "STAmount fail");
        unexpected((zeroSt >= hundred), "STAmount fail");
        unexpected(!(one >= zeroSt), "STAmount fail");
        unexpected(!(one >= one), "STAmount fail");
        unexpected((one >= hundred), "STAmount fail");
        unexpected(!(hundred >= zeroSt), "STAmount fail");
        unexpected(!(hundred >= one), "STAmount fail");
        unexpected(!(hundred >= hundred), "STAmount fail");
        unexpected(!(zeroSt == zeroSt), "STAmount fail");
        unexpected((zeroSt == one), "STAmount fail");
        unexpected((zeroSt == hundred), "STAmount fail");
        unexpected((one == zeroSt), "STAmount fail");
        unexpected(!(one == one), "STAmount fail");
        unexpected((one == hundred), "STAmount fail");
        unexpected((hundred == zeroSt), "STAmount fail");
        unexpected((hundred == one), "STAmount fail");
        unexpected(!(hundred == hundred), "STAmount fail");
        unexpected((zeroSt != zeroSt), "STAmount fail");
        unexpected(!(zeroSt != one), "STAmount fail");
        unexpected(!(zeroSt != hundred), "STAmount fail");
        unexpected(!(one != zeroSt), "STAmount fail");
        unexpected((one != one), "STAmount fail");
        unexpected(!(one != hundred), "STAmount fail");
        unexpected(!(hundred != zeroSt), "STAmount fail");
        unexpected(!(hundred != one), "STAmount fail");
        unexpected((hundred != hundred), "STAmount fail");
        unexpected(STAmount().getText() != "0", "STAmount fail");
        unexpected(STAmount(31).getText() != "31", "STAmount fail");
        unexpected(STAmount(310).getText() != "310", "STAmount fail");
        unexpected(to_string(Currency()) != "XRP", "cHC(XRP)");
        Currency c;
        unexpected(!to_currency(c, "USD"), "create USD currency");
        unexpected(to_string(c) != "USD", "check USD currency");

        std::string const cur = "015841551A748AD2C1F76FF6ECB0CCCD00000000";
        unexpected(!to_currency(c, cur), "create custom currency");
        unexpected(to_string(c) != cur, "check custom currency");
    }

    //--------------------------------------------------------------------------

    void
    testCustomCurrency()
    {
        testcase("custom currency");
        STAmount zeroSt(noIssue()), one(noIssue(), 1), hundred(noIssue(), 100);
        unexpected(serializeAndDeserialize(zeroSt) != zeroSt, "STAmount fail");
        unexpected(serializeAndDeserialize(one) != one, "STAmount fail");
        unexpected(
            serializeAndDeserialize(hundred) != hundred, "STAmount fail");
        unexpected(zeroSt.native(), "STAmount fail");
        unexpected(hundred.native(), "STAmount fail");
        unexpected(zeroSt != beast::zero, "STAmount fail");
        unexpected(one == beast::zero, "STAmount fail");
        unexpected(hundred == beast::zero, "STAmount fail");
        unexpected((zeroSt < zeroSt), "STAmount fail");
        unexpected(!(zeroSt < one), "STAmount fail");
        unexpected(!(zeroSt < hundred), "STAmount fail");
        unexpected((one < zeroSt), "STAmount fail");
        unexpected((one < one), "STAmount fail");
        unexpected(!(one < hundred), "STAmount fail");
        unexpected((hundred < zeroSt), "STAmount fail");
        unexpected((hundred < one), "STAmount fail");
        unexpected((hundred < hundred), "STAmount fail");
        unexpected((zeroSt > zeroSt), "STAmount fail");
        unexpected((zeroSt > one), "STAmount fail");
        unexpected((zeroSt > hundred), "STAmount fail");
        unexpected(!(one > zeroSt), "STAmount fail");
        unexpected((one > one), "STAmount fail");
        unexpected((one > hundred), "STAmount fail");
        unexpected(!(hundred > zeroSt), "STAmount fail");
        unexpected(!(hundred > one), "STAmount fail");
        unexpected((hundred > hundred), "STAmount fail");
        unexpected(!(zeroSt <= zeroSt), "STAmount fail");
        unexpected(!(zeroSt <= one), "STAmount fail");
        unexpected(!(zeroSt <= hundred), "STAmount fail");
        unexpected((one <= zeroSt), "STAmount fail");
        unexpected(!(one <= one), "STAmount fail");
        unexpected(!(one <= hundred), "STAmount fail");
        unexpected((hundred <= zeroSt), "STAmount fail");
        unexpected((hundred <= one), "STAmount fail");
        unexpected(!(hundred <= hundred), "STAmount fail");
        unexpected(!(zeroSt >= zeroSt), "STAmount fail");
        unexpected((zeroSt >= one), "STAmount fail");
        unexpected((zeroSt >= hundred), "STAmount fail");
        unexpected(!(one >= zeroSt), "STAmount fail");
        unexpected(!(one >= one), "STAmount fail");
        unexpected((one >= hundred), "STAmount fail");
        unexpected(!(hundred >= zeroSt), "STAmount fail");
        unexpected(!(hundred >= one), "STAmount fail");
        unexpected(!(hundred >= hundred), "STAmount fail");
        unexpected(!(zeroSt == zeroSt), "STAmount fail");
        unexpected((zeroSt == one), "STAmount fail");
        unexpected((zeroSt == hundred), "STAmount fail");
        unexpected((one == zeroSt), "STAmount fail");
        unexpected(!(one == one), "STAmount fail");
        unexpected((one == hundred), "STAmount fail");
        unexpected((hundred == zeroSt), "STAmount fail");
        unexpected((hundred == one), "STAmount fail");
        unexpected(!(hundred == hundred), "STAmount fail");
        unexpected((zeroSt != zeroSt), "STAmount fail");
        unexpected(!(zeroSt != one), "STAmount fail");
        unexpected(!(zeroSt != hundred), "STAmount fail");
        unexpected(!(one != zeroSt), "STAmount fail");
        unexpected((one != one), "STAmount fail");
        unexpected(!(one != hundred), "STAmount fail");
        unexpected(!(hundred != zeroSt), "STAmount fail");
        unexpected(!(hundred != one), "STAmount fail");
        unexpected((hundred != hundred), "STAmount fail");
        unexpected(STAmount(noIssue()).getText() != "0", "STAmount fail");
        unexpected(STAmount(noIssue(), 31).getText() != "31", "STAmount fail");
        unexpected(
            STAmount(noIssue(), 31, 1).getText() != "310", "STAmount fail");
        unexpected(
            STAmount(noIssue(), 31, -1).getText() != "3.1", "STAmount fail");
        unexpected(
            STAmount(noIssue(), 31, -2).getText() != "0.31", "STAmount fail");
        unexpected(
            multiply(STAmount(noIssue(), 20), STAmount(3), noIssue())
                    .getText() != "60",
            "STAmount multiply fail 1");
        unexpected(
            multiply(STAmount(noIssue(), 20), STAmount(3), xrpIssue())
                    .getText() != "60",
            "STAmount multiply fail 2");
        unexpected(
            multiply(STAmount(20), STAmount(3), noIssue()).getText() != "60",
            "STAmount multiply fail 3");
        unexpected(
            multiply(STAmount(20), STAmount(3), xrpIssue()).getText() != "60",
            "STAmount multiply fail 4");

        if (divide(STAmount(noIssue(), 60), STAmount(3), noIssue()).getText() !=
            "20")
        {
            log << "60/3 = "
                << divide(STAmount(noIssue(), 60), STAmount(3), noIssue())
                       .getText();
            fail("STAmount divide fail");
        }
        else
        {
            pass();
        }

        unexpected(
            divide(STAmount(noIssue(), 60), STAmount(3), xrpIssue())
                    .getText() != "20",
            "STAmount divide fail");

        unexpected(
            divide(STAmount(noIssue(), 60), STAmount(noIssue(), 3), noIssue())
                    .getText() != "20",
            "STAmount divide fail");

        unexpected(
            divide(STAmount(noIssue(), 60), STAmount(noIssue(), 3), xrpIssue())
                    .getText() != "20",
            "STAmount divide fail");

        STAmount a1(noIssue(), 60), a2(noIssue(), 10, -1);

        unexpected(
            divide(a2, a1, noIssue()) != amountFromQuality(getRate(a1, a2)),
            "STAmount setRate(getRate) fail");

        unexpected(
            divide(a1, a2, noIssue()) != amountFromQuality(getRate(a2, a1)),
            "STAmount setRate(getRate) fail");
    }

    //--------------------------------------------------------------------------

    void
    testArithmetic()
    {
        testcase("arithmetic");

        // Test currency multiplication and division operations such as
        // convertToDisplayAmount, convertToInternalAmount, getRate, getClaimed,
        // and getNeeded

        unexpected(
            getRate(STAmount(1), STAmount(10)) !=
                (((100ull - 14) << (64 - 8)) | 1000000000000000ull),
            "STAmount getRate fail 1");

        unexpected(
            getRate(STAmount(10), STAmount(1)) !=
                (((100ull - 16) << (64 - 8)) | 1000000000000000ull),
            "STAmount getRate fail 2");

        unexpected(
            getRate(STAmount(noIssue(), 1), STAmount(noIssue(), 10)) !=
                (((100ull - 14) << (64 - 8)) | 1000000000000000ull),
            "STAmount getRate fail 3");

        unexpected(
            getRate(STAmount(noIssue(), 10), STAmount(noIssue(), 1)) !=
                (((100ull - 16) << (64 - 8)) | 1000000000000000ull),
            "STAmount getRate fail 4");

        unexpected(
            getRate(STAmount(noIssue(), 1), STAmount(10)) !=
                (((100ull - 14) << (64 - 8)) | 1000000000000000ull),
            "STAmount getRate fail 5");

        unexpected(
            getRate(STAmount(noIssue(), 10), STAmount(1)) !=
                (((100ull - 16) << (64 - 8)) | 1000000000000000ull),
            "STAmount getRate fail 6");

        unexpected(
            getRate(STAmount(1), STAmount(noIssue(), 10)) !=
                (((100ull - 14) << (64 - 8)) | 1000000000000000ull),
            "STAmount getRate fail 7");

        unexpected(
            getRate(STAmount(10), STAmount(noIssue(), 1)) !=
                (((100ull - 16) << (64 - 8)) | 1000000000000000ull),
            "STAmount getRate fail 8");

        roundTest(1, 3, 3);
        roundTest(2, 3, 9);
        roundTest(1, 7, 21);
        roundTest(1, 2, 4);
        roundTest(3, 9, 18);
        roundTest(7, 11, 44);

        for (int i = 0; i <= 100000; ++i)
        {
            mulTest(rand_int(10000000), rand_int(10000000));
        }
    }

    //--------------------------------------------------------------------------

    void
    testUnderflow()
    {
        testcase("underflow");

        STAmount bigNative(STAmount::cMaxNative / 2);
        STAmount bigValue(
            noIssue(),
            (STAmount::cMinValue + STAmount::cMaxValue) / 2,
            STAmount::cMaxOffset - 1);
        STAmount smallValue(
            noIssue(),
            (STAmount::cMinValue + STAmount::cMaxValue) / 2,
            STAmount::cMinOffset + 1);
        STAmount zeroSt(noIssue(), 0);

        STAmount smallXsmall = multiply(smallValue, smallValue, noIssue());

        BEAST_EXPECT(smallXsmall == beast::zero);

        STAmount bigDsmall = divide(smallValue, bigValue, noIssue());

        BEAST_EXPECT(bigDsmall == beast::zero);

        BEAST_EXPECT(bigDsmall == beast::zero);

        bigDsmall = divide(smallValue, bigValue, xrpIssue());

        BEAST_EXPECT(bigDsmall == beast::zero);

        bigDsmall = divide(smallValue, bigNative, xrpIssue());

        BEAST_EXPECT(bigDsmall == beast::zero);

        // very bad offer
        std::uint64_t r = getRate(smallValue, bigValue);

        BEAST_EXPECT(r == 0);

        // very good offer
        r = getRate(bigValue, smallValue);

        BEAST_EXPECT(r == 0);
    }

    //--------------------------------------------------------------------------

    void
    testRounding()
    {
        // VFALCO TODO There are no actual tests here, just printed output?
        //             Change this to actually do something.

#if 0
        beginTestCase ("rounding ");

        std::uint64_t value = 25000000000000000ull;
        int offset = -14;
        canonicalizeRound (false, value, offset, true);

        STAmount one (noIssue(), 1);
        STAmount two (noIssue(), 2);
        STAmount three (noIssue(), 3);

        STAmount oneThird1 = divRound (one, three, noIssue(), false);
        STAmount oneThird2 = divide (one, three, noIssue());
        STAmount oneThird3 = divRound (one, three, noIssue(), true);
        log << oneThird1;
        log << oneThird2;
        log << oneThird3;

        STAmount twoThird1 = divRound (two, three, noIssue(), false);
        STAmount twoThird2 = divide (two, three, noIssue());
        STAmount twoThird3 = divRound (two, three, noIssue(), true);
        log << twoThird1;
        log << twoThird2;
        log << twoThird3;

        STAmount oneA = mulRound (oneThird1, three, noIssue(), false);
        STAmount oneB = multiply (oneThird2, three, noIssue());
        STAmount oneC = mulRound (oneThird3, three, noIssue(), true);
        log << oneA;
        log << oneB;
        log << oneC;

        STAmount fourThirdsB = twoThird2 + twoThird2;
        log << fourThirdsA;
        log << fourThirdsB;
        log << fourThirdsC;

        STAmount dripTest1 = mulRound (twoThird2, two, xrpIssue (), false);
        STAmount dripTest2 = multiply (twoThird2, two, xrpIssue ());
        STAmount dripTest3 = mulRound (twoThird2, two, xrpIssue (), true);
        log << dripTest1;
        log << dripTest2;
        log << dripTest3;
#endif
    }

    void
    testParseJson()
    {
        static_assert(!std::is_convertible_v<STAmount*, Number*>);

        {
            STAmount const stnum{sfNumber};
            BEAST_EXPECT(stnum.getSType() == STI_AMOUNT);
            BEAST_EXPECT(stnum.getText() == "0");
            BEAST_EXPECT(stnum.isDefault() == true);
            BEAST_EXPECT(stnum.value() == Number{0});
        }

        {
            BEAST_EXPECT(
                amountFromJson(sfNumber, Json::Value(42)) == XRPAmount(42));
            BEAST_EXPECT(
                amountFromJson(sfNumber, Json::Value(-42)) == XRPAmount(-42));

            BEAST_EXPECT(
                amountFromJson(sfNumber, Json::UInt(42)) == XRPAmount(42));

            BEAST_EXPECT(amountFromJson(sfNumber, "-123") == XRPAmount(-123));

            BEAST_EXPECT(amountFromJson(sfNumber, "123") == XRPAmount(123));
            BEAST_EXPECT(amountFromJson(sfNumber, "-123") == XRPAmount(-123));

            BEAST_EXPECT(amountFromJson(sfNumber, "3.14e2") == XRPAmount(314));
            BEAST_EXPECT(
                amountFromJson(sfNumber, "-3.14e2") == XRPAmount(-314));

            BEAST_EXPECT(amountFromJson(sfNumber, "0") == XRPAmount(0));
            BEAST_EXPECT(amountFromJson(sfNumber, "-0") == XRPAmount(0));

            constexpr auto imin = std::numeric_limits<int>::min();
            BEAST_EXPECT(amountFromJson(sfNumber, imin) == XRPAmount(imin));
            BEAST_EXPECT(
                amountFromJson(sfNumber, std::to_string(imin)) ==
                XRPAmount(imin));

            constexpr auto imax = std::numeric_limits<int>::max();
            BEAST_EXPECT(amountFromJson(sfNumber, imax) == XRPAmount(imax));
            BEAST_EXPECT(
                amountFromJson(sfNumber, std::to_string(imax)) ==
                XRPAmount(imax));

            constexpr auto umax = std::numeric_limits<unsigned int>::max();
            BEAST_EXPECT(amountFromJson(sfNumber, umax) == XRPAmount(umax));
            BEAST_EXPECT(
                amountFromJson(sfNumber, std::to_string(umax)) ==
                XRPAmount(umax));

            // XRP does not handle fractional part
            try
            {
                auto _ = amountFromJson(sfNumber, "0.0");
                BEAST_EXPECT(false);
            }
            catch (std::runtime_error const& e)
            {
                std::string const expected =
                    "XRP and MPT must be specified as integral amount.";
                BEAST_EXPECT(e.what() == expected);
            }

            // XRP does not handle fractional part
            try
            {
                auto _ = amountFromJson(sfNumber, "1000e-2");
                BEAST_EXPECT(false);
            }
            catch (std::runtime_error const& e)
            {
                std::string const expected =
                    "XRP and MPT must be specified as integral amount.";
                BEAST_EXPECT(e.what() == expected);
            }

            // Obvious non-numbers tested here
            try
            {
                auto _ = amountFromJson(sfNumber, "");
                BEAST_EXPECT(false);
            }
            catch (std::runtime_error const& e)
            {
                std::string const expected = "'' is not a number";
                BEAST_EXPECT(e.what() == expected);
            }

            try
            {
                auto _ = amountFromJson(sfNumber, "e");
                BEAST_EXPECT(false);
            }
            catch (std::runtime_error const& e)
            {
                std::string const expected = "'e' is not a number";
                BEAST_EXPECT(e.what() == expected);
            }

            try
            {
                auto _ = amountFromJson(sfNumber, "1e");
                BEAST_EXPECT(false);
            }
            catch (std::runtime_error const& e)
            {
                std::string const expected = "'1e' is not a number";
                BEAST_EXPECT(e.what() == expected);
            }

            try
            {
                auto _ = amountFromJson(sfNumber, "e2");
                BEAST_EXPECT(false);
            }
            catch (std::runtime_error const& e)
            {
                std::string const expected = "'e2' is not a number";
                BEAST_EXPECT(e.what() == expected);
            }

            try
            {
                auto _ = amountFromJson(sfNumber, Json::Value());
                BEAST_EXPECT(false);
            }
            catch (std::runtime_error const& e)
            {
                std::string const expected =
                    "XRP may not be specified with a null Json value";
                BEAST_EXPECT(e.what() == expected);
            }

            try
            {
                auto _ = amountFromJson(
                    sfNumber,
                    "123456789012345678901234567890123456789012345678901234"
                    "5678"
                    "901234567890123456789012345678901234567890123456789012"
                    "3456"
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
                auto _ = amountFromJson(sfNumber, "001");
                BEAST_EXPECT(false);
            }
            catch (std::runtime_error const& e)
            {
                std::string const expected = "'001' is not a number";
                BEAST_EXPECT(e.what() == expected);
            }

            try
            {
                auto _ = amountFromJson(sfNumber, "000.0");
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
                auto _ = amountFromJson(sfNumber, ".1");
                BEAST_EXPECT(false);
            }
            catch (std::runtime_error const& e)
            {
                std::string const expected = "'.1' is not a number";
                BEAST_EXPECT(e.what() == expected);
            }

            try
            {
                auto _ = amountFromJson(sfNumber, "1.");
                BEAST_EXPECT(false);
            }
            catch (std::runtime_error const& e)
            {
                std::string const expected = "'1.' is not a number";
                BEAST_EXPECT(e.what() == expected);
            }

            try
            {
                auto _ = amountFromJson(sfNumber, "1.e3");
                BEAST_EXPECT(false);
            }
            catch (std::runtime_error const& e)
            {
                std::string const expected = "'1.e3' is not a number";
                BEAST_EXPECT(e.what() == expected);
            }
        }
    }

    void
    testConvertXRP()
    {
        testcase("STAmount to XRPAmount conversions");

        Issue const usd{Currency(0x5553440000000000), AccountID(0x4985601)};
        Issue const xrp{xrpIssue()};

        for (std::uint64_t drops = 100000000000000000; drops != 1;
             drops = drops / 10)
        {
            auto const t = amountFromString(xrp, std::to_string(drops));
            auto const s = t.xrp();
            BEAST_EXPECT(s.drops() == drops);
            BEAST_EXPECT(t == STAmount(XRPAmount(drops)));
            BEAST_EXPECT(s == XRPAmount(drops));
        }

        try
        {
            auto const t = amountFromString(usd, "136500");
            fail(to_string(t.xrp()));
        }
        catch (std::logic_error const&)
        {
            pass();
        }
        catch (std::exception const&)
        {
            fail("wrong exception");
        }
    }

    void
    testConvertIOU()
    {
        testcase("STAmount to IOUAmount conversions");

        Issue const usd{Currency(0x5553440000000000), AccountID(0x4985601)};
        Issue const xrp{xrpIssue()};

        for (std::uint64_t dollars = 10000000000; dollars != 1;
             dollars = dollars / 10)
        {
            auto const t = amountFromString(usd, std::to_string(dollars));
            auto const s = t.iou();
            BEAST_EXPECT(t == STAmount(s, usd));
            BEAST_EXPECT(s.mantissa() == t.mantissa());
            BEAST_EXPECT(s.exponent() == t.exponent());
        }

        try
        {
            auto const t = amountFromString(xrp, "136500");
            fail(to_string(t.iou()));
        }
        catch (std::logic_error const&)
        {
            pass();
        }
        catch (std::exception const&)
        {
            fail("wrong exception");
        }
    }

    void
    testCanAddXRP()
    {
        testcase("can add xrp");

        // Adding zero
        {
            STAmount amt1(XRPAmount(0));
            STAmount amt2(XRPAmount(1000));
            BEAST_EXPECT(canAdd(amt1, amt2) == true);
        }

        // Adding zero
        {
            STAmount amt1(XRPAmount(1000));
            STAmount amt2(XRPAmount(0));
            BEAST_EXPECT(canAdd(amt1, amt2) == true);
        }

        // Adding two positive XRP amounts
        {
            STAmount amt1(XRPAmount(500));
            STAmount amt2(XRPAmount(1500));
            BEAST_EXPECT(canAdd(amt1, amt2) == true);
        }

        // Adding two negative XRP amounts
        {
            STAmount amt1(XRPAmount(-500));
            STAmount amt2(XRPAmount(-1500));
            BEAST_EXPECT(canAdd(amt1, amt2) == true);
        }

        // Adding a positive and a negative XRP amount
        {
            STAmount amt1(XRPAmount(1000));
            STAmount amt2(XRPAmount(-1000));
            BEAST_EXPECT(canAdd(amt1, amt2) == true);
        }

        // Overflow check for max XRP amounts
        {
            STAmount amt1(std::numeric_limits<XRPAmount::value_type>::max());
            STAmount amt2(XRPAmount(1));
            BEAST_EXPECT(canAdd(amt1, amt2) == false);
        }

        // Overflow check for min XRP amounts
        {
            STAmount amt1(std::numeric_limits<XRPAmount::value_type>::max());
            amt1 += XRPAmount(1);
            STAmount amt2(XRPAmount(-1));
            BEAST_EXPECT(canAdd(amt1, amt2) == false);
        }
    }

    void
    testCanAddIOU()
    {
        testcase("can add iou");

        Issue const usd{Currency(0x5553440000000000), AccountID(0x4985601)};
        Issue const eur{Currency(0x4555520000000000), AccountID(0x4985601)};

        // Adding two IOU amounts
        {
            STAmount amt1(usd, 500);
            STAmount amt2(usd, 1500);
            BEAST_EXPECT(canAdd(amt1, amt2) == true);
        }

        // Adding a positive and a negative IOU amount
        {
            STAmount amt1(usd, 1000);
            STAmount amt2(usd, -1000);
            BEAST_EXPECT(canAdd(amt1, amt2) == true);
        }

        // Overflow check for max IOU amounts
        {
            STAmount amt1(usd, std::numeric_limits<int64_t>::max());
            STAmount amt2(usd, 1);
            BEAST_EXPECT(canAdd(amt1, amt2) == false);
        }

        // Overflow check for min IOU amounts
        {
            STAmount amt1(usd, std::numeric_limits<std::int64_t>::min());
            STAmount amt2(usd, -1);
            BEAST_EXPECT(canAdd(amt1, amt2) == false);
        }

        // Adding XRP and IOU
        {
            STAmount amt1(XRPAmount(1));
            STAmount amt2(usd, 1);
            BEAST_EXPECT(canAdd(amt1, amt2) == false);
        }

        // Adding different IOU issues (non zero)
        {
            STAmount amt1(usd, 1000);
            STAmount amt2(eur, 500);
            BEAST_EXPECT(canAdd(amt1, amt2) == false);
        }

        // Adding different IOU issues (zero)
        {
            STAmount amt1(usd, 0);
            STAmount amt2(eur, 500);
            BEAST_EXPECT(canAdd(amt1, amt2) == false);
        }
    }

    void
    testCanAddMPT()
    {
        testcase("can add mpt");

        MPTIssue const mpt{MPTIssue{makeMptID(1, AccountID(0x4985601))}};
        MPTIssue const mpt2{MPTIssue{makeMptID(2, AccountID(0x4985601))}};

        // Adding zero
        {
            STAmount amt1(mpt, 0);
            STAmount amt2(mpt, 1000);
            BEAST_EXPECT(canAdd(amt1, amt2) == true);
        }

        // Adding zero
        {
            STAmount amt1(mpt, 1000);
            STAmount amt2(mpt, 0);
            BEAST_EXPECT(canAdd(amt1, amt2) == true);
        }

        // Adding two positive MPT amounts
        {
            STAmount amt1(mpt, 500);
            STAmount amt2(mpt, 1500);
            BEAST_EXPECT(canAdd(amt1, amt2) == true);
        }

        // Adding two negative MPT amounts
        {
            STAmount amt1(mpt, -500);
            STAmount amt2(mpt, -1500);
            BEAST_EXPECT(canAdd(amt1, amt2) == true);
        }

        // Adding a positive and a negative MPT amount
        {
            STAmount amt1(mpt, 1000);
            STAmount amt2(mpt, -1000);
            BEAST_EXPECT(canAdd(amt1, amt2) == true);
        }

        // Overflow check for max MPT amounts
        {
            STAmount amt1(
                mpt, std::numeric_limits<MPTAmount::value_type>::max());
            STAmount amt2(mpt, 1);
            BEAST_EXPECT(canAdd(amt1, amt2) == false);
        }

        // Overflow check for min MPT amounts
        // Note: Cannot check min MPT overflow because you cannot initialize the
        // STAmount with a negative MPT amount.

        // Adding MPT and XRP
        {
            STAmount amt1(XRPAmount(1000));
            STAmount amt2(mpt, 1000);
            BEAST_EXPECT(canAdd(amt1, amt2) == false);
        }

        // Adding different MPT issues (non zero)
        {
            STAmount amt1(mpt2, 500);
            STAmount amt2(mpt, 500);
            BEAST_EXPECT(canAdd(amt1, amt2) == false);
        }

        // Adding different MPT issues (non zero)
        {
            STAmount amt1(mpt2, 0);
            STAmount amt2(mpt, 500);
            BEAST_EXPECT(canAdd(amt1, amt2) == false);
        }
    }

    void
    testCanSubtractXRP()
    {
        testcase("can subtract xrp");

        // Subtracting zero
        {
            STAmount amt1(XRPAmount(1000));
            STAmount amt2(XRPAmount(0));
            BEAST_EXPECT(canSubtract(amt1, amt2) == true);
        }

        // Subtracting zero
        {
            STAmount amt1(XRPAmount(0));
            STAmount amt2(XRPAmount(1000));
            BEAST_EXPECT(canSubtract(amt1, amt2) == false);
        }

        // Subtracting two positive XRP amounts
        {
            STAmount amt1(XRPAmount(1500));
            STAmount amt2(XRPAmount(500));
            BEAST_EXPECT(canSubtract(amt1, amt2) == true);
        }

        // Subtracting two negative XRP amounts
        {
            STAmount amt1(XRPAmount(-1500));
            STAmount amt2(XRPAmount(-500));
            BEAST_EXPECT(canSubtract(amt1, amt2) == true);
        }

        // Subtracting a positive and a negative XRP amount
        {
            STAmount amt1(XRPAmount(1000));
            STAmount amt2(XRPAmount(-1000));
            BEAST_EXPECT(canSubtract(amt1, amt2) == true);
        }

        // Underflow check for min XRP amounts
        {
            STAmount amt1(std::numeric_limits<XRPAmount::value_type>::max());
            amt1 += XRPAmount(1);
            STAmount amt2(XRPAmount(1));
            BEAST_EXPECT(canSubtract(amt1, amt2) == false);
        }

        // Overflow check for max XRP amounts
        {
            STAmount amt1(std::numeric_limits<XRPAmount::value_type>::max());
            STAmount amt2(XRPAmount(-1));
            BEAST_EXPECT(canSubtract(amt1, amt2) == false);
        }
    }

    void
    testCanSubtractIOU()
    {
        testcase("can subtract iou");
        Issue const usd{Currency(0x5553440000000000), AccountID(0x4985601)};
        Issue const eur{Currency(0x4555520000000000), AccountID(0x4985601)};

        // Subtracting two IOU amounts
        {
            STAmount amt1(usd, 1500);
            STAmount amt2(usd, 500);
            BEAST_EXPECT(canSubtract(amt1, amt2) == true);
        }

        // Subtracting XRP and IOU
        {
            STAmount amt1(XRPAmount(1000));
            STAmount amt2(usd, 1000);
            BEAST_EXPECT(canSubtract(amt1, amt2) == false);
        }

        // Subtracting different IOU issues (non zero)
        {
            STAmount amt1(usd, 1000);
            STAmount amt2(eur, 500);
            BEAST_EXPECT(canSubtract(amt1, amt2) == false);
        }

        // Subtracting different IOU issues (zero)
        {
            STAmount amt1(usd, 0);
            STAmount amt2(eur, 500);
            BEAST_EXPECT(canSubtract(amt1, amt2) == false);
        }
    }

    void
    testCanSubtractMPT()
    {
        testcase("can subtract mpt");

        MPTIssue const mpt{MPTIssue{makeMptID(1, AccountID(0x4985601))}};
        MPTIssue const mpt2{MPTIssue{makeMptID(2, AccountID(0x4985601))}};

        // Subtracting zero
        {
            STAmount amt1(mpt, 1000);
            STAmount amt2(mpt, 0);
            BEAST_EXPECT(canSubtract(amt1, amt2) == true);
        }

        // Subtracting zero
        {
            STAmount amt1(mpt, 0);
            STAmount amt2(mpt, 1000);
            BEAST_EXPECT(canSubtract(amt1, amt2) == false);
        }

        // Subtracting two positive MPT amounts
        {
            STAmount amt1(mpt, 1500);
            STAmount amt2(mpt, 500);
            BEAST_EXPECT(canSubtract(amt1, amt2) == true);
        }

        // Subtracting two negative MPT amounts
        {
            STAmount amt1(mpt, -1500);
            STAmount amt2(mpt, -500);
            BEAST_EXPECT(canSubtract(amt1, amt2) == true);
        }

        // Subtracting a positive and a negative MPT amount
        {
            STAmount amt1(mpt, 1000);
            STAmount amt2(mpt, -1000);
            BEAST_EXPECT(canSubtract(amt1, amt2) == true);
        }

        // Underflow check for min MPT amounts
        // Note: Cannot check min MPT underflow because you cannot initialize
        // the STAmount with a negative MPT amount.

        // Overflow check for max positive MPT amounts (should fail)
        {
            STAmount amt1(
                mpt, std::numeric_limits<MPTAmount::value_type>::max());
            STAmount amt2(mpt, -2);
            BEAST_EXPECT(canSubtract(amt1, amt2) == false);
        }

        // Subtracting MPT and XRP
        {
            STAmount amt1(XRPAmount(1000));
            STAmount amt2(mpt, 1000);
            BEAST_EXPECT(canSubtract(amt1, amt2) == false);
        }

        // Subtracting different MPT issues (non zero)
        {
            STAmount amt1(mpt, 1000);
            STAmount amt2(mpt2, 500);
            BEAST_EXPECT(canSubtract(amt1, amt2) == false);
        }

        // Subtracting different MPT issues (zero)
        {
            STAmount amt1(mpt, 0);
            STAmount amt2(mpt2, 500);
            BEAST_EXPECT(canSubtract(amt1, amt2) == false);
        }
    }

    //--------------------------------------------------------------------------

    void
    run() override
    {
        testSetValue();
        testNativeCurrency();
        testCustomCurrency();
        testArithmetic();
        testUnderflow();
        testRounding();
        testParseJson();
        testConvertXRP();
        testConvertIOU();
        testCanAddXRP();
        testCanAddIOU();
        testCanAddMPT();
        testCanSubtractXRP();
        testCanSubtractIOU();
        testCanSubtractMPT();
    }
};

BEAST_DEFINE_TESTSUITE(STAmount, protocol, ripple);

}  // namespace ripple
