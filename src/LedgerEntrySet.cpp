#include "LedgerEntrySet.h"

#include <boost/make_shared.hpp>

void LedgerEntrySet::init(const uint256& transactionID, uint32 ledgerID)
{
	mEntries.clear();
	mSet.init(transactionID, ledgerID);
	mSeq = 0;
}

void LedgerEntrySet::clear()
{
	mEntries.clear();
	mSet.clear();
}

LedgerEntrySet LedgerEntrySet::duplicate() const
{
	return LedgerEntrySet(mEntries, mSet, mSeq + 1);
}

void LedgerEntrySet::setTo(const LedgerEntrySet& e)
{
	mEntries = e.mEntries;
	mSet = e.mSet;
	mSeq = e.mSeq;
}

void LedgerEntrySet::swapWith(LedgerEntrySet& e)
{
	std::swap(mSeq, e.mSeq);
	mSet.swap(e.mSet);
	mEntries.swap(e.mEntries);
}

// Find an entry in the set.  If it has the wrong sequence number, copy it and update the sequence number.
// This is basically: copy-on-read.
SLE::pointer LedgerEntrySet::getEntry(const uint256& index, LedgerEntryAction& action)
{
	boost::unordered_map<uint256, LedgerEntrySetEntry>::iterator it = mEntries.find(index);
	if (it == mEntries.end())
	{
		action = taaNONE;
		return SLE::pointer();
	}
	if (it->second.mSeq != mSeq)
	{
		it->second.mEntry = boost::make_shared<SerializedLedgerEntry>(*it->second.mEntry);
		it->second.mSeq = mSeq;
	}
	action = it->second.mAction;
	return it->second.mEntry;
}

LedgerEntryAction LedgerEntrySet::hasEntry(const uint256& index) const
{
	boost::unordered_map<uint256, LedgerEntrySetEntry>::const_iterator it = mEntries.find(index);
	if (it == mEntries.end())
		return taaNONE;
	return it->second.mAction;
}

void LedgerEntrySet::entryCache(SLE::ref sle)
{
	boost::unordered_map<uint256, LedgerEntrySetEntry>::iterator it = mEntries.find(sle->getIndex());
	if (it == mEntries.end())
	{
		mEntries.insert(std::make_pair(sle->getIndex(), LedgerEntrySetEntry(sle, taaCACHED, mSeq)));
		return;
	}

	switch (it->second.mAction)
	{
		case taaCACHED:
			it->second.mSeq	    = mSeq;
			it->second.mEntry   = sle;
			return;

		default:
			throw std::runtime_error("Cache after modify/delete/create");
	}
}

void LedgerEntrySet::entryCreate(SLE::ref sle)
{
	boost::unordered_map<uint256, LedgerEntrySetEntry>::iterator it = mEntries.find(sle->getIndex());
	if (it == mEntries.end())
	{
		mEntries.insert(std::make_pair(sle->getIndex(), LedgerEntrySetEntry(sle, taaCREATE, mSeq)));
		return;
	}

	assert(it->second.mSeq == mSeq);

	switch (it->second.mAction)
	{
		case taaMODIFY:
			throw std::runtime_error("Create after modify");

		case taaDELETE:
			throw std::runtime_error("Create after delete"); // We could make this a modify

		case taaCREATE:
			throw std::runtime_error("Create after create"); // We could make this work

		case taaCACHED:
			throw std::runtime_error("Create after cache");

		default:
			throw std::runtime_error("Unknown taa");
	}
}

void LedgerEntrySet::entryModify(SLE::ref sle)
{
	boost::unordered_map<uint256, LedgerEntrySetEntry>::iterator it = mEntries.find(sle->getIndex());
	if (it == mEntries.end())
	{
		mEntries.insert(std::make_pair(sle->getIndex(), LedgerEntrySetEntry(sle, taaMODIFY, mSeq)));
		return;
	}

	assert(it->second.mSeq == mSeq);
	assert(*it->second.mEntry == *sle);

	switch (it->second.mAction)
	{
		case taaCACHED:
			it->second.mAction  = taaMODIFY;
			fallthru();
		case taaMODIFY:
			it->second.mSeq	    = mSeq;
			it->second.mEntry   = sle;
			break;

		case taaDELETE:
			throw std::runtime_error("Modify after delete");

		case taaCREATE:
			it->second.mSeq	    = mSeq;
			it->second.mEntry   = sle;
			break;

		default:
			throw std::runtime_error("Unknown taa");
	}
 }

