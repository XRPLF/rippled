#include "RippleLines.h"

#include <boost/foreach.hpp>

#include "Application.h"
#include "Log.h"

SETUP_LOG();

RippleLines::RippleLines(const uint160& accountID, Ledger::ref ledger)
{
	fillLines(accountID, ledger);
}

void RippleLines::printRippleLines()
{
	for (unsigned int i =0; i < mLines.size(); i++) {
		std::cerr << i << ": " << mLines[i]->getAccountID().humanAccountID()  << std::endl;
	}
	std::cerr << std::endl;
}

RippleLines::RippleLines(const uint160& accountID )
{
	fillLines(accountID,theApp->getLedgerMaster().getCurrentLedger());
}

void RippleLines::fillLines(const uint160& accountID, Ledger::ref ledger)
{
	uint256 rootIndex		= Ledger::getOwnerDirIndex(accountID);
	uint256 currentIndex	= rootIndex;

	LedgerStateParms	lspNode		= lepNONE;

	while (1)
	{
		SLE::pointer rippleDir=ledger->getDirNode(lspNode, currentIndex);
		if (!rippleDir) return;

		STVector256 svOwnerNodes		= rippleDir->getFieldV256(sfIndexes);
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
					cLog(lsWARNING) << "doRippleLinesGet: Bad index: " << uNode.ToString();
				}
			}
		}

		uint64 uNodeNext	= rippleDir->getFieldU64(sfIndexNext);
		if (!uNodeNext) return;

		currentIndex	= Ledger::getDirNodeIndex(rootIndex, uNodeNext);
	}
}
// vim:ts=4
