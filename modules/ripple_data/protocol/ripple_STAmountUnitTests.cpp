//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

// For unit tests:
namespace ripple
{

static STAmount serdes (const STAmount& s)
{
    Serializer ser;

    s.add (ser);

    SerializerIterator sit (ser);

    return STAmount::deserialize (sit);
}

static bool roundTest (int n, int d, int m)
{
    // check STAmount rounding
    STAmount num (CURRENCY_ONE, ACCOUNT_ONE, n);
    STAmount den (CURRENCY_ONE, ACCOUNT_ONE, d);
    STAmount mul (CURRENCY_ONE, ACCOUNT_ONE, m);
    STAmount quot = STAmount::divide (n, d, CURRENCY_ONE, ACCOUNT_ONE);
    STAmount res = STAmount::multiply (quot, mul, CURRENCY_ONE, ACCOUNT_ONE);

    if (res.isNative ())
        BOOST_FAIL ("Product is native");

    res.roundSelf ();

    STAmount cmp (CURRENCY_ONE, ACCOUNT_ONE, (n * m) / d);

    if (cmp.isNative ())
        BOOST_FAIL ("Comparison amount is native");

    if (res == cmp)
        return true;

    cmp.throwComparable (res);
    WriteLog (lsWARNING, STAmount) << "(" << num.getText () << "/" << den.getText () << ") X " << mul.getText () << " = "
                                   << res.getText () << " not " << cmp.getText ();
    BOOST_FAIL ("Round fail");
    return false;
}

static void mulTest (int a, int b)
{
    STAmount aa (CURRENCY_ONE, ACCOUNT_ONE, a);
    STAmount bb (CURRENCY_ONE, ACCOUNT_ONE, b);
    STAmount prod1 (STAmount::multiply (aa, bb, CURRENCY_ONE, ACCOUNT_ONE));

    if (prod1.isNative ())
        BOOST_FAIL ("product is native");

    STAmount prod2 (CURRENCY_ONE, ACCOUNT_ONE, static_cast<uint64> (a) * static_cast<uint64> (b));

    if (prod1 != prod2)
    {
        WriteLog (lsWARNING, STAmount) << "nn(" << aa.getFullText () << " * " << bb.getFullText () << ") = " << prod1.getFullText ()
                                       << " not " << prod2.getFullText ();
        BOOST_WARN ("Multiplication result is not exact");
    }

    aa = a;
    prod1 = STAmount::multiply (aa, bb, CURRENCY_ONE, ACCOUNT_ONE);

    if (prod1 != prod2)
    {
        WriteLog (lsWARNING, STAmount) << "n(" << aa.getFullText () << " * " << bb.getFullText () << ") = " << prod1.getFullText ()
                                       << " not " << prod2.getFullText ();
        BOOST_WARN ("Multiplication result is not exact");
    }

}

}

//------------------------------------------------------------------------------

BOOST_AUTO_TEST_SUITE (amount)

BOOST_AUTO_TEST_CASE ( setValue_test )
{
    using namespace ripple;

    STAmount    saTmp;

#if 0
    // Check native floats
    saTmp.setFullValue ("1^0");
    BOOST_CHECK_MESSAGE (SYSTEM_CURRENCY_PARTS == saTmp.getNValue (), "float integer failed");
    saTmp.setFullValue ("0^1");
    BOOST_CHECK_MESSAGE (SYSTEM_CURRENCY_PARTS / 10 == saTmp.getNValue (), "float fraction failed");
    saTmp.setFullValue ("0^12");
    BOOST_CHECK_MESSAGE (12 * SYSTEM_CURRENCY_PARTS / 100 == saTmp.getNValue (), "float fraction failed");
    saTmp.setFullValue ("1^2");
    BOOST_CHECK_MESSAGE (SYSTEM_CURRENCY_PARTS + (2 * SYSTEM_CURRENCY_PARTS / 10) == saTmp.getNValue (), "float combined failed");
#endif

    // Check native integer
    saTmp.setFullValue ("1");
    BOOST_CHECK_MESSAGE (1 == saTmp.getNValue (), "integer failed");
}

