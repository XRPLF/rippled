
#include <boost/test/unit_test.hpp>

#include "SerializedTypes.h"

#if (ULONG_MAX > UINT_MAX)
#define BN_add_word64(bn, word) BN_add_word(bn, word)
#define BN_sub_word64(bn, word) BN_sub_word(bn, word)
#define BN_mul_word64(bn, word) BN_mul_word(bn, word)
#define BN_div_word64(bn, word) BN_div_word(bn, word)
#else
#include "BigNum64.h"
#endif

// CAUTION: This is early code and is *NOT* ready for real use yet.

static void canonicalizeRound(bool isNative, uint64& value, int& offset, bool roundUp)
{
	if (!roundUp) // canonicalize already rounds down
		return;

	WriteLog (lsTRACE, STAmount) << "canonicalize< " << value << ":" << offset << (roundUp ? " up" : " down");
	if (isNative)
	{
		if (offset < 0)
		{
			int loops = 0;
			while (offset < -1)
			{
				value /= 10;
				++offset;
				++loops;
			}
			value += (loops >= 2) ? 9 : 10;	// add before last divide
			value /= 10;
			++offset;
		}
	}
	else if (value > STAmount::cMaxValue)
	{
		while (value > (10 * STAmount::cMaxValue))
		{
			value /= 10;
			++offset;
		}
		value += 9;		// add before last divide
		value /= 10;
		++offset;
	}
	WriteLog (lsTRACE, STAmount) << "canonicalize> " << value << ":" << offset << (roundUp ? " up" : " down");
}

STAmount STAmount::addRound(const STAmount& v1, const STAmount& v2, bool roundUp)
{
	v1.throwComparable(v2);

	if (v2.mValue == 0)
		return v1;

	if (v1.mValue == 0)
		return STAmount(v1.getFName(), v1.mCurrency, v1.mIssuer, v2.mValue, v2.mOffset, v2.mIsNegative);

	if (v1.mIsNative)
		return STAmount(v1.getFName(), v1.getSNValue() + v2.getSNValue());

	int ov1 = v1.mOffset, ov2 = v2.mOffset;
	int64 vv1 = static_cast<int64>(v1.mValue), vv2 = static_cast<uint64>(v2.mValue);
	if (v1.mIsNegative)
		vv1 = -vv1;
	if (v2.mIsNegative)
		vv2 = -vv2;

	if (ov1 < ov2)
	{
		while (ov1 < (ov2 - 1))
		{
			vv1 /= 10;
			++ov1;
		}
		if (roundUp)
			vv1 += 9;
		vv1 /= 10;
		++ov1;
	}

	if (ov2 < ov1)
	{
		while (ov2 < (ov1 - 1))
		{
			vv2 /= 10;
			++ov2;
		}
		if (roundUp)
			vv2 += 9;
		vv2 /= 10;
		++ov2;
	}

	int64 fv = vv1 + vv2;
	if ((fv >= -10) && (fv <= 10))
		return STAmount(v1.getFName(), v1.mCurrency, v1.mIssuer);
	else if (fv >= 0)
	{
		uint64 v = static_cast<uint64>(fv);
		canonicalizeRound(false, v, ov1, roundUp);
		return STAmount(v1.getFName(), v1.mCurrency, v1.mIssuer, v, ov1, false);
	}
	else
	{
		uint64 v = static_cast<uint64>(-fv);
		canonicalizeRound(false, v, ov1, !roundUp);
		return STAmount(v1.getFName(), v1.mCurrency, v1.mIssuer, v, ov1, true);
	}
}

