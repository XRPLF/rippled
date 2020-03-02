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

#ifndef RIPPLE_APP_MISC_NEGATIVEUNLVOTE_H_INCLUDED
#define RIPPLE_APP_MISC_NEGATIVEUNLVOTE_H_INCLUDED

#include <ripple/beast/utility/Journal.h>

namespace ripple {

class Ledger;
template <class Adaptor>
class Validations;
class RCLValidationsAdaptor;
using RCLValidations = Validations<RCLValidationsAdaptor>;
class SHAMap;
namespace test {
class NegativeUNLVoteInternal_test;
class NegativeUNLVoteScoreTable_test;
}  // namespace test

/**
 * Manager to create NegativeUNL votes.
 */
class NegativeUNLVote final
{
public:
    /**
     * A validator is considered unreliable if its validations is less than
     * nUnlLowWaterMark in the last flag ledger period.
     * An unreliable validator is a candidate to be disabled by the NegativeUNL
     * protocol.
     */
    static constexpr size_t nUnlLowWaterMark = 128;  // 256 * 0.5;
    /**
     * An unreliable validator must have more than nUnlHighWaterMark validations
     * in the last flag ledger period to be re-enabled.
     */
    static constexpr size_t nUnlHighWaterMark = 204;  //~256 * 0.8;
    /**
     * The minimum number of validations of the local node for it to
     * participate in the voting.
     */
    static constexpr size_t nUnlMinLocalValsToVote = 230;  //~256 * 0.9;
    /**
     * We don't want to disable new validators immediately after adding them.
     * So we skip voting for disabling them for 2 flag ledgers.
     */
    static constexpr size_t newValidatorDisableSkip = 512;  // 256 * 2;
    /**
     * We only want to put 25% of the UNL on the NegativeUNL.
     */
    static constexpr float nUnlMaxListed = 0.25;

    /**
     * Constructor
     *
     * @param myId the NodeID of the local node
     * @param j log
     */
    NegativeUNLVote(NodeID const& myId, beast::Journal j);
    ~NegativeUNLVote() = default;

    using LedgerConstPtr = std::shared_ptr<Ledger const> const;

    /**
     * Cast our local vote on the NegativeUNL candidates.
     *
     * @param prevLedger the parent ledger
     * @param unlKeys the trusted master keys
     * @param validations the validation message container
     * @param initialSet the set of transactions
     */
    void
    doVoting(
        LedgerConstPtr& prevLedger,
        hash_set<PublicKey> const& unlKeys,
        RCLValidations& validations,
        std::shared_ptr<SHAMap> const& initialSet);

    /**
     * Notify NegativeUNLVote that new validators are added.
     * So that they don't get voted to the NegativeUNL immediately.
     *
     * @param seq the current LedgerIndex when adding the new validators
     * @param nowTrusted the new validators
     */
    void
    newValidators(LedgerIndex seq, hash_set<NodeID> const& nowTrusted);

private:
    NodeID const myId_;
    beast::Journal j_;
    std::mutex mutex_;
    hash_map<NodeID, LedgerIndex> newValidators_;

    /**
     * Add a ttUNL_MODIDY Tx to the transaction set.
     *
     * @param seq the LedgerIndex when adding the Tx
     * @param vp the master public key of the validator
     * @param disabling disabling or re-enabling the validator
     * @param initialSet the transaction set
     */
    void
    addTx(
        LedgerIndex seq,
        PublicKey const& vp,
        bool disabling,
        std::shared_ptr<SHAMap> const& initialSet);

    /**
     * Pick one candidate from a vector of candidates.
     *
     * @param randomPadData the data used for picking a candidate.
     *        @note Nodes must use the same randomPadData for picking the same
     *        candidate. The hash of the parent ledger is a good choice.
     * @param candidates the vector of candidates
     * @return the picked candidate
     */
    NodeID
    pickOneCandidate(uint256 randomPadData, std::vector<NodeID>& candidates);

    /**
     * Build a reliability measurement score table of validators' validation
     * messages in the last flag ledger period.
     *
     * @param prevLedger the parent ledger
     * @param unl the trusted master keys
     * @param validations the validation container
     * @param scoreTable the score table to be built
     * @return if the score table was successfully filled
     */
    bool
    buildScoreTable(
        LedgerConstPtr& prevLedger,
        hash_set<NodeID> const& unl,
        RCLValidations& validations,
        hash_map<NodeID, unsigned int>& scoreTable);

    /**
     * Process the score table and find all disabling and re-enabling
     * candidates.
     *
     * @param unl the trusted master keys
     * @param nUnl the NegativeUNL
     * @param scoreTable the score table
     * @param toDisableCandidates the candidates to disable
     * @param toReEnableCandidates the candidates to re-enable
     */
    void
    findAllCandidates(
        hash_set<NodeID> const& unl,
        hash_set<NodeID> const& nUnl,
        hash_map<NodeID, unsigned int> const& scoreTable,
        std::vector<NodeID>& toDisableCandidates,
        std::vector<NodeID>& toReEnableCandidates);

    /**
     * Purge validators that are not new anymore.
     *
     * @param seq the current LedgerIndex
     */
    void
    purgeNewValidators(LedgerIndex seq);

    friend class test::NegativeUNLVoteInternal_test;
    friend class test::NegativeUNLVoteScoreTable_test;
};

}  // namespace ripple

#endif