//------------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE ( NativeCurrency_test )
{
    using namespace ripple;

    STAmount zero, one (1), hundred (100);

    if (serdes (zero) != zero) BOOST_FAIL ("STAmount fail");

    if (serdes (one) != one) BOOST_FAIL ("STAmount fail");

    if (serdes (hundred) != hundred) BOOST_FAIL ("STAmount fail");

    if (!zero.isNative ()) BOOST_FAIL ("STAmount fail");

    if (!hundred.isNative ()) BOOST_FAIL ("STAmount fail");

    if (!zero.isZero ()) BOOST_FAIL ("STAmount fail");

    if (one.isZero ()) BOOST_FAIL ("STAmount fail");

    if (hundred.isZero ()) BOOST_FAIL ("STAmount fail");

    if ((zero < zero)) BOOST_FAIL ("STAmount fail");

    if (! (zero < one)) BOOST_FAIL ("STAmount fail");

    if (! (zero < hundred)) BOOST_FAIL ("STAmount fail");

    if ((one < zero)) BOOST_FAIL ("STAmount fail");

    if ((one < one)) BOOST_FAIL ("STAmount fail");

    if (! (one < hundred)) BOOST_FAIL ("STAmount fail");

    if ((hundred < zero)) BOOST_FAIL ("STAmount fail");

    if ((hundred < one)) BOOST_FAIL ("STAmount fail");

    if ((hundred < hundred)) BOOST_FAIL ("STAmount fail");

    if ((zero > zero)) BOOST_FAIL ("STAmount fail");

    if ((zero > one)) BOOST_FAIL ("STAmount fail");

    if ((zero > hundred)) BOOST_FAIL ("STAmount fail");

    if (! (one > zero)) BOOST_FAIL ("STAmount fail");

    if ((one > one)) BOOST_FAIL ("STAmount fail");

    if ((one > hundred)) BOOST_FAIL ("STAmount fail");

    if (! (hundred > zero)) BOOST_FAIL ("STAmount fail");

    if (! (hundred > one)) BOOST_FAIL ("STAmount fail");

    if ((hundred > hundred)) BOOST_FAIL ("STAmount fail");

    if (! (zero <= zero)) BOOST_FAIL ("STAmount fail");

    if (! (zero <= one)) BOOST_FAIL ("STAmount fail");

    if (! (zero <= hundred)) BOOST_FAIL ("STAmount fail");

    if ((one <= zero)) BOOST_FAIL ("STAmount fail");

    if (! (one <= one)) BOOST_FAIL ("STAmount fail");

    if (! (one <= hundred)) BOOST_FAIL ("STAmount fail");

    if ((hundred <= zero)) BOOST_FAIL ("STAmount fail");

    if ((hundred <= one)) BOOST_FAIL ("STAmount fail");

    if (! (hundred <= hundred)) BOOST_FAIL ("STAmount fail");

    if (! (zero >= zero)) BOOST_FAIL ("STAmount fail");

    if ((zero >= one)) BOOST_FAIL ("STAmount fail");

    if ((zero >= hundred)) BOOST_FAIL ("STAmount fail");

    if (! (one >= zero)) BOOST_FAIL ("STAmount fail");

    if (! (one >= one)) BOOST_FAIL ("STAmount fail");

    if ((one >= hundred)) BOOST_FAIL ("STAmount fail");

    if (! (hundred >= zero)) BOOST_FAIL ("STAmount fail");

    if (! (hundred >= one)) BOOST_FAIL ("STAmount fail");

    if (! (hundred >= hundred)) BOOST_FAIL ("STAmount fail");

    if (! (zero == zero)) BOOST_FAIL ("STAmount fail");

    if ((zero == one)) BOOST_FAIL ("STAmount fail");

    if ((zero == hundred)) BOOST_FAIL ("STAmount fail");

    if ((one == zero)) BOOST_FAIL ("STAmount fail");

    if (! (one == one)) BOOST_FAIL ("STAmount fail");

    if ((one == hundred)) BOOST_FAIL ("STAmount fail");

    if ((hundred == zero)) BOOST_FAIL ("STAmount fail");

    if ((hundred == one)) BOOST_FAIL ("STAmount fail");

    if (! (hundred == hundred)) BOOST_FAIL ("STAmount fail");

    if ((zero != zero)) BOOST_FAIL ("STAmount fail");

    if (! (zero != one)) BOOST_FAIL ("STAmount fail");

    if (! (zero != hundred)) BOOST_FAIL ("STAmount fail");

    if (! (one != zero)) BOOST_FAIL ("STAmount fail");

    if ((one != one)) BOOST_FAIL ("STAmount fail");

    if (! (one != hundred)) BOOST_FAIL ("STAmount fail");

    if (! (hundred != zero)) BOOST_FAIL ("STAmount fail");

    if (! (hundred != one)) BOOST_FAIL ("STAmount fail");

    if ((hundred != hundred)) BOOST_FAIL ("STAmount fail");

    if (STAmount ().getText () != "0") BOOST_FAIL ("STAmount fail");

    if (STAmount (31).getText () != "31") BOOST_FAIL ("STAmount fail");

    if (STAmount (310).getText () != "310") BOOST_FAIL ("STAmount fail");

    BOOST_TEST_MESSAGE ("Amount NC Complete");
}

