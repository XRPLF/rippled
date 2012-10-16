
#include "LedgerEntrySet.h"

#include <boost/make_shared.hpp>

#include "Log.h"

SETUP_LOG();

// #define META_DEBUG

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
	std::map<uint256, LedgerEntrySetEntry>::iterator it = mEntries.find(index);
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
	std::map<uint256, LedgerEntrySetEntry>::const_iterator it = mEntries.find(index);
	if (it == mEntries.end())
		return taaNONE;
	return it->second.mAction;
}

void LedgerEntrySet::entryCache(SLE::ref sle)
{
	std::map<uint256, LedgerEntrySetEntry>::iterator it = mEntries.find(sle->getIndex());
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
	std::map<uint256, LedgerEntrySetEntry>::iterator it = mEntries.find(sle->getIndex());
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
	std::map<uint256, LedgerEntrySetEntry>::iterator it = mEntries.find(sle->getIndex());
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
	std::map<uint256, LedgerEntrySetEntry>::iterator it = mEntries.find(sle->getIndex());
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
	for (std::map<uint256, LedgerEntrySetEntry>::const_iterator it = mEntries.begin(),
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

	ret["metaData"] = mSet.getJson(0);

	return ret;
}

SLE::pointer LedgerEntrySet::getForMod(const uint256& node, Ledger::ref ledger,
	boost::unordered_map<uint256, SLE::pointer>& newMods)
{
	std::map<uint256, LedgerEntrySetEntry>::iterator it = mEntries.find(node);
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

bool LedgerEntrySet::threadTx(const NewcoinAddress& threadTo, Ledger::ref ledger,
	boost::unordered_map<uint256, SLE::pointer>& newMods)
{
#ifdef META_DEBUG
	cLog(lsTRACE) << "Thread to " << threadTo.getAccountID();
#endif
	SLE::pointer sle = getForMod(Ledger::getAccountRootIndex(threadTo.getAccountID()), ledger, newMods);
	if (!sle)
	{
		assert(false);
		return false;
	}
	return threadTx(sle, ledger, newMods);
}

bool LedgerEntrySet::threadTx(SLE::ref threadTo, Ledger::ref ledger,
	boost::unordered_map<uint256, SLE::pointer>& newMods)
{  // node = the node that was modified/deleted/created
   // threadTo = the node that needs to know
	uint256 prevTxID;
	uint32 prevLgrID;
	if (!threadTo->thread(mSet.getTxID(), mSet.getLgrSeq(), prevTxID, prevLgrID))
		return false;
	if (TransactionMetaSet::thread(mSet.getAffectedNode(threadTo->getIndex(), sfModifiedNode, false),
			prevTxID, prevLgrID))
		return true;
	assert(false);
	return false;
}

bool LedgerEntrySet::threadOwners(SLE::ref node, Ledger::ref ledger,
	boost::unordered_map<uint256, SLE::pointer>& newMods)
{ // thread new or modified node to owner or owners
	if (node->hasOneOwner()) // thread to owner's account
	{
#ifdef META_DEBUG
		cLog(lsTRACE) << "Thread to single owner";
#endif
		return threadTx(node->getOwner(), ledger, newMods);
	}
	else if (node->hasTwoOwners()) // thread to owner's accounts]
	{
#ifdef META_DEBUG
		cLog(lsTRACE) << "Thread to two owners";
#endif
		return
			threadTx(node->getFirstOwner(), ledger, newMods) &&
			threadTx(node->getSecondOwner(), ledger, newMods);
	}
	else
		return false;
}

void LedgerEntrySet::calcRawMeta(Serializer& s)
{ // calculate the raw meta data and return it. This must be called before the set is committed

	// Entries modified only as a result of building the transaction metadata
	boost::unordered_map<uint256, SLE::pointer> newMod;

	for (std::map<uint256, LedgerEntrySetEntry>::const_iterator it = mEntries.begin(),
			end = mEntries.end(); it != end; ++it)
	{
		SField::ptr type = &sfGeneric;

		switch (it->second.mAction)
		{
			case taaMODIFY:
#ifdef META_DEBUG
				cLog(lsTRACE) << "Modified Node " << it->first;
#endif
				type = &sfModifiedNode;
				break;

			case taaDELETE:
#ifdef META_DEBUG
				cLog(lsTRACE) << "Deleted Node " << it->first;
#endif
				type = &sfDeletedNode;
				break;

			case taaCREATE:
#ifdef META_DEBUG
				cLog(lsTRACE) << "Created Node " << it->first;
#endif
				type = &sfCreatedNode;
				break;

			default: // ignore these
				break;
		}

		if (type == &sfGeneric)
			continue;

		SLE::pointer origNode = mLedger->getSLE(it->first);

		if (origNode && (origNode->getType() == ltDIR_NODE)) // No metadata for dir nodes
			continue;

		SLE::pointer curNode = it->second.mEntry;
		STObject &metaNode = mSet.getAffectedNode(it->first, *type, true);

		if (type == &sfDeletedNode)
		{
			assert(origNode);
			threadOwners(origNode, mLedger, newMod);

			if (origNode->isFieldPresent(sfAmount))
			{ // node has an amount, covers ripple state nodes
				STAmount amount = origNode->getFieldAmount(sfAmount);
				if (amount.isNonZero())
					metaNode.setFieldAmount(sfPreviousBalance, amount);
				amount = curNode->getFieldAmount(sfAmount);
				if (amount.isNonZero())
					metaNode.setFieldAmount(sfFinalBalance, amount);

				if (origNode->getType() == ltRIPPLE_STATE)
				{
					metaNode.setFieldAccount(sfLowID,
						NewcoinAddress::createAccountID(origNode->getFieldAmount(sfLowLimit).getIssuer()));
					metaNode.setFieldAccount(sfHighID,
						NewcoinAddress::createAccountID(origNode->getFieldAmount(sfHighLimit).getIssuer()));
				}
			}

			if (origNode->getType() == ltOFFER)
			{ // check for non-zero balances
				STAmount amount = origNode->getFieldAmount(sfTakerPays);
				if (amount.isNonZero())
					metaNode.setFieldAmount(sfFinalTakerPays, amount);
				amount = origNode->getFieldAmount(sfTakerGets);
				if (amount.isNonZero())
					metaNode.setFieldAmount(sfFinalTakerGets, amount);
			}

		}

		if (type == &sfCreatedNode) // if created, thread to owner(s)
		{
			assert(!origNode);
			threadOwners(curNode, mLedger, newMod);
		}

		if ((type == &sfCreatedNode) || (type == &sfModifiedNode))
		{
			if (curNode->isThreadedType()) // always thread to self
				threadTx(curNode, mLedger, newMod);
		}

		if (type == &sfModifiedNode)
		{
			assert(origNode);
			if (origNode->isFieldPresent(sfAmount))
			{ // node has an amount, covers account root nodes and ripple nodes
				STAmount amount = origNode->getFieldAmount(sfAmount);
				if (amount != curNode->getFieldAmount(sfAmount))
					metaNode.setFieldAmount(sfPreviousBalance, amount);
			}

			if (origNode->getType() == ltOFFER)
			{
				STAmount amount = origNode->getFieldAmount(sfTakerPays);
				if (amount != curNode->getFieldAmount(sfTakerPays))
					metaNode.setFieldAmount(sfPreviousTakerPays, amount);
				amount = origNode->getFieldAmount(sfTakerGets);
				if (amount != curNode->getFieldAmount(sfTakerGets))
					metaNode.setFieldAmount(sfPreviousTakerGets, amount);
			}

		}
	}

	// add any new modified nodes to the modification set
	for (boost::unordered_map<uint256, SLE::pointer>::iterator it = newMod.begin(), end = newMod.end();
			it != end; ++it)
		entryModify(it->second);

#ifdef META_DEBUG
	cLog(lsINFO) << "Metadata:" << mSet.getJson(0);
#endif

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
		uNodeDir	= sleRoot->getFieldU64(sfIndexPrevious);		// Get index to last directory node.

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

		svIndexes	= sleNode->getFieldV256(sfIndexes);

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

				sleRoot->setFieldU64(sfIndexNext, uNodeDir);
			}
			else
			{
				// Previous node is not root node.

				SLE::pointer	slePrevious	= entryCache(ltDIR_NODE, Ledger::getDirNodeIndex(uRootIndex, uNodeDir-1));

				slePrevious->setFieldU64(sfIndexNext, uNodeDir);
				entryModify(slePrevious);

				sleNode->setFieldU64(sfIndexPrevious, uNodeDir-1);
			}

			// Have root point to new node.
			sleRoot->setFieldU64(sfIndexPrevious, uNodeDir);
			entryModify(sleRoot);

			// Create the new node.
			sleNode		= entryCreate(ltDIR_NODE, Ledger::getDirNodeIndex(uRootIndex, uNodeDir));
			svIndexes	= STVector256();
		}
	}

	svIndexes.peekValue().push_back(uLedgerIndex);	// Append entry.
	sleNode->setFieldV256(sfIndexes, svIndexes);	// Save entry.

	cLog(lsINFO) << "dirAdd:   creating: root: " << uRootIndex.ToString();
	cLog(lsINFO) << "dirAdd:  appending: Entry: " << uLedgerIndex.ToString();
	cLog(lsINFO) << "dirAdd:  appending: Node: " << strHex(uNodeDir);
	// cLog(lsINFO) << "dirAdd:  appending: PREV: " << svIndexes.peekValue()[0].ToString();

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
		cLog(lsWARNING) << "dirDelete: no such node";

		return tefBAD_LEDGER;
	}

	STVector256						svIndexes	= sleNode->getFieldV256(sfIndexes);
	std::vector<uint256>&			vuiIndexes	= svIndexes.peekValue();
	std::vector<uint256>::iterator	it;

	it = std::find(vuiIndexes.begin(), vuiIndexes.end(), uLedgerIndex);

	assert(vuiIndexes.end() != it);
	if (vuiIndexes.end() == it)
	{
		assert(false);

		cLog(lsWARNING) << "dirDelete: no such entry";

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

	sleNode->setFieldV256(sfIndexes, svIndexes);
	entryModify(sleNode);

	if (vuiIndexes.empty())
	{
		// May be able to delete nodes.
		uint64				uNodePrevious	= sleNode->getFieldU64(sfIndexPrevious);
		uint64				uNodeNext		= sleNode->getFieldU64(sfIndexNext);

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

				if (sleLast->getFieldV256(sfIndexes).peekValue().empty())
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
				cLog(lsWARNING) << "dirDelete: previous node is missing";

				return tefBAD_LEDGER;
			}

			if (!sleNext)
			{
				cLog(lsWARNING) << "dirDelete: next node is missing";

				return tefBAD_LEDGER;
			}

			// Fix previous to point to its new next.
			slePrevious->setFieldU64(sfIndexNext, uNodeNext);
			entryModify(slePrevious);

			// Fix next to point to its new previous.
			sleNext->setFieldU64(sfIndexPrevious, uNodePrevious);
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

			if (sleRoot->getFieldV256(sfIndexes).peekValue().empty())
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
	STVector256				svIndexes	= sleNode->getFieldV256(sfIndexes);
	std::vector<uint256>&	vuiIndexes	= svIndexes.peekValue();

	if (uDirEntry == vuiIndexes.size())
	{
		uint64				uNodeNext	= sleNode->getFieldU64(sfIndexNext);

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
	cLog(lsINFO) << boost::str(boost::format("dirNext: uDirEntry=%d uEntryIndex=%s") % uDirEntry % uEntryIndex);

	return true;
}

TER LedgerEntrySet::offerDelete(const SLE::pointer& sleOffer, const uint256& uOfferIndex, const uint160& uOwnerID)
{
	uint64	uOwnerNode	= sleOffer->getFieldU64(sfOwnerNode);
	TER		terResult	= dirDelete(false, uOwnerNode, Ledger::getOwnerDirIndex(uOwnerID), uOfferIndex, false);

	if (tesSUCCESS == terResult)
	{
		uint256		uDirectory	= sleOffer->getFieldH256(sfBookDirectory);
		uint64		uBookNode	= sleOffer->getFieldU64(sfBookNode);

		terResult	= dirDelete(false, uBookNode, uDirectory, uOfferIndex, true);
	}

	entryDelete(sleOffer);

	return terResult;
}

TER LedgerEntrySet::offerDelete(const uint256& uOfferIndex)
{
	SLE::pointer	sleOffer	= entryCache(ltOFFER, uOfferIndex);
	const uint160	uOwnerID	= sleOffer->getFieldAccount(sfAccount).getAccountID();

	return offerDelete(sleOffer, uOfferIndex, uOwnerID);
}

// Returns amount owed by uToAccountID to uFromAccountID.
// <-- $owed/uCurrencyID/uToAccountID: positive: uFromAccountID holds IOUs., negative: uFromAccountID owes IOUs.
STAmount LedgerEntrySet::rippleOwed(const uint160& uToAccountID, const uint160& uFromAccountID, const uint160& uCurrencyID)
{
	STAmount		saBalance;
	SLE::pointer	sleRippleState	= entryCache(ltRIPPLE_STATE, Ledger::getRippleStateIndex(uToAccountID, uFromAccountID, uCurrencyID));

	if (sleRippleState)
	{
		saBalance	= sleRippleState->getFieldAmount(sfBalance);
		if (uToAccountID < uFromAccountID)
			saBalance.negate();
		saBalance.setIssuer(uToAccountID);
	}
	else
	{
		cLog(lsINFO) << "rippleOwed: No credit line between "
			<< NewcoinAddress::createHumanAccountID(uFromAccountID)
			<< " and "
			<< NewcoinAddress::createHumanAccountID(uToAccountID)
			<< " for "
			<< STAmount::createHumanCurrency(uCurrencyID)
			<< "." ;

		assert(false);
	}

	return saBalance;
}

// Maximum amount of IOUs uToAccountID will hold from uFromAccountID.
// <-- $amount/uCurrencyID/uToAccountID.
STAmount LedgerEntrySet::rippleLimit(const uint160& uToAccountID, const uint160& uFromAccountID, const uint160& uCurrencyID)
{
	STAmount		saLimit;
	SLE::pointer	sleRippleState	= entryCache(ltRIPPLE_STATE, Ledger::getRippleStateIndex(uToAccountID, uFromAccountID, uCurrencyID));

	assert(sleRippleState);
	if (sleRippleState)
	{
		saLimit	= sleRippleState->getFieldAmount(uToAccountID < uFromAccountID ? sfLowLimit : sfHighLimit);
		saLimit.setIssuer(uToAccountID);
	}

	return saLimit;

}

uint32 LedgerEntrySet::rippleTransferRate(const uint160& uIssuerID)
{
	SLE::pointer	sleAccount	= entryCache(ltACCOUNT_ROOT, Ledger::getAccountRootIndex(uIssuerID));

	uint32			uQuality	= sleAccount && sleAccount->isFieldPresent(sfTransferRate)
									? sleAccount->getFieldU32(sfTransferRate)
									: QUALITY_ONE;

	cLog(lsINFO) << boost::str(boost::format("rippleTransferRate: uIssuerID=%s account_exists=%d transfer_rate=%f")
		% NewcoinAddress::createHumanAccountID(uIssuerID)
		% !!sleAccount
		% (uQuality/1000000000.0));

	assert(sleAccount);

	return uQuality;
}

uint32 LedgerEntrySet::rippleTransferRate(const uint160& uSenderID, const uint160& uReceiverID, const uint160& uIssuerID)
{
	return uSenderID == uIssuerID || uReceiverID == uIssuerID
			? QUALITY_ONE
			: rippleTransferRate(uIssuerID);
}

// XXX Might not need this, might store in nodes on calc reverse.
uint32 LedgerEntrySet::rippleQualityIn(const uint160& uToAccountID, const uint160& uFromAccountID, const uint160& uCurrencyID, SField::ref sfLow, SField::ref sfHigh)
{
	uint32			uQuality		= QUALITY_ONE;
	SLE::pointer	sleRippleState;

	if (uToAccountID == uFromAccountID)
	{
		nothing();
	}
	else
	{
		sleRippleState	= entryCache(ltRIPPLE_STATE, Ledger::getRippleStateIndex(uToAccountID, uFromAccountID, uCurrencyID));

		if (sleRippleState)
		{
			SField::ref	sfField	= uToAccountID < uFromAccountID ? sfLow: sfHigh;

			uQuality	= sleRippleState->isFieldPresent(sfField)
							? sleRippleState->getFieldU32(sfField)
							: QUALITY_ONE;

			if (!uQuality)
				uQuality	= 1;	// Avoid divide by zero.
		}
	}

	cLog(lsINFO) << boost::str(boost::format("rippleQuality: %s uToAccountID=%s uFromAccountID=%s uCurrencyID=%s bLine=%d uQuality=%f")
		% (sfLow == sfLowQualityIn ? "in" : "out")
		% NewcoinAddress::createHumanAccountID(uToAccountID)
		% NewcoinAddress::createHumanAccountID(uFromAccountID)
		% STAmount::createHumanCurrency(uCurrencyID)
		% !!sleRippleState
		% (uQuality/1000000000.0));

	assert(uToAccountID == uFromAccountID || !!sleRippleState);

	return uQuality;
}

// Return how much of uIssuerID's uCurrencyID IOUs that uAccountID holds.  May be negative.
// <-- IOU's uAccountID has of uIssuerID
STAmount LedgerEntrySet::rippleHolds(const uint160& uAccountID, const uint160& uCurrencyID, const uint160& uIssuerID)
{
	STAmount			saBalance;
	SLE::pointer		sleRippleState	= entryCache(ltRIPPLE_STATE, Ledger::getRippleStateIndex(uAccountID, uIssuerID, uCurrencyID));

	if (sleRippleState)
	{
		saBalance	= sleRippleState->getFieldAmount(sfBalance);

		if (uAccountID > uIssuerID)
			saBalance.negate();		// Put balance in uAccountID terms.
	}

	return saBalance;
}

// <-- saAmount: amount of uCurrencyID held by uAccountID. May be negative.
STAmount LedgerEntrySet::accountHolds(const uint160& uAccountID, const uint160& uCurrencyID, const uint160& uIssuerID)
{
	STAmount	saAmount;

	if (!uCurrencyID)
	{
		SLE::pointer	sleAccount	= entryCache(ltACCOUNT_ROOT, Ledger::getAccountRootIndex(uAccountID));

		saAmount	= sleAccount->getFieldAmount(sfBalance);
	}
	else
	{
		saAmount	= rippleHolds(uAccountID, uCurrencyID, uIssuerID);
	}

	cLog(lsINFO) << boost::str(boost::format("accountHolds: uAccountID=%s saAmount=%s")
		% NewcoinAddress::createHumanAccountID(uAccountID)
		% saAmount.getFullText());

	return saAmount;
}

// Returns the funds available for uAccountID for a currency/issuer.
// Use when you need a default for rippling uAccountID's currency.
// --> saDefault/currency/issuer
// <-- saFunds: Funds available. May be negative.
//              If the issuer is the same as uAccountID, funds are unlimited, use result is saDefault.
STAmount LedgerEntrySet::accountFunds(const uint160& uAccountID, const STAmount& saDefault)
{
	STAmount	saFunds;

	if (!saDefault.isNative() && saDefault.getIssuer() == uAccountID)
	{
		saFunds	= saDefault;

		cLog(lsINFO) << boost::str(boost::format("accountFunds: uAccountID=%s saDefault=%s SELF-FUNDED")
			% NewcoinAddress::createHumanAccountID(uAccountID)
			% saDefault.getFullText());
	}
	else
	{
		saFunds	= accountHolds(uAccountID, saDefault.getCurrency(), saDefault.getIssuer());

		cLog(lsINFO) << boost::str(boost::format("accountFunds: uAccountID=%s saDefault=%s saFunds=%s")
			% NewcoinAddress::createHumanAccountID(uAccountID)
			% saDefault.getFullText()
			% saFunds.getFullText());
	}

	return saFunds;
}

// Calculate transit fee.
STAmount LedgerEntrySet::rippleTransferFee(const uint160& uSenderID, const uint160& uReceiverID, const uint160& uIssuerID, const STAmount& saAmount)
{
	STAmount	saTransitFee;

	if (uSenderID != uIssuerID && uReceiverID != uIssuerID)
	{
		uint32		uTransitRate	= rippleTransferRate(uIssuerID);

		if (QUALITY_ONE != uTransitRate)
		{
			STAmount		saTransitRate(CURRENCY_ONE, uTransitRate, -9);

			saTransitFee	= STAmount::multiply(saAmount, saTransitRate, saAmount.getCurrency(), saAmount.getIssuer());
		}
	}

	return saTransitFee;
}

// Direct send w/o fees: redeeming IOUs and/or sending own IOUs.
void LedgerEntrySet::rippleCredit(const uint160& uSenderID, const uint160& uReceiverID, const STAmount& saAmount, bool bCheckIssuer)
{
	uint160				uIssuerID		= saAmount.getIssuer();
	uint160				uCurrencyID		= saAmount.getCurrency();

	assert(!bCheckIssuer || uSenderID == uIssuerID || uReceiverID == uIssuerID);

	bool				bFlipped		= uSenderID > uReceiverID;
	uint256				uIndex			= Ledger::getRippleStateIndex(uSenderID, uReceiverID, saAmount.getCurrency());
	SLE::pointer		sleRippleState	= entryCache(ltRIPPLE_STATE, uIndex);

	if (!sleRippleState)
	{
		cLog(lsINFO) << "rippleCredit: Creating ripple line: " << uIndex.ToString();

		STAmount	saBalance	= saAmount;

		saBalance.setIssuer(ACCOUNT_ONE);

		sleRippleState	= entryCreate(ltRIPPLE_STATE, uIndex);

		if (!bFlipped)
			saBalance.negate();

		sleRippleState->setFieldAmount(sfBalance, saBalance);
		sleRippleState->setFieldAmount(bFlipped ? sfHighLimit : sfLowLimit, STAmount(uCurrencyID, uSenderID));
		sleRippleState->setFieldAmount(bFlipped ? sfLowLimit : sfHighLimit, STAmount(uCurrencyID, uReceiverID));
	}
	else
	{
		STAmount	saBalance	= sleRippleState->getFieldAmount(sfBalance);

		if (!bFlipped)
			saBalance.negate();		// Put balance in low terms.

		saBalance	+= saAmount;

		if (!bFlipped)
			saBalance.negate();

		sleRippleState->setFieldAmount(sfBalance, saBalance);

		entryModify(sleRippleState);
	}
}

// Send regardless of limits.
// --> saAmount: Amount/currency/issuer for receiver to get.
// <-- saActual: Amount actually sent.  Sender pay's fees.
STAmount LedgerEntrySet::rippleSend(const uint160& uSenderID, const uint160& uReceiverID, const STAmount& saAmount)
{
	STAmount		saActual;
	const uint160	uIssuerID	= saAmount.getIssuer();

	assert(!!uSenderID && !!uReceiverID);

	if (uSenderID == uIssuerID || uReceiverID == uIssuerID)
	{
		// Direct send: redeeming IOUs and/or sending own IOUs.
		rippleCredit(uSenderID, uReceiverID, saAmount);

		saActual	= saAmount;
	}
	else
	{
		// Sending 3rd party IOUs: transit.

		STAmount		saTransitFee	= rippleTransferFee(uSenderID, uReceiverID, uIssuerID, saAmount);

		saActual	= !saTransitFee ? saAmount : saAmount+saTransitFee;

		saActual.setIssuer(uIssuerID);	// XXX Make sure this done in + above.

		rippleCredit(uIssuerID, uReceiverID, saAmount);
		rippleCredit(uSenderID, uIssuerID, saActual);
	}

	return saActual;
}

void LedgerEntrySet::accountSend(const uint160& uSenderID, const uint160& uReceiverID, const STAmount& saAmount)
{
	assert(!saAmount.isNegative());

	if (!saAmount)
	{
		nothing();
	}
	else if (saAmount.isNative())
	{
		SLE::pointer		sleSender	= !!uSenderID
											? entryCache(ltACCOUNT_ROOT, Ledger::getAccountRootIndex(uSenderID))
											: SLE::pointer();
		SLE::pointer		sleReceiver	= !!uReceiverID
											? entryCache(ltACCOUNT_ROOT, Ledger::getAccountRootIndex(uReceiverID))
											: SLE::pointer();

		cLog(lsINFO) << boost::str(boost::format("accountSend> %s (%s) -> %s (%s) : %s")
			% NewcoinAddress::createHumanAccountID(uSenderID)
			% (sleSender ? (sleSender->getFieldAmount(sfBalance)).getFullText() : "-")
			% NewcoinAddress::createHumanAccountID(uReceiverID)
			% (sleReceiver ? (sleReceiver->getFieldAmount(sfBalance)).getFullText() : "-")
			% saAmount.getFullText());

		if (sleSender)
		{
			sleSender->setFieldAmount(sfBalance, sleSender->getFieldAmount(sfBalance) - saAmount);
			entryModify(sleSender);
		}

		if (sleReceiver)
		{
			sleReceiver->setFieldAmount(sfBalance, sleReceiver->getFieldAmount(sfBalance) + saAmount);
			entryModify(sleReceiver);
		}

		cLog(lsINFO) << boost::str(boost::format("accountSend< %s (%s) -> %s (%s) : %s")
			% NewcoinAddress::createHumanAccountID(uSenderID)
			% (sleSender ? (sleSender->getFieldAmount(sfBalance)).getFullText() : "-")
			% NewcoinAddress::createHumanAccountID(uReceiverID)
			% (sleReceiver ? (sleReceiver->getFieldAmount(sfBalance)).getFullText() : "-")
			% saAmount.getFullText());
	}
	else
	{
		rippleSend(uSenderID, uReceiverID, saAmount);
	}
}
// vim:ts=4
