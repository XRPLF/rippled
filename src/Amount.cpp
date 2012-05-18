
#include <cmath>
#include <iomanip>

#include <boost/lexical_cast.hpp>
#include <boost/test/included/unit_test.hpp>

#include "SerializedTypes.h"

// amount = value * [10 ^ offset]
// representation range is 10^80 - 10^(-80)
// on the wire, high 8 bits are (offset+142), low 56 bits are value
// value is zero if amount is zero, otherwise value is 10^15 to (10^16 - 1) inclusive

void STAmount::canonicalize()
{
	if (!mCurrency)
	{
		mIsNative = true;

		if (mValue == 0)
		{
			mOffset = 0;
			return;
		}

		while (mOffset < 0)
		{
			mValue /= 10;
			++mOffset;
		}

		while (mOffset > 0)
		{
			mValue *= 10;
			--mOffset;
		}

		assert(mValue <= cMaxNative);
		return;
	}

	mIsNative = false;
	if (mValue == 0)
	{
		mOffset = -100;
		return;
	}

	while (mValue < cMinValue)
	{
		if (mOffset <= cMinOffset)
			throw std::runtime_error("value overflow");
		mValue *= 10;
		--mOffset;
	}

	while (mValue > cMaxValue)
	{
		if (mOffset >= cMaxOffset)
			throw std::runtime_error("value underflow");
		mValue /= 10;
		++mOffset;
	}
	assert((mValue == 0) || ((mValue >= cMinValue) && (mValue <= cMaxValue)) );
	assert((mValue == 0) || ((mOffset >= cMinOffset) && (mOffset <= cMaxOffset)) );
	assert((mValue != 0) || (mOffset != -100) );
}

void STAmount::add(Serializer& s) const
{
	if (mIsNative)
	{
		assert(mOffset == 0);
		s.add64(mValue);
		return;
	}
	if (isZero())
		s.add64(cNotNative);
	else
		s.add64(mValue + (static_cast<uint64>(mOffset + 396) << (64 - 9)));
	s.add160(mCurrency);
}

STAmount* STAmount::construct(SerializerIterator& sit, const char *name)
{
	uint64 value = sit.get64();

	if ((value & cNotNative) == 0)
		return new STAmount(name, value);

	uint160 currency = sit.get160();
	if (!currency)
		throw std::runtime_error("invalid native currency");

	int offset = static_cast<int>(value >> (64-9));
	value &= ~(1023ull << (64-9));

	if (value == 0)
	{
		if (offset != 256)
			throw std::runtime_error("invalid currency value");
	}
	else
	{
		offset -= 396; // center the range and set the "not native" bit
		if ((value < cMinValue) || (value > cMaxValue) || (offset < cMinOffset) || (offset > cMaxOffset))
			throw std::runtime_error("invalid currency value");
	}
	return new STAmount(name, currency, value, offset);
}

std::string STAmount::getRaw() const
{ // show raw internal form
	if (mValue == 0) return "0";
	if (mIsNative) return boost::lexical_cast<std::string>(mValue);
	return mCurrency.GetHex() + ": " +
		boost::lexical_cast<std::string>(mValue) + "e" + boost::lexical_cast<std::string>(mOffset);
}

std::string STAmount::getText() const
{ // keep full internal accuracy, but make more human friendly if posible
	if (isZero()) return "0";
	if (mIsNative)
		return boost::lexical_cast<std::string>(mValue);
	if ((mOffset < -25) || (mOffset > -5))
		return boost::lexical_cast<std::string>(mValue) + "e" + boost::lexical_cast<std::string>(mOffset);

	std::string val = "000000000000000000000000000";
	val += boost::lexical_cast<std::string>(mValue);
	val += "00000000000000000000000";

	std::string pre = val.substr(0, mOffset + 43);
	std::string post = val.substr(mOffset + 43);

	size_t s_pre = pre.find_first_not_of('0');
	if (s_pre == std::string::npos)
		pre="0";
	else
		pre = pre.substr(s_pre);

	size_t s_post = post.find_last_not_of('0');
	if (s_post == std::string::npos)
		return pre;
	else
		return pre + "." + post.substr(0, s_post+1);
}