STAmount STAmount::subRound(const STAmount& v1, const STAmount& v2, bool roundUp)
{
	v1.throwComparable(v2);

	if (v2.mValue == 0)
		return v1;

	if (v1.mValue == 0)
		return STAmount(v1.getFName(), v1.mCurrency, v1.mIssuer, v2.mValue, v2.mOffset, !v2.mIsNegative);

	if (v1.mIsNative)
		return STAmount(v1.getFName(), v1.getSNValue() - v2.getSNValue());

	int ov1 = v1.mOffset, ov2 = v2.mOffset;
	int64 vv1 = static_cast<int64>(v1.mValue), vv2 = static_cast<uint64>(v2.mValue);

	if (v1.mIsNegative)
		vv1 = -vv1;

	if (!v2.mIsNegative)
		vv2 = -vv2;

	if (ov1 < ov2)
	{
		while (ov1 < (ov2 - 1))
		{
			vv1 /= 10;
			++ov1;
		}
		if (roundUp)
			vv1 += 9;
		vv1 /= 10;
		++ov1;
	}

	if (ov2 < ov1)
	{
		while (ov2 < (ov1 - 1))
		{
			vv2 /= 10;
			++ov2;
		}
		if (roundUp)
			vv2 += 9;
		vv2 /= 10;
		++ov2;
	}

	int64 fv = vv1 + vv2;
	if ((fv >= -10) && (fv <= 10))
		return STAmount(v1.getFName(), v1.mCurrency, v1.mIssuer);
	else if (fv >= 0)
	{
		uint64 v = static_cast<uint64>(fv);
		canonicalizeRound(false, v, ov1, roundUp);
		return STAmount(v1.getFName(), v1.mCurrency, v1.mIssuer, v, ov1, false);
	}
	else
	{
		uint64 v = static_cast<uint64>(-fv);
		canonicalizeRound(false, v, ov1, !roundUp);
		return STAmount(v1.getFName(), v1.mCurrency, v1.mIssuer, v, ov1, true);
	}
}

STAmount STAmount::mulRound(const STAmount& v1, const STAmount& v2,
	const uint160& uCurrencyID, const uint160& uIssuerID, bool roundUp)
{
	if (v1.isZero() || v2.isZero())
		return STAmount(uCurrencyID, uIssuerID);

	if (v1.mIsNative && v2.mIsNative && uCurrencyID.isZero())
	{
		uint64 minV = (v1.getSNValue() < v2.getSNValue()) ? v1.getSNValue() : v2.getSNValue();
		uint64 maxV = (v1.getSNValue() < v2.getSNValue()) ? v2.getSNValue() : v1.getSNValue();
		if (minV > 3000000000ull) // sqrt(cMaxNative)
			throw std::runtime_error("Native value overflow");
		if (((maxV >> 32) * minV) > 2095475792ull) // cMaxNative / 2^32
			throw std::runtime_error("Native value overflow");
		return STAmount(v1.getFName(), minV * maxV);
	}

	uint64 value1 = v1.mValue, value2 = v2.mValue;
	int offset1 = v1.mOffset, offset2 = v2.mOffset;

	if (v1.mIsNative)
	{
		while (value1 < STAmount::cMinValue)
		{
			value1 *= 10;
			--offset1;
		}
	}

	if (v2.mIsNative)
	{
		while (value2 < STAmount::cMinValue)
		{
			value2 *= 10;
			--offset2;
		}
	}

	bool resultNegative = v1.mIsNegative != v2.mIsNegative;
	// Compute (numerator * denominator) / 10^14 with rounding
	// 10^16 <= result <= 10^18
	CBigNum v;
	if ((BN_add_word64(&v, value1) != 1) || (BN_mul_word64(&v, value2) != 1))
		throw std::runtime_error("internal bn error");

	if (resultNegative != roundUp) // rounding down is automatic when we divide
		BN_add_word64(&v, tenTo14m1);

	if	(BN_div_word64(&v, tenTo14) == ((uint64) -1))
		throw std::runtime_error("internal bn error");

	// 10^16 <= product <= 10^18
	assert(BN_num_bytes(&v) <= 64);

	uint64 amount = v.getuint64();
	int offset = offset1 + offset2 + 14;
	canonicalizeRound(uCurrencyID.isZero(), amount, offset, resultNegative != roundUp);
	return STAmount(uCurrencyID, uIssuerID, amount, offset, resultNegative);
}