//------------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE ( CustomCurrency_test )
{
    using namespace ripple;

    STAmount zero (CURRENCY_ONE, ACCOUNT_ONE), one (CURRENCY_ONE, ACCOUNT_ONE, 1), hundred (CURRENCY_ONE, ACCOUNT_ONE, 100);

    serdes (one).getRaw ();

    if (serdes (zero) != zero) BOOST_FAIL ("STAmount fail");

    if (serdes (one) != one) BOOST_FAIL ("STAmount fail");

    if (serdes (hundred) != hundred) BOOST_FAIL ("STAmount fail");

    if (zero.isNative ()) BOOST_FAIL ("STAmount fail");

    if (hundred.isNative ()) BOOST_FAIL ("STAmount fail");

    if (!zero.isZero ()) BOOST_FAIL ("STAmount fail");

    if (one.isZero ()) BOOST_FAIL ("STAmount fail");

    if (hundred.isZero ()) BOOST_FAIL ("STAmount fail");

    if ((zero < zero)) BOOST_FAIL ("STAmount fail");

    if (! (zero < one)) BOOST_FAIL ("STAmount fail");

    if (! (zero < hundred)) BOOST_FAIL ("STAmount fail");

    if ((one < zero)) BOOST_FAIL ("STAmount fail");

    if ((one < one)) BOOST_FAIL ("STAmount fail");

    if (! (one < hundred)) BOOST_FAIL ("STAmount fail");

    if ((hundred < zero)) BOOST_FAIL ("STAmount fail");

    if ((hundred < one)) BOOST_FAIL ("STAmount fail");

    if ((hundred < hundred)) BOOST_FAIL ("STAmount fail");

    if ((zero > zero)) BOOST_FAIL ("STAmount fail");

    if ((zero > one)) BOOST_FAIL ("STAmount fail");

    if ((zero > hundred)) BOOST_FAIL ("STAmount fail");

    if (! (one > zero)) BOOST_FAIL ("STAmount fail");

    if ((one > one)) BOOST_FAIL ("STAmount fail");

    if ((one > hundred)) BOOST_FAIL ("STAmount fail");

    if (! (hundred > zero)) BOOST_FAIL ("STAmount fail");

    if (! (hundred > one)) BOOST_FAIL ("STAmount fail");

    if ((hundred > hundred)) BOOST_FAIL ("STAmount fail");

    if (! (zero <= zero)) BOOST_FAIL ("STAmount fail");

    if (! (zero <= one)) BOOST_FAIL ("STAmount fail");

    if (! (zero <= hundred)) BOOST_FAIL ("STAmount fail");

    if ((one <= zero)) BOOST_FAIL ("STAmount fail");

    if (! (one <= one)) BOOST_FAIL ("STAmount fail");

    if (! (one <= hundred)) BOOST_FAIL ("STAmount fail");

    if ((hundred <= zero)) BOOST_FAIL ("STAmount fail");

    if ((hundred <= one)) BOOST_FAIL ("STAmount fail");

    if (! (hundred <= hundred)) BOOST_FAIL ("STAmount fail");

    if (! (zero >= zero)) BOOST_FAIL ("STAmount fail");

    if ((zero >= one)) BOOST_FAIL ("STAmount fail");

    if ((zero >= hundred)) BOOST_FAIL ("STAmount fail");

    if (! (one >= zero)) BOOST_FAIL ("STAmount fail");

    if (! (one >= one)) BOOST_FAIL ("STAmount fail");

    if ((one >= hundred)) BOOST_FAIL ("STAmount fail");

    if (! (hundred >= zero)) BOOST_FAIL ("STAmount fail");

    if (! (hundred >= one)) BOOST_FAIL ("STAmount fail");

    if (! (hundred >= hundred)) BOOST_FAIL ("STAmount fail");

    if (! (zero == zero)) BOOST_FAIL ("STAmount fail");

    if ((zero == one)) BOOST_FAIL ("STAmount fail");

    if ((zero == hundred)) BOOST_FAIL ("STAmount fail");

    if ((one == zero)) BOOST_FAIL ("STAmount fail");

    if (! (one == one)) BOOST_FAIL ("STAmount fail");

    if ((one == hundred)) BOOST_FAIL ("STAmount fail");

    if ((hundred == zero)) BOOST_FAIL ("STAmount fail");

    if ((hundred == one)) BOOST_FAIL ("STAmount fail");

    if (! (hundred == hundred)) BOOST_FAIL ("STAmount fail");

    if ((zero != zero)) BOOST_FAIL ("STAmount fail");

    if (! (zero != one)) BOOST_FAIL ("STAmount fail");

    if (! (zero != hundred)) BOOST_FAIL ("STAmount fail");

    if (! (one != zero)) BOOST_FAIL ("STAmount fail");

    if ((one != one)) BOOST_FAIL ("STAmount fail");

    if (! (one != hundred)) BOOST_FAIL ("STAmount fail");

    if (! (hundred != zero)) BOOST_FAIL ("STAmount fail");

    if (! (hundred != one)) BOOST_FAIL ("STAmount fail");

    if ((hundred != hundred)) BOOST_FAIL ("STAmount fail");

    if (STAmount (CURRENCY_ONE, ACCOUNT_ONE).getText () != "0") BOOST_FAIL ("STAmount fail");

    if (STAmount (CURRENCY_ONE, ACCOUNT_ONE, 31).getText () != "31") BOOST_FAIL ("STAmount fail");

    if (STAmount (CURRENCY_ONE, ACCOUNT_ONE, 31, 1).getText () != "310") BOOST_FAIL ("STAmount fail");

    if (STAmount (CURRENCY_ONE, ACCOUNT_ONE, 31, -1).getText () != "3.1") BOOST_FAIL ("STAmount fail");

    if (STAmount (CURRENCY_ONE, ACCOUNT_ONE, 31, -2).getText () != "0.31") BOOST_FAIL ("STAmount fail");

    if (STAmount::multiply (STAmount (CURRENCY_ONE, ACCOUNT_ONE, 20), STAmount (3), CURRENCY_ONE, ACCOUNT_ONE).getText () != "60")
        BOOST_FAIL ("STAmount multiply fail 1");

    if (STAmount::multiply (STAmount (CURRENCY_ONE, ACCOUNT_ONE, 20), STAmount (3), uint160 (), ACCOUNT_XRP).getText () != "60")
        BOOST_FAIL ("STAmount multiply fail 2");

    if (STAmount::multiply (STAmount (20), STAmount (3), CURRENCY_ONE, ACCOUNT_ONE).getText () != "60")
        BOOST_FAIL ("STAmount multiply fail 3");

    if (STAmount::multiply (STAmount (20), STAmount (3), uint160 (), ACCOUNT_XRP).getText () != "60")
        BOOST_FAIL ("STAmount multiply fail 4");

    if (STAmount::divide (STAmount (CURRENCY_ONE, ACCOUNT_ONE, 60), STAmount (3), CURRENCY_ONE, ACCOUNT_ONE).getText () != "20")
    {
        WriteLog (lsFATAL, STAmount) << "60/3 = " <<
                                     STAmount::divide (STAmount (CURRENCY_ONE, ACCOUNT_ONE, 60),
                                             STAmount (3), CURRENCY_ONE, ACCOUNT_ONE).getText ();
        BOOST_FAIL ("STAmount divide fail");
    }

    if (STAmount::divide (STAmount (CURRENCY_ONE, ACCOUNT_ONE, 60), STAmount (3), uint160 (), ACCOUNT_XRP).getText () != "20")
        BOOST_FAIL ("STAmount divide fail");

    if (STAmount::divide (STAmount (CURRENCY_ONE, ACCOUNT_ONE, 60), STAmount (CURRENCY_ONE, ACCOUNT_ONE, 3), CURRENCY_ONE, ACCOUNT_ONE).getText () != "20")
        BOOST_FAIL ("STAmount divide fail");

    if (STAmount::divide (STAmount (CURRENCY_ONE, ACCOUNT_ONE, 60), STAmount (CURRENCY_ONE, ACCOUNT_ONE, 3), uint160 (), ACCOUNT_XRP).getText () != "20")
        BOOST_FAIL ("STAmount divide fail");

    STAmount a1 (CURRENCY_ONE, ACCOUNT_ONE, 60), a2 (CURRENCY_ONE, ACCOUNT_ONE, 10, -1);

    if (STAmount::divide (a2, a1, CURRENCY_ONE, ACCOUNT_ONE) != STAmount::setRate (STAmount::getRate (a1, a2)))
        BOOST_FAIL ("STAmount setRate(getRate) fail");

    if (STAmount::divide (a1, a2, CURRENCY_ONE, ACCOUNT_ONE) != STAmount::setRate (STAmount::getRate (a2, a1)))
        BOOST_FAIL ("STAmount setRate(getRate) fail");

    BOOST_TEST_MESSAGE ("Amount CC Complete");
}

