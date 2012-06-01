
#include "Ledger.h"

#include <boost/make_shared.hpp>

#include "utils.h"
#include "Log.h"

LedgerStateParms Ledger::writeBack(LedgerStateParms parms, SerializedLedgerEntry::pointer entry)
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
		if(!mAccountStateMap->addGiveItem(item, false))
		{
			assert(false);
			return lepERROR;
		}
		return lepCREATED;
	}

	if (!mAccountStateMap->updateGiveItem(item, false))
	{
		assert(false);
		return lepERROR;
	}
	return lepOKAY;
}

SerializedLedgerEntry::pointer Ledger::getASNode(LedgerStateParms& parms, const uint256& nodeID,
 LedgerEntryType let )
{
	SHAMapItem::pointer account = mAccountStateMap->peekItem(nodeID);

	if (!account)
	{
		if ( (parms & lepCREATE) == 0 )
		{
			parms = lepMISSING;
			return SerializedLedgerEntry::pointer();
		}

		parms = parms | lepCREATED | lepOKAY;
		SerializedLedgerEntry::pointer sle=boost::make_shared<SerializedLedgerEntry>(let);
		sle->setIndex(nodeID);

		return sle;
	}

	SerializedLedgerEntry::pointer sle =
		boost::make_shared<SerializedLedgerEntry>(account->peekSerializer(), nodeID);

	if(sle->getType() != let)
	{ // maybe it's a currency or something
		parms = parms | lepWRONGTYPE;
		return SerializedLedgerEntry::pointer();
	}

	parms = parms | lepOKAY;

	return sle;

}

SerializedLedgerEntry::pointer Ledger::getAccountRoot(LedgerStateParms& parms, const uint160& accountID)
{
	uint256 nodeID=getAccountRootIndex(accountID);

	return getASNode(parms, nodeID, ltACCOUNT_ROOT);
}

SerializedLedgerEntry::pointer Ledger::getAccountRoot(LedgerStateParms& parms, const NewcoinAddress& naAccountID)
{
	return getAccountRoot(parms, naAccountID.getAccountID());
}

SerializedLedgerEntry::pointer Ledger::getNickname(LedgerStateParms& parms, const std::string& nickname)
{
	return getNickname(parms, Serializer::getSHA512Half(nickname));
}

SerializedLedgerEntry::pointer Ledger::getNickname(LedgerStateParms& parms, const uint256& nickHash)
{
	ScopedLock l(mAccountStateMap->Lock());

	return getASNode(parms, nickHash, ltNICKNAME);
}

//
// Generator Map
//

SerializedLedgerEntry::pointer Ledger::getGenerator(LedgerStateParms& parms, const uint160& uGeneratorID)
{
	uint256 nodeID=getGeneratorIndex(uGeneratorID);

	ScopedLock l(mAccountStateMap->Lock());

	try
	{
		return getASNode(parms, nodeID, ltGENERATOR_MAP);
	}
	catch (...)
	{
		parms = lepERROR;
		return SerializedLedgerEntry::pointer();
	}
}

//
// Ripple State
//

SerializedLedgerEntry::pointer Ledger::getRippleState(LedgerStateParms& parms, const uint256& uNode)
{
	ScopedLock l(mAccountStateMap->Lock());

	return getASNode(parms, uNode, ltRIPPLE_STATE);
}

//
// Directory
//

// For a directory entry put in the 64 bit index or quality.
uint256 Ledger::getDirIndex(const uint256& uBase, const uint64 uNodeDir)
{
	// Indexes are stored in big endian format: they print as hex as stored.
	// Most significant bytes are first.  Least significant bytes repesent adjcent entries.
	// We place uNodeDir in the 8 right most bytes to be adjcent.
	// Want uNodeDir in big endian format so ++ goes to the next entry for indexes.
	uint256	uNode(uBase);

	((uint64*) uNode.end())[-1]	= htobe64(uNodeDir);

	return uNode;
}

SerializedLedgerEntry::pointer Ledger::getDirRoot(LedgerStateParms& parms, const uint256& uRootIndex)
{
	ScopedLock l(mAccountStateMap->Lock());

	return getASNode(parms, uRootIndex, ltDIR_ROOT);
}

SerializedLedgerEntry::pointer Ledger::getDirNode(LedgerStateParms& parms, const uint256& uNodeIndex)
{
	ScopedLock l(mAccountStateMap->Lock());

	return getASNode(parms, uNodeIndex, ltDIR_NODE);
}

// vim:ts=4
