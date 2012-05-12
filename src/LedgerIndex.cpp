
#include "Ledger.h"

uint256 Ledger::getAccountRootIndex(const uint160& accountID)
{ // Index is accountID extended to 256 bits
	uint256 index;
	memcpy(index.begin() + (index.size() - accountID.size()), accountID.begin(), accountID.size());
	return index;
}

uint256 Ledger::getRippleIndex(const uint160& accountID, const uint160& extendTo, const uint160& currency)
{ // Index is 160-bit account credit extended to, 96-bit XOR of extending account and currency
	uint256 base=getAccountRootIndex(extendTo);
	memcpy(base.begin() + (160/8), (accountID^currency).begin(), (256/8)-(160/8));
	return base;
}
// vim:ts=4
