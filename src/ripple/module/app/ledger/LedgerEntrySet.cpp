//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

namespace ripple {

SETUP_LOG (LedgerEntrySet)

// #define META_DEBUG

// VFALCO TODO Replace this macro with a documented language constant
//        NOTE Is this part of the protocol?
//
#define DIR_NODE_MAX        32

void LedgerEntrySet::init (Ledger::ref ledger, uint256 const& transactionID,
                           std::uint32_t ledgerID, TransactionEngineParams params)
{
    mEntries.clear ();
    mLedger = ledger;
    mSet.init (transactionID, ledgerID);
    mParams = params;
    mSeq    = 0;
}

void LedgerEntrySet::clear ()
{
    mEntries.clear ();
    mSet.clear ();
}

LedgerEntrySet LedgerEntrySet::duplicate () const
{
    return LedgerEntrySet (mLedger, mEntries, mSet, mSeq + 1);
}

void LedgerEntrySet::setTo (const LedgerEntrySet& e)
{
    mLedger = e.mLedger;
    mEntries = e.mEntries;
    mSet = e.mSet;
    mParams = e.mParams;
    mSeq = e.mSeq;
}

void LedgerEntrySet::swapWith (LedgerEntrySet& e)
{
    std::swap (mLedger, e.mLedger);
    mEntries.swap (e.mEntries);
    mSet.swap (e.mSet);
    std::swap (mParams, e.mParams);
    std::swap (mSeq, e.mSeq);
}

// Find an entry in the set.  If it has the wrong sequence number, copy it and update the sequence number.
// This is basically: copy-on-read.
SLE::pointer LedgerEntrySet::getEntry (uint256 const& index, LedgerEntryAction& action)
{
    auto it = mEntries.find (index);

    if (it == mEntries.end ())
    {
        action = taaNONE;
        return SLE::pointer ();
    }

    if (it->second.mSeq != mSeq)
    {
        assert (it->second.mSeq < mSeq);
        it->second.mEntry = std::make_shared<SerializedLedgerEntry> (*it->second.mEntry);
        it->second.mSeq = mSeq;
    }

    action = it->second.mAction;
    return it->second.mEntry;
}

SLE::pointer LedgerEntrySet::entryCreate (LedgerEntryType letType, uint256 const& index)
{
    assert (index.isNonZero ());
    SLE::pointer sleNew = std::make_shared<SLE> (letType, index);
    entryCreate (sleNew);
    return sleNew;
}

SLE::pointer LedgerEntrySet::entryCache (LedgerEntryType letType, uint256 const& index)
{
    assert (mLedger);
    SLE::pointer sleEntry;

    if (index.isNonZero ())
    {
        LedgerEntryAction action;
        sleEntry = getEntry (index, action);

        if (!sleEntry)
        {
            assert (action != taaDELETE);
            sleEntry = mImmutable ? mLedger->getSLEi (index) : mLedger->getSLE (index);

            if (sleEntry)
                entryCache (sleEntry);
        }
        else if (action == taaDELETE)
            sleEntry.reset ();
    }

    return sleEntry;
}

LedgerEntryAction LedgerEntrySet::hasEntry (uint256 const& index) const
{
    std::map<uint256, LedgerEntrySetEntry>::const_iterator it = mEntries.find (index);

    if (it == mEntries.end ())
        return taaNONE;

    return it->second.mAction;
}

void LedgerEntrySet::entryCache (SLE::ref sle)
{
    assert (mLedger);
    assert (sle->isMutable () || mImmutable); // Don't put an immutable SLE in a mutable LES
    auto it = mEntries.find (sle->getIndex ());

    if (it == mEntries.end ())
    {
        mEntries.insert (std::make_pair (sle->getIndex (), LedgerEntrySetEntry (sle, taaCACHED, mSeq)));
        return;
    }

    switch (it->second.mAction)
    {
    case taaCACHED:
        assert (sle == it->second.mEntry);
        it->second.mSeq     = mSeq;
        it->second.mEntry   = sle;
        return;

    default:
        throw std::runtime_error ("Cache after modify/delete/create");
    }
}

void LedgerEntrySet::entryCreate (SLE::ref sle)
{
    assert (mLedger && !mImmutable);
    assert (sle->isMutable ());
    auto it = mEntries.find (sle->getIndex ());

    if (it == mEntries.end ())
    {
        mEntries.insert (std::make_pair (sle->getIndex (), LedgerEntrySetEntry (sle, taaCREATE, mSeq)));
        return;
    }

    switch (it->second.mAction)
    {

    case taaDELETE:
        WriteLog (lsDEBUG, LedgerEntrySet) << "Create after Delete = Modify";
        it->second.mEntry = sle;
        it->second.mAction = taaMODIFY;
        it->second.mSeq = mSeq;
        break;

    case taaMODIFY:
        throw std::runtime_error ("Create after modify");

    case taaCREATE:
        throw std::runtime_error ("Create after create"); // This could be made to work

    case taaCACHED:
        throw std::runtime_error ("Create after cache");

    default:
        throw std::runtime_error ("Unknown taa");
    }

    assert (it->second.mSeq == mSeq);
}

void LedgerEntrySet::entryModify (SLE::ref sle)
{
    assert (sle->isMutable () && !mImmutable);
    assert (mLedger);
    auto it = mEntries.find (sle->getIndex ());

    if (it == mEntries.end ())
    {
        mEntries.insert (std::make_pair (sle->getIndex (), LedgerEntrySetEntry (sle, taaMODIFY, mSeq)));
        return;
    }

    assert (it->second.mSeq == mSeq);
    assert (it->second.mEntry == sle);

    switch (it->second.mAction)
    {
    case taaCACHED:
        it->second.mAction  = taaMODIFY;

        // Fall through

    case taaCREATE:
    case taaMODIFY:
        it->second.mSeq     = mSeq;
        it->second.mEntry   = sle;
        break;

    case taaDELETE:
        throw std::runtime_error ("Modify after delete");

    default:
        throw std::runtime_error ("Unknown taa");
    }
}

void LedgerEntrySet::entryDelete (SLE::ref sle)
{
    assert (sle->isMutable () && !mImmutable);
    assert (mLedger);
    auto it = mEntries.find (sle->getIndex ());

    if (it == mEntries.end ())
    {
        assert (false); // deleting an entry not cached?
        mEntries.insert (std::make_pair (sle->getIndex (), LedgerEntrySetEntry (sle, taaDELETE, mSeq)));
        return;
    }

    assert (it->second.mSeq == mSeq);
    assert (it->second.mEntry == sle);

    switch (it->second.mAction)
    {
    case taaCACHED:
    case taaMODIFY:
        it->second.mSeq     = mSeq;
        it->second.mEntry   = sle;
        it->second.mAction  = taaDELETE;
        break;

    case taaCREATE:
        mEntries.erase (it);
        break;

    case taaDELETE:
        break;

    default:
        throw std::runtime_error ("Unknown taa");
    }
}

bool LedgerEntrySet::hasChanges ()
{
    for (auto const& it : mEntries)
        if (it.second.mAction != taaCACHED)
            return true;

    return false;
}

Json::Value LedgerEntrySet::getJson (int) const
{
    Json::Value ret (Json::objectValue);

    Json::Value nodes (Json::arrayValue);

    for (auto it = mEntries.begin (), end = mEntries.end (); it != end; ++it)
    {
        Json::Value entry (Json::objectValue);
        entry["node"] = to_string (it->first);

        switch (it->second.mEntry->getType ())
        {
        case ltINVALID:
            entry["type"] = "invalid";
            break;

        case ltACCOUNT_ROOT:
            entry["type"] = "acccount_root";
            break;

        case ltDIR_NODE:
            entry["type"] = "dir_node";
            break;

        case ltGENERATOR_MAP:
            entry["type"] = "generator_map";
            break;

        case ltRIPPLE_STATE:
            entry["type"] = "ripple_state";
            break;

        case ltNICKNAME:
            entry["type"] = "nickname";
            break;

        case ltOFFER:
            entry["type"] = "offer";
            break;

        default:
            assert (false);
        }

        switch (it->second.mAction)
        {
        case taaCACHED:
            entry["action"] = "cache";
            break;

        case taaMODIFY:
            entry["action"] = "modify";
            break;

        case taaDELETE:
            entry["action"] = "delete";
            break;

        case taaCREATE:
            entry["action"] = "create";
            break;

        default:
            assert (false);
        }

        nodes.append (entry);
    }

    ret["nodes" ] = nodes;

    ret["metaData"] = mSet.getJson (0);

    return ret;
}

SLE::pointer LedgerEntrySet::getForMod (uint256 const& node, Ledger::ref ledger,
                                        NodeToLedgerEntry& newMods)
{
    auto it = mEntries.find (node);

    if (it != mEntries.end ())
    {
        if (it->second.mAction == taaDELETE)
        {
            WriteLog (lsFATAL, LedgerEntrySet) << "Trying to thread to deleted node";
            return SLE::pointer ();
        }

        if (it->second.mAction == taaCACHED)
            it->second.mAction = taaMODIFY;

        if (it->second.mSeq != mSeq)
        {
            it->second.mEntry = std::make_shared<SerializedLedgerEntry> (*it->second.mEntry);
            it->second.mSeq = mSeq;
        }

        return it->second.mEntry;
    }

    auto me = newMods.find (node);

    if (me != newMods.end ())
    {
        assert (me->second);
        return me->second;
    }

    SLE::pointer ret = ledger->getSLE (node);

    if (ret)
        newMods.insert (std::make_pair (node, ret));

    return ret;

}

bool LedgerEntrySet::threadTx (const RippleAddress& threadTo, Ledger::ref ledger,
                               NodeToLedgerEntry& newMods)
{
#ifdef META_DEBUG
    WriteLog (lsTRACE, LedgerEntrySet) << "Thread to " << threadTo.getAccountID ();
#endif
    SLE::pointer sle = getForMod (Ledger::getAccountRootIndex (threadTo.getAccountID ()), ledger, newMods);

    if (!sle)
    {
        WriteLog (lsFATAL, LedgerEntrySet) << "Threading to non-existent account: " << threadTo.humanAccountID ();
        assert (false);
        return false;
    }

    return threadTx (sle, ledger, newMods);
}

bool LedgerEntrySet::threadTx (SLE::ref threadTo, Ledger::ref ledger,
                               NodeToLedgerEntry& newMods)
{
    // node = the node that was modified/deleted/created
    // threadTo = the node that needs to know
    uint256 prevTxID;
    std::uint32_t prevLgrID;

    if (!threadTo->thread (mSet.getTxID (), mSet.getLgrSeq (), prevTxID, prevLgrID))
        return false;

    if (prevTxID.isZero () ||
            TransactionMetaSet::thread (mSet.getAffectedNode (threadTo, sfModifiedNode), prevTxID, prevLgrID))
        return true;

    assert (false);
    return false;
}

bool LedgerEntrySet::threadOwners (SLE::ref node, Ledger::ref ledger,
                                   NodeToLedgerEntry& newMods)
{
    // thread new or modified node to owner or owners
    if (node->hasOneOwner ()) // thread to owner's account
    {
#ifdef META_DEBUG
        WriteLog (lsTRACE, LedgerEntrySet) << "Thread to single owner";
#endif
        return threadTx (node->getOwner (), ledger, newMods);
    }
    else if (node->hasTwoOwners ()) // thread to owner's accounts
    {
#ifdef META_DEBUG
        WriteLog (lsTRACE, LedgerEntrySet) << "Thread to two owners";
#endif
        return
            threadTx (node->getFirstOwner (), ledger, newMods) &&
            threadTx (node->getSecondOwner (), ledger, newMods);
    }
    else
        return false;
}

void LedgerEntrySet::calcRawMeta (Serializer& s, TER result, std::uint32_t index)
{
    // calculate the raw meta data and return it. This must be called before the set is committed

    // Entries modified only as a result of building the transaction metadata
    NodeToLedgerEntry newMod;

    for (auto& it : mEntries)
    {
        SField::ptr type = &sfGeneric;

        switch (it.second.mAction)
        {
        case taaMODIFY:
#ifdef META_DEBUG
            WriteLog (lsTRACE, LedgerEntrySet) << "Modified Node " << it.first;
#endif
            type = &sfModifiedNode;
            break;

        case taaDELETE:
#ifdef META_DEBUG
            WriteLog (lsTRACE, LedgerEntrySet) << "Deleted Node " << it.first;
#endif
            type = &sfDeletedNode;
            break;

        case taaCREATE:
#ifdef META_DEBUG
            WriteLog (lsTRACE, LedgerEntrySet) << "Created Node " << it.first;
#endif
            type = &sfCreatedNode;
            break;

        default: // ignore these
            break;
        }

        if (type == &sfGeneric)
            continue;

        SLE::pointer origNode = mLedger->getSLEi (it.first);
        SLE::pointer curNode = it.second.mEntry;

        if ((type == &sfModifiedNode) && (*curNode == *origNode))
            continue;

        std::uint16_t nodeType = curNode
            ? curNode->getFieldU16 (sfLedgerEntryType)
            : origNode->getFieldU16 (sfLedgerEntryType);

        mSet.setAffectedNode (it.first, *type, nodeType);

        if (type == &sfDeletedNode)
        {
            assert (origNode && curNode);
            threadOwners (origNode, mLedger, newMod); // thread transaction to owners

            STObject prevs (sfPreviousFields);
            for (auto const& obj : *origNode)
            {
                // go through the original node for modified fields saved on modification
                if (obj.getFName ().shouldMeta (SField::sMD_ChangeOrig) && !curNode->hasMatchingEntry (obj))
                    prevs.addObject (obj);
            }

            if (!prevs.empty ())
                mSet.getAffectedNode (it.first).addObject (prevs);

            STObject finals (sfFinalFields);
            for (auto const& obj : *curNode)
            {
                // go through the final node for final fields
                if (obj.getFName ().shouldMeta (SField::sMD_Always | SField::sMD_DeleteFinal))
                    finals.addObject (obj);
            }

            if (!finals.empty ())
                mSet.getAffectedNode (it.first).addObject (finals);
        }
        else if (type == &sfModifiedNode)
        {
            assert (curNode && origNode);

            if (curNode->isThreadedType ()) // thread transaction to node it modified
                threadTx (curNode, mLedger, newMod);

            STObject prevs (sfPreviousFields);
            for (auto const& obj : *origNode)
            {
                // search the original node for values saved on modify
                if (obj.getFName ().shouldMeta (SField::sMD_ChangeOrig) && !curNode->hasMatchingEntry (obj))
                    prevs.addObject (obj);
            }

            if (!prevs.empty ())
                mSet.getAffectedNode (it.first).addObject (prevs);

            STObject finals (sfFinalFields);
            for (auto const& obj : *curNode)
            {
                // search the final node for values saved always
                if (obj.getFName ().shouldMeta (SField::sMD_Always | SField::sMD_ChangeNew))
                    finals.addObject (obj);
            }

            if (!finals.empty ())
                mSet.getAffectedNode (it.first).addObject (finals);
        }
        else if (type == &sfCreatedNode) // if created, thread to owner(s)
        {
            assert (curNode && !origNode);
            threadOwners (curNode, mLedger, newMod);

            if (curNode->isThreadedType ()) // always thread to self
                threadTx (curNode, mLedger, newMod);

            STObject news (sfNewFields);
            for (auto const& obj : *curNode)
            {
                // save non-default values
                if (!obj.isDefault () && obj.getFName ().shouldMeta (SField::sMD_Create | SField::sMD_Always))
                    news.addObject (obj);
            }

            if (!news.empty ())
                mSet.getAffectedNode (it.first).addObject (news);
        }
        else assert (false);
    }

    // add any new modified nodes to the modification set
    for (auto& it : newMod)
        entryModify (it.second);

    mSet.addRaw (s, result, index);
    WriteLog (lsTRACE, LedgerEntrySet) << "Metadata:" << mSet.getJson (0);
}

TER LedgerEntrySet::dirCount (uint256 const& uRootIndex, std::uint32_t& uCount)
{
    std::uint64_t  uNodeDir    = 0;

    uCount  = 0;

    do
    {
        SLE::pointer    sleNode = entryCache (ltDIR_NODE, Ledger::getDirNodeIndex (uRootIndex, uNodeDir));

        if (sleNode)
        {
            uCount      += sleNode->getFieldV256 (sfIndexes).peekValue ().size ();

            uNodeDir    = sleNode->getFieldU64 (sfIndexNext);       // Get next node.
        }
        else if (uNodeDir)
        {
            WriteLog (lsWARNING, LedgerEntrySet) << "dirCount: no such node";

            assert (false);

            return tefBAD_LEDGER;
        }
    }
    while (uNodeDir);

    return tesSUCCESS;
}

bool LedgerEntrySet::dirIsEmpty (uint256 const& uRootIndex)
{
    std::uint64_t  uNodeDir = 0;

    SLE::pointer sleNode = entryCache (ltDIR_NODE, Ledger::getDirNodeIndex (uRootIndex, uNodeDir));

    if (!sleNode)
        return true;

    if (!sleNode->getFieldV256 (sfIndexes).peekValue ().empty ())
        return false;

    // If there's another page, it must be non-empty
    return sleNode->getFieldU64 (sfIndexNext) == 0;
}

// <--     uNodeDir: For deletion, present to make dirDelete efficient.
// -->   uRootIndex: The index of the base of the directory.  Nodes are based off of this.
// --> uLedgerIndex: Value to add to directory.
// Only append. This allow for things that watch append only structure to just monitor from the last node on ward.
// Within a node with no deletions order of elements is sequential.  Otherwise, order of elements is random.
TER LedgerEntrySet::dirAdd (
    std::uint64_t&                          uNodeDir,
    uint256 const&                          uRootIndex,
    uint256 const&                          uLedgerIndex,
    std::function<void (SLE::ref, bool)>    fDescriber)
{
    WriteLog (lsTRACE, LedgerEntrySet) << "dirAdd:" <<
        " uRootIndex=" << to_string (uRootIndex) <<
        " uLedgerIndex=" << to_string (uLedgerIndex);

    SLE::pointer        sleNode;
    STVector256         svIndexes;
    SLE::pointer        sleRoot     = entryCache (ltDIR_NODE, uRootIndex);

    if (!sleRoot)
    {
        // No root, make it.
        sleRoot     = entryCreate (ltDIR_NODE, uRootIndex);
        sleRoot->setFieldH256 (sfRootIndex, uRootIndex);
        fDescriber (sleRoot, true);

        sleNode     = sleRoot;
        uNodeDir    = 0;
    }
    else
    {
        uNodeDir    = sleRoot->getFieldU64 (sfIndexPrevious);       // Get index to last directory node.

        if (uNodeDir)
        {
            // Try adding to last node.
            sleNode     = entryCache (ltDIR_NODE, Ledger::getDirNodeIndex (uRootIndex, uNodeDir));

            assert (sleNode);
        }
        else
        {
            // Try adding to root.  Didn't have a previous set to the last node.
            sleNode     = sleRoot;
        }

        svIndexes   = sleNode->getFieldV256 (sfIndexes);

        if (DIR_NODE_MAX != svIndexes.peekValue ().size ())
        {
            // Add to current node.
            entryModify (sleNode);
        }
        // Add to new node.
        else if (!++uNodeDir)
        {
            return tecDIR_FULL;
        }
        else
        {
            // Have old last point to new node
            sleNode->setFieldU64 (sfIndexNext, uNodeDir);
            entryModify (sleNode);

            // Have root point to new node.
            sleRoot->setFieldU64 (sfIndexPrevious, uNodeDir);
            entryModify (sleRoot);

            // Create the new node.
            sleNode     = entryCreate (ltDIR_NODE, Ledger::getDirNodeIndex (uRootIndex, uNodeDir));
            sleNode->setFieldH256 (sfRootIndex, uRootIndex);

            if (uNodeDir != 1)
                sleNode->setFieldU64 (sfIndexPrevious, uNodeDir - 1);

            fDescriber (sleNode, false);

            svIndexes   = STVector256 ();
        }
    }

    svIndexes.peekValue ().push_back (uLedgerIndex); // Append entry.
    sleNode->setFieldV256 (sfIndexes, svIndexes);   // Save entry.

    WriteLog (lsTRACE, LedgerEntrySet) <<
        "dirAdd:   creating: root: " << to_string (uRootIndex);
    WriteLog (lsTRACE, LedgerEntrySet) <<
        "dirAdd:  appending: Entry: " << to_string (uLedgerIndex);
    WriteLog (lsTRACE, LedgerEntrySet) <<
        "dirAdd:  appending: Node: " << strHex (uNodeDir);
    // WriteLog (lsINFO, LedgerEntrySet) << "dirAdd:  appending: PREV: " << svIndexes.peekValue()[0].ToString();

    return tesSUCCESS;
}

// Ledger must be in a state for this to work.
TER LedgerEntrySet::dirDelete (
    const bool                      bKeepRoot,      // --> True, if we never completely clean up, after we overflow the root node.
    const std::uint64_t&            uNodeDir,       // --> Node containing entry.
    uint256 const&                  uRootIndex,     // --> The index of the base of the directory.  Nodes are based off of this.
    uint256 const&                  uLedgerIndex,   // --> Value to remove from directory.
    const bool                      bStable,        // --> True, not to change relative order of entries.
    const bool                      bSoft)          // --> True, uNodeDir is not hard and fast (pass uNodeDir=0).
{
    std::uint64_t       uNodeCur    = uNodeDir;
    SLE::pointer        sleNode     = entryCache (ltDIR_NODE, Ledger::getDirNodeIndex (uRootIndex, uNodeCur));

    if (!sleNode)
    {
        WriteLog (lsWARNING, LedgerEntrySet) << "dirDelete: no such node:" <<
            " uRootIndex=" << to_string (uRootIndex) <<
            " uNodeDir=" << strHex (uNodeDir) <<
            " uLedgerIndex=" << to_string (uLedgerIndex);

        if (!bSoft)
        {
            assert (false);
            return tefBAD_LEDGER;
        }
        else if (uNodeDir < 20)
        {
            // Go the extra mile. Even if node doesn't exist, try the next node.

            return dirDelete (bKeepRoot, uNodeDir + 1, uRootIndex, uLedgerIndex, bStable, true);
        }
        else
        {
            return tefBAD_LEDGER;
        }
    }

    STVector256 svIndexes   = sleNode->getFieldV256 (sfIndexes);
    std::vector<uint256>& vuiIndexes  = svIndexes.peekValue ();

    auto it = std::find (vuiIndexes.begin (), vuiIndexes.end (), uLedgerIndex);

    if (vuiIndexes.end () == it)
    {
        if (!bSoft)
        {
            assert (false);

            WriteLog (lsWARNING, LedgerEntrySet) << "dirDelete: no such entry";

            return tefBAD_LEDGER;
        }
        else if (uNodeDir < 20)
        {
            // Go the extra mile. Even if entry not in node, try the next node.

            return dirDelete (bKeepRoot, uNodeDir + 1, uRootIndex, uLedgerIndex,
                bStable, true);
        }
        else
        {
            return tefBAD_LEDGER;
        }
    }

    // Remove the element.
    if (vuiIndexes.size () > 1)
    {
        if (bStable)
        {
            vuiIndexes.erase (it);
        }
        else
        {
            *it = vuiIndexes[vuiIndexes.size () - 1];
            vuiIndexes.resize (vuiIndexes.size () - 1);
        }
    }
    else
    {
        vuiIndexes.clear ();
    }

    sleNode->setFieldV256 (sfIndexes, svIndexes);
    entryModify (sleNode);

    if (vuiIndexes.empty ())
    {
        // May be able to delete nodes.
        std::uint64_t       uNodePrevious   = sleNode->getFieldU64 (sfIndexPrevious);
        std::uint64_t       uNodeNext       = sleNode->getFieldU64 (sfIndexNext);

        if (!uNodeCur)
        {
            // Just emptied root node.

            if (!uNodePrevious)
            {
                // Never overflowed the root node.  Delete it.
                entryDelete (sleNode);
            }
            // Root overflowed.
            else if (bKeepRoot)
            {
                // If root overflowed and not allowed to delete overflowed root node.
            }
            else if (uNodePrevious != uNodeNext)
            {
                // Have more than 2 nodes.  Can't delete root node.
            }
            else
            {
                // Have only a root node and a last node.
                SLE::pointer        sleLast = entryCache (ltDIR_NODE, Ledger::getDirNodeIndex (uRootIndex, uNodeNext));

                assert (sleLast);

                if (sleLast->getFieldV256 (sfIndexes).peekValue ().empty ())
                {
                    // Both nodes are empty.

                    entryDelete (sleNode);  // Delete root.
                    entryDelete (sleLast);  // Delete last.
                }
                else
                {
                    // Have an entry, can't delete root node.
                }
            }
        }
        // Just emptied a non-root node.
        else if (uNodeNext)
        {
            // Not root and not last node. Can delete node.

            SLE::pointer        slePrevious = entryCache (ltDIR_NODE, Ledger::getDirNodeIndex (uRootIndex, uNodePrevious));

            assert (slePrevious);

            SLE::pointer        sleNext     = entryCache (ltDIR_NODE, Ledger::getDirNodeIndex (uRootIndex, uNodeNext));

            assert (slePrevious);
            assert (sleNext);

            if (!slePrevious)
            {
                WriteLog (lsWARNING, LedgerEntrySet) << "dirDelete: previous node is missing";

                return tefBAD_LEDGER;
            }

            if (!sleNext)
            {
                WriteLog (lsWARNING, LedgerEntrySet) << "dirDelete: next node is missing";

                return tefBAD_LEDGER;
            }

            // Fix previous to point to its new next.
            slePrevious->setFieldU64 (sfIndexNext, uNodeNext);
            entryModify (slePrevious);

            // Fix next to point to its new previous.
            sleNext->setFieldU64 (sfIndexPrevious, uNodePrevious);
            entryModify (sleNext);

            entryDelete(sleNode);
        }
        // Last node.
        else if (bKeepRoot || uNodePrevious)
        {
            // Not allowed to delete last node as root was overflowed.
            // Or, have pervious entries preventing complete delete.
        }
        else
        {
            // Last and only node besides the root.
            SLE::pointer            sleRoot = entryCache (ltDIR_NODE, uRootIndex);

            assert (sleRoot);

            if (sleRoot->getFieldV256 (sfIndexes).peekValue ().empty ())
            {
                // Both nodes are empty.

                entryDelete (sleRoot);  // Delete root.
                entryDelete (sleNode);  // Delete last.
            }
            else
            {
                // Root has an entry, can't delete.
            }
        }
    }

    return tesSUCCESS;
}

// Return the first entry and advance uDirEntry.
// <-- true, if had a next entry.
bool LedgerEntrySet::dirFirst (
    uint256 const& uRootIndex,  // --> Root of directory.
    SLE::pointer& sleNode,      // <-- current node
    unsigned int& uDirEntry,    // <-- next entry
    uint256& uEntryIndex)       // <-- The entry, if available. Otherwise, zero.
{
    sleNode     = entryCache (ltDIR_NODE, uRootIndex);
    uDirEntry   = 0;

    assert (sleNode);           // Never probe for directories.

    return LedgerEntrySet::dirNext (uRootIndex, sleNode, uDirEntry, uEntryIndex);
}

// Return the current entry and advance uDirEntry.
// <-- true, if had a next entry.
bool LedgerEntrySet::dirNext (
    uint256 const& uRootIndex,  // --> Root of directory
    SLE::pointer& sleNode,      // <-> current node
    unsigned int& uDirEntry,    // <-> next entry
    uint256& uEntryIndex)       // <-- The entry, if available. Otherwise, zero.
{
    STVector256             svIndexes   = sleNode->getFieldV256 (sfIndexes);
    std::vector<uint256>&   vuiIndexes  = svIndexes.peekValue ();

    assert (uDirEntry <= vuiIndexes.size ());

    if (uDirEntry >= vuiIndexes.size ())
    {
        std::uint64_t         uNodeNext   = sleNode->getFieldU64 (sfIndexNext);

        if (!uNodeNext)
        {
            uEntryIndex.zero ();

            return false;
        }
        else
        {
            SLE::pointer sleNext = entryCache (ltDIR_NODE, Ledger::getDirNodeIndex (uRootIndex, uNodeNext));
            uDirEntry   = 0;

            if (!sleNext)
            { // This should never happen
                WriteLog (lsFATAL, LedgerEntrySet) << "Corrupt directory: index:" << uRootIndex << " next:" << uNodeNext;
                return false;
            }

            sleNode = sleNext;
            // TODO(tom): make this iterative.
            return dirNext (uRootIndex, sleNode, uDirEntry, uEntryIndex);
        }
    }

    uEntryIndex = vuiIndexes[uDirEntry++];

    WriteLog (lsTRACE, LedgerEntrySet) << "dirNext:" <<
        " uDirEntry=" << uDirEntry <<
        " uEntryIndex=" << uEntryIndex;

    return true;
}

uint256 LedgerEntrySet::getNextLedgerIndex (uint256 const& uHash)
{
    // find next node in ledger that isn't deleted by LES
    uint256 ledgerNext = uHash;
    std::map<uint256, LedgerEntrySetEntry>::const_iterator it;

    do
    {
        ledgerNext = mLedger->getNextLedgerIndex (ledgerNext);
        it  = mEntries.find (ledgerNext);
    }
    while ((it != mEntries.end ()) && (it->second.mAction == taaDELETE));

    // find next node in LES that isn't deleted
    for (it = mEntries.upper_bound (uHash); it != mEntries.end (); ++it)
    {
        // node found in LES, node found in ledger, return earliest
        if (it->second.mAction != taaDELETE)
            return (ledgerNext.isNonZero () && (ledgerNext < it->first)) ? ledgerNext : it->first;
    }

    // nothing next in LES, return next ledger node
    return ledgerNext;
}

uint256 LedgerEntrySet::getNextLedgerIndex (uint256 const& uHash, uint256 const& uEnd)
{
    uint256 next = getNextLedgerIndex (uHash);

    if (next > uEnd)
        return uint256 ();

    return next;
}

// If there is a count, adjust the owner count by iAmount. Otherwise, compute the owner count and store it.
void LedgerEntrySet::ownerCountAdjust (const uint160& uOwnerID, int iAmount, SLE::ref sleAccountRoot)
{
    SLE::pointer    sleHold = sleAccountRoot
                              ? SLE::pointer ()
                              : entryCache (ltACCOUNT_ROOT, Ledger::getAccountRootIndex (uOwnerID));

    SLE::ref        sleRoot = sleAccountRoot
                              ? sleAccountRoot
                              : sleHold;

    const std::uint32_t uOwnerCount = sleRoot->getFieldU32 (sfOwnerCount);

    const std::uint32_t uNew        = iAmount + int (uOwnerCount) > 0
                                      ? uOwnerCount + iAmount
                                      : 0;

    if (uOwnerCount != uNew)
    {
        sleRoot->setFieldU32 (sfOwnerCount, uOwnerCount + iAmount);
        entryModify (sleRoot);
    }
}

TER LedgerEntrySet::offerDelete (SLE::pointer sleOffer)
{

    uint256 offerIndex = sleOffer->getIndex ();
    uint160 uOwnerID   = sleOffer->getFieldAccount160 (sfAccount);

    // Detect legacy directories.
    bool bOwnerNode = sleOffer->isFieldPresent (sfOwnerNode);
    std::uint64_t uOwnerNode = sleOffer->getFieldU64 (sfOwnerNode);
    uint256 uDirectory = sleOffer->getFieldH256 (sfBookDirectory);
    std::uint64_t uBookNode  = sleOffer->getFieldU64 (sfBookNode);

    TER terResult  = dirDelete (
        false, uOwnerNode,
        Ledger::getOwnerDirIndex (uOwnerID), offerIndex, false, !bOwnerNode);
    TER terResult2 = dirDelete (
        false, uBookNode, uDirectory, offerIndex, true, false);

    if (tesSUCCESS == terResult)
        ownerCountAdjust (uOwnerID, -1);

    entryDelete (sleOffer);

    return (terResult == tesSUCCESS) ? terResult2 : terResult;
}

TER LedgerEntrySet::offerDelete (uint256 const& offerIndex)
{
    SLE::pointer    sleOffer    = entryCache (ltOFFER, offerIndex);

    if (!sleOffer)
        return tesSUCCESS;

    return offerDelete (sleOffer);
}

// Returns amount owed by uToAccountID to uFromAccountID.
// <-- $owed/currency/uToAccountID:
// positive: uFromAccountID holds IOUs.,
// negative: uFromAccountID owes IOUs.
STAmount LedgerEntrySet::rippleOwed (
    const uint160& uToAccountID, const uint160& uFromAccountID,
    const uint160& currency)
{
    STAmount saBalance;
    SLE::pointer sleRippleState  = entryCache (
        ltRIPPLE_STATE,
        Ledger::getRippleStateIndex (
            uToAccountID, uFromAccountID, currency));

    if (sleRippleState)
    {
        saBalance   = sleRippleState->getFieldAmount (sfBalance);

        if (uToAccountID < uFromAccountID)
            saBalance.negate ();

        saBalance.setIssuer (uToAccountID);
    }
    else
    {
        saBalance.clear (currency, uToAccountID);

        WriteLog (lsDEBUG, LedgerEntrySet) << "rippleOwed:" <<
            " No credit line between " <<
            RippleAddress::createHumanAccountID (uFromAccountID) <<
            " and " << RippleAddress::createHumanAccountID (uToAccountID) <<
            " for " << STAmount::createHumanCurrency (currency);
    }

    return saBalance;
}

// Maximum amount of IOUs uToAccountID will hold from uFromAccountID.
// <-- $amount/currency/uToAccountID.
STAmount LedgerEntrySet::rippleLimit (
    const uint160& uToAccountID, const uint160& uFromAccountID,
    const uint160& currency)
{
    STAmount saLimit;
    auto sleRippleState  = entryCache (
        ltRIPPLE_STATE,
        Ledger::getRippleStateIndex (
            uToAccountID, uFromAccountID, currency));

    if (sleRippleState)
    {
        saLimit = sleRippleState->getFieldAmount (
            uToAccountID < uFromAccountID ? sfLowLimit : sfHighLimit);
        saLimit.setIssuer (uToAccountID);
    }
    else
    {
        saLimit.clear (currency, uToAccountID);
    }

    return saLimit;

}

std::uint32_t LedgerEntrySet::rippleTransferRate (const uint160& issuer)
{
    SLE::pointer sleAccount (entryCache (
        ltACCOUNT_ROOT, Ledger::getAccountRootIndex (issuer)));

    std::uint32_t uQuality =
        sleAccount && sleAccount->isFieldPresent (sfTransferRate)
            ? sleAccount->getFieldU32 (sfTransferRate)
            : QUALITY_ONE;

    WriteLog (lsTRACE, LedgerEntrySet) << "rippleTransferRate:" <<
        " issuer=" << RippleAddress::createHumanAccountID (issuer) <<
        " account_exists=" << std::boolalpha << !!sleAccount <<
        " transfer_rate=" << (uQuality / 1000000000.0);

    return uQuality;
}

std::uint32_t
LedgerEntrySet::rippleTransferRate (const uint160& uSenderID,
                                    const uint160& uReceiverID,
                                    const uint160& issuer)
{
    // If calculating the transfer rate from or to the issuer of the currency
    // no fees are assessed.
    return uSenderID == issuer || uReceiverID == issuer
           ? QUALITY_ONE
           : rippleTransferRate (issuer);
}

// XXX Might not need this, might store in nodes on calc reverse.
std::uint32_t
LedgerEntrySet::rippleQualityIn (const uint160& uToAccountID,
                                 const uint160& uFromAccountID,
                                 const uint160& uCurrencyID,
                                 SField::ref sfLow, SField::ref sfHigh)
{
    std::uint32_t uQuality (QUALITY_ONE);

    if (uToAccountID == uFromAccountID)
        return uQuality;

    SLE::pointer sleRippleState (entryCache (ltRIPPLE_STATE,
        Ledger::getRippleStateIndex (uToAccountID, uFromAccountID, uCurrencyID)));

    if (sleRippleState)
    {
        SField::ref sfField = uToAccountID < uFromAccountID ? sfLow : sfHigh;

        uQuality    = sleRippleState->isFieldPresent (sfField)
                      ? sleRippleState->getFieldU32 (sfField)
                      : QUALITY_ONE;

        if (!uQuality)
            uQuality    = 1;    // Avoid divide by zero.
    }
    else
    {
        // XXX Ideally, catch no before this. So we can assert to be stricter.
        uQuality    = QUALITY_ONE;
    }

    WriteLog (lsTRACE, LedgerEntrySet) << "rippleQuality: " <<
        (sfLow == sfLowQualityIn ? "in" : "out") <<
        " uToAccountID=" << RippleAddress::createHumanAccountID (uToAccountID) <<
        " uFromAccountID=" << RippleAddress::createHumanAccountID (uFromAccountID) <<
        " uCurrencyID=" << STAmount::createHumanCurrency (uCurrencyID) <<
        " bLine=" << std::boolalpha << !!sleRippleState <<
        " uQuality=" << (uQuality / 1000000000.0);

    return uQuality;
}

// Return how much of issuer's currency IOUs that account holds.  May be negative.
// <-- IOU's account has of issuer.
STAmount LedgerEntrySet::rippleHolds (const uint160& account, const uint160& currency, const uint160& issuer)
{
    STAmount saBalance;
    SLE::pointer sleRippleState = entryCache (ltRIPPLE_STATE,
        Ledger::getRippleStateIndex (account, issuer, currency));

    if (!sleRippleState)
    {
        saBalance.clear (currency, issuer);
    }
    else if (account > issuer)
    {
        saBalance   = sleRippleState->getFieldAmount (sfBalance);
        saBalance.negate ();    // Put balance in account terms.

        saBalance.setIssuer (issuer);
    }
    else
    {
        saBalance   = sleRippleState->getFieldAmount (sfBalance);

        saBalance.setIssuer (issuer);
    }

    return saBalance;
}

// Returns the amount an account can spend without going into debt.
//
// <-- saAmount: amount of currency held by account. May be negative.
STAmount LedgerEntrySet::accountHolds (
    const uint160& account, const uint160& currency,
    const uint160& issuer)
{
    STAmount    saAmount;

    if (!currency)
    {
        SLE::pointer sleAccount = entryCache (ltACCOUNT_ROOT,
            Ledger::getAccountRootIndex (account));
        std::uint64_t uReserve = mLedger->getReserve (
            sleAccount->getFieldU32 (sfOwnerCount));

        STAmount saBalance   = sleAccount->getFieldAmount (sfBalance);

        if (saBalance < uReserve)
        {
            saAmount.clear ();
        }
        else
        {
            saAmount = saBalance - uReserve;
        }

        WriteLog (lsTRACE, LedgerEntrySet) << "accountHolds:" <<
            " account=" << RippleAddress::createHumanAccountID (account) <<
            " saAmount=" << saAmount.getFullText () <<
            " saBalance=" << saBalance.getFullText () <<
            " uReserve=" << uReserve;
    }
    else
    {
        saAmount    = rippleHolds (account, currency, issuer);

        WriteLog (lsTRACE, LedgerEntrySet) << "accountHolds:" <<
            " account=" << RippleAddress::createHumanAccountID (account) <<
            " saAmount=" << saAmount.getFullText ();
    }

    return saAmount;
}

// Returns the funds available for account for a currency/issuer.
// Use when you need a default for rippling account's currency.
// XXX Should take into account quality?
// --> saDefault/currency/issuer
// <-- saFunds: Funds available. May be negative.
//
// If the issuer is the same as account, funds are unlimited, use result is
// saDefault.
STAmount LedgerEntrySet::accountFunds (
    const uint160& account, const STAmount& saDefault)
{
    STAmount    saFunds;

    if (!saDefault.isNative () && saDefault.getIssuer () == account)
    {
        saFunds = saDefault;

        WriteLog (lsTRACE, LedgerEntrySet) << "accountFunds:" <<
            " account=" << RippleAddress::createHumanAccountID (account) <<
            " saDefault=" << saDefault.getFullText () <<
            " SELF-FUNDED";
    }
    else
    {
        saFunds = accountHolds (
            account, saDefault.getCurrency (), saDefault.getIssuer ());

        WriteLog (lsTRACE, LedgerEntrySet) << "accountFunds:" <<
            " account=" << RippleAddress::createHumanAccountID (account) <<
            " saDefault=" << saDefault.getFullText () <<
            " saFunds=" << saFunds.getFullText ();
    }

    return saFunds;
}

// Calculate transit fee.
STAmount LedgerEntrySet::rippleTransferFee (
    const uint160& uSenderID,
    const uint160& uReceiverID,
    const uint160& issuer,
    const STAmount& saAmount)
{
    if (uSenderID != issuer && uReceiverID != issuer)
    {
        std::uint32_t uTransitRate = rippleTransferRate (issuer);

        if (QUALITY_ONE != uTransitRate)
        {
            // NIKB use STAmount::saFromRate
            STAmount saTransitRate (
                CURRENCY_ONE, ACCOUNT_ONE,
                static_cast<std::uint64_t> (uTransitRate), -9);

            STAmount saTransferTotal = STAmount::multiply (
                saAmount, saTransitRate,
                saAmount.getCurrency (), saAmount.getIssuer ());
            STAmount saTransferFee = saTransferTotal - saAmount;

            WriteLog (lsDEBUG, LedgerEntrySet) << "rippleTransferFee:" <<
                " saTransferFee=" << saTransferFee.getFullText ();

            return saTransferFee;
        }
    }

    return STAmount (saAmount.getCurrency (), saAmount.getIssuer ());
}

TER LedgerEntrySet::trustCreate (
    const bool      bSrcHigh,
    const uint160&  uSrcAccountID,
    const uint160&  uDstAccountID,
    uint256 const&  uIndex,             // --> ripple state entry
    SLE::ref        sleAccount,         // --> the account being set.
    const bool      bAuth,              // --> authorize account.
    const bool      bNoRipple,          // --> others cannot ripple through
    const STAmount& saBalance,          // --> balance of account being set.
                                        // Issuer should be ACCOUNT_ONE
    const STAmount& saLimit,            // --> limit for account being set.
                                        // Issuer should be the account being set.
    const std::uint32_t uQualityIn,
    const std::uint32_t uQualityOut)
{
    const uint160&  uLowAccountID   = !bSrcHigh ? uSrcAccountID : uDstAccountID;
    const uint160&  uHighAccountID  =  bSrcHigh ? uSrcAccountID : uDstAccountID;

    SLE::pointer    sleRippleState  = entryCreate (ltRIPPLE_STATE, uIndex);

    std::uint64_t   uLowNode;
    std::uint64_t   uHighNode;

    TER terResult   = dirAdd (
                          uLowNode,
                          Ledger::getOwnerDirIndex (uLowAccountID),
                          sleRippleState->getIndex (),
                          std::bind (&Ledger::ownerDirDescriber,
                                     std::placeholders::_1, std::placeholders::_2,
                                     uLowAccountID));

    if (tesSUCCESS == terResult)
    {
        terResult   = dirAdd (
                          uHighNode,
                          Ledger::getOwnerDirIndex (uHighAccountID),
                          sleRippleState->getIndex (),
                          std::bind (&Ledger::ownerDirDescriber,
                                     std::placeholders::_1, std::placeholders::_2,
                                     uHighAccountID));
    }

    if (tesSUCCESS == terResult)
    {
        const bool  bSetDst     = saLimit.getIssuer () == uDstAccountID;
        const bool  bSetHigh    = bSrcHigh ^ bSetDst;

        // Remember deletion hints.
        sleRippleState->setFieldU64 (sfLowNode, uLowNode);
        sleRippleState->setFieldU64 (sfHighNode, uHighNode);

        sleRippleState->setFieldAmount (
            !bSetHigh ? sfLowLimit : sfHighLimit, saLimit);
        sleRippleState->setFieldAmount (
            bSetHigh ? sfLowLimit : sfHighLimit,
            STAmount (saBalance.getCurrency (),
                      bSetDst ? uSrcAccountID : uDstAccountID));

        if (uQualityIn)
            sleRippleState->setFieldU32 (
                !bSetHigh ? sfLowQualityIn : sfHighQualityIn, uQualityIn);

        if (uQualityOut)
            sleRippleState->setFieldU32 (
                !bSetHigh ? sfLowQualityOut : sfHighQualityOut, uQualityOut);

        std::uint32_t  uFlags  = !bSetHigh ? lsfLowReserve : lsfHighReserve;

        if (bAuth)
        {
            uFlags      |= (!bSetHigh ? lsfLowAuth : lsfHighAuth);
        }
        if (bNoRipple)
        {
            uFlags      |= (!bSetHigh ? lsfLowNoRipple : lsfHighNoRipple);
        }

        sleRippleState->setFieldU32 (sfFlags, uFlags);
        ownerCountAdjust (
            !bSetDst ? uSrcAccountID : uDstAccountID, 1, sleAccount);

        // ONLY: Create ripple balance.
        sleRippleState->setFieldAmount (
            sfBalance, bSetHigh ? -saBalance : saBalance);
    }

    return terResult;
}

TER LedgerEntrySet::trustDelete (
    SLE::ref sleRippleState, const uint160& uLowAccountID,
    const uint160& uHighAccountID)
{
    // Detect legacy dirs.
    bool        bLowNode    = sleRippleState->isFieldPresent (sfLowNode);
    bool        bHighNode   = sleRippleState->isFieldPresent (sfHighNode);
    std::uint64_t uLowNode    = sleRippleState->getFieldU64 (sfLowNode);
    std::uint64_t uHighNode   = sleRippleState->getFieldU64 (sfHighNode);
    TER         terResult;

    WriteLog (lsTRACE, LedgerEntrySet)
        << "trustDelete: Deleting ripple line: low";
    terResult   = dirDelete (
        false,
        uLowNode,
        Ledger::getOwnerDirIndex (uLowAccountID),
        sleRippleState->getIndex (),
        false,
        !bLowNode);

    if (tesSUCCESS == terResult)
    {
        WriteLog (lsTRACE, LedgerEntrySet)
                << "trustDelete: Deleting ripple line: high";
        terResult   = dirDelete (
            false,
            uHighNode,
            Ledger::getOwnerDirIndex (uHighAccountID),
            sleRippleState->getIndex (),
            false,
            !bHighNode);
    }

    WriteLog (lsTRACE, LedgerEntrySet) << "trustDelete: Deleting ripple line: state";
    entryDelete (sleRippleState);

    return terResult;
}

// Direct send w/o fees:
// - Redeeming IOUs and/or sending sender's own IOUs.
// - Create trust line of needed.
// --> bCheckIssuer : normally require issuer to be involved.
TER LedgerEntrySet::rippleCredit (
    const uint160& uSenderID, const uint160& uReceiverID,
    const STAmount& saAmount, bool bCheckIssuer)
{
    uint160 issuer = saAmount.getIssuer ();
    uint160 currency = saAmount.getCurrency ();

    // Make sure issuer is involved.
    assert (
        !bCheckIssuer || uSenderID == issuer || uReceiverID == issuer);

    // Disallow sending to self.
    assert (uSenderID != uReceiverID);

    bool bSenderHigh = uSenderID > uReceiverID;
    uint256 uIndex = Ledger::getRippleStateIndex (
        uSenderID, uReceiverID, saAmount.getCurrency ());
    auto sleRippleState  = entryCache (ltRIPPLE_STATE, uIndex);

    TER terResult;

    assert (!!uSenderID && uSenderID != ACCOUNT_ONE);
    assert (!!uReceiverID && uReceiverID != ACCOUNT_ONE);

    if (!sleRippleState)
    {
        STAmount    saReceiverLimit = STAmount (currency, uReceiverID);
        STAmount    saBalance       = saAmount;

        saBalance.setIssuer (ACCOUNT_ONE);

        WriteLog (lsDEBUG, LedgerEntrySet) << "rippleCredit: "
            "create line: " << RippleAddress::createHumanAccountID (uSenderID) <<
            " -> " << RippleAddress::createHumanAccountID (uReceiverID) <<
            " : " << saAmount.getFullText ();

        terResult = trustCreate (
            bSenderHigh,
            uSenderID,
            uReceiverID,
            uIndex,
            entryCache (ltACCOUNT_ROOT, Ledger::getAccountRootIndex (uReceiverID)),
            false,
            false,
            saBalance,
            saReceiverLimit);
    }
    else
    {
        STAmount    saBalance   = sleRippleState->getFieldAmount (sfBalance);

        if (bSenderHigh)
            saBalance.negate ();    // Put balance in sender terms.

        STAmount    saBefore    = saBalance;

        saBalance   -= saAmount;

        WriteLog (lsTRACE, LedgerEntrySet) << "rippleCredit: " <<
            RippleAddress::createHumanAccountID (uSenderID) <<
            " -> " << RippleAddress::createHumanAccountID (uReceiverID) <<
            " : before=" << saBefore.getFullText () <<
            " amount=" << saAmount.getFullText () <<
            " after=" << saBalance.getFullText ();

        std::uint32_t const uFlags (sleRippleState->getFieldU32 (sfFlags));
        bool bDelete = false;

        // YYY Could skip this if rippling in reverse.
        if (saBefore > zero
            // Sender balance was positive.
            && saBalance <= zero
            // Sender is zero or negative.
            && (uFlags & (!bSenderHigh ? lsfLowReserve : lsfHighReserve))
            // Sender reserve is set.
            && !(uFlags & (!bSenderHigh ? lsfLowNoRipple : lsfHighNoRipple))
            && !sleRippleState->getFieldAmount (
                !bSenderHigh ? sfLowLimit : sfHighLimit)
            // Sender trust limit is 0.
            && !sleRippleState->getFieldU32 (
                !bSenderHigh ? sfLowQualityIn : sfHighQualityIn)
            // Sender quality in is 0.
            && !sleRippleState->getFieldU32 (
                !bSenderHigh ? sfLowQualityOut : sfHighQualityOut))
            // Sender quality out is 0.
        {
            // Clear the reserve of the sender, possibly delete the line!
            auto sleSender = entryCache (
                ltACCOUNT_ROOT, Ledger::getAccountRootIndex (uSenderID));

            ownerCountAdjust (uSenderID, -1, sleSender);

            // Clear reserve flag.
            sleRippleState->setFieldU32 (
                sfFlags,
                uFlags & (!bSenderHigh ? ~lsfLowReserve : ~lsfHighReserve));

            // Balance is zero, receiver reserve is clear.
            bDelete = !saBalance        // Balance is zero.
                && !(uFlags & (bSenderHigh ? lsfLowReserve : lsfHighReserve));
            // Receiver reserve is clear.
        }

        if (bSenderHigh)
            saBalance.negate ();

        // Want to reflect balance to zero even if we are deleting line.
        sleRippleState->setFieldAmount (sfBalance, saBalance);
        // ONLY: Adjust ripple balance.

        if (bDelete)
        {
            terResult   = trustDelete (
                sleRippleState,
                bSenderHigh ? uReceiverID : uSenderID,
                !bSenderHigh ? uReceiverID : uSenderID);
        }
        else
        {
            entryModify (sleRippleState);
            terResult   = tesSUCCESS;
        }
    }

    return terResult;
}

// Send regardless of limits.
// --> saAmount: Amount/currency/issuer to deliver to reciever.
// <-- saActual: Amount actually cost.  Sender pay's fees.
TER LedgerEntrySet::rippleSend (
    const uint160& uSenderID, const uint160& uReceiverID,
    const STAmount& saAmount, STAmount& saActual)
{
    const uint160   issuer   = saAmount.getIssuer ();
    TER             terResult;

    assert (!!uSenderID && !!uReceiverID);
    assert (uSenderID != uReceiverID);

    if (uSenderID == issuer || uReceiverID == issuer ||
        issuer == ACCOUNT_ONE)
    {
        // Direct send: redeeming IOUs and/or sending own IOUs.
        terResult   = rippleCredit (uSenderID, uReceiverID, saAmount, false);

        saActual    = saAmount;

        terResult   = tesSUCCESS;
    }
    else
    {
        // Sending 3rd party IOUs: transit.

        STAmount saTransitFee = rippleTransferFee (
            uSenderID, uReceiverID, issuer, saAmount);

        saActual = !saTransitFee ? saAmount : saAmount + saTransitFee;

        saActual.setIssuer (issuer); // XXX Make sure this done in + above.

        WriteLog (lsDEBUG, LedgerEntrySet) << "rippleSend> " <<
            RippleAddress::createHumanAccountID (uSenderID) <<
            " - > " << RippleAddress::createHumanAccountID (uReceiverID) <<
            " : deliver=" << saAmount.getFullText () <<
            " fee=" << saTransitFee.getFullText () <<
            " cost=" << saActual.getFullText ();

        terResult   = rippleCredit (issuer, uReceiverID, saAmount);

        if (tesSUCCESS == terResult)
            terResult   = rippleCredit (uSenderID, issuer, saActual);
    }

    return terResult;
}

TER LedgerEntrySet::accountSend (
    const uint160& uSenderID, const uint160& uReceiverID,
    const STAmount& saAmount)
{
    TER terResult = tesSUCCESS;

    assert (saAmount >= zero);

    if (!saAmount || (uSenderID == uReceiverID))
        return tesSUCCESS;

    if (saAmount.isNative ())
    {
        // XRP send which does not check reserve and can do pure adjustment.
        SLE::pointer sleSender = !!uSenderID
            ? entryCache (ltACCOUNT_ROOT,
                          Ledger::getAccountRootIndex (uSenderID))
            : SLE::pointer ();
        SLE::pointer sleReceiver = !!uReceiverID
            ? entryCache (ltACCOUNT_ROOT,
                          Ledger::getAccountRootIndex (uReceiverID))
            : SLE::pointer ();

        auto get_balance = [](SLE::pointer acct)
        {
            std::string ret ("-");

            if (acct)
                ret = acct->getFieldAmount (sfBalance).getFullText ();

            return ret;
        };

        WriteLog (lsTRACE, LedgerEntrySet) << "accountSend> " <<
            RippleAddress::createHumanAccountID (uSenderID) <<
            " (" << get_balance(sleSender) <<
            ") -> " << RippleAddress::createHumanAccountID (uReceiverID) <<
            " (" << get_balance(sleReceiver) <<
            ") : " << saAmount.getFullText ();

        if (sleSender)
        {
            if (sleSender->getFieldAmount (sfBalance) < saAmount)
            {
                terResult = (mParams & tapOPEN_LEDGER)
                    ? telFAILED_PROCESSING
                    : tecFAILED_PROCESSING;
            }
            else
            {
                // Decrement XRP balance.
                sleSender->setFieldAmount (sfBalance,
                    sleSender->getFieldAmount (sfBalance) - saAmount);
                entryModify (sleSender);
            }
        }

        if (tesSUCCESS == terResult && sleReceiver)
        {
            // Increment XRP balance.
            sleReceiver->setFieldAmount (sfBalance,
                sleReceiver->getFieldAmount (sfBalance) + saAmount);
            entryModify (sleReceiver);
        }

        WriteLog (lsTRACE, LedgerEntrySet) << "accountSend< " <<
            RippleAddress::createHumanAccountID (uSenderID) <<
            " (" << get_balance(sleSender) <<
            ") -> " << RippleAddress::createHumanAccountID (uReceiverID) <<
            " (" << get_balance(sleReceiver) <<
            ") : " << saAmount.getFullText ();
    }
    else
    {
        STAmount saActual;

        WriteLog (lsTRACE, LedgerEntrySet) << "accountSend: " <<
            RippleAddress::createHumanAccountID (uSenderID) <<
            " -> " << RippleAddress::createHumanAccountID (uReceiverID) <<
            " : " << saAmount.getFullText ();


        terResult = rippleSend (uSenderID, uReceiverID, saAmount, saActual);
    }

    return terResult;
}

} // ripple