STAmount STAmount::divRound(const STAmount& num, const STAmount& den,
	const uint160& uCurrencyID, const uint160& uIssuerID, bool roundUp)
{
	if (den.isZero())
		throw std::runtime_error("division by zero");
	if (num.isZero())
		return STAmount(uCurrencyID, uIssuerID);

	uint64 numVal = num.mValue, denVal = den.mValue;
	int numOffset = num.mOffset, denOffset = den.mOffset;

	if (num.mIsNative)
		while (numVal < STAmount::cMinValue)
		{ // Need to bring into range
			numVal *= 10;
			--numOffset;
		}

	if (den.mIsNative)
		while (denVal < STAmount::cMinValue)
		{
			denVal *= 10;
			--denOffset;
		}

	bool resultNegative = num.mIsNegative != den.mIsNegative;
	// Compute (numerator * 10^17) / denominator
	CBigNum v;
	if ((BN_add_word64(&v, numVal) != 1) || (BN_mul_word64(&v, tenTo17) != 1))
		throw std::runtime_error("internal bn error");

	if (resultNegative != roundUp) // Rounding down is automatic when we divide
		BN_add_word64(&v, denVal - 1);

	if (BN_div_word64(&v, denVal) == ((uint64) -1))
		throw std::runtime_error("internal bn error");

	// 10^16 <= quotient <= 10^18
	assert(BN_num_bytes(&v) <= 64);

	uint64 amount = v.getuint64();
	int offset = numOffset - denOffset - 17;
	canonicalizeRound(uCurrencyID.isZero(), amount, offset, resultNegative != roundUp);
	return STAmount(uCurrencyID, uIssuerID, amount, offset, resultNegative);
}

BOOST_AUTO_TEST_SUITE(amountRound)

BOOST_AUTO_TEST_CASE( amountRound_test )
{
	uint64 value = 25000000000000000ull;
	int offset = -14;
	canonicalizeRound(false, value, offset, true);

	STAmount one(CURRENCY_ONE, ACCOUNT_ONE, 1);
	STAmount two(CURRENCY_ONE, ACCOUNT_ONE, 2);
	STAmount three(CURRENCY_ONE, ACCOUNT_ONE, 3);

	STAmount oneThird1 = STAmount::divRound(one, three, CURRENCY_ONE, ACCOUNT_ONE, false);
	STAmount oneThird2 = STAmount::divide(one, three, CURRENCY_ONE, ACCOUNT_ONE);
	STAmount oneThird3 = STAmount::divRound(one, three, CURRENCY_ONE, ACCOUNT_ONE, true);
	WriteLog (lsINFO, STAmount) << oneThird1;
	WriteLog (lsINFO, STAmount) << oneThird2;
	WriteLog (lsINFO, STAmount) << oneThird3;

	STAmount twoThird1 = STAmount::divRound(two, three, CURRENCY_ONE, ACCOUNT_ONE, false);
	STAmount twoThird2 = STAmount::divide(two, three, CURRENCY_ONE, ACCOUNT_ONE);
	STAmount twoThird3 = STAmount::divRound(two, three, CURRENCY_ONE, ACCOUNT_ONE, true);
	WriteLog (lsINFO, STAmount) << twoThird1;
	WriteLog (lsINFO, STAmount) << twoThird2;
	WriteLog (lsINFO, STAmount) << twoThird3;

	STAmount oneA = STAmount::mulRound(oneThird1, three, CURRENCY_ONE, ACCOUNT_ONE, false);
	STAmount oneB = STAmount::multiply(oneThird2, three, CURRENCY_ONE, ACCOUNT_ONE);
	STAmount oneC = STAmount::mulRound(oneThird3, three, CURRENCY_ONE, ACCOUNT_ONE, true);
	WriteLog (lsINFO, STAmount) << oneA;
	WriteLog (lsINFO, STAmount) << oneB;
	WriteLog (lsINFO, STAmount) << oneC;

	STAmount fourThirdsA = STAmount::addRound(twoThird2, twoThird2, false);
	STAmount fourThirdsB = twoThird2 + twoThird2;
	STAmount fourThirdsC = STAmount::addRound(twoThird2, twoThird2, true);
	WriteLog (lsINFO, STAmount) << fourThirdsA;
	WriteLog (lsINFO, STAmount) << fourThirdsB;
	WriteLog (lsINFO, STAmount) << fourThirdsC;

	STAmount dripTest1 = STAmount::mulRound(twoThird2, two, uint160(), uint160(), false);
	STAmount dripTest2 = STAmount::multiply(twoThird2, two, uint160(), uint160());
	STAmount dripTest3 = STAmount::mulRound(twoThird2, two, uint160(), uint160(), true);
	WriteLog (lsINFO, STAmount) << dripTest1;
	WriteLog (lsINFO, STAmount) << dripTest2;
	WriteLog (lsINFO, STAmount) << dripTest3;
}

BOOST_AUTO_TEST_SUITE_END()

// vim:ts=4