bool STAmount::isComparable(const STAmount& t) const
{
	if (mIsNative) return t.mIsNative;
	if (t.mIsNative) return false;
	return mCurrency == t.mCurrency;
}

bool STAmount::isEquivalent(const SerializedType& t) const
{
	const STAmount* v = dynamic_cast<const STAmount*>(&t);
	if (!v) return false;
	return isComparable(*v) && (mValue == v->mValue) && (mOffset == v->mOffset);
}

void STAmount::throwComparable(const STAmount& t) const
{
	if (!isComparable(t))
		throw std::runtime_error("amounts are not comparable");
}

bool STAmount::operator==(const STAmount& a) const
{
	return isComparable(a) && (mOffset == a.mOffset) && (mValue == a.mValue);
}

bool STAmount::operator!=(const STAmount& a) const
{
	return (mOffset != a.mOffset) || (mValue != a.mValue) || !isComparable(a);
}

bool STAmount::operator<(const STAmount& a) const
{
	throwComparable(a);
	if (mOffset < a.mOffset) return true;
	if (a.mOffset < mOffset) return false;
	return mValue < a.mValue;
}

bool STAmount::operator>(const STAmount& a) const
{
	throwComparable(a);
	if (mOffset > a.mOffset) return true;
	if (a.mOffset > mOffset) return false;
	return mValue > a.mValue;
}

bool STAmount::operator<=(const STAmount& a) const
{
	throwComparable(a);
	if (mOffset < a.mOffset) return true;
	if (a.mOffset < mOffset) return false;
	return mValue <= a.mValue;
}

bool STAmount::operator>=(const STAmount& a) const
{
	throwComparable(a);
	if (mOffset > a.mOffset) return true;
	if (a.mOffset > mOffset) return false;
	return mValue >= a.mValue;
}

STAmount& STAmount::operator+=(const STAmount& a)
{
	*this = *this + a;
	return *this;
}

STAmount& STAmount::operator-=(const STAmount& a)
{
	*this = *this - a;
	return *this;
}

STAmount& STAmount::operator=(const STAmount& a)
{
	mValue = a.mValue;
	mOffset = a.mOffset;
	mCurrency = a.mCurrency;
	mIsNative = a.mIsNative;
	return *this;
}

STAmount& STAmount::operator=(uint64 v)
{ // does not copy name
	mOffset = 0;
	mValue = v;
	if (!mIsNative) canonicalize();
	return *this;
}

STAmount& STAmount::operator+=(uint64 v)
{
	if (mIsNative) mValue += v;
	*this += STAmount(mCurrency, v);
	return *this;
}

STAmount& STAmount::operator-=(uint64 v)
{
	if (mIsNative)
	{
		if (v > mValue)
			throw std::runtime_error("amount underflow");
		mValue -= v;
	}
	*this -= STAmount(mCurrency, v);
	return *this;
}

STAmount::operator double() const
{ // Does not keep the precise value. Not recommended
	if (!mValue)
		return 0.0;
	return static_cast<double>(mValue) * pow(10.0, mOffset);
}

STAmount operator+(STAmount v1, STAmount v2)
{ // We can check for precision loss here (value%10)!=0
	v1.throwComparable(v2);
	if (v1.mIsNative)
		return STAmount(v1.mValue + v2.mValue);

	if (v1.isZero()) return v2;
	if (v2.isZero()) return v1;
	while (v1.mOffset < v2.mOffset)
	{
		v1.mValue /= 10;
		++v1.mOffset;
	}
	while (v2.mOffset < v1.mOffset)
	{
		v2.mValue /= 10;
		++v2.mOffset;
	}
	// this addition cannot overflow
	return STAmount(v1.name, v1.mCurrency, v1.mValue + v2.mValue, v1.mOffset);
}

STAmount operator-(STAmount v1, STAmount v2)
{
	v1.throwComparable(v2);
	if (v2.mIsNative)
	{
		if (v2.mValue > v1.mValue)
			throw std::runtime_error("amount underflow");
		return STAmount(v1.mValue - v2.mValue);
	}
	if (v2.isZero()) return v1;
	if ( v1.isZero() || (v2.mOffset > v1.mOffset) )
		throw std::runtime_error("value underflow");

	while (v1.mOffset > v2.mOffset)
	{
		v2.mValue /= 10;
		++v2.mOffset;
	}
	if (v1.mValue < v2.mValue)
		throw std::runtime_error("value underflow");

	return STAmount(v1.name, v1.mCurrency, v1.mValue - v2.mValue, v1.mOffset);
}