//------------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE ( CurrencyMulDivTests )
{
    using namespace ripple;

    CBigNum b;

    for (int i = 0; i < 16; ++i)
    {
        uint64 r = rand ();
        r <<= 32;
        r |= rand ();
        b.setuint64 (r);

        if (b.getuint64 () != r)
        {
            WriteLog (lsFATAL, STAmount) << r << " != " << b.getuint64 () << " " << b.ToString (16);
            BOOST_FAIL ("setull64/getull64 failure");
        }
    }

    // Test currency multiplication and division operations such as
    // convertToDisplayAmount, convertToInternalAmount, getRate, getClaimed, and getNeeded

    if (STAmount::getRate (STAmount (1), STAmount (10)) != (((100ull - 14) << (64 - 8)) | 1000000000000000ull))
        BOOST_FAIL ("STAmount getRate fail 1");

    if (STAmount::getRate (STAmount (10), STAmount (1)) != (((100ull - 16) << (64 - 8)) | 1000000000000000ull))
        BOOST_FAIL ("STAmount getRate fail 2");

    if (STAmount::getRate (STAmount (CURRENCY_ONE, ACCOUNT_ONE, 1), STAmount (CURRENCY_ONE, ACCOUNT_ONE, 10)) != (((100ull - 14) << (64 - 8)) | 1000000000000000ull))
        BOOST_FAIL ("STAmount getRate fail 3");

    if (STAmount::getRate (STAmount (CURRENCY_ONE, ACCOUNT_ONE, 10), STAmount (CURRENCY_ONE, ACCOUNT_ONE, 1)) != (((100ull - 16) << (64 - 8)) | 1000000000000000ull))
        BOOST_FAIL ("STAmount getRate fail 4");

    if (STAmount::getRate (STAmount (CURRENCY_ONE, ACCOUNT_ONE, 1), STAmount (10)) != (((100ull - 14) << (64 - 8)) | 1000000000000000ull))
        BOOST_FAIL ("STAmount getRate fail 5");

    if (STAmount::getRate (STAmount (CURRENCY_ONE, ACCOUNT_ONE, 10), STAmount (1)) != (((100ull - 16) << (64 - 8)) | 1000000000000000ull))
        BOOST_FAIL ("STAmount getRate fail 6");

    if (STAmount::getRate (STAmount (1), STAmount (CURRENCY_ONE, ACCOUNT_ONE, 10)) != (((100ull - 14) << (64 - 8)) | 1000000000000000ull))
        BOOST_FAIL ("STAmount getRate fail 7");

    if (STAmount::getRate (STAmount (10), STAmount (CURRENCY_ONE, ACCOUNT_ONE, 1)) != (((100ull - 16) << (64 - 8)) | 1000000000000000ull))
        BOOST_FAIL ("STAmount getRate fail 8");

    roundTest (1, 3, 3);
    roundTest (2, 3, 9);
    roundTest (1, 7, 21);
    roundTest (1, 2, 4);
    roundTest (3, 9, 18);
    roundTest (7, 11, 44);

    for (int i = 0; i <= 100000; ++i)
        mulTest (rand () % 10000000, rand () % 10000000);
}

