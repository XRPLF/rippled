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
	uint256 rootIndex		= Ledger::getOwnerDirIndex(accountID);
	uint256 currentIndex	= rootIndex;

	LedgerStateParms	lspNode		= lepNONE;

	while (1)
	{
		SLE::pointer rippleDir=ledger->getDirNode(lspNode, currentIndex);
		if (!rippleDir) return;

		STVector256 svOwnerNodes		= rippleDir->getValueFieldV256(sfIndexes);
		BOOST_FOREACH(uint256& uNode, svOwnerNodes.peekValue())
		{
			SLE::pointer	sleCur	= ledger->getSLE(uNode);

			if (ltRIPPLE_STATE == sleCur->getType())
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
		}

		uint64 uNodeNext	= rippleDir->getValueFieldU64(sfIndexNext);
		if (!uNodeNext) return;

		currentIndex	= Ledger::getDirNodeIndex(rootIndex, uNodeNext);
	}
}
// vim:ts=4