void LedgerEntrySet::entryDelete(SLE::ref sle, bool unfunded)
{
	boost::unordered_map<uint256, LedgerEntrySetEntry>::iterator it = mEntries.find(sle->getIndex());
	if (it == mEntries.end())
	{
		mEntries.insert(std::make_pair(sle->getIndex(), LedgerEntrySetEntry(sle, taaDELETE, mSeq)));
		return;
	}

	assert(it->second.mSeq == mSeq);
	assert(*it->second.mEntry == *sle);

	switch (it->second.mAction)
	{
		case taaCACHED:
		case taaMODIFY:
			it->second.mSeq	    = mSeq;
			it->second.mEntry   = sle;
			it->second.mAction  = taaDELETE;
			if (unfunded)
			{
				assert(sle->getType() == ltOFFER); // only offers can be unfunded
#if 0
				mSet.deleteUnfunded(sle->getIndex(),
					sle->getIValueFieldAmount(sfTakerPays),
					sle->getIValueFieldAmount(sfTakerGets));
#endif
			}
			break;

		case taaCREATE:
			mEntries.erase(it);
			break;

		case taaDELETE:
			break;

		default:
			throw std::runtime_error("Unknown taa");
	}
}

bool LedgerEntrySet::intersect(const LedgerEntrySet& lesLeft, const LedgerEntrySet& lesRight)
{
	return true;	// XXX Needs implementation
}

Json::Value LedgerEntrySet::getJson(int) const
{
	Json::Value ret(Json::objectValue);

	Json::Value nodes(Json::arrayValue);
	for (boost::unordered_map<uint256, LedgerEntrySetEntry>::const_iterator it = mEntries.begin(),
			end = mEntries.end(); it != end; ++it)
	{
		Json::Value entry(Json::objectValue);
		entry["node"] = it->first.GetHex();
		switch (it->second.mEntry->getType())
		{
			case ltINVALID:			entry["type"] = "invalid"; break;
			case ltACCOUNT_ROOT:	entry["type"] = "acccount_root"; break;
			case ltDIR_NODE:		entry["type"] = "dir_node"; break;
			case ltGENERATOR_MAP:	entry["type"] = "generator_map"; break;
			case ltRIPPLE_STATE:	entry["type"] = "ripple_state"; break;
			case ltNICKNAME:		entry["type"] = "nickname"; break;
			case ltOFFER:			entry["type"] = "offer"; break;
			default:				assert(false);
		}
		switch (it->second.mAction)
		{
			case taaCACHED:			entry["action"] = "cache"; break;
			case taaMODIFY:			entry["action"] = "modify"; break;
			case taaDELETE:			entry["action"] = "delete"; break;
			case taaCREATE:			entry["action"] = "create"; break;
			default:				assert(false);
		}
		nodes.append(entry);
	}
	ret["nodes" ] = nodes;

	return ret;
}

SLE::pointer LedgerEntrySet::getForMod(const uint256& node, Ledger::ref ledger,
	boost::unordered_map<uint256, SLE::pointer>& newMods)
{
	boost::unordered_map<uint256, LedgerEntrySetEntry>::iterator it = mEntries.find(node);
	if (it != mEntries.end())
	{
		if (it->second.mAction == taaDELETE)
			return SLE::pointer();
		if (it->second.mAction == taaCACHED)
			it->second.mAction = taaMODIFY;
		if (it->second.mSeq != mSeq)
		{
			it->second.mEntry = boost::make_shared<SerializedLedgerEntry>(*it->second.mEntry);
			it->second.mSeq = mSeq;
		}
		return it->second.mEntry;
	}

	boost::unordered_map<uint256, SLE::pointer>::iterator me = newMods.find(node);
	if (me != newMods.end())
		return me->second;

	SLE::pointer ret = ledger->getSLE(node);
	if (ret)
		newMods.insert(std::make_pair(node, ret));
	return ret;

}

bool LedgerEntrySet::threadTx(TransactionMetaNode& metaNode, const NewcoinAddress& threadTo, Ledger::ref ledger,
	boost::unordered_map<uint256, SLE::pointer>& newMods)
{
	SLE::pointer sle = getForMod(Ledger::getAccountRootIndex(threadTo.getAccountID()), ledger, newMods);
	if (!sle)
		return false;
	return threadTx(metaNode, sle, ledger, newMods);
}

bool LedgerEntrySet::threadTx(TransactionMetaNode& metaNode, SLE::ref threadTo, Ledger::ref ledger,
	boost::unordered_map<uint256, SLE::pointer>& newMods)
{  // node = the node that was modified/deleted/created
   // threadTo = the node that needs to know
	uint256 prevTxID;
	uint32 prevLgrID;
	if (!threadTo->thread(mSet.getTxID(), mSet.getLgrSeq(), prevTxID, prevLgrID))
		return false;
	if (metaNode.thread(prevTxID, prevLgrID))
		return true;
	assert(false);
	return false;
}

