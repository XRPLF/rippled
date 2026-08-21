#pragma once

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>

#include <map>
#include <unordered_map>
#include <utility>
#include <vector>

namespace xrpl {

/**
 * @brief Invariants: Loan brokers are internally consistent
 *
 * 1. If `LoanBroker.OwnerCount = 0` the `DirectoryNode` will have at most one
 *    node (the root), which will only hold entries for `RippleState` or
 * `MPToken` objects.
 *
 */
class ValidLoanBroker
{
    // Not all of these elements will necessarily be populated. Remaining items
    // will be looked up as needed.
    struct BrokerInfo
    {
        SLE::const_pointer brokerBefore = nullptr;
        // After is used for most of the checks, except
        // those that check changed values.
        SLE::const_pointer brokerAfter = nullptr;
    };
    // Collect all the LoanBrokers found directly or indirectly through
    // pseudo-accounts. Key is the brokerID / index. It will be used to find the
    // LoanBroker object if brokerBefore and brokerAfter are nullptr. Populated
    // for live (created or modified) brokers only; deletions go to
    // deletedBrokers_ below so the live-broker check loop does not have to
    // exempt the delete case.
    std::map<uint256, BrokerInfo> brokers_;
    // Pre-deletion snapshots of brokers erased by this transaction. Deletion
    // preconditions (e.g. first-loss capital was returned to the owner) are
    // stated positively against this collection, rather than by exempting the
    // delete case from the live-broker checks.
    std::vector<SLE::const_pointer> deletedBrokers_;
    // Collect all the modified trust lines. Their high and low accounts will be
    // loaded to look for LoanBroker pseudo-accounts.
    std::vector<SLE::const_pointer> lines_;
    // Collect all the modified MPTokens. Their accounts will be loaded to look
    // for LoanBroker pseudo-accounts.
    std::vector<SLE::const_pointer> mpts_;
    // Every touched (created or modified) balance-bearing entry, keyed by its
    // ledger key. Used by the broker-deletion checks to compute the change in
    // the owner's vault-asset balance and verify it matches the returned
    // first-loss capital. Deletion-side entries are not stored: their
    // after-state balance is zero by construction.
    std::unordered_map<uint256, std::pair<SLE::const_pointer, SLE::const_pointer>>
        touchedBalances_;

    static bool
    goodZeroDirectory(ReadView const& view, SLE::const_ref dir, beast::Journal const& j);

    // Return the balance of @p id in @p asset held by @p sle. Handles XRP
    // (ACCOUNT_ROOT), IOU (RIPPLE_STATE, sign-flipped per which side is @p id)
    // and MPT (MPTOKEN). Returns 0 when @p sle is null, so callers can compute
    // a delta uniformly across create / modify / delete transitions.
    [[nodiscard]] static Number
    balanceOf(SLE::const_ref sle, AccountID const& id, Asset const& asset);

public:
    void
    visitEntry(bool, SLE::const_ref, SLE::const_ref);

    // The TER parameter is named because some checks (deltas, deletion
    // post-conditions, participant-flow identities) are class-2 and must gate
    // on isTesSuccess(result) to avoid firing against the fee-claim-only state
    // the framework re-runs in InvariantScope::ProtocolOnly.
    bool
    finalize(
        STTx const&,
        TER const result,
        XRPAmount const,
        ReadView const&,
        beast::Journal const&);
};

}  // namespace xrpl
