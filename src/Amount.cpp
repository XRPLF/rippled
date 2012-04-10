
#include <cmath>

#include <boost/lexical_cast.hpp>

#include "SerializedTypes.h"

void STAmount::canonicalize()
{
	if(value==0)
	{
		offset=0;
		value=0;
	}
	while(value<cMinValue)
	{
		if(offset<=cMinOffset) throw std::runtime_error("value overflow");
		value*=10;
		offset-=1;
	}
	while(value>cMaxValue)
	{ // Here we can make it throw on precision loss if we wish: ((value%10)!=0)
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
	int offset = static_cast<int>(value>>(64-8));
	offset-=142;
	value&=~(255ull<<(64-8));
	if(value==0)
	{
		if(offset!=0)
			throw std::runtime_error("invalid currency value");
	}
	else
	{
		if( (value<cMinValue) || (value>cMaxValue) || (offset<cMinOffset) || (offset>cMaxOffset) )
			throw std::runtime_error("invalid currency value");
	}
	return new STAmount(name, value, offset);
}

std::string STAmount::getText() const
{
	return boost::lexical_cast<std::string>(static_cast<double>(*this));
}

void STAmount::add(Serializer& s) const
{
	uint64 v=value;
	v+=(static_cast<uint64>(offset+142) << (64-8));
	s.add64(v);
}

bool STAmount::isEquivalent(const SerializedType& t) const
{
	const STAmount* v=dynamic_cast<const STAmount*>(&t);
	if(!v) return false;
	return (value==v->value) && (offset==v->offset);
}

bool STAmount::operator==(const STAmount& a) const
{
	return (offset==a.offset) && (value==a.value);
}

bool STAmount::operator!=(const STAmount& a) const
{
	return (offset!=a.offset) || (value!=a.value);
}

bool STAmount::operator<(const STAmount& a) const
{
	if(offset<a.offset) return true;
	if(a.offset<offset) return false;
	return value < a.value;
}

bool STAmount::operator>(const STAmount& a) const
{
	if(offset>a.offset) return true;
	if(a.offset>offset) return false;
	return value > a.value;
}

bool STAmount::operator<=(const STAmount& a) const
{
	if(offset<a.offset) return true;
	if(a.offset<offset) return false;
	return value <= a.value;
}

bool STAmount::operator>=(const STAmount& a) const
{
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
	if(v1.value < v2.value) throw std::runtime_error("value overflow");
	return STAmount(v1.name, v1.value - v2.value, v1.offset);
}

STAmount::operator double() const
{
	if(!value) return 0.0;
	return static_cast<double>(value) * pow(10.0, offset);
}
