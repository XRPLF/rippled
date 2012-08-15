#include "RippleLines.h"
#include "Application.h"
#include "Log.h"
#include <boost/foreach.hpp>

RippleLines::RippleLines(const uint160& accountID, Ledger::pointer ledger)
{
	fillLines(accountID,ledger);
}

RippleLines::RippleLines(const uint160& accountID )
{
	fillLines(accountID,theApp->getMasterLedger().getCurrentLedger());
}

void RippleLines::fillLines(const uint160& accountID, Ledger::pointer ledger)
{
	uint256 rootIndex	= Ledger::getRippleDirIndex(accountID);
	uint256 currentIndex=rootIndex;

	LedgerStateParms	lspNode		= lepNONE;

	while(1)
	{
		SerializedLedgerEntry::pointer rippleDir=ledger->getDirNode(lspNode,currentIndex);
		if(!rippleDir) return;

		STVector256 svRippleNodes		= rippleDir->getIFieldV256(sfIndexes);
		BOOST_FOREACH(uint256& uNode, svRippleNodes.peekValue())
		{
			RippleState::pointer	rsLine	= ledger->accessRippleState(uNode);
			if (rsLine)
			{
				rsLine->setViewAccount(accountID);
				mLines.push_back(rsLine);
			}
			else
			{
				Log(lsWARNING) << "doRippleLinesGet: Bad index: " << uNode.ToString();
			}
		}

		uint64 uNodeNext		= rippleDir->getIFieldU64(sfIndexNext);
		if(!uNodeNext) return;

		currentIndex	= Ledger::getDirNodeIndex(rootIndex, uNodeNext);
	}
}