STAmount divide(const STAmount& num, const STAmount& den, const uint160& currencyOut)
{
	if (den.isZero()) throw std::runtime_error("division by zero");
	if (num.isZero()) return STAmount(currencyOut);

	uint64 numVal = num.mValue, denVal = den.mValue;
	int numOffset = num.mOffset, denOffset = den.mOffset;

	if (num.mIsNative)
		while (numVal < STAmount::cMinValue)
		{
			numVal *= 10;
			--numOffset;
		}

	if (den.mIsNative)
		while (denVal < STAmount::cMinValue)
		{
			denVal *= 10;
			--denOffset;
		}

	// Compute (numerator * 10^16) / denominator
	CBigNum v;
	if ((BN_add_word(&v, numVal) != 1) ||
		(BN_mul_word(&v, 10000000000000000ull) != 1) ||
		(BN_div_word(&v, denVal) == ((BN_ULONG) - 1)))
	{
		throw std::runtime_error("internal bn error");
	}

	// 10^15 <= quotient <= 10^17
	assert(BN_num_bytes(&v) <= 64);

	return STAmount(currencyOut, v.getulong(), numOffset - denOffset - 16);
}

STAmount multiply(const STAmount& v1, const STAmount& v2, const uint160& currencyOut)
{
	if (v1.isZero() || v2.isZero())
		return STAmount(currencyOut);

	if (v1.mIsNative && v2.mIsNative)
		return STAmount(currencyOut, v1.mValue * v2.mValue);

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
	else
	{
		value1 *= 10;
		value1 += 3;
		--offset1;
	}

	if (v2.mIsNative)
	{
		while (value2 < STAmount::cMinValue)
		{
			value2 *= 10;
			--offset2;
		}
	}
	else
	{
		value2 *= 10;
		value2 += 3;
		--offset2;
	}

	// Compute (numerator*10 * denominator*10) / 10^18 with rounding
	CBigNum v;
	if ((BN_add_word(&v, value1) != 1) ||
		(BN_mul_word(&v, value2) != 1) ||
		(BN_div_word(&v, 1000000000000000000ull) == ((BN_ULONG) - 1)))
	{
		throw std::runtime_error("internal bn error");
	}

	// 10^16 <= product <= 10^18
	assert(BN_num_bytes(&v) <= 64);

	return STAmount(currencyOut, v.getulong(), offset1 + offset2 + 14);
}

uint64 getRate(const STAmount& offerOut, const STAmount& offerIn)
{
	// offerOut = how much comes out of the offer, from the offeror to the taker
	// offerIn = how much goes into the offer, from the taker to the offeror
	// FIXME
	return offerOut / offerIn;
}

STAmount getClaimed(STAmount& offerOut, STAmount& offerIn, STAmount& paid)
{ // if someone is offering (offerOut) for (offerIn), and I pay (paid), how much do I get?

	if (offerIn.isZero() || offerOut.isZero())
	{ // If the offer is invalid or empty, you pay nothing and get nothing and the offer is dead
		offerIn.zero();
		offerOut.zero();
		paid.zero();
		return STAmount();
	}

	// If you pay nothing, you get nothing. Offer is untouched
	if (paid.isZero()) return STAmount();

	if (paid >= offerIn)
	{ // If you pay equal to or more than the offer amount, you get the whole offer and pay its input
		STAmount ret(offerOut);
		paid = offerIn;
		offerOut.zero();
		offerIn.zero();
		return ret;
	}

	// partial satisfaction of a normal offer
	STAmount ret = (paid * offerOut) / offerIn;
	offerOut -= ret;
	offerIn -= paid;
	if (offerOut.isZero() || offerIn.isZero())
	{
		offerIn.zero();
		offerOut.zero();
	}
	return ret;
}

