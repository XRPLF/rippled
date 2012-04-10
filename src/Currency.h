#ifndef __CURRENCY__
#define __CURRENCY__

#include <string>

#include "uint256.h"

enum CurrencyType
{
	ctNATIVE,	// Currency transaction fees are paid in
	ctNATIONAL,	// State-issued or ISO-recognized currencies
	ctCUSTOM,	// Custom currencies
};

class Currency
{
protected:
	uint160 mValue;
	CurrencyType mType;

	static uint160 sNatMask; // bits that indicate national currency ISO code and version
	static uint160 sNatZero; // bits that must be zero on a national currency

public:
	Currency() : mType(ctNATIVE) { ; }
	Currency(const uint160& v);
	Currency(const std::string& iso, uint16 version, unsigned char scale);

	bool isCommensurate(const Currency&) const;
	bool isNational() const { return mType == ctNATIONAL; }
	bool isNative() const { return mType == ctNATIVE; }
	bool isCustom() const { return mType == ctCUSTOM; }

	const uint160& getCurrency() { return mValue; }
	unsigned char getScale() const;
	void setScale(unsigned char c);

	// These are only valid for national currencies
	std::string getISO() const;
	uint16 getVersion() const;
};

class Amount
{
	// CAUTION: Currency operations throw on overflows, underflos, or
	// incommensurate currency opeations (like adding USD to Euros)
protected:
	Currency mCurrency;
	uint64 mQuantity;

	void canonicalize();

	static uint64 sMaxCanon; // Max native currency value before shift

public:

	Amount(const Currency& c, const uint64& q) : mCurrency(c), mQuantity(q) { canonicalize(); }

	const Currency& getCurrency() const { return mCurrency; }
	uint64 getQuantity() const { return mQuantity; }
	double getDisplayQuantity() const;

	// These throw if the currencies are incommensurate
	// They handle scaling and represent the result as accurately as possible
	bool operator==(const Amount&) const;
	bool operator!=(const Amount&) const;
	bool operator>=(const Amount&) const;
	bool operator<=(const Amount&) const;
	bool operator>(const Amount&) const;
	bool operator<(const Amount&) const;
	Amount& operator+=(const Amount& a) { return *this = *this + a; }
	Amount& operator-=(const Amount& a) { return *this = *this - a; }

	// This is used to score offers and works with incommensurate currencies
	friend void divide(const Amount& offering, const Amount& taking, uint16& exponent, uint64& mantissa);
	friend Amount& operator+(const Amount&, const Amount&);
	friend Amount& operator-(const Amount&, const Amount&);
};

#endif
