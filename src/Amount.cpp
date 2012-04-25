
#include <cmath>
#include <iomanip>

#include <boost/lexical_cast.hpp>

#include "SerializedTypes.h"

// amount = value * [10 ^ offset]
// representation range is 10^80 - 10^(-80)
// on the wire, high 8 bits are (offset+142), low 56 bits are value
// value is zero if amount is zero, otherwise value is 10^15 to (10^16 - 1) inclusive

void STAmount::canonicalize()
{
	if (value == 0)
	{
		offset = -100;
		return;
	}
	while (value < cMinValue)
	{
		if (offset <= cMinOffset)
			throw std::runtime_error("value overflow");
		value *= 10;
		--offset;
	}
	while (value > cMaxValue)
	{
		if (offset >= cMaxOffset)
			throw std::runtime_error("value underflow");
		value /= 10;
		++offset;
	}
	assert( (value == 0) || ( (value >= cMinValue) && (value <= cMaxValue) ) );
	assert( (value == 0) || ( (offset >= cMinOffset) && (offset <= cMaxOffset) ) );
	assert( (value != 0) || (offset != -100) );
}

STAmount* STAmount::construct(SerializerIterator& sit, const char *name)
{
	uint64 value = sit.get64();

	int offset = static_cast<int>(value >> (64-8));
	value &= ~(255ull << (64-8));

	if (value == 0)
	{
		if (offset != 0)
			throw std::runtime_error("invalid currency value");
	}
	else
	{
		offset -= 142; // center the range
		if ( (value < cMinValue) || (value > cMaxValue) || (offset < cMinOffset) || (offset > cMaxOffset) )
			throw std::runtime_error("invalid currency value");
	}
	return new STAmount(name, value, offset);
}

std::string STAmount::getRaw() const
{ // show raw internal form
	if (isZero()) return "0";
	return boost::lexical_cast<std::string>(value) + "e" + boost::lexical_cast<std::string>(offset);
}

