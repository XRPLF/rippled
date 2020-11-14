//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2020 Ripple Labs Inc.

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
    std::shared_ptr<Ledger const> const& prevLedger,
    hash_set<PublicKey> const& unlKeys,
    RCLValidations& validations,
    std::shared_ptr<SHAMap> const& initialSet)
{
    // Voting steps:
    // -- build a reliability score table of validators
    // -- process the table and find all candidates to disable or to re-enable
    // -- pick one to disable and one to re-enable if any
    // -- if found candidates, add ttUNL_MODIFY Tx

    // Build NodeID set for internal use.
    // Build NodeID to PublicKey map for lookup before creating ttUNL_MODIFY Tx.
    hash_set<NodeID> unlNodeIDs;
    hash_map<NodeID, PublicKey> nidToKeyMap;
    for (auto const& k : unlKeys)
    {
        auto nid = calcNodeID(k);
        nidToKeyMap.emplace(nid, k);
        unlNodeIDs.emplace(nid);
    }

    // Build a reliability score table of validators
    if (std::optional<hash_map<NodeID, std::uint32_t>> scoreTable =
            buildScoreTable(prevLedger, unlNodeIDs, validations))
    {
        // build next negUnl
        auto negUnlKeys = prevLedger->negativeUNL();
        auto negUnlToDisable = prevLedger->validatorToDisable();
        auto negUnlToReEnable = prevLedger->validatorToReEnable();
        if (negUnlToDisable)
            negUnlKeys.insert(*negUnlToDisable);
        if (negUnlToReEnable)
            negUnlKeys.erase(*negUnlToReEnable);

        hash_set<NodeID> negUnlNodeIDs;
        for (auto const& k : negUnlKeys)
        {
            auto nid = calcNodeID(k);
            negUnlNodeIDs.emplace(nid);
            if (!nidToKeyMap.count(nid))
            {
                nidToKeyMap.emplace(nid, k);
            }
        }

        auto const seq = prevLedger->info().seq + 1;
        purgeNewValidators(seq);

        // Process the table and find all candidates to disable or to re-enable
        auto const candidates =
            findAllCandidates(unlNodeIDs, negUnlNodeIDs, *scoreTable);

        // Pick one to disable and one to re-enable if any, add ttUNL_MODIFY Tx
        if (!candidates.toDisableCandidates.empty())
        {
            auto n =
                choose(prevLedger->info().hash, candidates.toDisableCandidates);
            assert(nidToKeyMap.count(n));
            addTx(seq, nidToKeyMap[n], ToDisable, initialSet);
        }

        if (!candidates.toReEnableCandidates.empty())
        {
            auto n = choose(
                prevLedger->info().hash, candidates.toReEnableCandidates);
            assert(nidToKeyMap.count(n));
            addTx(seq, nidToKeyMap[n], ToReEnable, initialSet);
        }
    }
}

void
NegativeUNLVote::addTx(
    LedgerIndex seq,
    PublicKey const& vp,
    NegativeUNLModify modify,
    std::shared_ptr<SHAMap> const& initialSet)
{
    STTx negUnlTx(ttUNL_MODIFY, [&](auto& obj) {
        obj.setFieldU8(sfUNLModifyDisabling, modify == ToDisable ? 1 : 0);
        obj.setFieldU32(sfLedgerSequence, seq);
        obj.setFieldVL(sfUNLModifyValidator, vp.slice());
    });

    uint256 txID = negUnlTx.getTransactionID();
    Serializer s;
    negUnlTx.add(s);
    if (!initialSet->addGiveItem(
            SHAMapNodeType::tnTRANSACTION_NM,
            std::make_shared<SHAMapItem>(txID, s.peekData())))
    {
        JLOG(j_.warn()) << "N-UNL: ledger seq=" << seq
                        << ", add ttUNL_MODIFY tx failed";
    }
    else
    {
        JLOG(j_.debug()) << "N-UNL: ledger seq=" << seq
                         << ", add a ttUNL_MODIFY Tx with txID: " << txID
                         << ", the validator to "
                         << (modify == ToDisable ? "disable: " : "re-enable: ")
                         << vp;
    }
}

NodeID
NegativeUNLVote::choose(
    uint256 const& randomPadData,
    std::vector<NodeID> const& candidates)
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

