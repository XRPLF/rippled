/** @file
 *  Invariant checker declarations for Multi-Purpose Token (MPT) ledger
 *  consistency.
 *
 *  Declares `ValidMPTIssuance` and `ValidMPTPayment`, which together guard
 *  the MPT subsystem against state corruption after every transaction
 *  application.  Both classes follow the two-phase invariant contract:
 *  `visitEntry()` accumulates per-SLE data, and `finalize()` renders a
 *  pass/fail verdict once all entries have been visited.  They are registered
 *  in the `InvariantChecks` tuple in `InvariantCheck.h` and run
 *  unconditionally — including on failed transactions — as the last line of
 *  defence against unexpected ledger mutations.
 *
 *  `ValidMPTIssuance` addresses *object lifecycle* (did the correct set of
 *  `ltMPTOKEN_ISSUANCE` and `ltMPTOKEN` entries appear or disappear?).
 *  `ValidMPTPayment` addresses *numeric conservation* (does each issuance's
 *  `OutstandingAmount` remain equal to the sum of all holder balances?).
 *  The two concerns are kept in separate classes so each remains small and
 *  straightforward to audit.
 */
#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>

#include <cstdint>

namespace xrpl {

/** Invariant checker that enforces structural integrity of MPT lifecycle
 *  objects.
 *
 *  During `visitEntry()` the checker counts `ltMPTOKEN_ISSUANCE` and
 *  `ltMPTOKEN` entries created and deleted, and flags the pathological case
 *  where a new `ltMPTOKEN` was auto-created for the issuance's own issuer
 *  account (always a protocol violation).
 *
 *  During `finalize()` the accumulated counts are validated against the
 *  transaction's privilege bitmask (see `InvariantCheckPrivilege.h`):
 *  - `createMPTIssuance` → exactly one `ltMPTOKEN_ISSUANCE` created, none
 *    deleted.
 *  - `destroyMPTIssuance` → exactly one deleted, none created.
 *  - `mustAuthorizeMPT` (holder submission) → exactly one `ltMPTOKEN`
 *    created or deleted.
 *  - `mayAuthorizeMPT` (e.g. `ttAMM_WITHDRAW`, `ttAMM_CLAWBACK`) → at most
 *    one created, at most two deleted (covers both sides of an AMM pool
 *    dissolution).
 *  - `mayCreateMPT` → auto-creation only: up to two for `ttAMM_CREATE`, up
 *    to one for `ttCHECK_CASH`; no deletions.
 *  - `mayDeleteMPT` → deletions only, no creations.
 *  - No privilege → all counters must be zero on a successful result.
 *
 *  The issuer-MPToken check uses the `assert(enforce)` soft-rollout pattern:
 *  the fatal log fires unconditionally, but the hard `false` return is gated
 *  on `featureSingleAssetVault` or `featureLendingProtocol` being active,
 *  preserving backward compatibility while surfacing problems to operators
 *  and debug builds.
 *
 *  @see ValidMPTPayment
 *  @see InvariantCheckPrivilege.h
 */
class ValidMPTIssuance
{
    std::uint32_t mptIssuancesCreated_ = 0;
    std::uint32_t mptIssuancesDeleted_ = 0;

    std::uint32_t mptokensCreated_ = 0;
    std::uint32_t mptokensDeleted_ = 0;
    // non-MPT transactions may attempt to create
    // MPToken by an issuer
    bool mptCreatedByIssuer_ = false;

public:
    /** Accumulate MPT object counts from a single modified ledger entry.
     *
     *  Updates the four creation/deletion counters for `ltMPTOKEN_ISSUANCE`
     *  and `ltMPTOKEN` entries.  Sets `mptCreatedByIssuer_` when a newly
     *  created `ltMPTOKEN` belongs to the issuance's own issuer account —
     *  a condition that is always a protocol violation.
     *
     *  @param isDelete `true` when the entry is being removed from the ledger.
     *  @param before   Snapshot of the SLE before the transaction; null if the
     *      entry did not exist prior to this transaction.
     *  @param after    Snapshot of the SLE after the transaction; null when
     *      the entry has been erased.
     */
    void
    visitEntry(bool isDelete, std::shared_ptr<SLE const> const& before, std::shared_ptr<SLE const> const& after);

    /** Verify that the transaction's MPT object lifecycle changes were
     *  authorized by its privilege mask.
     *
     *  Dispatches on the transaction's privilege flags rather than its type,
     *  keeping the logic independent of the growing set of MPT-capable
     *  transaction types.  With `featureMPTokensV2` enabled, `tecINCOMPLETE`
     *  results are also subject to full invariant checking because partial
     *  progress is still valid ledger state.
     *
     *  @param tx     The transaction that was applied.
     *  @param result The `TER` returned by `doApply()` or the post-reset
     *      result.
     *  @param fee    The fee deducted (unused by this checker).
     *  @param view   Read-only view of the post-apply ledger, used to query
     *      active amendment rules.
     *  @param j      Journal for fatal-level diagnostics on violation.
     *  @return `true` if all MPT lifecycle constraints are satisfied.
     */
    [[nodiscard]] bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&) const;
};

