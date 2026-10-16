#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>

#include <cstdint>
#include <map>
#include <memory>

namespace xrpl {

/**
 * @brief Invariants: Confidential ballot consistency.
 *
 * - The encrypted tally array always has exactly OptionCount entries.
 * - A results array, when present, has exactly OptionCount entries and only
 *   appears on a finalized ballot.
 * - A ballot's VoteCount never decreases, and rises by exactly the number of
 *   BallotVote objects created against it in the same transaction.
 * - No BallotVote is created without a matching VoteCount increment on its
 *   ballot.
 */
class ValidBallot
{
    struct BallotChange
    {
        std::shared_ptr<SLE const> before;
        std::shared_ptr<SLE const> after;
    };

    std::map<uint256, BallotChange> ballots_;
    std::map<uint256, std::uint32_t> createdVotes_;

public:
    /**
     * @brief Track ballot modifications and BallotVote creations.
     *
     * @param isDelete Whether the ledger entry is being deleted.
     * @param before The ledger entry before transaction application.
     * @param after The ledger entry after transaction application.
     */
    void
    visitEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after);

    /**
     * @brief Verify ballot tally, results, and vote-count invariants.
     *
     * @param tx The transaction being checked.
     * @param result The transaction result code.
     * @param fee The fee charged by the transaction.
     * @param view The ledger view after transaction application.
     * @param j Journal used for diagnostics.
     * @return true if the invariant checks pass, otherwise false.
     */
    bool
    finalize(
        STTx const& tx,
        TER const result,
        XRPAmount const fee,
        ReadView const& view,
        beast::Journal const& j);
};

}  // namespace xrpl
