
#include <cmath>
#include <iomanip>

#include <boost/lexical_cast.hpp>

#include "SerializedTypes.h"

// amount = value * [10 ^ offset]
// representation range is 10^80 - 10^(-80)
// on the wire, high 8 bits are (offset+142), low 56 bits are value
// value is zero if amount is zero, otherwise value is 10^15 - (10^16 - 1) inclusive

void STAmount::canonicalize()
{
	if(value==0)
	{
		offset=0;
		value=0;
		return;
	}
	while(value<cMinValue)
	{
		if(offset<=cMinOffset) throw std::runtime_error("value overflow");
		value*=10;
		offset-=1;
	}
	while(value>cMaxValue)
	{
		if(offset>=cMaxOffset) throw std::runtime_error("value underflow");
		value/=10;
		offset+=1;
	}
	assert( (value==0) || ( (value>=cMinValue) && (value<=cMaxValue) ) );
	assert( (offset>=cMinOffset) && (offset<=cMaxOffset) );
}

STAmount* STAmount::construct(SerializerIterator& sit, const char *name)
{
	uint64 value = sit.get64();

	int offset = static_cast<int>(value >> (64-8));
	value &= ~(255ull << (64-8));

	if(value==0)
	{
		if(offset!=0)
			throw std::runtime_error("invalid currency value");
	}
	else
	{
		offset -= 142; // center the range
		if( (value<cMinValue) || (value>cMaxValue) || (offset<cMinOffset) || (offset>cMaxOffset) )
			throw std::runtime_error("invalid currency value");
	}
	return new STAmount(name, value, offset);
}

std::string STAmount::getText() const
{ // This should be re-implemented to put a '.' in the string form of the integer value
	std::ostringstream str;
	str << std::setprecision(16) << static_cast<double>(*this);
	return str.str();
}

void STAmount::add(Serializer& s) const
{
	if (value==0)
		s.add64(0);
	else
		s.add64(value + (static_cast<uint64>(offset+142) << (64-8))));
}

bool STAmount::isEquivalent(const SerializedType& t) const
{
	const STAmount* v = dynamic_cast<const STAmount*>(&t);
	if(!v) return false;
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
	if(value == 0) return false;
	if(offset < a.offset) return true;
	if(a.offset < offset) return false;
	return value < a.value;
}

bool STAmount::operator>(const STAmount& a) const
{
	if(value == 0) return a.value != 0;
	if(offset > a.offset) return true;
	if(a.offset > offset) return false;
	return value > a.value;
}

bool STAmount::operator<=(const STAmount& a) const
{
	if(value == 0) return a.value== 0;
	if(offset<a.offset) return true;
	if(a.offset<offset) return false;
	return value <= a.value;
}

bool STAmount::operator>=(const STAmount& a) const
{
	if(value == 0) return true;
	if(offset>a.offset) return true;
	if(a.offset>offset) return false;
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
	value=a.value;
	offset=a.offset;
	return *this;
}

STAmount& STAmount::operator=(uint64 v)
{
	return *this=STAmount(v, 0);
}

STAmount& STAmount::operator+=(uint64 v)
{
	return *this+=STAmount(v);
}

STAmount& STAmount::operator-=(uint64 v)
{
	return *this-=STAmount(v);
}

STAmount::operator double() const
{
	if(!value) return 0.0;
	return (static_cast<double>(value)) * pow(10.0, offset);
}

STAmount operator+(STAmount v1, STAmount v2)
{ // We can check for precision loss here (value%10)!=0
	while(v1.offset < v2.offset)
	{
		v1.value/=10;
		v1.offset+=1;
	}
	while(v2.offset < v1.offset)
	{
		v2.value/=10;
		v2.offset+=1;
	}
	// this addition cannot overflow
	return STAmount(v1.name, v1.value + v2.value, v1.offset);
}

STAmount operator-(STAmount v1, STAmount v2)
{ // We can check for precision loss here (value%10)!=0
	while(v1.offset < v2.offset)
	{
		v1.value/=10;
		v1.offset+=1;
	}
	while(v2.offset < v1.offset)
	{
		v2.value/=10;
		v2.offset+=1;
	}
	if(v1.value < v2.value) throw std::runtime_error("value underflow");
	return STAmount(v1.name, v1.value - v2.value, v1.offset);
}

STAmount getRate(const STAmount& offerIn, const STAmount& offerOut)
{
	CBigNum numerator, denominator, quotient;

	if(offerOut.value == 0) throw std::runtime_error("illegal offer");
	if(offerIn.value == 0) return STAmount();

	// Compute (numerator * 10^16) / denominator
	if(	(BN_zero(&numerator) != 1) || (BN_zero(&denominator) != 1) ||
		(BN_add_word(&numerator, offerIn.value) != 1) ||
		(BN_add_word(&denominator, offerOut.value) != 1) ||
		(BN_mul_word(&numerator, 10000000000000000ull) != 1) ||
		(BN_div(&quotient, NULL, &numerator, &denominator, CAutoBN_CTX()) != 1) )
	{
		throw std::runtime_error("internal bn error");
	}

	// 10^15 <= quotient <= 10^17
	assert(BN_num_bytes(&quotient)<=60);

	return STAmount(quotient.getulong(), offerIn.offset - offerOut.offset - 16);
}
