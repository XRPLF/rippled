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
	mOfType	= ofType;

	fillItems(accountID, ledger);
}

void AccountItems::fillItems(const uint160& accountID, Ledger::ref ledger)
{
	uint256 rootIndex		= Ledger::getOwnerDirIndex(accountID);
	uint256 currentIndex	= rootIndex;

	while (1)
	{
		SLE::pointer ownerDir	= ledger->getDirNode(currentIndex);
		if (!ownerDir) return;

		BOOST_FOREACH(const uint256& uNode, ownerDir->getFieldV256(sfIndexes).peekValue())
		{
			SLE::pointer sleCur	= ledger->getSLEi(uNode);

			AccountItem::pointer item = mOfType->makeItem(accountID, sleCur);
			if (item)
			{
				mItems.push_back(item);
			}
		}

		uint64 uNodeNext	= ownerDir->getFieldU64(sfIndexNext);
		if (!uNodeNext) return;

		currentIndex	= Ledger::getDirNodeIndex(rootIndex, uNodeNext);
	}
}

Json::Value AccountItems::getJson(int v)
{
	Json::Value ret(Json::arrayValue);
	BOOST_FOREACH(AccountItem::ref ai, mItems)
		ret.append(ai->getJson(v));
	return ret;
}

// vim:ts=4
