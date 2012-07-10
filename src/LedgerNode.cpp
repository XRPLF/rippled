
#include "Ledger.h"

#include <boost/make_shared.hpp>

#include "utils.h"
#include "Log.h"

// XXX Use shared locks where possible?

LedgerStateParms Ledger::writeBack(LedgerStateParms parms, SLE::pointer entry)
{
	ScopedLock l(mAccountStateMap->Lock());
	bool create = false;

	if (!mAccountStateMap->hasItem(entry->getIndex()))
	{
		if ((parms & lepCREATE) == 0)
		{
			Log(lsERROR) << "WriteBack non-existent node without create";
			return lepMISSING;
		}
		create = true;
	}

	SHAMapItem::pointer item = boost::make_shared<SHAMapItem>(entry->getIndex());
	entry->add(item->peekSerializer());

	if (create)
	{
		assert(!mAccountStateMap->hasItem(entry->getIndex()));
		if(!mAccountStateMap->addGiveItem(item, false, false)) // FIXME: TX metadata
		{
			assert(false);
			return lepERROR;
		}
		return lepCREATED;
	}

	if (!mAccountStateMap->updateGiveItem(item, false, false)) // FIXME: TX metadata
	{
		assert(false);
		return lepERROR;
	}
	return lepOKAY;
}

SLE::pointer Ledger::getNextSLE(const uint256& hash)
{
	SHAMapItem::pointer node = mAccountStateMap->peekNextItem(hash);
	if (!node)
		return SLE::pointer();
	return boost::make_shared<SLE>(node->peekSerializer(), node->getTag());
}

SLE::pointer Ledger::getNextSLE(const uint256& hash, const uint256& max)
{
	SHAMapItem::pointer node = mAccountStateMap->peekNextItem(hash);
	if ((!node) || (node->getTag() > max))
		return SLE::pointer();
	return boost::make_shared<SLE>(node->peekSerializer(), node->getTag());
}

SLE::pointer Ledger::getPrevSLE(const uint256& hash)
{
	SHAMapItem::pointer node = mAccountStateMap->peekPrevItem(hash);
	if (!node)
		return SLE::pointer();
	return boost::make_shared<SLE>(node->peekSerializer(), node->getTag());
}

SLE::pointer Ledger::getPrevSLE(const uint256& hash, const uint256& min)
{
	SHAMapItem::pointer node = mAccountStateMap->peekNextItem(hash);
	if ((!node) || (node->getTag() < min))
		return SLE::pointer();
	return boost::make_shared<SLE>(node->peekSerializer(), node->getTag());
}

SLE::pointer Ledger::getASNode(LedgerStateParms& parms, const uint256& nodeID,
 LedgerEntryType let )
{
	SHAMapItem::pointer account = mAccountStateMap->peekItem(nodeID);

	if (!account)
	{
		if ( (parms & lepCREATE) == 0 )
		{
			parms = lepMISSING;
			return SLE::pointer();
		}

		parms = parms | lepCREATED | lepOKAY;
		SLE::pointer sle=boost::make_shared<SLE>(let);
		sle->setIndex(nodeID);

		return sle;
	}

	SLE::pointer sle =
		boost::make_shared<SLE>(account->peekSerializer(), nodeID);

	if (sle->getType() != let)
	{ // maybe it's a currency or something
		parms = parms | lepWRONGTYPE;
		return SLE::pointer();
	}

	parms = parms | lepOKAY;

	return sle;

}

SLE::pointer Ledger::getAccountRoot(LedgerStateParms& parms, const uint160& accountID)
{
	uint256 nodeID=getAccountRootIndex(accountID);

	return getASNode(parms, nodeID, ltACCOUNT_ROOT);
}

SLE::pointer Ledger::getAccountRoot(LedgerStateParms& parms, const NewcoinAddress& naAccountID)
{
	return getAccountRoot(parms, naAccountID.getAccountID());
}

//
// Directory
//

SLE::pointer Ledger::getDirNode(LedgerStateParms& parms, const uint256& uNodeIndex)
{
	ScopedLock l(mAccountStateMap->Lock());

	return getASNode(parms, uNodeIndex, ltDIR_NODE);
}

//
// Generator Map
//

SLE::pointer Ledger::getGenerator(LedgerStateParms& parms, const uint160& uGeneratorID)
{
	ScopedLock l(mAccountStateMap->Lock());

	return getASNode(parms, getGeneratorIndex(uGeneratorID), ltGENERATOR_MAP);
}

//
// Nickname
//

SLE::pointer Ledger::getNickname(LedgerStateParms& parms, const uint256& uNickname)
{
	ScopedLock l(mAccountStateMap->Lock());

	return getASNode(parms, uNickname, ltNICKNAME);
}

//
// Offer
//


SLE::pointer Ledger::getOffer(LedgerStateParms& parms, const uint256& uIndex)
{
	ScopedLock l(mAccountStateMap->Lock());

	return getASNode(parms, uIndex, ltOFFER);
}

//
// Ripple State
//

SLE::pointer Ledger::getRippleState(LedgerStateParms& parms, const uint256& uNode)
{
	ScopedLock l(mAccountStateMap->Lock());

	return getASNode(parms, uNode, ltRIPPLE_STATE);
}

// vim:ts=4