STAmount getNeeded(const STAmount& offerOut, const STAmount& offerIn, const STAmount& needed)
{ // Someone wants to get (needed) out of the offer, how much should they pay in?
	if (offerOut.isZero()) return STAmount();
	if (needed >= offerOut) return needed;
	STAmount ret = (needed * offerIn) / offerOut;
	return (ret > offerIn) ? offerIn : ret;
}

static uint64_t muldiv(uint64_t a, uint64_t b, uint64_t c)
{ // computes (a*b)/c rounding up - supports values up to 10^18
	if (c == 0) throw std::runtime_error("underflow");
	if ((a == 0) || (b == 0)) return 0;

	CBigNum v;
	if ((BN_add_word(&v, a * 10 + 3) != 1) || (BN_mul_word(&v, b * 10 + 3) != 1) ||
		(BN_div_word(&v, c) == ((BN_ULONG) - 1)) || (BN_div_word(&v, 100) == ((BN_ULONG) - 1)))
		throw std::runtime_error("muldiv error");

	return v.getulong();
}

uint64 convertToDisplayAmount(const STAmount& internalAmount, uint64_t totalNow, uint64_t totalInit)
{ // Convert an internal ledger/account quantity of native currency to a display amount
	if (internalAmount.isNative()) throw std::runtime_error("not native curency");
	return muldiv(internalAmount.getValue(), totalInit, totalNow);
}

STAmount convertToInternalAmount(uint64_t displayAmount, uint64_t totalNow, uint64_t totalInit,
	const char *name)
{ // Convert a display/request currency amount to an internal amount
	return STAmount(name, muldiv(displayAmount, totalNow, totalInit));
}

STAmount STAmount::deSerialize(SerializerIterator& it)
{
	STAmount *s = construct(it);
	STAmount ret(*s);
	delete s;
	return ret;
}

static STAmount serdes(const STAmount &s)
{
	Serializer ser;
	s.add(ser);
	SerializerIterator sit(ser);
	return STAmount::deSerialize(sit);
}


BOOST_AUTO_TEST_SUITE(amount)

