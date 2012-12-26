
#include "LedgerEntrySet.h"

#include <boost/bind.hpp>
#include <boost/make_shared.hpp>
#include <boost/foreach.hpp>

#include "Log.h"

SETUP_LOG();

DECLARE_INSTANCE(LedgerEntrySetEntry);
DECLARE_INSTANCE(LedgerEntrySet)

// #define META_DEBUG

// Small for testing, should likely be 32 or 64.
#define DIR_NODE_MAX		2

void LedgerEntrySet::init(Ledger::ref ledger, const uint256& transactionID, uint32 ledgerID)
{
	mEntries.clear();
	mLedger	= ledger;
	mSet.init(transactionID, ledgerID);
	mSeq	= 0;
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
	SLE::pointer sleNew = boost::make_shared<SLE>(letType, index);
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
		{
			cLog(lsFATAL) << "Trying to thread to deleted node";
			return SLE::pointer();
		}
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
	{
		assert(me->second);
		return me->second;
	}

	SLE::pointer ret = ledger->getSLE(node);
	if (ret)
		newMods.insert(std::make_pair(node, ret));
	return ret;

}

bool LedgerEntrySet::threadTx(const RippleAddress& threadTo, Ledger::ref ledger,
	boost::unordered_map<uint256, SLE::pointer>& newMods)
{
#ifdef META_DEBUG
	cLog(lsTRACE) << "Thread to " << threadTo.getAccountID();
#endif
	SLE::pointer sle = getForMod(Ledger::getAccountRootIndex(threadTo.getAccountID()), ledger, newMods);
	if (!sle)
	{
		cLog(lsFATAL) << "Threading to non-existent account: " << threadTo.humanAccountID();
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

	if (prevTxID.isZero() ||
		TransactionMetaSet::thread(mSet.getAffectedNode(threadTo, sfModifiedNode), prevTxID, prevLgrID))
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
	else if (node->hasTwoOwners()) // thread to owner's accounts
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

void LedgerEntrySet::calcRawMeta(Serializer& s, TER result, uint32 index)
{ // calculate the raw meta data and return it. This must be called before the set is committed

	// Entries modified only as a result of building the transaction metadata
	boost::unordered_map<uint256, SLE::pointer> newMod;

	typedef std::map<uint256, LedgerEntrySetEntry>::value_type u256_LES_pair;
	BOOST_FOREACH(u256_LES_pair& it, mEntries)
	{
		SField::ptr type = &sfGeneric;

		switch (it.second.mAction)
		{
			case taaMODIFY:
#ifdef META_DEBUG
				cLog(lsTRACE) << "Modified Node " << it.first;
#endif
				type = &sfModifiedNode;
				break;

			case taaDELETE:
#ifdef META_DEBUG
				cLog(lsTRACE) << "Deleted Node " << it.first;
#endif
				type = &sfDeletedNode;
				break;

			case taaCREATE:
#ifdef META_DEBUG
				cLog(lsTRACE) << "Created Node " << it.first;
#endif
				type = &sfCreatedNode;
				break;

			default: // ignore these
				break;
		}

		if (type == &sfGeneric)
			continue;

		SLE::pointer origNode = mLedger->getSLE(it.first);
		SLE::pointer curNode = it.second.mEntry;

		if ((type == &sfModifiedNode) && (*curNode == *origNode))
			continue;

		uint16 nodeType = curNode ? curNode->getFieldU16(sfLedgerEntryType) : origNode->getFieldU16(sfLedgerEntryType);

		mSet.setAffectedNode(it.first, *type, nodeType);

		if (type == &sfDeletedNode)
		{
			assert(origNode && curNode);
			threadOwners(origNode, mLedger, newMod); // thread transaction to owners

			STObject prevs(sfPreviousFields);
			BOOST_FOREACH(const SerializedType& obj, *origNode)
			{ // go through the original node for modified fields saved on modification
				if (obj.getFName().shouldMeta(SField::sMD_ChangeOrig) && !curNode->hasMatchingEntry(obj))
					prevs.addObject(obj);
			}
			if (!prevs.empty())
				mSet.getAffectedNode(it.first).addObject(prevs);

			STObject finals(sfFinalFields);
			BOOST_FOREACH(const SerializedType& obj, *curNode)
			{ // go through the final node for final fields
				if (obj.getFName().shouldMeta(SField::sMD_Always | SField::sMD_DeleteFinal))
					finals.addObject(obj);
			}
			if (!finals.empty())
				mSet.getAffectedNode(it.first).addObject(finals);
		}
		else if (type == &sfModifiedNode)
		{
			assert(curNode && origNode);
			if (curNode->isThreadedType()) // thread transaction to node it modified
				threadTx(curNode, mLedger, newMod);

			STObject prevs(sfPreviousFields);
			BOOST_FOREACH(const SerializedType& obj, *origNode)
			{ // search the original node for values saved on modify
				if (obj.getFName().shouldMeta(SField::sMD_ChangeOrig) && !curNode->hasMatchingEntry(obj))
					prevs.addObject(obj);
			}
			if (!prevs.empty())
				mSet.getAffectedNode(it.first).addObject(prevs);

			STObject finals(sfFinalFields);
			BOOST_FOREACH(const SerializedType& obj, *curNode)
			{ // search the final node for values saved always
				if (obj.getFName().shouldMeta(SField::sMD_Always | SField::sMD_ChangeNew))
					finals.addObject(obj);
			}
			if (!finals.empty())
				mSet.getAffectedNode(it.first).addObject(finals);
		}
		else if (type == &sfCreatedNode) // if created, thread to owner(s)
		{
			assert(curNode && !origNode);
			threadOwners(curNode, mLedger, newMod);

			if (curNode->isThreadedType()) // always thread to self
				threadTx(curNode, mLedger, newMod);

			STObject news(sfNewFields);
			BOOST_FOREACH(const SerializedType& obj, *curNode)
			{ // save non-default values
				if (!obj.isDefault() && obj.getFName().shouldMeta(SField::sMD_Create | SField::sMD_Always))
					news.addObject(obj);
			}
			if (!news.empty())
				mSet.getAffectedNode(it.first).addObject(news);
		}
		else assert(false);
	}

	// add any new modified nodes to the modification set
	typedef std::map<uint256, SLE::pointer>::value_type u256_sle_pair;
	BOOST_FOREACH(u256_sle_pair& it, newMod)
		entryModify(it.second);

	mSet.addRaw(s, result, index);
	cLog(lsTRACE) << "Metadata:" << mSet.getJson(0);
}

TER LedgerEntrySet::dirCount(const uint256& uRootIndex, uint32& uCount)
{
	uint64	uNodeDir	= 0;

	uCount	= 0;

	do
	{
		SLE::pointer	sleNode	= entryCache(ltDIR_NODE, Ledger::getDirNodeIndex(uRootIndex, uNodeDir));

		if (sleNode)
		{
			uCount		+= sleNode->getFieldV256(sfIndexes).peekValue().size();

			uNodeDir	= sleNode->getFieldU64(sfIndexNext);		// Get next node.
		}
		else if (uNodeDir)
		{
			cLog(lsWARNING) << "dirCount: no such node";

			assert(false);

			return tefBAD_LEDGER;
		}
	} while (uNodeDir);

	return tesSUCCESS;
}

// <--     uNodeDir: For deletion, present to make dirDelete efficient.
// -->   uRootIndex: The index of the base of the directory.  Nodes are based off of this.
// --> uLedgerIndex: Value to add to directory.
// We only append. This allow for things that watch append only structure to just monitor from the last node on ward.
// Within a node with no deletions order of elements is sequential.  Otherwise, order of elements is random.
TER LedgerEntrySet::dirAdd(
	uint64&								uNodeDir,
	const uint256&						uRootIndex,
	const uint256&						uLedgerIndex,
	boost::function<void (SLE::ref)>	fDescriber)
{
	SLE::pointer		sleNode;
	STVector256			svIndexes;
	SLE::pointer		sleRoot		= entryCache(ltDIR_NODE, uRootIndex);

	if (!sleRoot)
	{
		// No root, make it.
		sleRoot		= entryCreate(ltDIR_NODE, uRootIndex);
		sleRoot->setFieldH256(sfRootIndex, uRootIndex);
		fDescriber(sleRoot);

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
			return tecDIR_FULL;
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
			sleNode->setFieldH256(sfRootIndex, uRootIndex);
			fDescriber(sleNode);

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
	const uint256&					uLedgerIndex,	// --> Value to remove from directory.
	const bool						bStable)		// --> True, not to change relative order of entries.
{
	uint64				uNodeCur	= uNodeDir;
	SLE::pointer		sleNode		= entryCache(ltDIR_NODE, uNodeCur ? Ledger::getDirNodeIndex(uRootIndex, uNodeCur) : uRootIndex);

	if (!sleNode)
	{
		cLog(lsWARNING)
			<< boost::str(boost::format("dirDelete: no such node: uRootIndex=%s uNodeDir=%s uLedgerIndex=%s")
				% uRootIndex.ToString()
				% strHex(uNodeDir)
				% uLedgerIndex.ToString());

		assert(false);
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

// If there is a count, adjust the owner count by iAmount. Otherwise, compute the owner count and store it.
void LedgerEntrySet::ownerCountAdjust(const uint160& uOwnerID, int iAmount, SLE::ref sleAccountRoot)
{
	SLE::pointer	sleHold	= sleAccountRoot
								? SLE::pointer()
								: entryCache(ltACCOUNT_ROOT, Ledger::getAccountRootIndex(uOwnerID));

	SLE::ref		sleRoot	= sleAccountRoot
								? sleAccountRoot
								: sleHold;

	const uint32	uOwnerCount		= sleRoot->getFieldU32(sfOwnerCount);

	if (iAmount + int(uOwnerCount) >= 0)
		sleRoot->setFieldU32(sfOwnerCount, uOwnerCount+iAmount);
}

TER LedgerEntrySet::offerDelete(const SLE::pointer& sleOffer, const uint256& uOfferIndex, const uint160& uOwnerID)
{
	uint64	uOwnerNode	= sleOffer->getFieldU64(sfOwnerNode);
	TER		terResult	= dirDelete(false, uOwnerNode, Ledger::getOwnerDirIndex(uOwnerID), uOfferIndex, false);

	if (tesSUCCESS == terResult)
	{
		ownerCountAdjust(uOwnerID, -1);

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
			<< RippleAddress::createHumanAccountID(uFromAccountID)
			<< " and "
			<< RippleAddress::createHumanAccountID(uToAccountID)
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
		% RippleAddress::createHumanAccountID(uIssuerID)
		% !!sleAccount
		% (uQuality/1000000000.0));

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
		% RippleAddress::createHumanAccountID(uToAccountID)
		% RippleAddress::createHumanAccountID(uFromAccountID)
		% STAmount::createHumanCurrency(uCurrencyID)
		% !!sleRippleState
		% (uQuality/1000000000.0));

	assert(uToAccountID == uFromAccountID || !!sleRippleState);

	return uQuality;
}

// Return how much of uIssuerID's uCurrencyID IOUs that uAccountID holds.  May be negative.
// <-- IOU's uAccountID has of uIssuerID.
STAmount LedgerEntrySet::rippleHolds(const uint160& uAccountID, const uint160& uCurrencyID, const uint160& uIssuerID, bool bAvail)
{
	STAmount			saBalance;
	SLE::pointer		sleRippleState	= entryCache(ltRIPPLE_STATE, Ledger::getRippleStateIndex(uAccountID, uIssuerID, uCurrencyID));

	if (!sleRippleState)
	{
		saBalance.zero(uCurrencyID, uIssuerID);
	}
	else if (uAccountID > uIssuerID)
	{
		if (false && bAvail)
		{
			saBalance	= sleRippleState->getFieldAmount(sfLowLimit);
			saBalance	-= sleRippleState->getFieldAmount(sfBalance);
		}
		else
		{
			saBalance	= sleRippleState->getFieldAmount(sfBalance);
			saBalance.negate();		// Put balance in uAccountID terms.
		}

		saBalance.setIssuer(uIssuerID);
	}
	else
	{
		if (false && bAvail)
		{
			saBalance	= sleRippleState->getFieldAmount(sfHighLimit);
			saBalance	+= sleRippleState->getFieldAmount(sfBalance);
		}
		else
		{
			saBalance	= sleRippleState->getFieldAmount(sfBalance);
		}

		saBalance.setIssuer(uIssuerID);
	}

	return saBalance;
}

// <-- saAmount: amount of uCurrencyID held by uAccountID. May be negative.
STAmount LedgerEntrySet::accountHolds(const uint160& uAccountID, const uint160& uCurrencyID, const uint160& uIssuerID, bool bAvail)
{
	STAmount	saAmount;

	if (!uCurrencyID)
	{
		SLE::pointer	sleAccount	= entryCache(ltACCOUNT_ROOT, Ledger::getAccountRootIndex(uAccountID));
		uint64			uReserve	= mLedger->getReserve(sleAccount->getFieldU32(sfOwnerCount));

		saAmount	= sleAccount->getFieldAmount(sfBalance)-uReserve;

		if (saAmount < uReserve)
		{
			saAmount.zero();
		}
		else
		{
			saAmount	-= uReserve;
		}
	}
	else
	{
		saAmount	= rippleHolds(uAccountID, uCurrencyID, uIssuerID, bAvail);
	}

	cLog(lsINFO) << boost::str(boost::format("accountHolds: uAccountID=%s saAmount=%s bAvail=%d")
		% RippleAddress::createHumanAccountID(uAccountID)
		% saAmount.getFullText()
		% bAvail);

	return saAmount;
}

// Returns the funds available for uAccountID for a currency/issuer.
// Use when you need a default for rippling uAccountID's currency.
// XXX Should take into account quality?
// --> saDefault/currency/issuer
// --> bAvail: true to include going into debt.
// <-- saFunds: Funds available. May be negative.
//              If the issuer is the same as uAccountID, funds are unlimited, use result is saDefault.
STAmount LedgerEntrySet::accountFunds(const uint160& uAccountID, const STAmount& saDefault, bool bAvail)
{
	STAmount	saFunds;

	if (!saDefault.isNative() && saDefault.getIssuer() == uAccountID)
	{
		saFunds	= saDefault;

		cLog(lsINFO) << boost::str(boost::format("accountFunds: uAccountID=%s saDefault=%s SELF-FUNDED")
			% RippleAddress::createHumanAccountID(uAccountID)
			% saDefault.getFullText());
	}
	else
	{
		saFunds	= accountHolds(uAccountID, saDefault.getCurrency(), saDefault.getIssuer(), bAvail);

		cLog(lsINFO) << boost::str(boost::format("accountFunds: uAccountID=%s saDefault=%s saFunds=%s bAvail=%d")
			% RippleAddress::createHumanAccountID(uAccountID)
			% saDefault.getFullText()
			% saFunds.getFullText()
			% bAvail);
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

TER LedgerEntrySet::trustCreate(
	const bool		bSrcHigh,			// Who to charge with reserve.
	const uint160&	uSrcAccountID,
	SLE::ref		sleSrcAccount,
	const uint160&	uDstAccountID,
	const uint256&	uIndex,
	const STAmount& saSrcBalance,		// Issuer should be ACCOUNT_ONE
	const STAmount& saSrcLimit,
	const uint32	uSrcQualityIn,
	const uint32	uSrcQualityOut)
{
	const uint160&	uLowAccountID	= !bSrcHigh ? uSrcAccountID : uDstAccountID;
	const uint160&	uHighAccountID	=  bSrcHigh ? uSrcAccountID : uDstAccountID;

	SLE::pointer	sleRippleState	= entryCreate(ltRIPPLE_STATE, uIndex);

	uint64			uLowNode;
	uint64			uHighNode;

	TER terResult	= dirAdd(
		uLowNode,
		Ledger::getOwnerDirIndex(uLowAccountID),
		sleRippleState->getIndex(),
		boost::bind(&Ledger::ownerDirDescriber, _1, uLowAccountID));

	if (tesSUCCESS == terResult)
	{
		terResult	= dirAdd(
			uHighNode,
			Ledger::getOwnerDirIndex(uHighAccountID),
			sleRippleState->getIndex(),
			boost::bind(&Ledger::ownerDirDescriber, _1, uHighAccountID));
	}

	if (tesSUCCESS == terResult)
	{
		sleRippleState->setFieldU64(sfLowNode, uLowNode);
		sleRippleState->setFieldU64(sfHighNode, uHighNode);

		sleRippleState->setFieldAmount(!bSrcHigh ? sfLowLimit : sfHighLimit, saSrcLimit);
		sleRippleState->setFieldAmount( bSrcHigh ? sfLowLimit : sfHighLimit, STAmount(saSrcBalance.getCurrency(), uDstAccountID));

		if (uSrcQualityIn)
			sleRippleState->setFieldU32(bSrcHigh ? sfHighQualityIn : sfLowQualityIn, uSrcQualityIn);

		if (uSrcQualityOut)
			sleRippleState->setFieldU32(bSrcHigh ? sfHighQualityOut : sfLowQualityOut, uSrcQualityIn);

		sleRippleState->setFieldU32(sfFlags, !bSrcHigh ? lsfLowReserve : lsfHighReserve);

		ownerCountAdjust(uSrcAccountID, 1, sleSrcAccount);

		sleRippleState->setFieldAmount(sfBalance, bSrcHigh ? -saSrcBalance: saSrcBalance);
	}

	return terResult;
}

// Direct send w/o fees: redeeming IOUs and/or sending own IOUs.
TER LedgerEntrySet::rippleCredit(const uint160& uSenderID, const uint160& uReceiverID, const STAmount& saAmount, bool bCheckIssuer)
{
	uint160				uIssuerID		= saAmount.getIssuer();
	uint160				uCurrencyID		= saAmount.getCurrency();

	assert(!bCheckIssuer || uSenderID == uIssuerID || uReceiverID == uIssuerID);

	bool				bSenderHigh		= uSenderID > uReceiverID;
	uint256				uIndex			= Ledger::getRippleStateIndex(uSenderID, uReceiverID, saAmount.getCurrency());
	SLE::pointer		sleRippleState	= entryCache(ltRIPPLE_STATE, uIndex);

	TER					terResult;

	assert(!!uSenderID && uSenderID != ACCOUNT_ONE);
	assert(!!uReceiverID && uReceiverID != ACCOUNT_ONE);

	if (!sleRippleState)
	{
		STAmount	saSrcLimit	= STAmount(uCurrencyID, uSenderID);
		STAmount	saBalance	= saAmount;

		saBalance.setIssuer(ACCOUNT_ONE);

		cLog(lsDEBUG) << boost::str(boost::format("rippleCredit: create line: %s (%s) -> %s : %s")
			% RippleAddress::createHumanAccountID(uSenderID)
			% saBalance.getFullText()
			% RippleAddress::createHumanAccountID(uReceiverID)
			% saAmount.getFullText());

		terResult	= trustCreate(
			bSenderHigh,
			uSenderID,
			entryCache(ltACCOUNT_ROOT, Ledger::getAccountRootIndex(uSenderID)),
			uReceiverID,
			uIndex,
			saBalance,
			saSrcLimit);
	}
	else
	{
		STAmount	saBalance	= sleRippleState->getFieldAmount(sfBalance);

		if (!bSenderHigh)
			saBalance.negate();		// Put balance in low terms.

		cLog(lsDEBUG) << boost::str(boost::format("rippleCredit> %s (%s) -> %s : %s")
			% RippleAddress::createHumanAccountID(uSenderID)
			% saBalance.getFullText()
			% RippleAddress::createHumanAccountID(uReceiverID)
			% saAmount.getFullText());

		saBalance	+= saAmount;

		if (!bSenderHigh)
			saBalance.negate();

		sleRippleState->setFieldAmount(sfBalance, saBalance);

		entryModify(sleRippleState);

		terResult	= tesSUCCESS;
	}

	return terResult;
}

// Send regardless of limits.
// --> saAmount: Amount/currency/issuer for receiver to get.
// <-- saActual: Amount actually sent.  Sender pay's fees.
TER LedgerEntrySet::rippleSend(const uint160& uSenderID, const uint160& uReceiverID, const STAmount& saAmount, STAmount& saActual)
{
	const uint160	uIssuerID	= saAmount.getIssuer();
	TER				terResult;

	assert(!!uSenderID && !!uReceiverID);

	if (uSenderID == uIssuerID || uReceiverID == uIssuerID || uIssuerID == ACCOUNT_ONE)
	{
		// Direct send: redeeming IOUs and/or sending own IOUs.
		terResult	= rippleCredit(uSenderID, uReceiverID, saAmount, false);

		saActual	= saAmount;

		terResult	= tesSUCCESS;
	}
	else
	{
		// Sending 3rd party IOUs: transit.

		STAmount		saTransitFee	= rippleTransferFee(uSenderID, uReceiverID, uIssuerID, saAmount);

		saActual	= !saTransitFee ? saAmount : saAmount+saTransitFee;

		saActual.setIssuer(uIssuerID);	// XXX Make sure this done in + above.

		terResult	= rippleCredit(uIssuerID, uReceiverID, saAmount);

		if (tesSUCCESS == terResult)
			terResult	= rippleCredit(uSenderID, uIssuerID, saActual);
	}

	return terResult;
}

TER LedgerEntrySet::accountSend(const uint160& uSenderID, const uint160& uReceiverID, const STAmount& saAmount)
{
	assert(!saAmount.isNegative());
	TER	terResult	= tesSUCCESS;

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
			% RippleAddress::createHumanAccountID(uSenderID)
			% (sleSender ? (sleSender->getFieldAmount(sfBalance)).getFullText() : "-")
			% RippleAddress::createHumanAccountID(uReceiverID)
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
			% RippleAddress::createHumanAccountID(uSenderID)
			% (sleSender ? (sleSender->getFieldAmount(sfBalance)).getFullText() : "-")
			% RippleAddress::createHumanAccountID(uReceiverID)
			% (sleReceiver ? (sleReceiver->getFieldAmount(sfBalance)).getFullText() : "-")
			% saAmount.getFullText());
	}
	else
	{
		STAmount	saActual;

		terResult	= rippleSend(uSenderID, uReceiverID, saAmount, saActual);
	}

	return terResult;
}

// vim:ts=4
