
#include "LedgerEntrySet.h"

#include <boost/make_shared.hpp>

#include "Log.h"

// Small for testing, should likely be 32 or 64.
#define DIR_NODE_MAX		2

void LedgerEntrySet::init(Ledger::ref ledger, const uint256& transactionID, uint32 ledgerID)
{
	mEntries.clear();
	mLedger = ledger;
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
	return LedgerEntrySet(mLedger, mEntries, mSet, mSeq + 1);
}

void LedgerEntrySet::setTo(const LedgerEntrySet& e)
{
	mEntries = e.mEntries;
	mSet = e.mSet;
	mSeq = e.mSeq;
	mLedger = e.mLedger;
}

void LedgerEntrySet::swapWith(LedgerEntrySet& e)
{
	std::swap(mSeq, e.mSeq);
	std::swap(mLedger, e.mLedger);
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

SLE::pointer LedgerEntrySet::entryCreate(LedgerEntryType letType, const uint256& index)
{
	assert(index.isNonZero());
	SLE::pointer sleNew = boost::make_shared<SLE>(letType);
	sleNew->setIndex(index);
	entryCreate(sleNew);
	return sleNew;
}

SLE::pointer LedgerEntrySet::entryCache(LedgerEntryType letType, const uint256& index)
{
	SLE::pointer sleEntry;
	if (index.isNonZero())
	{
		LedgerEntryAction action;
		sleEntry = getEntry(index, action);
		if (!sleEntry)
		{
			sleEntry = mLedger->getSLE(index);
			if (sleEntry)
				entryCache(sleEntry);
		}
		else if (action == taaDELETE)
		{
			assert(false);
		}
	}
	return sleEntry;
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

void LedgerEntrySet::entryDelete(SLE::ref sle)
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

// <--     uNodeDir: For deletion, present to make dirDelete efficient.
// -->   uRootIndex: The index of the base of the directory.  Nodes are based off of this.
// --> uLedgerIndex: Value to add to directory.
// We only append. This allow for things that watch append only structure to just monitor from the last node on ward.
// Within a node with no deletions order of elements is sequential.  Otherwise, order of elements is random.
TER LedgerEntrySet::dirAdd(
	uint64&							uNodeDir,
	const uint256&					uRootIndex,
	const uint256&					uLedgerIndex)
{
	SLE::pointer		sleNode;
	STVector256			svIndexes;
	SLE::pointer		sleRoot		= entryCache(ltDIR_NODE, uRootIndex);

	if (!sleRoot)
	{
		// No root, make it.
		sleRoot		= entryCreate(ltDIR_NODE, uRootIndex);

		sleNode		= sleRoot;
		uNodeDir	= 0;
	}
	else
	{
		uNodeDir	= sleRoot->getIFieldU64(sfIndexPrevious);		// Get index to last directory node.

		if (uNodeDir)
		{
			// Try adding to last node.
			sleNode		= entryCache(ltDIR_NODE, Ledger::getDirNodeIndex(uRootIndex, uNodeDir));

			assert(sleNode);
		}
		else
		{
			// Try adding to root.  Didn't have a previous set to the last node.
			sleNode		= sleRoot;
		}

		svIndexes	= sleNode->getIFieldV256(sfIndexes);

		if (DIR_NODE_MAX != svIndexes.peekValue().size())
		{
			// Add to current node.
			entryModify(sleNode);
		}
		// Add to new node.
		else if (!++uNodeDir)
		{
			return terDIR_FULL;
		}
		else
		{
			// Have old last point to new node, if it was not root.
			if (uNodeDir == 1)
			{
				// Previous node is root node.

				sleRoot->setIFieldU64(sfIndexNext, uNodeDir);
			}
			else
			{
				// Previous node is not root node.

				SLE::pointer	slePrevious	= entryCache(ltDIR_NODE, Ledger::getDirNodeIndex(uRootIndex, uNodeDir-1));

				slePrevious->setIFieldU64(sfIndexNext, uNodeDir);
				entryModify(slePrevious);

				sleNode->setIFieldU64(sfIndexPrevious, uNodeDir-1);
			}

			// Have root point to new node.
			sleRoot->setIFieldU64(sfIndexPrevious, uNodeDir);
			entryModify(sleRoot);

			// Create the new node.
			sleNode		= entryCreate(ltDIR_NODE, Ledger::getDirNodeIndex(uRootIndex, uNodeDir));
			svIndexes	= STVector256();
		}
	}

	svIndexes.peekValue().push_back(uLedgerIndex);	// Append entry.
	sleNode->setIFieldV256(sfIndexes, svIndexes);	// Save entry.

	Log(lsINFO) << "dirAdd:   creating: root: " << uRootIndex.ToString();
	Log(lsINFO) << "dirAdd:  appending: Entry: " << uLedgerIndex.ToString();
	Log(lsINFO) << "dirAdd:  appending: Node: " << strHex(uNodeDir);
	// Log(lsINFO) << "dirAdd:  appending: PREV: " << svIndexes.peekValue()[0].ToString();

	return tesSUCCESS;
}

// Ledger must be in a state for this to work.
TER LedgerEntrySet::dirDelete(
	const bool						bKeepRoot,		// --> True, if we never completely clean up, after we overflow the root node.
	const uint64&					uNodeDir,		// --> Node containing entry.
	const uint256&					uRootIndex,		// --> The index of the base of the directory.  Nodes are based off of this.
	const uint256&					uLedgerIndex,	// --> Value to add to directory.
	const bool						bStable)		// --> True, not to change relative order of entries.
{
	uint64				uNodeCur	= uNodeDir;
	SLE::pointer		sleNode		= entryCache(ltDIR_NODE, uNodeCur ? Ledger::getDirNodeIndex(uRootIndex, uNodeCur) : uRootIndex);

	assert(sleNode);

	if (!sleNode)
	{
		Log(lsWARNING) << "dirDelete: no such node";

		return tefBAD_LEDGER;
	}

	STVector256						svIndexes	= sleNode->getIFieldV256(sfIndexes);
	std::vector<uint256>&			vuiIndexes	= svIndexes.peekValue();
	std::vector<uint256>::iterator	it;

	it = std::find(vuiIndexes.begin(), vuiIndexes.end(), uLedgerIndex);

	assert(vuiIndexes.end() != it);
	if (vuiIndexes.end() == it)
	{
		assert(false);

		Log(lsWARNING) << "dirDelete: no such entry";

		return tefBAD_LEDGER;
	}

	// Remove the element.
	if (vuiIndexes.size() > 1)
	{
		if (bStable)
		{
			vuiIndexes.erase(it);
		}
		else
		{
			*it = vuiIndexes[vuiIndexes.size()-1];
			vuiIndexes.resize(vuiIndexes.size()-1);
		}
	}
	else
	{
		vuiIndexes.clear();
	}

	sleNode->setIFieldV256(sfIndexes, svIndexes);
	entryModify(sleNode);

	if (vuiIndexes.empty())
	{
		// May be able to delete nodes.
		uint64				uNodePrevious	= sleNode->getIFieldU64(sfIndexPrevious);
		uint64				uNodeNext		= sleNode->getIFieldU64(sfIndexNext);

		if (!uNodeCur)
		{
			// Just emptied root node.

			if (!uNodePrevious)
			{
				// Never overflowed the root node.  Delete it.
				entryDelete(sleNode);
			}
			// Root overflowed.
			else if (bKeepRoot)
			{
				// If root overflowed and not allowed to delete overflowed root node.

				nothing();
			}
			else if (uNodePrevious != uNodeNext)
			{
				// Have more than 2 nodes.  Can't delete root node.

				nothing();
			}
			else
			{
				// Have only a root node and a last node.
				SLE::pointer		sleLast	= entryCache(ltDIR_NODE, Ledger::getDirNodeIndex(uRootIndex, uNodeNext));

				assert(sleLast);

				if (sleLast->getIFieldV256(sfIndexes).peekValue().empty())
				{
					// Both nodes are empty.

					entryDelete(sleNode);	// Delete root.
					entryDelete(sleLast);	// Delete last.
				}
				else
				{
					// Have an entry, can't delete root node.

					nothing();
				}
			}
		}
		// Just emptied a non-root node.
		else if (uNodeNext)
		{
			// Not root and not last node. Can delete node.

			SLE::pointer		slePrevious	= entryCache(ltDIR_NODE, uNodePrevious ? Ledger::getDirNodeIndex(uRootIndex, uNodePrevious) : uRootIndex);

			assert(slePrevious);

			SLE::pointer		sleNext		= entryCache(ltDIR_NODE, uNodeNext ? Ledger::getDirNodeIndex(uRootIndex, uNodeNext) : uRootIndex);

			assert(slePrevious);
			assert(sleNext);

			if (!slePrevious)
			{
				Log(lsWARNING) << "dirDelete: previous node is missing";

				return tefBAD_LEDGER;
			}

			if (!sleNext)
			{
				Log(lsWARNING) << "dirDelete: next node is missing";

				return tefBAD_LEDGER;
			}

			// Fix previous to point to its new next.
			slePrevious->setIFieldU64(sfIndexNext, uNodeNext);
			entryModify(slePrevious);

			// Fix next to point to its new previous.
			sleNext->setIFieldU64(sfIndexPrevious, uNodePrevious);
			entryModify(sleNext);
		}
		// Last node.
		else if (bKeepRoot || uNodePrevious)
		{
			// Not allowed to delete last node as root was overflowed.
			// Or, have pervious entries preventing complete delete.

			nothing();
		}
		else
		{
			// Last and only node besides the root.
			SLE::pointer			sleRoot	= entryCache(ltDIR_NODE, uRootIndex);

			assert(sleRoot);

			if (sleRoot->getIFieldV256(sfIndexes).peekValue().empty())
			{
				// Both nodes are empty.

				entryDelete(sleRoot);	// Delete root.
				entryDelete(sleNode);	// Delete last.
			}
			else
			{
				// Root has an entry, can't delete.

				nothing();
			}
		}
	}

	return tesSUCCESS;
}

// Return the first entry and advance uDirEntry.
// <-- true, if had a next entry.
bool LedgerEntrySet::dirFirst(
	const uint256& uRootIndex,	// --> Root of directory.
	SLE::pointer& sleNode,		// <-- current node
	unsigned int& uDirEntry,	// <-- next entry
	uint256& uEntryIndex)		// <-- The entry, if available. Otherwise, zero.
{
	sleNode		= entryCache(ltDIR_NODE, uRootIndex);
	uDirEntry	= 0;

	assert(sleNode);			// We never probe for directories.

	return LedgerEntrySet::dirNext(uRootIndex, sleNode, uDirEntry, uEntryIndex);
}

// Return the current entry and advance uDirEntry.
// <-- true, if had a next entry.
bool LedgerEntrySet::dirNext(
	const uint256& uRootIndex,	// --> Root of directory
	SLE::pointer& sleNode,		// <-> current node
	unsigned int& uDirEntry,	// <-> next entry
	uint256& uEntryIndex)		// <-- The entry, if available. Otherwise, zero.
{
	STVector256				svIndexes	= sleNode->getIFieldV256(sfIndexes);
	std::vector<uint256>&	vuiIndexes	= svIndexes.peekValue();

	if (uDirEntry == vuiIndexes.size())
	{
		uint64				uNodeNext	= sleNode->getIFieldU64(sfIndexNext);

		if (!uNodeNext)
		{
			uEntryIndex.zero();

			return false;
		}
		else
		{
			sleNode		= entryCache(ltDIR_NODE, Ledger::getDirNodeIndex(uRootIndex, uNodeNext));
			uDirEntry	= 0;

			return dirNext(uRootIndex, sleNode, uDirEntry, uEntryIndex);
		}
	}

	uEntryIndex	= vuiIndexes[uDirEntry++];
Log(lsINFO) << boost::str(boost::format("dirNext: uDirEntry=%d uEntryIndex=%s") % uDirEntry % uEntryIndex);

	return true;
}

// vim:ts=4
