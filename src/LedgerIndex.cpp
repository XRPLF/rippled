
#include "Ledger.h"

uint256 Ledger::getRippleIndex(const uint160& accountID, const uint160& extendTo, const uint160& currency)
{ // Index is 160-bit account credit extended to, 96-bit XOR of extending account and currency
	uint256 base = getAccountRootIndex(extendTo);
	memcpy(base.begin() + (160 / 8), (accountID ^ currency).begin(), (256 / 8) - (160 / 8));
	return base;
}

uint160 Ledger::getOfferBase(const uint160& currencyIn, const uint160& accountIn,
	const uint160& currencyOut, const uint160& accountOut)
{
	bool inNative = !!currencyIn;
	bool outNative = !!currencyOut;

	if (inNative && outNative)
		throw std::runtime_error("native to native offer");

	Serializer s(80);

	if (inNative)
	{
		if (!currencyIn) throw std::runtime_error("native currencies are untied");
		s.add32(0); // prevent collisions by ensuring different lengths
	}
	else
	{
		if (!!currencyIn) throw std::runtime_error("national currencies must be tied");
		s.add160(currencyIn);
		s.add160(accountIn);
	}

	if (outNative)
	{
		if (!currencyOut) throw std::runtime_error("native currencies are untied");
	}
	else
	{
		if (!!currencyOut) throw std::runtime_error("national currencies must be tied");
		s.add160(currencyOut);
		s.add160(accountOut);
	}

	return s.getRIPEMD160();
}

uint256 Ledger::getOfferIndex(const uint160& offerBase, uint64 rate, int skip)
{ // the node for an offer index
	Serializer s;
	s.add160(offerBase);
	s.add64(rate);
	s.add32(skip);
	return s.get256(0);
}

int Ledger::getOfferSkip(const uint256& offerId)
{ // how far ahead we skip if an index node is full
	return *offerId.begin();
}

// vim:ts=4
