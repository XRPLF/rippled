
#include "Ledger.h"

#include "boost/make_shared.hpp"

SerializedLedgerEntry::pointer Ledger::getAccountRoot(LedgerStateParms& parms, const uint160& accountID)
{
	uint256 nodeID=getAccountRootIndex(accountID);

	ScopedLock l(mAccountStateMap->Lock());

	SHAMapItem::pointer account = mAccountStateMap->peekItem(nodeID);
	if (!account)
	{
		if ( (parms & lepCREATE) == 0 )
		{
			parms = lepMISSING;
			return SerializedLedgerEntry::pointer();
		}

		parms = lepCREATED;
		SerializedLedgerEntry::pointer sle=boost::make_shared<SerializedLedgerEntry>(ltACCOUNT_ROOT);
		sle->setIndex(nodeID);
		return sle;
	}

	try
	{
		SerializedLedgerEntry::pointer sle =
			boost::make_shared<SerializedLedgerEntry>(account->peekSerializer(), nodeID);

		if(sle->getType() != ltACCOUNT_ROOT)
		{ // maybe it's a currency or something
			parms = lepWRONGTYPE;
			return SerializedLedgerEntry::pointer();
		}
		parms = lepOKAY;
		return sle;
	}
	catch(...)
	{
		parms = lepERROR;
		return SerializedLedgerEntry::pointer();
	}
}
