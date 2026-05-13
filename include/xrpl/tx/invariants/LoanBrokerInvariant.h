#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>

#include <map>
#include <vector>

namespace xrpl {

/** Invariant checker that verifies every touched `LoanBroker` ledger object is
 *  internally consistent after a transaction.
 *
 *  Implements the `InvariantChecker_PROTOTYPE` two-method interface and is
 *  registered unconditionally in the `InvariantChecks` tuple.  It enforces:
 *
 *  1. **Zero-OwnerCount directory structure** — when `sfOwnerCount == 0` the
 *     broker's owner directory may have at most one root page containing at
 *     most one entry, and that entry must be an `ltRIPPLE_STATE` or
 *     `ltMPTOKEN` object.
 *  2. **Sequence monotonicity** — `sfLoanSequence` must not decrease between
 *     the before and after snapshots.
 *  3. **Non-negative financials** — `sfDebtTotal` and `sfCoverAvailable` must
 *     each be ≥ 0.
 *  4. **Vault linkage** — `sfVaultID` must reference an existing `ltVAULT`.
 *  5. **Cover–balance lower bound** — `sfCoverAvailable` must be ≥ the
 *     pseudo-account's actual asset balance (via `accountHolds`).
 *  6. **Cover–balance upper bound** (when `fixSecurity3_1_3` is active) —
 *     `sfCoverAvailable` must also be ≤ the pseudo-account balance, except
 *     during `ttLOAN_BROKER_DELETE` where `sfCoverAvailable` is intentionally
 *     left un-zeroed at deletion.
 *
 *  Brokers are discovered through three channels during `visitEntry`: direct
 *  `ltLOAN_BROKER` touches, `ltACCOUNT_ROOT` pseudo-account touches carrying
 *  `sfLoanBrokerID`, and deferred resolution of modified `ltRIPPLE_STATE` /
 *  `ltMPTOKEN` entries whose owning accounts may be broker pseudo-accounts.
 *  The map-keyed design deduplicates brokers reached through multiple channels.
 *
 *  @note No amendment gate is needed: `LoanBroker` objects can only exist when
 *      the Lending Protocol amendment is active, so an empty `brokers_` map
 *      on pre-amendment ledgers is the correct fast-path.
 */
class ValidLoanBroker
{
    /** Snapshot pair for one broker implicated by a transaction.
     *
     *  `brokerAfter` is the primary snapshot used for absolute-value checks.
     *  `brokerBefore` is used only for monotonicity checks that require a
     *  before/after comparison; it may be `nullptr` for newly-created brokers.
     *  Either pointer may be `nullptr` if the broker was discovered indirectly
     *  (e.g., through a modified trust line); in that case `finalize()` reads
     *  the current SLE from the view.
     */
    struct BrokerInfo
    {
        SLE::const_pointer brokerBefore = nullptr;
        SLE::const_pointer brokerAfter = nullptr;
    };

    /** Brokers implicated by this transaction, keyed by ledger index (broker
     *  ID).  Populated directly from `ltLOAN_BROKER` or `ltACCOUNT_ROOT`
     *  (`sfLoanBrokerID`) touches in `visitEntry`, and extended in `finalize`
     *  via trust-line and MPToken endpoint resolution.  `std::map::emplace`
     *  prevents double-entry when the same broker is reached through multiple
     *  channels.
     */
    std::map<uint256, BrokerInfo> brokers_;

    /** Modified trust lines collected during `visitEntry`.  In `finalize`,
     *  both the high and low account of each line are resolved; if either
     *  carries `sfLoanBrokerID` the broker is added to `brokers_`.
     */
    std::vector<SLE::const_pointer> lines_;

    /** Modified MPToken entries collected during `visitEntry`.  In `finalize`,
     *  the owning account of each token is resolved; if it carries
     *  `sfLoanBrokerID` the broker is added to `brokers_`.
     */
    std::vector<SLE::const_pointer> mpts_;

    /** Validate the owner directory of a broker whose `sfOwnerCount` is zero.
     *
     *  Checks that `dir` is a single-page directory (no chained pages via
     *  `sfIndexNext` / `sfIndexPrevious`) containing at most one entry, and
     *  that the entry — if present — resolves to an `ltRIPPLE_STATE` or
     *  `ltMPTOKEN` object.  Logs a fatal message for each violation found.
     *
     *  @param view   Read-only view used to resolve the referenced object.
     *  @param dir    The owner-directory root SLE to inspect.
     *  @param j      Journal for fatal-level diagnostic logging.
     *  @return `true` if the directory satisfies the zero-owner-count
     *      structural constraint; `false` otherwise.
     */
    static bool
    goodZeroDirectory(ReadView const& view, SLE::const_ref dir, beast::Journal const& j);

public:
    /** Accumulate ledger entries modified by the transaction for later
     *  validation.
     *
     *  Records `ltLOAN_BROKER` SLEs directly into `brokers_` with their
     *  before/after snapshots.  Records `ltACCOUNT_ROOT` entries that carry
     *  `sfLoanBrokerID` (broker pseudo-accounts) as broker stubs so that
     *  `finalize()` will look them up even if the `ltLOAN_BROKER` SLE was not
     *  itself modified.  Collects `ltRIPPLE_STATE` and `ltMPTOKEN` entries
     *  into `lines_` and `mpts_` for deferred broker-discovery in `finalize`.
     *
     *  @param isDelete  `true` when the entry is being removed from the ledger.
     *  @param before    SLE snapshot before the transaction; `nullptr` if the
     *      entry was created by this transaction.
     *  @param after     SLE snapshot after the transaction; `nullptr` if the
     *      entry was deleted by this transaction.
     */
    void
    visitEntry(bool isDelete, std::shared_ptr<SLE const> const& before, std::shared_ptr<SLE const> const& after);

    /** Validate all `LoanBroker` objects implicated by the transaction.
     *
     *  First extends `brokers_` by resolving the high/low accounts of every
     *  collected trust line and the owner account of every collected MPToken,
     *  adding any broker pseudo-accounts discovered there.  Then iterates over
     *  `brokers_` and for each broker asserts the six invariants described in
     *  the class documentation.
     *
     *  @param tx    The transaction being applied (used to detect
     *      `ttLOAN_BROKER_DELETE`, which exempts the cover upper-bound check).
     *  @param ter   Result code returned by `doApply` (unused by this checker).
     *  @param fee   Fee charged by the transaction (unused by this checker).
     *  @param view  Read-only post-transaction ledger view used to resolve
     *      broker SLEs not captured directly during `visitEntry`, vault
     *      objects, and pseudo-account balances.
     *  @param j     Journal for fatal-level diagnostic logging on failure.
     *  @return `true` if every implicated broker satisfies all invariants;
     *      `false` if any invariant is violated (transaction will be rolled
     *      back and escalated to `tecINVARIANT_FAILED`).
     */
    bool
    finalize(STTx const& tx, TER const ter, XRPAmount const fee, ReadView const& view, beast::Journal const& j);
};

}  // namespace xrpl