BOOST_AUTO_TEST_CASE( NativeCurrency_test )
{
	STAmount zero, one(1), hundred(100);

	if (serdes(zero) != zero) BOOST_FAIL("STAmount fail");
	if (serdes(one) != one) BOOST_FAIL("STAmount fail");
	if (serdes(hundred) != hundred) BOOST_FAIL("STAmount fail");

	if (!zero.isNative()) BOOST_FAIL("STAmount fail");
	if (!hundred.isNative()) BOOST_FAIL("STAmount fail");
	if (!zero.isZero()) BOOST_FAIL("STAmount fail");
	if (one.isZero()) BOOST_FAIL("STAmount fail");
	if (hundred.isZero()) BOOST_FAIL("STAmount fail");
	if ((zero < zero)) BOOST_FAIL("STAmount fail");
	if (!(zero < one)) BOOST_FAIL("STAmount fail");
	if (!(zero < hundred)) BOOST_FAIL("STAmount fail");
	if ((one < zero)) BOOST_FAIL("STAmount fail");
	if ((one < one)) BOOST_FAIL("STAmount fail");
	if (!(one < hundred)) BOOST_FAIL("STAmount fail");
	if ((hundred < zero)) BOOST_FAIL("STAmount fail");
	if ((hundred < one)) BOOST_FAIL("STAmount fail");
	if ((hundred < hundred)) BOOST_FAIL("STAmount fail");
	if ((zero > zero)) BOOST_FAIL("STAmount fail");
	if ((zero > one)) BOOST_FAIL("STAmount fail");
	if ((zero > hundred)) BOOST_FAIL("STAmount fail");
	if (!(one > zero)) BOOST_FAIL("STAmount fail");
	if ((one > one)) BOOST_FAIL("STAmount fail");
	if ((one > hundred)) BOOST_FAIL("STAmount fail");
	if (!(hundred > zero)) BOOST_FAIL("STAmount fail");
	if (!(hundred > one)) BOOST_FAIL("STAmount fail");
	if ((hundred > hundred)) BOOST_FAIL("STAmount fail");
	if (!(zero <= zero)) BOOST_FAIL("STAmount fail");
	if (!(zero <= one)) BOOST_FAIL("STAmount fail");
	if (!(zero <= hundred)) BOOST_FAIL("STAmount fail");
	if ((one <= zero)) BOOST_FAIL("STAmount fail");
	if (!(one <= one)) BOOST_FAIL("STAmount fail");
	if (!(one <= hundred)) BOOST_FAIL("STAmount fail");
	if ((hundred <= zero)) BOOST_FAIL("STAmount fail");
	if ((hundred <= one)) BOOST_FAIL("STAmount fail");
	if (!(hundred <= hundred)) BOOST_FAIL("STAmount fail");
	if (!(zero >= zero)) BOOST_FAIL("STAmount fail");
	if ((zero >= one)) BOOST_FAIL("STAmount fail");
	if ((zero >= hundred)) BOOST_FAIL("STAmount fail");
	if (!(one >= zero)) BOOST_FAIL("STAmount fail");
	if (!(one >= one)) BOOST_FAIL("STAmount fail");
	if ((one >= hundred)) BOOST_FAIL("STAmount fail");
	if (!(hundred >= zero)) BOOST_FAIL("STAmount fail");
	if (!(hundred >= one)) BOOST_FAIL("STAmount fail");
	if (!(hundred >= hundred)) BOOST_FAIL("STAmount fail");
	if (!(zero == zero)) BOOST_FAIL("STAmount fail");
	if ((zero == one)) BOOST_FAIL("STAmount fail");
	if ((zero == hundred)) BOOST_FAIL("STAmount fail");
	if ((one == zero)) BOOST_FAIL("STAmount fail");
	if (!(one == one)) BOOST_FAIL("STAmount fail");
	if ((one == hundred)) BOOST_FAIL("STAmount fail");
	if ((hundred == zero)) BOOST_FAIL("STAmount fail");
	if ((hundred == one)) BOOST_FAIL("STAmount fail");
	if (!(hundred == hundred)) BOOST_FAIL("STAmount fail");
	if ((zero != zero)) BOOST_FAIL("STAmount fail");
	if (!(zero != one)) BOOST_FAIL("STAmount fail");
	if (!(zero != hundred)) BOOST_FAIL("STAmount fail");
	if (!(one != zero)) BOOST_FAIL("STAmount fail");
	if ((one != one)) BOOST_FAIL("STAmount fail");
	if (!(one != hundred)) BOOST_FAIL("STAmount fail");
	if (!(hundred != zero)) BOOST_FAIL("STAmount fail");
	if (!(hundred != one)) BOOST_FAIL("STAmount fail");
	if ((hundred != hundred)) BOOST_FAIL("STAmount fail");
	if (STAmount().getText() != "0") BOOST_FAIL("STAmount fail");
	if (STAmount(31).getText() != "31")	BOOST_FAIL("STAmount fail");
	if (STAmount(310).getText() != "310") BOOST_FAIL("STAmount fail");
}

