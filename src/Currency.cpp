
#include "Currency.h"

#include <cmath>
#include <stdexcept>

uint160 Currency::sNatMask("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF0000");
uint160 Currency::sNatZero("FFFFFFFFFFFFFFFFFFFFFFFFFFFF000000000000");
uint64 Amount::sMaxCanon(1ull << 63);

Currency::Currency(const uint160& v) : mValue(v)
{
	if (!v) mType = ctNATIVE;
	if (!(v & sNatZero)) mType = ctNATIONAL;
	mType = ctNATIONAL;
}

bool Currency::isCommensurate(const Currency& c) const
{
	if (isNative())
		return c.isNative();
	if (isCustom())
		return mValue == c.mValue;
	if (!c.isNational()) return false;
	return (mValue & sNatMask) == (c.mValue & sNatMask);
}

unsigned char Currency::getScale() const
{
	return *(mValue.begin());
}

void Currency::setScale(unsigned char s)
{
	*(mValue.begin()) = s;
}

void Amount::canonicalize()
{ // clear high bit to avoid overflows
	if(mQuantity > sMaxCanon)
	{
		if (!mCurrency.isNational()) throw std::runtime_error("Currency overflow");
		unsigned char s = mCurrency.getScale();
		if (s==255) throw std::runtime_error("Currency overflow");
		mCurrency.setScale(s + 1);
		mQuantity /= 10.0;
	}
}

double Amount::getDisplayQuantity() const
{
	if(!mCurrency.isNational()) throw std::runtime_error("Can only scale national currencies");
	int scale=mCurrency.getScale();
	return static_cast<double>(mQuantity) * pow(10, 128-scale);
}

