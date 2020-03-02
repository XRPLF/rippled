//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2020 Ripple Labs Inc.

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

#include <ripple/app/consensus/RCLValidations.h>
#include <ripple/app/ledger/Ledger.h>
#include <ripple/app/misc/NegativeUNLVote.h>

namespace ripple {

NegativeUNLVote::NegativeUNLVote(NodeID const& myId, beast::Journal j)
    : myId_(myId), j_(j)
{
}

void
NegativeUNLVote::doVoting(
    LedgerConstPtr& prevLedger,
    hash_set<PublicKey> const& unlKeys,
    RCLValidations& validations,
    std::shared_ptr<SHAMap> const& initialSet)
{
    /*
     * Voting steps:
     * -- build a reliability score table of validators
     * -- process the table and find all candidates to disable or to re-enable
     * -- pick one to disable and one to re-enable if any
     * -- if found candidates, add ttUNL_MODIDY Tx
     */

    // Build NodeID set for internal use.
    // Build NodeID to PublicKey map for lookup before creating ttUNL_MODIDY Tx.
    hash_set<NodeID> unlNodeIDs;
    hash_map<NodeID, PublicKey> nidToKeyMap;
    for (auto const& k : unlKeys)
    {
        auto nid = calcNodeID(k);
        nidToKeyMap.emplace(nid, k);
        unlNodeIDs.emplace(nid);
    }

    hash_map<NodeID, unsigned int> scoreTable;
    if (buildScoreTable(prevLedger, unlNodeIDs, validations, scoreTable))
    {
        // build next nUNL
        auto nUnlKeys = prevLedger->nUnl();
        auto nUnlToDisable = prevLedger->nUnlToDisable();
        auto nUnlToReEnable = prevLedger->nUnlToReEnable();
        if (nUnlToDisable)
            nUnlKeys.insert(*nUnlToDisable);
        if (nUnlToReEnable)
            nUnlKeys.erase(*nUnlToReEnable);

        hash_set<NodeID> nUnlNodeIDs;
        for (auto const& k : nUnlKeys)
        {
            auto nid = calcNodeID(k);
            nUnlNodeIDs.emplace(nid);
            if (nidToKeyMap.find(nid) == nidToKeyMap.end())
            {
                nidToKeyMap.emplace(nid, k);
            }
        }

        auto const seq = prevLedger->info().seq + 1;
        purgeNewValidators(seq);

        std::vector<NodeID> toDisableCandidates;
        std::vector<NodeID> toReEnableCandidates;
        findAllCandidates(
            unlNodeIDs,
            nUnlNodeIDs,
            scoreTable,
            toDisableCandidates,
            toReEnableCandidates);

        if (!toDisableCandidates.empty())
        {
            auto n =
                pickOneCandidate(prevLedger->info().hash, toDisableCandidates);
            assert(nidToKeyMap.find(n) != nidToKeyMap.end());
            addTx(seq, nidToKeyMap[n], true, initialSet);
        }

        if (!toReEnableCandidates.empty())
        {
            auto n =
                pickOneCandidate(prevLedger->info().hash, toReEnableCandidates);
            assert(nidToKeyMap.find(n) != nidToKeyMap.end());
            addTx(seq, nidToKeyMap[n], false, initialSet);
        }
    }
}

void
NegativeUNLVote::addTx(
    LedgerIndex seq,
    PublicKey const& vp,
    bool disabling,
    std::shared_ptr<SHAMap> const& initialSet)
{
    STTx nUnlTx(ttUNL_MODIDY, [&](auto& obj) {
        obj.setFieldU8(sfUNLModifyDisabling, disabling ? 1 : 0);
        obj.setFieldU32(sfLedgerSequence, seq);
        obj.setFieldVL(sfUNLModifyValidator, vp.slice());
    });

    uint256 txID = nUnlTx.getTransactionID();
    Serializer s;
    nUnlTx.add(s);
    auto tItem = std::make_shared<SHAMapItem>(txID, s.peekData());
    if (!initialSet->addGiveItem(tItem, true, false))
    {
        JLOG(j_.warn()) << "N-UNL: ledger seq=" << seq
                        << ", add ttUNL_MODIDY tx failed";
    }
    else
    {
        JLOG(j_.debug()) << "N-UNL: ledger seq=" << seq
                         << ", add a ttUNL_MODIDY Tx with txID: " << txID
                         << ", the validator to "
                         << (disabling ? "disable: " : "re-enable: ") << vp;
    }
}

NodeID
NegativeUNLVote::pickOneCandidate(
    uint256 randomPadData,
    std::vector<NodeID>& candidates)
{
    assert(!candidates.empty());
    static_assert(NodeID::bytes <= uint256::bytes);
    NodeID randomPad = NodeID::fromVoid(randomPadData.data());
    NodeID txNodeID = candidates[0];
    for (int j = 1; j < candidates.size(); ++j)
    {
        if ((candidates[j] ^ randomPad) < (txNodeID ^ randomPad))
        {
            txNodeID = candidates[j];
        }
    }
    return txNodeID;
}

bool
NegativeUNLVote::buildScoreTable(
    LedgerConstPtr& prevLedger,
    hash_set<NodeID> const& unl,
    RCLValidations& validations,
    hash_map<NodeID, unsigned int>& scoreTable)
{
    assert(scoreTable.empty());

    /*
     * Find agreed validation messages received for the last 256 ledgers,
     * for every validator, and fill the score table.
     *
     * -- ask the validation container to keep enough validation message
     *    history for next time.
     *
     * -- find 256 previous ledger hashes, the same length as a flag ledger
     *    period.
     *
     * -- query the validation container for every ledger hash and fill
     *    the score table.
     *
     * -- return false if the validation message history or local node's
     *    participation in the history is not good.
     */

    auto const seq = prevLedger->info().seq + 1;
    validations.setSeqToKeep(seq - 1);

    auto const hashIndex = prevLedger->read(keylet::skip());
    if (!hashIndex || !hashIndex->isFieldPresent(sfHashes))
    {
        JLOG(j_.debug()) << "N-UNL: ledger " << seq << " no history.";
        return false;
    }

    auto ledgerAncestors = hashIndex->getFieldV256(sfHashes).value();
    auto numAncestors = ledgerAncestors.size();
    if (numAncestors < 256)
    {
        JLOG(j_.debug()) << "N-UNL: ledger " << seq
                         << " not enough history. Can trace back only "
                         << numAncestors << " ledgers.";
        return false;
    }

    // have enough ledger ancestors, build the score table
    for (auto const& k : unl)
    {
        scoreTable[k] = 0;
    }
    auto idx = numAncestors - 1;
    for (int i = 0; i < 256; ++i)
    {
        for (auto const& v :
             validations.getTrustedForLedger(ledgerAncestors[idx--]))
        {
            if (scoreTable.find(v->getNodeID()) != scoreTable.end())
                ++scoreTable[v->getNodeID()];
        }
    }

    unsigned int myValidationCount = 0;
    if (scoreTable.find(myId_) != scoreTable.end())
        myValidationCount = scoreTable[myId_];
    if (myValidationCount < nUnlMinLocalValsToVote)
    {
        JLOG(j_.debug()) << "N-UNL: ledger " << seq << ". I only issued "
                         << myValidationCount
                         << " validations in last 256 ledgers."
                         << " My reliability measurement could be wrong.";
        return false;
    }
    else if (
        myValidationCount > nUnlMinLocalValsToVote && myValidationCount <= 256)
    {
        return true;
    }
    else
    {
        // cannot happen because validations.getTrustedForLedger does not
        // return multiple validations of the same ledger from a validator.
        JLOG(j_.error()) << "N-UNL: ledger " << seq << ". I issued "
                         << myValidationCount
                         << " validations in last 256 ledgers. Too many!";
        return false;
    }
}

void
NegativeUNLVote::findAllCandidates(
    hash_set<NodeID> const& unl,
    hash_set<NodeID> const& nUnl,
    hash_map<NodeID, unsigned int> const& scoreTable,
    std::vector<NodeID>& toDisableCandidates,
    std::vector<NodeID>& toReEnableCandidates)
{
    /*
     * -- Compute if need to find more validators to disable, by checking if
     *    canAdd = sizeof nUnl < maxNegativeListed.
     *
     * -- Find toDisableCandidates: check if
     *    (1) canAdd,
     *    (2) has less than nUnlLowWaterMark validations,
     *    (3) is not in nUnl, and
     *    (4) is not a new validator.
     *
     * -- Find toReEnableCandidates: check if
     *    (1) is in nUnl and has more than nUnlHighWaterMark validations
     *    (2) if did not find any by (1), try to find candidates:
     *        (a) is in nUnl and (b) is not in unl.
     */
    auto maxNegativeListed = (std::size_t)std::ceil(unl.size() * nUnlMaxListed);
    std::size_t negativeListed = 0;
    for (auto const& n : unl)
    {
        if (nUnl.find(n) != nUnl.end())
            ++negativeListed;
    }
    bool canAdd = negativeListed < maxNegativeListed;
    JLOG(j_.trace()) << "N-UNL: my nodeId " << myId_ << " lowWaterMark "
                     << nUnlLowWaterMark << " highWaterMark "
                     << nUnlHighWaterMark << " canAdd " << canAdd
                     << " negativeListed " << negativeListed
                     << " maxNegativeListed " << maxNegativeListed;

    for (auto it = scoreTable.cbegin(); it != scoreTable.cend(); ++it)
    {
        JLOG(j_.trace()) << "N-UNL: node " << it->first << " score "
                         << it->second;

        if (canAdd && it->second < nUnlLowWaterMark &&
            nUnl.find(it->first) == nUnl.end() &&
            newValidators_.find(it->first) == newValidators_.end())
        {
            JLOG(j_.trace()) << "N-UNL: toDisable candidate " << it->first;
            toDisableCandidates.push_back(it->first);
        }

        if (it->second > nUnlHighWaterMark &&
            nUnl.find(it->first) != nUnl.end())
        {
            JLOG(j_.trace()) << "N-UNL: toReEnable candidate " << it->first;
            toReEnableCandidates.push_back(it->first);
        }
    }

    if (toReEnableCandidates.empty())
    {
        for (auto const& n : nUnl)
        {
            if (unl.find(n) == unl.end())
            {
                toReEnableCandidates.push_back(n);
            }
        }
    }
}

void
NegativeUNLVote::newValidators(
    LedgerIndex seq,
    hash_set<NodeID> const& nowTrusted)
{
    std::lock_guard lock(mutex_);
    for (auto const& n : nowTrusted)
    {
        if (newValidators_.find(n) == newValidators_.end())
        {
            JLOG(j_.trace()) << "N-UNL: add a new validator " << n
                             << " at ledger seq=" << seq;
            newValidators_[n] = seq;
        }
    }
}

void
NegativeUNLVote::purgeNewValidators(LedgerIndex seq)
{
    std::lock_guard lock(mutex_);
    auto i = newValidators_.begin();
    while (i != newValidators_.end())
    {
        if (seq - i->second > newValidatorDisableSkip)
        {
            i = newValidators_.erase(i);
        }
        else
        {
            ++i;
        }
    }
}

}  // namespace ripple