BOOST_AUTO_TEST_CASE( CustomCurrency_test )
{
	uint160 currency(1);
	STAmount zero(currency), one(currency, 1), hundred(currency, 100);

	if (serdes(zero) != zero) BOOST_FAIL("STAmount fail");
	if (serdes(one) != one) BOOST_FAIL("STAmount fail");
	if (serdes(hundred) != hundred) BOOST_FAIL("STAmount fail");

	if (zero.isNative()) BOOST_FAIL("STAmount fail");
	if (hundred.isNative()) BOOST_FAIL("STAmount fail");
	if (!zero.isZero()) BOOST_FAIL("STAmount fail");
	if (one.isZero()) BOOST_FAIL("STAmount fail");
	if (hundred.isZero()) BOOST_FAIL("STAmount fail");
	if ((zero < zero)) BOOST_FAIL("STAmount fail");
	if (!(zero < one)) BOOST_FAIL("STAmount fail");
	if (!(zero < hundred)) BOOST_FAIL("STAmount fail");
	if ((one < zero)) BOOST_FAIL("STAmount fail");
	if ((one < one)) BOOST_FAIL("STAmount fail");
	if (!(one < hundred)) BOOST_FAIL("STAmount fail");
	if ((hundred < zero)) BOOST_FAIL("STAmount fail");
	if ((hundred < one)) BOOST_FAIL("STAmount fail");
	if ((hundred < hundred)) BOOST_FAIL("STAmount fail");
	if ((zero > zero)) BOOST_FAIL("STAmount fail");
	if ((zero > one)) BOOST_FAIL("STAmount fail");
	if ((zero > hundred)) BOOST_FAIL("STAmount fail");
	if (!(one > zero)) BOOST_FAIL("STAmount fail");
	if ((one > one)) BOOST_FAIL("STAmount fail");
	if ((one > hundred)) BOOST_FAIL("STAmount fail");
	if (!(hundred > zero)) BOOST_FAIL("STAmount fail");
	if (!(hundred > one)) BOOST_FAIL("STAmount fail");
	if ((hundred > hundred)) BOOST_FAIL("STAmount fail");
	if (!(zero <= zero)) BOOST_FAIL("STAmount fail");
	if (!(zero <= one)) BOOST_FAIL("STAmount fail");
	if (!(zero <= hundred)) BOOST_FAIL("STAmount fail");
	if ((one <= zero)) BOOST_FAIL("STAmount fail");
	if (!(one <= one)) BOOST_FAIL("STAmount fail");
	if (!(one <= hundred)) BOOST_FAIL("STAmount fail");
	if ((hundred <= zero)) BOOST_FAIL("STAmount fail");
	if ((hundred <= one)) BOOST_FAIL("STAmount fail");
	if (!(hundred <= hundred)) BOOST_FAIL("STAmount fail");
	if (!(zero >= zero)) BOOST_FAIL("STAmount fail");
	if ((zero >= one)) BOOST_FAIL("STAmount fail");
	if ((zero >= hundred)) BOOST_FAIL("STAmount fail");
	if (!(one >= zero)) BOOST_FAIL("STAmount fail");
	if (!(one >= one)) BOOST_FAIL("STAmount fail");
	if ((one >= hundred)) BOOST_FAIL("STAmount fail");
	if (!(hundred >= zero)) BOOST_FAIL("STAmount fail");
	if (!(hundred >= one)) BOOST_FAIL("STAmount fail");
	if (!(hundred >= hundred)) BOOST_FAIL("STAmount fail");
	if (!(zero == zero)) BOOST_FAIL("STAmount fail");
	if ((zero == one)) BOOST_FAIL("STAmount fail");
	if ((zero == hundred)) BOOST_FAIL("STAmount fail");
	if ((one == zero)) BOOST_FAIL("STAmount fail");
	if (!(one == one)) BOOST_FAIL("STAmount fail");
	if ((one == hundred)) BOOST_FAIL("STAmount fail");
	if ((hundred == zero)) BOOST_FAIL("STAmount fail");
	if ((hundred == one)) BOOST_FAIL("STAmount fail");
	if (!(hundred == hundred)) BOOST_FAIL("STAmount fail");
	if ((zero != zero)) BOOST_FAIL("STAmount fail");
	if (!(zero != one)) BOOST_FAIL("STAmount fail");
	if (!(zero != hundred)) BOOST_FAIL("STAmount fail");
	if (!(one != zero)) BOOST_FAIL("STAmount fail");
	if ((one != one)) BOOST_FAIL("STAmount fail");
	if (!(one != hundred)) BOOST_FAIL("STAmount fail");
	if (!(hundred != zero)) BOOST_FAIL("STAmount fail");
	if (!(hundred != one)) BOOST_FAIL("STAmount fail");
	if ((hundred != hundred)) BOOST_FAIL("STAmount fail");
	if (STAmount(currency).getText() != "0") BOOST_FAIL("STAmount fail");
	if (STAmount(currency,31).getText() != "31") BOOST_FAIL("STAmount fail");
	if (STAmount(currency,31,1).getText() != "310") BOOST_FAIL("STAmount fail");
	if (STAmount(currency,31,-1).getText() != "3.1") BOOST_FAIL("STAmount fail");
	if (STAmount(currency,31,-2).getText() != "0.31") BOOST_FAIL("STAmount fail");
}

BOOST_AUTO_TEST_SUITE_END()

// vim:ts=4