std::optional<hash_map<NodeID, std::uint32_t>>
NegativeUNLVote::buildScoreTable(
    std::shared_ptr<Ledger const> const& prevLedger,
    hash_set<NodeID> const& unl,
    RCLValidations& validations)
{
    // Find agreed validation messages received for
    // the last FLAG_LEDGER_INTERVAL (i.e. 256) ledgers,
    // for every validator, and fill the score table.

    // Ask the validation container to keep enough validation message history
    // for next time.
    auto const seq = prevLedger->info().seq + 1;
    validations.setSeqToKeep(seq - 1);

    // Find FLAG_LEDGER_INTERVAL (i.e. 256) previous ledger hashes
    auto const hashIndex = prevLedger->read(keylet::skip());
    if (!hashIndex || !hashIndex->isFieldPresent(sfHashes))
    {
        JLOG(j_.debug()) << "N-UNL: ledger " << seq << " no history.";
        return {};
    }
    auto const ledgerAncestors = hashIndex->getFieldV256(sfHashes).value();
    auto const numAncestors = ledgerAncestors.size();
    if (numAncestors < FLAG_LEDGER_INTERVAL)
    {
        JLOG(j_.debug()) << "N-UNL: ledger " << seq
                         << " not enough history. Can trace back only "
                         << numAncestors << " ledgers.";
        return {};
    }

    // have enough ledger ancestors, build the score table
    hash_map<NodeID, std::uint32_t> scoreTable;
    for (auto const& k : unl)
    {
        scoreTable[k] = 0;
    }

    // Query the validation container for every ledger hash and fill
    // the score table.
    for (int i = 0; i < FLAG_LEDGER_INTERVAL; ++i)
    {
        for (auto const& v : validations.getTrustedForLedger(
                 ledgerAncestors[numAncestors - 1 - i]))
        {
            if (scoreTable.count(v->getNodeID()))
                ++scoreTable[v->getNodeID()];
        }
    }

    // Return false if the validation message history or local node's
    // participation in the history is not good.
    auto const myValidationCount = [&]() -> std::uint32_t {
        if (auto const it = scoreTable.find(myId_); it != scoreTable.end())
            return it->second;
        return 0;
    }();
    if (myValidationCount < negativeUNLMinLocalValsToVote)
    {
        JLOG(j_.debug()) << "N-UNL: ledger " << seq
                         << ". Local node only issued " << myValidationCount
                         << " validations in last " << FLAG_LEDGER_INTERVAL
                         << " ledgers."
                         << " The reliability measurement could be wrong.";
        return {};
    }
    else if (
        myValidationCount > negativeUNLMinLocalValsToVote &&
        myValidationCount <= FLAG_LEDGER_INTERVAL)
    {
        return scoreTable;
    }
    else
    {
        // cannot happen because validations.getTrustedForLedger does not
        // return multiple validations of the same ledger from a validator.
        JLOG(j_.error()) << "N-UNL: ledger " << seq << ". Local node issued "
                         << myValidationCount << " validations in last "
                         << FLAG_LEDGER_INTERVAL << " ledgers. Too many!";
        return {};
    }
}

NegativeUNLVote::Candidates const
NegativeUNLVote::findAllCandidates(
    hash_set<NodeID> const& unl,
    hash_set<NodeID> const& negUnl,
    hash_map<NodeID, std::uint32_t> const& scoreTable)
{
    // Compute if need to find more validators to disable
    auto const canAdd = [&]() -> bool {
        auto const maxNegativeListed = static_cast<std::size_t>(
            std::ceil(unl.size() * negativeUNLMaxListed));
        std::size_t negativeListed = 0;
        for (auto const& n : unl)
        {
            if (negUnl.count(n))
                ++negativeListed;
        }
        bool const result = negativeListed < maxNegativeListed;
        JLOG(j_.trace()) << "N-UNL: nodeId " << myId_ << " lowWaterMark "
                         << negativeUNLLowWaterMark << " highWaterMark "
                         << negativeUNLHighWaterMark << " canAdd " << result
                         << " negativeListed " << negativeListed
                         << " maxNegativeListed " << maxNegativeListed;
        return result;
    }();

    Candidates candidates;
    for (auto const& [nodeId, score] : scoreTable)
    {
        JLOG(j_.trace()) << "N-UNL: node " << nodeId << " score " << score;

        // Find toDisable Candidates: check if
        //  (1) canAdd,
        //  (2) has less than negativeUNLLowWaterMark validations,
        //  (3) is not in negUnl, and
        //  (4) is not a new validator.
        if (canAdd && score < negativeUNLLowWaterMark &&
            !negUnl.count(nodeId) && !newValidators_.count(nodeId))
        {
            JLOG(j_.trace()) << "N-UNL: toDisable candidate " << nodeId;
            candidates.toDisableCandidates.push_back(nodeId);
        }

        // Find toReEnable Candidates: check if
        //  (1) has more than negativeUNLHighWaterMark validations,
        //  (2) is in negUnl
        if (score > negativeUNLHighWaterMark && negUnl.count(nodeId))
        {
            JLOG(j_.trace()) << "N-UNL: toReEnable candidate " << nodeId;
            candidates.toReEnableCandidates.push_back(nodeId);
        }
    }

    // If a negative UNL validator is removed from nodes' UNLs, it is no longer
    // a validator. It should be removed from the negative UNL too.
    // Note that even if it is still offline and in minority nodes' UNLs, it
    // will not be re-added to the negative UNL. Because the UNLModify Tx will
    // not be included in the agreed TxSet of a ledger.
    //
    // Find this kind of toReEnable Candidate if did not find any toReEnable
    // candidate yet: check if
    //  (1) is in negUnl
    //  (2) is not in unl.
    if (candidates.toReEnableCandidates.empty())
    {
        for (auto const& n : negUnl)
        {
            if (!unl.count(n))
            {
                candidates.toReEnableCandidates.push_back(n);
            }
        }
    }
    return candidates;
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