//------------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE ( UnderFlowTests )
{
    using namespace ripple;

    STAmount bigNative (STAmount::cMaxNative / 2);
    STAmount bigValue (CURRENCY_ONE, ACCOUNT_ONE,
                       (STAmount::cMinValue + STAmount::cMaxValue) / 2, STAmount::cMaxOffset - 1);
    STAmount smallValue (CURRENCY_ONE, ACCOUNT_ONE,
                         (STAmount::cMinValue + STAmount::cMaxValue) / 2, STAmount::cMinOffset + 1);
    STAmount zero (CURRENCY_ONE, ACCOUNT_ONE, 0);

    STAmount smallXsmall = STAmount::multiply (smallValue, smallValue, CURRENCY_ONE, ACCOUNT_ONE);

    if (!smallXsmall.isZero ())
        BOOST_FAIL ("STAmount: smallXsmall != 0");

    STAmount bigDsmall = STAmount::divide (smallValue, bigValue, CURRENCY_ONE, ACCOUNT_ONE);

    if (!bigDsmall.isZero ())
        BOOST_FAIL ("STAmount: small/big != 0: " << bigDsmall);

    bigDsmall = STAmount::divide (smallValue, bigNative, CURRENCY_ONE, uint160 ());

    if (!bigDsmall.isZero ())
        BOOST_FAIL ("STAmount: small/bigNative != 0: " << bigDsmall);

    bigDsmall = STAmount::divide (smallValue, bigValue, uint160 (), uint160 ());

    if (!bigDsmall.isZero ())
        BOOST_FAIL ("STAmount: (small/big)->N != 0: " << bigDsmall);

    bigDsmall = STAmount::divide (smallValue, bigNative, uint160 (), uint160 ());

    if (!bigDsmall.isZero ())
        BOOST_FAIL ("STAmount: (small/bigNative)->N != 0: " << bigDsmall);

    // very bad offer
    uint64 r = STAmount::getRate (smallValue, bigValue);

    if (r != 0)
        BOOST_FAIL ("STAmount: getRate(smallOut/bigIn) != 0" << r);

    // very good offer
    r = STAmount::getRate (bigValue, smallValue);

    if (r != 0)
        BOOST_FAIL ("STAmount:: getRate(smallIn/bigOUt) != 0" << r);
}