std::string STAmount::getText() const
{ // keep full internal accuracy, but make more human friendly if posible
	if (isZero()) return "0";
	if ( (offset < -25) || (offset > -5) )
		return boost::lexical_cast<std::string>(value) + "e" + boost::lexical_cast<std::string>(offset);

	std::string val = "000000000000000000000000000";
	val += boost::lexical_cast<std::string>(value);
	val += "00000000000000000000000";

	std::string pre = val.substr(0, offset+43);
	std::string post = val.substr(offset+43);

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

void STAmount::add(Serializer& s) const
{
	if (isZero())
		s.add64(0);
	else
		s.add64(value + (static_cast<uint64>(offset + 142) << (64 - 8)));
}

bool STAmount::isEquivalent(const SerializedType& t) const
{
	const STAmount* v = dynamic_cast<const STAmount*>(&t);
	if (!v) return false;
	return (value == v->value) && (offset == v->offset);
}

bool STAmount::operator==(const STAmount& a) const
{
	return (offset == a.offset) && (value == a.value);
}

bool STAmount::operator!=(const STAmount& a) const
{
	return (offset != a.offset) || (value != a.value);
}

bool STAmount::operator<(const STAmount& a) const
{
	if (offset < a.offset) return true;
	if (a.offset < offset) return false;
	return value < a.value;
}

bool STAmount::operator>(const STAmount& a) const
{
	if (offset > a.offset) return true;
	if (a.offset > offset) return false;
	return value > a.value;
}

bool STAmount::operator<=(const STAmount& a) const
{
	if (offset < a.offset) return true;
	if (a.offset < offset) return false;
	return value <= a.value;
}

bool STAmount::operator>=(const STAmount& a) const
{
	if (offset > a.offset) return true;
	if (a.offset > offset) return false;
	return value >= a.value;
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
	value = a.value;
	offset = a.offset;
	return *this;
}

STAmount& STAmount::operator=(uint64 v)
{ // does not copy name
	return *this = STAmount(v, 0);
}

STAmount& STAmount::operator+=(uint64 v)
{
	return *this += STAmount(v);
}

STAmount& STAmount::operator-=(uint64 v)
{
	return *this -= STAmount(v);
}

STAmount::operator double() const
{ // Does not keep the precise value. Not recommended
	if (!value)
		return 0.0;
	return static_cast<double>(value) * pow(10.0, offset);
}

STAmount operator+(STAmount v1, STAmount v2)
{ // We can check for precision loss here (value%10)!=0
	if (v1.isZero()) return v2;
	if (v2.isZero()) return v1;
	while (v1.offset < v2.offset)
	{
		v1.value /= 10;
		++v1.offset;
	}
	while (v2.offset < v1.offset)
	{
		v2.value /= 10;
		++v2.offset;
	}
	// this addition cannot overflow
	return STAmount(v1.name, v1.value + v2.value, v1.offset);
}

STAmount operator-(STAmount v1, STAmount v2)
{
	if (v2.isZero()) return v1;
	if ( v1.isZero() || (v2.offset > v1.offset) )
		throw std::runtime_error("value underflow");

	while (v1.offset > v2.offset)
	{
		v2.value /= 10;
		++v2.offset;
	}
	if (v1.value < v2.value)
		throw std::runtime_error("value underflow");

	return STAmount(v1.name, v1.value - v2.value, v1.offset);
}

STAmount operator/(const STAmount& num, const STAmount& den)
{
	if (den.isZero()) throw std::runtime_error("division by zero");
	if (num.isZero()) return STAmount();

	// Compute (numerator * 10^16) / denominator
	CBigNum v;
	if ((BN_add_word(&v, num.value) != 1) ||
		(BN_mul_word(&v, 10000000000000000ull) != 1) ||
		(BN_div_word(&v, den.value) == ((BN_ULONG)-1)))
	{
		throw std::runtime_error("internal bn error");
	}

	// 10^15 <= quotient <= 10^17
	assert(BN_num_bytes(&v) <= 60);

	return STAmount(v.getulong(), num.offset - den.offset - 16);
}

STAmount operator*(const STAmount &v1, const STAmount &v2)
{
	if (v1.isZero() || v2.isZero())
		return STAmount();

	// Compute (numerator*10 * denominator*10) / 10^18 with rounding
	CBigNum v;
	if ((BN_add_word(&v, (v1.value*10) + 3) != 1) ||
		(BN_mul_word(&v, (v2.value*10) + 3) != 1) ||
		(BN_div_word(&v, 1000000000000000000ull) == ((BN_ULONG)-1)))
	{
		throw std::runtime_error("internal bn error");
	}

	// 10^16 <= product <= 10^18
	assert(BN_num_bytes(&v) <= 60);

	return STAmount(v.getulong(), v1.offset + v2.offset + 16);
}

STAmount getRate(const STAmount& offerOut, const STAmount& offerIn)
{
	// offerOut = how much comes out of the offer, from the offeror to the taker
	// offerIn = how much goes into the offer, from the taker to the offeror
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

STAmount convertToDisplayAmount(const STAmount& internalAmount, const STAmount& totalNow, const STAmount& totalInit)
{ // Convert an internal ledger/account quantity of native currency to a display amount
	return (internalAmount * totalInit) / totalNow;
}

STAmount convertToInternalAmount(const STAmount& displayAmount, const STAmount& totalNow, const STAmount& totalInit)
{ // Convert a display/request currency amount to an internal amount
	return (displayAmount * totalNow) / totalInit;
}

void STAmount::unitTest()
{
	STAmount zero, one(1), hundred(100);

	if (!zero.isZero()) throw std::runtime_error("STAmount fail");
	if (one.isZero()) throw std::runtime_error("STAmount fail");
	if (hundred.isZero()) throw std::runtime_error("STAmount fail");
	if ((zero < zero)) throw std::runtime_error("STAmount fail");
	if (!(zero < one)) throw std::runtime_error("STAmount fail");
	if (!(zero < hundred)) throw std::runtime_error("STAmount fail");
	if ((one < zero)) throw std::runtime_error("STAmount fail");
	if ((one < one)) throw std::runtime_error("STAmount fail");
	if (!(one < hundred)) throw std::runtime_error("STAmount fail");
	if ((hundred < zero)) throw std::runtime_error("STAmount fail");
	if ((hundred < one)) throw std::runtime_error("STAmount fail");
	if ((hundred < hundred)) throw std::runtime_error("STAmount fail");
	if ((zero > zero)) throw std::runtime_error("STAmount fail");
	if ((zero > one)) throw std::runtime_error("STAmount fail");
	if ((zero > hundred)) throw std::runtime_error("STAmount fail");
	if (!(one > zero)) throw std::runtime_error("STAmount fail");
	if ((one > one)) throw std::runtime_error("STAmount fail");
	if ((one > hundred)) throw std::runtime_error("STAmount fail");
	if (!(hundred > zero)) throw std::runtime_error("STAmount fail");
	if (!(hundred > one)) throw std::runtime_error("STAmount fail");
	if ((hundred > hundred)) throw std::runtime_error("STAmount fail");
	if (!(zero <= zero)) throw std::runtime_error("STAmount fail");
	if (!(zero <= one)) throw std::runtime_error("STAmount fail");
	if (!(zero <= hundred)) throw std::runtime_error("STAmount fail");
	if ((one <= zero)) throw std::runtime_error("STAmount fail");
	if (!(one <= one)) throw std::runtime_error("STAmount fail");
	if (!(one <= hundred)) throw std::runtime_error("STAmount fail");
	if ((hundred <= zero)) throw std::runtime_error("STAmount fail");
	if ((hundred <= one)) throw std::runtime_error("STAmount fail");
	if (!(hundred <= hundred)) throw std::runtime_error("STAmount fail");
	if (!(zero >= zero)) throw std::runtime_error("STAmount fail");
	if ((zero >= one)) throw std::runtime_error("STAmount fail");
	if ((zero >= hundred)) throw std::runtime_error("STAmount fail");
	if (!(one >= zero)) throw std::runtime_error("STAmount fail");
	if (!(one >= one)) throw std::runtime_error("STAmount fail");
	if ((one >= hundred)) throw std::runtime_error("STAmount fail");
	if (!(hundred >= zero)) throw std::runtime_error("STAmount fail");
	if (!(hundred >= one)) throw std::runtime_error("STAmount fail");
	if (!(hundred >= hundred)) throw std::runtime_error("STAmount fail");
	if (!(zero == zero)) throw std::runtime_error("STAmount fail");
	if ((zero == one)) throw std::runtime_error("STAmount fail");
	if ((zero == hundred)) throw std::runtime_error("STAmount fail");
	if ((one == zero)) throw std::runtime_error("STAmount fail");
	if (!(one == one)) throw std::runtime_error("STAmount fail");
	if ((one == hundred)) throw std::runtime_error("STAmount fail");
	if ((hundred == zero)) throw std::runtime_error("STAmount fail");
	if ((hundred == one)) throw std::runtime_error("STAmount fail");
	if (!(hundred == hundred)) throw std::runtime_error("STAmount fail");
	if ((zero != zero)) throw std::runtime_error("STAmount fail");
	if (!(zero != one)) throw std::runtime_error("STAmount fail");
	if (!(zero != hundred)) throw std::runtime_error("STAmount fail");
	if (!(one != zero)) throw std::runtime_error("STAmount fail");
	if ((one != one)) throw std::runtime_error("STAmount fail");
	if (!(one != hundred)) throw std::runtime_error("STAmount fail");
	if (!(hundred != zero)) throw std::runtime_error("STAmount fail");
	if (!(hundred != one)) throw std::runtime_error("STAmount fail");
	if ((hundred != hundred)) throw std::runtime_error("STAmount fail");
	if (STAmount().getText() != "0") throw std::runtime_error("STAmount fail");
	if (STAmount(31).getText() != "31") throw std::runtime_error("STAmount fail");
	if (STAmount(31,1).getText() != "310") throw std::runtime_error("STAmount fail");
	if (STAmount(31,-1).getText() != "3.1") throw std::runtime_error("STAmount fail");
	if (STAmount(31,-2).getText() != "0.31") throw std::runtime_error("STAmount fail");
}