/** Invariant checker that enforces conservation of MPT outstanding amounts.
 *
 *  After any successful transaction, each `ltMPTOKEN_ISSUANCE` entry's
 *  `OutstandingAmount` must equal the sum of all holder `ltMPTOKEN` balances
 *  (`sfMPTAmount + sfLockedAmount`) for that issuance.  The conservation
 *  equation is:
 *
 *  ```
 *  OutstandingAmount[After] ==
 *      OutstandingAmount[Before] + Σ(MPTAmount[After] − MPTAmount[Before])
 *  ```
 *
 *  where locked amounts are included because they remain part of the
 *  outstanding supply.
 *
 *  Overflow is treated as a first-class failure: any individual amount that
 *  exceeds `kMAX_MP_TOKEN_AMOUNT`, or any arithmetic wrap-around in the
 *  accumulated delta, sets `overflow_` and causes `finalize()` to fail
 *  immediately.  Enforcement is amendment-gated on `featureMPTokensV2`.
 *
 *  @note Unlike `ValidMPTIssuance`, `finalize()` is non-`const` because the
 *      `data_` accumulator may be mutated lazily; `finalize()` is the single
 *      point at which all data is final.
 *
 *  @see ValidMPTIssuance
 */
class ValidMPTPayment
{
    /** Index into `MPTData::outstanding` distinguishing the pre- and
     *  post-transaction `OutstandingAmount` snapshots.
     */
    enum class Order { Before = 0, After = 1 };

    /** Per-issuance accounting data accumulated across all visited SLEs. */
    struct MPTData
    {
        /** `OutstandingAmount` before (`[0]`) and after (`[1]`) the
         *  transaction.
         */
        std::array<std::int64_t, 2> outstanding{};

        /** Net delta across all holder `ltMPTOKEN` entries:
         *  Σ(`sfMPTAmount` + `sfLockedAmount`)_after −
         *  Σ(`sfMPTAmount` + `sfLockedAmount`)_before.
         */
        // sum (MPT after - MPT before)
        std::int64_t mptAmount{0};
    };

    // true if OutstandingAmount > MaximumAmount in after for any MPT
    bool overflow_{false};
    // mptid:MPTData
    hash_map<uint192, MPTData> data_;

public:
    /** Accumulate outstanding-amount snapshots and holder-balance deltas for
     *  a single modified ledger entry.
     *
     *  For `ltMPTOKEN_ISSUANCE` entries, records the before/after
     *  `sfOutstandingAmount` and checks that the post-apply value does not
     *  exceed `sfMaximumAmount`.  For `ltMPTOKEN` entries, adds or subtracts
     *  `sfMPTAmount + sfLockedAmount` from the running `mptAmount` delta.
     *  Sets `overflow_` and returns early if any individual value exceeds
     *  `kMAX_MP_TOKEN_AMOUNT` or if the addition would wrap a 64-bit integer.
     *
     *  @note The `isDelete` parameter is unused because deletion is already
     *      signalled by `after == nullptr`; the presence of `before` is
     *      sufficient to handle the before-state contribution.
     *
     *  @param before Snapshot of the SLE before the transaction; null for
     *      newly inserted entries.
     *  @param after  Snapshot of the SLE after the transaction; null for
     *      deleted entries.
     */
    void
    visitEntry(bool, std::shared_ptr<SLE const> const& before, std::shared_ptr<SLE const> const& after);

    /** Verify the `OutstandingAmount` conservation invariant for all touched
     *  MPT issuances.
     *
     *  Iterates over every MPT ID recorded during `visitEntry()` and checks
     *  that `OutstandingAmount[After] == OutstandingAmount[Before] + mptAmount`.
     *  Fails immediately if `overflow_` was set during accumulation, or if the
     *  final delta arithmetic would overflow.  Only `tesSUCCESS` results
     *  trigger the check; the invariant passes silently for failed transactions.
     *  Hard-fails (returns `false`) only when `featureMPTokensV2` is active;
     *  before activation, violations are logged at fatal severity but return
     *  `true`.
     *
     *  @param tx     The transaction that was applied (unused by this checker).
     *  @param result The `TER` result; only `tesSUCCESS` triggers the check.
     *  @param fee    The fee deducted (unused by this checker).
     *  @param view   Read-only view used to query active amendment rules.
     *  @param j      Journal for fatal-level diagnostics on violation.
     *  @return `true` if outstanding-amount conservation holds for all touched
     *      MPT issuances.
     */
    bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&);
};

}  // namespace xrpl
