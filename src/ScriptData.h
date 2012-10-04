#ifndef __SCRIPT_DATA__
#define __SCRIPT_DATA__
#include "uint256.h"
#include <boost/shared_ptr.hpp>

namespace Script {
class Data  
{
public:
	typedef boost::shared_ptr<Data> pointer;

	virtual ~Data(){ ; }

	virtual bool isInt32(){ return(false); }
	virtual bool isFloat(){ return(false); }
	virtual bool isUint160(){ return(false); }
	virtual bool isError(){ return(false); }
	virtual bool isTrue(){ return(false); }
	virtual bool isBool(){ return(false); }
	//virtual bool isBlockEnd(){ return(false); }

	virtual int getInt(){ return(0); }
	virtual float getFloat(){ return(0); }
	virtual uint160 getUint160(){ return(0); }

	//virtual bool isCurrency(){ return(false); }
};

class IntData : public Data
{
	int mValue;
public:
	IntData(int value)
	{
		mValue=value;
	}
	bool isInt32(){ return(true); }
	int getInt(){ return(mValue); }
	float getFloat(){ return((float)mValue); }
	bool isTrue(){ return(mValue!=0); }
};

class FloatData : public Data
{
	float mValue;
public:
	FloatData(float value)
	{
		mValue=value;
	}
	bool isFloat(){ return(true); }
	float getFloat(){ return(mValue); }
	bool isTrue(){ return(mValue!=0); }
};

class Uint160Data : public Data
{
	uint160 mValue;
public:
	Uint160Data(uint160 value)
	{
		mValue=value;
	}
	bool isUint160(){ return(true); }
	uint160 getUint160(){ return(mValue); }
};

class BoolData : public Data
{
	bool mValue;
public:
	BoolData(bool value)
	{
		mValue=value;
	}
	bool isBool(){ return(true); }
	bool isTrue(){ return(mValue); }
};

class ErrorData : public Data
{
public:
	bool isError(){ return(true); }
};

class BlockEndData : public Data
{
public:
	bool isBlockEnd(){ return(true); }
};


}


#endif