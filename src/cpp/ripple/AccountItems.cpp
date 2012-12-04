#include "AccountItems.h"

#include <boost/foreach.hpp>

#include "Application.h"
#include "Log.h"

SETUP_LOG();

AccountItem::AccountItem(SerializedLedgerEntry::ref ledger) : mLedgerEntry(ledger)
{

}

AccountItems::AccountItems(const uint160& accountID, Ledger::ref ledger, AccountItem::pointer ofType)
{
	mOfType=ofType;
	fillItems(accountID, ledger);
}

// looks in the current ledger
AccountItems::AccountItems(const uint160& accountID, AccountItem::pointer ofType )
{
	mOfType=ofType;
	fillItems(accountID,theApp->getLedgerMaster().getClosedLedger());
}

void AccountItems::fillItems(const uint160& accountID, Ledger::ref ledger)
{
	uint256 rootIndex		= Ledger::getOwnerDirIndex(accountID);
	uint256 currentIndex	= rootIndex;

	LedgerStateParms	lspNode		= lepNONE;

	while (1)
	{
		SLE::pointer ownerDir=ledger->getDirNode(lspNode, currentIndex);
		if (!ownerDir) return;

		STVector256 svOwnerNodes		= ownerDir->getFieldV256(sfIndexes);
		BOOST_FOREACH(uint256& uNode, svOwnerNodes.peekValue())
		{
			SLE::pointer sleCur	= ledger->getSLE(uNode);

			AccountItem::pointer item=mOfType->makeItem(accountID, sleCur);
			if(item)
			{
				mItems.push_back(item);
			}
		}

		uint64 uNodeNext	= ownerDir->getFieldU64(sfIndexNext);
		if (!uNodeNext) return;

		currentIndex	= Ledger::getDirNodeIndex(rootIndex, uNodeNext);
	}
}