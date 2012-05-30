
#include "Ledger.h"

#include <boost/make_shared.hpp>

LedgerStateParms Ledger::writeBack(LedgerStateParms parms, SerializedLedgerEntry::pointer entry)
{
	ScopedLock l(mAccountStateMap->Lock());
	bool create = false;

	if (!mAccountStateMap->hasItem(entry->getIndex()))
	{
		if ((parms & lepCREATE) == 0)
		{
#ifdef DEBUG
			std::cerr << "writeBack no create" << std::endl;
#endif
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

	ScopedLock l(mAccountStateMap->Lock());

	try
	{
		return getASNode(parms, nodeID, ltACCOUNT_ROOT);
	}
	catch (...)
	{
		parms = lepERROR;
		return SerializedLedgerEntry::pointer();
	}
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

	try
	{
		return getASNode(parms, nickHash, ltNICKNAME);
	}
	catch (...)
	{
		parms = lepERROR;
		return SerializedLedgerEntry::pointer();
	}
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

	try
	{
		return getASNode(parms, uNode, ltRIPPLE_STATE);
	}
	catch (...)
	{
		parms = lepERROR;
		return SerializedLedgerEntry::pointer();
	}
}

//
// Directory
//

uint256 Ledger::getDirIndex(const uint256& uBase, const LedgerEntryType letKind, const uint64 uNodeDir)
{
	// Indexes are stored in little endian format.
	// The low bytes are indexed first, so when printed as a hex stream the hex is in byte order.
	// Therefore, we place uNodeDir in the 8 right most bytes.
	Serializer	sKey;

	sKey.add256(uBase);
	sKey.add8(letKind);

	uint256	uResult	= sKey.getSHA512Half();

	Serializer	sNode;	// Put in a fixed byte order: BIG. YYY

	sNode.add64(uNodeDir);

	// YYY SLOPPY
	std::vector<unsigned char>	vucData	= sNode.getData();

	std::copy(vucData.begin(), vucData.end(), uResult.end()-(64/8));

	return uResult;
}

SerializedLedgerEntry::pointer Ledger::getDirRoot(LedgerStateParms& parms, const uint256& uRootIndex)
{
	ScopedLock l(mAccountStateMap->Lock());

	try
	{
		return getASNode(parms, uRootIndex, ltDIR_ROOT);
	}
	catch (...)
	{
		parms = lepERROR;
		return SerializedLedgerEntry::pointer();
	}
}

SerializedLedgerEntry::pointer Ledger::getDirNode(LedgerStateParms& parms, const uint256& uNodeIndex)
{
	ScopedLock l(mAccountStateMap->Lock());

	try
	{
		return getASNode(parms, uNodeIndex, ltDIR_NODE);
	}
	catch (...)
	{
		parms = lepERROR;
		return SerializedLedgerEntry::pointer();
	}
}

// vim:ts=4