bool LedgerEntrySet::threadOwners(TransactionMetaNode& metaNode, SLE::ref node, Ledger::ref ledger,
	boost::unordered_map<uint256, SLE::pointer>& newMods)
{ // thread new or modified node to owner or owners
	if (node->hasOneOwner()) // thread to owner's account
		return threadTx(metaNode, node->getOwner(), ledger, newMods);
	else if (node->hasTwoOwners()) // thread to owner's accounts
		return
			threadTx(metaNode, node->getFirstOwner(), ledger, newMods) ||
			threadTx(metaNode, node->getSecondOwner(), ledger, newMods);
	else
		return false;
}

void LedgerEntrySet::calcRawMeta(Serializer& s, Ledger::ref origLedger)
{ // calculate the raw meta data and return it. This must be called before the set is committed

	// Entries modified only as a result of building the transaction metadata
	boost::unordered_map<uint256, SLE::pointer> newMod;

	for (boost::unordered_map<uint256, LedgerEntrySetEntry>::const_iterator it = mEntries.begin(),
			end = mEntries.end(); it != end; ++it)
	{
		int nType = TMNEndOfMetadata;

		switch (it->second.mAction)
		{
			case taaMODIFY:
				nType = TMNModifiedNode;
				break;

			case taaDELETE:
				nType = TMNDeletedNode;
				break;

			case taaCREATE:
				nType = TMNCreatedNode;
				break;

			default:
				// ignore these
				break;
		}

		if (nType == TMNEndOfMetadata)
			continue;

		SLE::pointer origNode = origLedger->getSLE(it->first);
		SLE::pointer curNode = it->second.mEntry;
		TransactionMetaNode &metaNode = mSet.getAffectedNode(it->first, nType);

		if (nType == TMNDeletedNode)
		{
			threadOwners(metaNode, origNode, origLedger, newMod);

			if (origNode->getIFieldPresent(sfAmount))
			{ // node has an amount, covers ripple state nodes
				STAmount amount = origNode->getIValueFieldAmount(sfAmount);
				if (amount.isNonZero())
					metaNode.addAmount(TMSPrevBalance, amount);
				amount = curNode->getIValueFieldAmount(sfAmount);
				if (amount.isNonZero())
					metaNode.addAmount(TMSFinalBalance, amount);

				if (origNode->getType() == ltRIPPLE_STATE)
				{
					metaNode.addAccount(TMSLowID, origNode->getIValueFieldAccount(sfLowID));
					metaNode.addAccount(TMSHighID, origNode->getIValueFieldAccount(sfHighID));
				}

			}

			if (origNode->getType() == ltOFFER)
			{ // check for non-zero balances
				STAmount amount = origNode->getIValueFieldAmount(sfTakerPays);
				if (amount.isNonZero())
					metaNode.addAmount(TMSFinalTakerPays, amount);
				amount = origNode->getIValueFieldAmount(sfTakerGets);
				if (amount.isNonZero())
					metaNode.addAmount(TMSFinalTakerGets, amount);
			}

		}

		if (nType == TMNCreatedNode) // if created, thread to owner(s)
			threadOwners(metaNode, curNode, origLedger, newMod);

		if ((nType == TMNCreatedNode) || (nType == TMNModifiedNode))
		{
			if (curNode->isThreadedType()) // always thread to self
				threadTx(metaNode, curNode, origLedger, newMod);
		}

		if (nType == TMNModifiedNode)
		{
			if (origNode->getIFieldPresent(sfAmount))
			{ // node has an amount, covers account root nodes and ripple nodes
				STAmount amount = origNode->getIValueFieldAmount(sfAmount);
				if (amount != curNode->getIValueFieldAmount(sfAmount))
					metaNode.addAmount(TMSPrevBalance, amount);
			}

			if (origNode->getType() == ltOFFER)
			{
				STAmount amount = origNode->getIValueFieldAmount(sfTakerPays);
				if (amount != curNode->getIValueFieldAmount(sfTakerPays))
					metaNode.addAmount(TMSPrevTakerPays, amount);
				amount = origNode->getIValueFieldAmount(sfTakerGets);
				if (amount != curNode->getIValueFieldAmount(sfTakerGets))
					metaNode.addAmount(TMSPrevTakerGets, amount);
			}

		}
	}

	// add any new modified nodes to the modification set
	for (boost::unordered_map<uint256, SLE::pointer>::iterator it = newMod.begin(), end = newMod.end();
			it != end; ++it)
		entryCache(it->second);

	mSet.addRaw(s);
}

// vim:ts=4
