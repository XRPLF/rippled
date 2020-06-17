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

#ifndef RIPPLE_APP_MISC_NEGATIVEUNLVOTE_H_INCLUDED
#define RIPPLE_APP_MISC_NEGATIVEUNLVOTE_H_INCLUDED

#include <ripple/app/ledger/Ledger.h>
#include <ripple/beast/utility/Journal.h>
#include <ripple/protocol/Protocol.h>
#include <ripple/protocol/PublicKey.h>
#include <ripple/protocol/UintTypes.h>

#include <optional>

namespace ripple {

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
     * negativeUnlLowWaterMark in the last flag ledger period.
     * An unreliable validator is a candidate to be disabled by the NegativeUNL
     * protocol.
     */
    static constexpr size_t negativeUnlLowWaterMark =
        FLAG_LEDGER_INTERVAL * 50 / 100;
    /**
     * An unreliable validator must have more than negativeUnlHighWaterMark
     * validations in the last flag ledger period to be re-enabled.
     */
    static constexpr size_t negativeUnlHighWaterMark =
        FLAG_LEDGER_INTERVAL * 80 / 100;
    /**
     * The minimum number of validations of the local node for it to
     * participate in the voting.
     */
    static constexpr size_t negativeUnlMinLocalValsToVote =
        FLAG_LEDGER_INTERVAL * 90 / 100;
    /**
     * We don't want to disable new validators immediately after adding them.
     * So we skip voting for disabling them for 2 flag ledgers.
     */
    static constexpr size_t newValidatorDisableSkip = FLAG_LEDGER_INTERVAL * 2;
    /**
     * We only want to put 25% of the UNL on the NegativeUNL.
     */
    static constexpr float negativeUnlMaxListed = 0.25;

    /**
     * A flag indicating whether a UNLModify Tx is to disable or to re-enable
     * a validator.
     */
    enum NegativeUNLModify {
        ToDisable,  // UNLModify Tx is to disable a validator
        ToReEnable  // UNLModify Tx is to re-enable a validator
    };

    /**
     * Constructor
     *
     * @param myId the NodeID of the local node
     * @param j log
     */
    NegativeUNLVote(NodeID const& myId, beast::Journal j);
    ~NegativeUNLVote() = default;

    /**
     * Cast our local vote on the NegativeUNL candidates.
     *
     * @param prevLedger the parent ledger
     * @param unlKeys the trusted master keys of validators in the UNL
     * @param validations the validation message container
     * @note validations is an in/out parameter. It contains validation messages
     * that will be deleted when no longer needed by other consensus logic. This
     * function asks it to keep the validation messages long enough for this
     * function to use.
     * @param initialSet the transactions set for adding ttUNL_MODIFY Tx if any
     */
    void
    doVoting(
        std::shared_ptr<Ledger const> const& prevLedger,
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
    mutable std::mutex mutex_;
    hash_map<NodeID, LedgerIndex> newValidators_;

    /**
     * UNLModify Tx candidates
     */
    struct Candidates
    {
        std::vector<NodeID> toDisableCandidates;
        std::vector<NodeID> toReEnableCandidates;
    };

    /**
     * Add a ttUNL_MODIFY Tx to the transaction set.
     *
     * @param seq the LedgerIndex when adding the Tx
     * @param vp the master public key of the validator
     * @param modify disabling or re-enabling the validator
     * @param initialSet the transaction set
     */
    void
    addTx(
        LedgerIndex seq,
        PublicKey const& vp,
        NegativeUNLModify modify,
        std::shared_ptr<SHAMap> const& initialSet);

    /**
     * Pick one candidate from a vector of candidates.
     *
     * @param randomPadData the data used for picking a candidate.
     *        @note Nodes must use the same randomPadData for picking the same
     *        candidate. The hash of the parent ledger is used.
     * @param candidates the vector of candidates
     * @return the picked candidate
     */
    NodeID
    choose(uint256 const& randomPadData, std::vector<NodeID> const& candidates);

    /**
     * Build a reliability measurement score table of validators' validation
     * messages in the last flag ledger period.
     *
     * @param prevLedger the parent ledger
     * @param unl the trusted master keys
     * @param validations the validation container
     * @note validations is an in/out parameter. It contains validation messages
     * that will be deleted when no longer needed by other consensus logic. This
     * function asks it to keep the validation messages long enough for this
     * function to use.
     * @return the built scoreTable or empty optional if table could not be
     * built
     */
    std::optional<hash_map<NodeID, std::uint32_t>>
    buildScoreTable(
        std::shared_ptr<Ledger const> const& prevLedger,
        hash_set<NodeID> const& unl,
        RCLValidations& validations);

    /**
     * Process the score table and find all disabling and re-enabling
     * candidates.
     *
     * @param unl the trusted master keys
     * @param negUnl the NegativeUNL
     * @param scoreTable the score table
     * @return the candidates to disable and the candidates to re-enable
     */
    Candidates const
    findAllCandidates(
        hash_set<NodeID> const& unl,
        hash_set<NodeID> const& negUnl,
        hash_map<NodeID, std::uint32_t> const& scoreTable);

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