BOOST_AUTO_TEST_SUITE_END ()

//------------------------------------------------------------------------------

BOOST_AUTO_TEST_SUITE (amountRound)

BOOST_AUTO_TEST_CASE ( amountRound_test )
{
    using namespace ripple;

    uint64 value = 25000000000000000ull;
    int offset = -14;
    STAmount::canonicalizeRound (false, value, offset, true);

    STAmount one (CURRENCY_ONE, ACCOUNT_ONE, 1);
    STAmount two (CURRENCY_ONE, ACCOUNT_ONE, 2);
    STAmount three (CURRENCY_ONE, ACCOUNT_ONE, 3);

    STAmount oneThird1 = STAmount::divRound (one, three, CURRENCY_ONE, ACCOUNT_ONE, false);
    STAmount oneThird2 = STAmount::divide (one, three, CURRENCY_ONE, ACCOUNT_ONE);
    STAmount oneThird3 = STAmount::divRound (one, three, CURRENCY_ONE, ACCOUNT_ONE, true);
    WriteLog (lsINFO, STAmount) << oneThird1;
    WriteLog (lsINFO, STAmount) << oneThird2;
    WriteLog (lsINFO, STAmount) << oneThird3;

    STAmount twoThird1 = STAmount::divRound (two, three, CURRENCY_ONE, ACCOUNT_ONE, false);
    STAmount twoThird2 = STAmount::divide (two, three, CURRENCY_ONE, ACCOUNT_ONE);
    STAmount twoThird3 = STAmount::divRound (two, three, CURRENCY_ONE, ACCOUNT_ONE, true);
    WriteLog (lsINFO, STAmount) << twoThird1;
    WriteLog (lsINFO, STAmount) << twoThird2;
    WriteLog (lsINFO, STAmount) << twoThird3;

    STAmount oneA = STAmount::mulRound (oneThird1, three, CURRENCY_ONE, ACCOUNT_ONE, false);
    STAmount oneB = STAmount::multiply (oneThird2, three, CURRENCY_ONE, ACCOUNT_ONE);
    STAmount oneC = STAmount::mulRound (oneThird3, three, CURRENCY_ONE, ACCOUNT_ONE, true);
    WriteLog (lsINFO, STAmount) << oneA;
    WriteLog (lsINFO, STAmount) << oneB;
    WriteLog (lsINFO, STAmount) << oneC;

    STAmount fourThirdsA = STAmount::addRound (twoThird2, twoThird2, false);
    STAmount fourThirdsB = twoThird2 + twoThird2;
    STAmount fourThirdsC = STAmount::addRound (twoThird2, twoThird2, true);
    WriteLog (lsINFO, STAmount) << fourThirdsA;
    WriteLog (lsINFO, STAmount) << fourThirdsB;
    WriteLog (lsINFO, STAmount) << fourThirdsC;

    STAmount dripTest1 = STAmount::mulRound (twoThird2, two, uint160 (), uint160 (), false);
    STAmount dripTest2 = STAmount::multiply (twoThird2, two, uint160 (), uint160 ());
    STAmount dripTest3 = STAmount::mulRound (twoThird2, two, uint160 (), uint160 (), true);
    WriteLog (lsINFO, STAmount) << dripTest1;
    WriteLog (lsINFO, STAmount) << dripTest2;
    WriteLog (lsINFO, STAmount) << dripTest3;
}

BOOST_AUTO_TEST_SUITE_END ()
