/** @file
 *  Central registry for the XRPL transaction invariant-checking system.
 *
 *  After every transaction (whether it succeeded or failed), the invariant
 *  framework scans all modified ledger entries and verifies that the result
 *  is internally consistent. If any check fails, the transaction is rolled
 *  back to a fee-only charge (`tecINVARIANT_FAILED`), or excluded from the
 *  ledger entirely (`tefINVARIANT_FAILED`) if even that minimal commit
 *  breaks an invariant.
 *
 *  This file declares the core checker classes and aggregates every checker
 *  — including those from sibling headers such as `FreezeInvariant.h`,
 *  `NFTInvariant.h`, `AMMInvariant.h`, and `VaultInvariant.h` — into the
 *  `InvariantChecks` tuple. That tuple is the single source of truth for
 *  which invariants exist. Dispatch in `ApplyContext::checkInvariantsHelper`
 *  iterates the tuple at compile time via `std::index_sequence`; no virtual
 *  calls are involved.
 *
 *  To add a new invariant: declare its class (here or in a new sibling
 *  header), then append it to `InvariantChecks`. No other registration is
 *  needed.
 *
 *  @see InvariantChecker_PROTOTYPE for the duck-typed interface every
 *       checker must satisfy.
 *  @see InvariantCheckPrivilege.h for the `Privilege` bitmask and
 *       `hasPrivilege()` used by several checkers.
 */
#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/tx/invariants/AMMInvariant.h>
#include <xrpl/tx/invariants/FreezeInvariant.h>
#include <xrpl/tx/invariants/LoanBrokerInvariant.h>
#include <xrpl/tx/invariants/LoanInvariant.h>
#include <xrpl/tx/invariants/MPTInvariant.h>
#include <xrpl/tx/invariants/NFTInvariant.h>
#include <xrpl/tx/invariants/PermissionedDEXInvariant.h>
#include <xrpl/tx/invariants/PermissionedDomainInvariant.h>
#include <xrpl/tx/invariants/VaultInvariant.h>

#include <cstdint>
#include <tuple>

namespace xrpl {

#if GENERATING_DOCS
/**
 * @brief Prototype for invariant check implementations.
 *
 * __THIS CLASS DOES NOT EXIST__ - or rather it exists in documentation only to
 * communicate the interface required of any invariant checker. Any invariant
 * check implementation should implement the public methods documented here.
 *
 * ## Rules for implementing `finalize`
 *
 * ### Invariants must run regardless of transaction result
 *
 * An invariant's `finalize` method MUST perform meaningful checks even when
 * the transaction has failed (i.e., `!isTesSuccess(tec)`). The following
 * pattern is almost certainly wrong and must never be used:
 *
 * @code
 * // WRONG: skipping all checks on failure defeats the purpose of invariants
 * if (!isTesSuccess(tec))
 *     return true;
 * @endcode
 *
 * The entire purpose of invariants is to detect and prevent the impossible.
 * A bug or exploit could cause a failed transaction to mutate ledger state in
 * unexpected ways. Invariants are the last line of defense against such
 * scenarios.
 *
 * In general: an invariant that expects a domain-specific state change to
 * occur (e.g., a new object being created) should only expect that change
 * when the transaction succeeded. A failed VaultCreate must not have created
 * a Vault. A failed LoanSet must not have created a Loan.
 *
 * Also be aware that failed transactions, regardless of type, carry no
 * Privileges. Any privilege-gated checks must therefore also be applied to
 * failed transactions.
 */
class InvariantChecker_PROTOTYPE
{
public:
    explicit InvariantChecker_PROTOTYPE() = default;

    /**
     * @brief called for each ledger entry in the current transaction.
     *
     * @param isDelete true if the SLE is being deleted
     * @param before ledger entry before modification by the transaction
     * @param after ledger entry after modification by the transaction
     */
    void
    visitEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after);

    /**
     * @brief called after all ledger entries have been visited to determine
     * the final status of the check.
     *
     * This method MUST perform meaningful checks even when `tec` indicates a
     * failed transaction. See the class-level documentation for the rules
     * governing how failed transactions must be handled.
     *
     * @param tx the transaction being applied
     * @param tec the current TER result of the transaction
     * @param fee the fee actually charged for this transaction
     * @param view a ReadView of the ledger being modified
     * @param j journal for logging
     *
     * @return true if check passes, false if it fails
     */
    bool
    finalize(
        STTx const& tx,
        TER const tec,
        XRPAmount const fee,
        ReadView const& view,
        beast::Journal const& j);
};
#endif

/** Invariant: the fee charged must be non-negative, less than the total XRP
 *  supply, and no greater than the fee the transaction authorized.
 *
 *  Undercharging is permitted (e.g., when an account's balance is clamped by
 *  `reset()`), but overcharging or a negative fee is always a bug.
 *  `visitEntry` is a no-op; all logic is in `finalize`.
 */
class TransactionFeeCheck
{
public:
    /** No-op: fee validation needs no per-entry state. */
    void
    visitEntry(bool, std::shared_ptr<SLE const> const&, std::shared_ptr<SLE const> const&);

    /** Verify the fee is in `[0, sfFee]` and strictly below `INITIAL_XRP`.
     *
     *  @param fee The fee actually deducted from the sending account.
     *  @return `true` if the fee is valid; `false` with a `fatal` log entry
     *      on any violation.
     */
    static bool
    finalize(STTx const&, TER const, XRPAmount const fee, ReadView const&, beast::Journal const&);
};

/** Invariant: a transaction must not create XRP; it may only destroy XRP
 *  equal to the fee charged.
 *
 *  Accumulates the net drop-level change across account roots, payment
 *  channels, and escrows. Payment channel net is `sfAmount - sfBalance`
 *  (unclaimed funds). Escrow and pay-channel deletions skip the `after`
 *  side because those entries' amount fields are not adjusted at deletion
 *  time — only the pre-deletion value is subtracted.
 */
class XRPNotCreated
{
    std::int64_t drops_ = 0;

public:
    /** Accumulate XRP delta for account roots, payment channels, and escrows.
     *
     *  @param isDelete `true` when the entry is being deleted; suppresses the
     *      `after` contribution for pay-channel and escrow entries.
     *  @param before Entry state before the transaction; null for new entries.
     *  @param after Entry state after the transaction; null for deleted entries.
     */
    void
    visitEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after);

    /** Verify net XRP change equals exactly `-fee`.
     *
     *  @return `true` if the net drop delta equals the negative of the fee;
     *      `false` with a `fatal` log entry if XRP was created or the net
     *      change does not match the fee.
     */
    [[nodiscard]] bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&) const;
};

/** Invariant: account roots may only be deleted by transactions that hold
 *  the correct deletion privilege.
 *
 *  Transactions with `MustDeleteAcct` (e.g., `AccountDelete`, `AMMDelete`)
 *  must delete exactly one account root on success. Transactions with
 *  `MayDeleteAcct` (e.g., `AMMWithdraw`, `AMMClawback`) may delete at most
 *  one. All other transactions must delete zero.
 *
 *  @note Privilege semantics are defined in `InvariantCheckPrivilege.h` and
 *      encoded per-transaction-type in `transactions.macro`.
 */
class AccountRootsNotDeleted
{
    std::uint32_t accountsDeleted_ = 0;

public:
    /** Count deleted account roots.
     *
     *  @param isDelete `true` when the entry is being deleted.
     *  @param before Entry state before modification; null for new entries.
     *  @param after Unused by this checker.
     */
    void
    visitEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after);

    /** Verify deletion count matches the transaction's privilege.
     *
     *  @return `true` if the deletion count is consistent with the privilege
     *      held by the transaction; `false` with a `fatal` log entry otherwise.
     */
    [[nodiscard]] bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&) const;
};

/** Invariant: a deleted account must leave no orphaned ledger objects behind.
 *
 *  For every deleted account root, verifies: the post-deletion balance is
 *  zero, the owner count is zero, and no directly-keyed objects (trust
 *  lines, escrows, offers, NFT pages, pay channels, etc.) remain in the
 *  ledger. For pseudo-accounts (AMM, Vault, etc.), the linked protocol
 *  object must also be absent.
 *
 *  The `before` snapshot is used to locate linked objects even when an
 *  ID field is cleared during deletion; `after` is used only for
 *  post-deletion balance and owner-count assertions.
 *
 *  @note This checker is amendment-gated: violations are always logged at
 *      `fatal` level and trigger a debug-build `XRPL_ASSERT`, but
 *      `finalize` only returns `false` (blocking the transaction) when
 *      `featureInvariantsV1_1`, `featureSingleAssetVault`, or
 *      `featureLendingProtocol` is enabled.
 */
class AccountRootsDeletedClean
{
    // Pair is <before, after>. Before is used for most of the checks, so that
    // if, for example, an object ID field is cleared, but the object is not
    // deleted, it can still be found. After is used specifically for any checks
    // that are expected as part of the deletion, such as zeroing out the
    // balance.
    std::vector<std::pair<std::shared_ptr<SLE const>, std::shared_ptr<SLE const>>> accountsDeleted_;

public:
    /** Record each deleted account root's before/after snapshots.
     *
     *  @param isDelete `true` when the entry is being deleted.
     *  @param before Entry state before deletion; used to locate linked objects.
     *  @param after Entry state after deletion; used to check balance/owner count.
     */
    void
    visitEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after);

    /** Scan the ledger for objects orphaned by each deleted account.
     *
     *  @return `true` if all deleted accounts are clean; `false` with `fatal`
     *      log entries for each violation when the gating amendment is active.
     */
    bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&);
};

/** Invariant: every account root's XRP balance must be a native amount in
 *  `[0, INITIAL_XRP]` (inclusive) both before and after the transaction.
 *
 *  The `bad_` flag is sticky — once set by any visited entry it remains set
 *  regardless of subsequent entries.
 */
class XRPBalanceChecks
{
    bool bad_ = false;

public:
    /** Set `bad_` if any visited account root has an out-of-range balance.
     *
     *  @param before Entry state before modification; null for new entries.
     *  @param after Entry state after modification; null for deleted entries.
     */
    void
    visitEntry(bool, std::shared_ptr<SLE const> const& before, std::shared_ptr<SLE const> const& after);

    /** Report failure if any balance violation was detected.
     *
     *  @return `true` if all visited balances were valid; `false` with a
     *      `fatal` log entry if any balance was out of range.
     */
    [[nodiscard]] bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&) const;
};

/** Invariant: modified entries must not change `LedgerEntryType`, and newly
 *  created entries must be a type recognized by `ledger_entries.macro`.
 *
 *  Catches two distinct corruption scenarios: a modification that mutates
 *  the type field of an existing object (`typeMismatch_`), and creation of
 *  an object with an unregistered type tag (`invalidTypeAdded_`). Both
 *  flags are checked independently in `finalize` so each failure produces
 *  its own diagnostic.
 */
class LedgerEntryTypesMatch
{
    bool typeMismatch_ = false;
    bool invalidTypeAdded_ = false;

public:
    /** Detect type changes on modified entries and unrecognized types on new ones.
     *
     *  @param before Entry state before modification; null for new entries.
     *  @param after Entry state after modification; null for deleted entries.
     */
    void
    visitEntry(bool, std::shared_ptr<SLE const> const& before, std::shared_ptr<SLE const> const& after);

    /** Report any type mismatch or unknown type that was detected.
     *
     *  @return `true` if no type anomaly was seen; `false` with separate
     *      `fatal` log entries for each distinct violation kind.
     */
    [[nodiscard]] bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&) const;
};

/** Invariant: no trust line (`ltRIPPLE_STATE`) may reference XRP as its asset.
 *
 *  XRP is natively held in account roots; a trust line denominated in XRP
 *  has no valid semantics and indicates a bug. Both `sfLowLimit` and
 *  `sfHighLimit` are checked so the asset comparison does not rely solely
 *  on the `native()` predicate.
 */
class NoXRPTrustLines
{
    bool xrpTrustLine_ = false;

public:
    /** Set the violation flag if any trust line's asset is XRP.
     *
     *  Only inspects the `after` snapshot; pre-existing invalid state
     *  cannot be introduced by this transaction.
     */
    void
    visitEntry(bool, std::shared_ptr<SLE const> const&, std::shared_ptr<SLE const> const& after);

    /** @return `true` if no XRP trust lines were seen; `false` with a
     *      `fatal` log entry otherwise.
     */
    [[nodiscard]] bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&) const;
};

/** Invariant: `lsfLowDeepFreeze`/`lsfHighDeepFreeze` may only be set when
 *  the corresponding `lsfLowFreeze`/`lsfHighFreeze` flag is also set.
 *
 *  Deep freeze is a stricter form of freeze; it is undefined behaviour to
 *  deep-freeze a trust line that is not already frozen.
 */
class NoDeepFreezeTrustLinesWithoutFreeze
{
    bool deepFreezeWithoutFreeze_ = false;

public:
    /** Set the violation flag if any trust line has deep freeze without freeze.
     *
     *  Only inspects the `after` snapshot; checks both low and high sides
     *  of the trust line independently.
     */
    void
    visitEntry(bool, std::shared_ptr<SLE const> const&, std::shared_ptr<SLE const> const& after);

    /** @return `true` if no deep-freeze-without-freeze condition was seen;
     *      `false` with a `fatal` log entry otherwise.
     */
    [[nodiscard]] bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&) const;
};

/** Invariant: all offer entries (`ltOFFER`) must have non-negative amounts
 *  and must not exchange XRP for XRP.
 *
 *  Both the `before` and `after` snapshots of each offer are inspected, so
 *  a modification that corrupts an existing offer is caught alongside a
 *  newly created bad offer.
 */
class NoBadOffers
{
    bool bad_ = false;

public:
    /** Set the violation flag if any offer has negative amounts or is XRP↔XRP.
     *
     *  @param before Entry state before modification; null for new entries.
     *  @param after Entry state after modification; null for deleted entries.
     */
    void
    visitEntry(bool, std::shared_ptr<SLE const> const& before, std::shared_ptr<SLE const> const& after);

    /** @return `true` if no bad offers were seen; `false` with a `fatal`
     *      log entry otherwise.
     */
    [[nodiscard]] bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&) const;
};

/** Invariant: escrow and MPToken amounts must be strictly positive and
 *  within protocol bounds.
 *
 *  Despite its name this checker is not limited to escrows. It validates:
 *  - `ltESCROW` amounts: XRP in `(0, INITIAL_XRP)`, IOU > 0 with a valid
 *    currency, MPT in `(0, MAX_MP_TOKEN_AMOUNT]`.
 *  - `ltMPTOKEN_ISSUANCE` `sfOutstandingAmount` and optional `sfLockedAmount`:
 *    both in `[0, MAX_MP_TOKEN_AMOUNT]`, with `lockedAmount ≤ outstandingAmount`.
 *  - `ltMPTOKEN` `sfMPTAmount` and optional `sfLockedAmount`:
 *    both in `[0, MAX_MP_TOKEN_AMOUNT]`.
 */
class NoZeroEscrow
{
    bool bad_ = false;

public:
    /** Set the violation flag if any escrow or MPToken entry has an invalid amount.
     *
     *  @param before Entry state before modification; null for new entries.
     *  @param after Entry state after modification; null for deleted entries.
     */
    void
    visitEntry(bool, std::shared_ptr<SLE const> const& before, std::shared_ptr<SLE const> const& after);

    /** @return `true` if all amounts were in range; `false` with a `fatal`
     *      log entry otherwise.
     */
    [[nodiscard]] bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&) const;
};

/** Invariant: at most one account root may be created per transaction, it
 *  must originate from a transaction with the correct creation privilege,
 *  and it must start with the ledger-mandated sequence number and flags.
 *
 *  Normal accounts start with `sfSequence == view.seq()` (current ledger
 *  sequence). Pseudo-accounts (AMM, Vault, LoanBroker) start with
 *  `sfSequence == 0` and must have exactly `lsfDisableMaster |
 *  lsfDefaultRipple | lsfDepositAuth` set. Creating a pseudo-account
 *  requires the `CreatePseudoAcct` privilege; creating a normal account
 *  requires `CreateAcct`.
 */
class ValidNewAccountRoot
{
    std::uint32_t accountsCreated_ = 0;
    std::uint32_t accountSeq_ = 0;
    bool pseudoAccount_ = false;
    std::uint32_t flags_ = 0;

public:
    /** Record creation details for newly added account roots.
     *
     *  @param before Null for newly created entries (creation only).
     *  @param after The new account root's state.
     */
    void
    visitEntry(bool, std::shared_ptr<SLE const> const& before, std::shared_ptr<SLE const> const& after);

    /** Verify creation count, privilege, starting sequence, and flags.
     *
     *  @return `true` if the account was created validly; `false` with a
     *      `fatal` log entry for each distinct violation.
     */
    [[nodiscard]] bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&) const;
};

/** Invariant: a `ttCLAWBACK` transaction must not modify more than one
 *  trust line or MPToken entry, and must leave the holder's balance
 *  non-negative.
 *
 *  On success: at most one trust line and at most one MPToken modified;
 *  the holder's resulting balance ≥ 0. On failure: neither trust lines
 *  nor MPTokens may have been modified at all. For all other transaction
 *  types `finalize` is a no-op.
 */
class ValidClawback
{
    std::uint32_t trustlinesChanged_ = 0;
    std::uint32_t mptokensChanged_ = 0;

public:
    /** Count touched trust lines and MPToken entries.
     *
     *  @param before Entry state before modification; counts only when non-null.
     *  @param after Unused by this checker.
     */
    void
    visitEntry(bool, std::shared_ptr<SLE const> const& before, std::shared_ptr<SLE const> const& after);

    /** Validate clawback constraints if the transaction is `ttCLAWBACK`.
     *
     *  @return `true` for non-clawback transactions unconditionally; for
     *      clawback, `true` only if modification counts and holder balance
     *      pass all checks; `false` with a `fatal` log entry otherwise.
     */
    [[nodiscard]] bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&) const;
};

/** Invariant: pseudo-accounts must maintain their structural invariants
 *  after every modification.
 *
 *  Any account root with a pseudo-account discriminator field (e.g.,
 *  `sfAMMID`, `sfVaultID`) or with `sfSequence == 0` is treated as a
 *  pseudo-account and checked for:
 *  1. Exactly one pseudo-account discriminator field present.
 *  2. `sfSequence` unchanged from before.
 *  3. Flags `lsfDisableMaster | lsfDefaultRipple | lsfDepositAuth` all set.
 *  4. `sfRegularKey` absent.
 *
 *  All violations are collected into `errors_` and logged individually.
 *  Deletion events are ignored (covered by `AccountRootsDeletedClean`).
 *
 *  @note Enforcement is gated on `featureSingleAssetVault`: violations log
 *      and fire a debug `XRPL_ASSERT` unconditionally but only return
 *      `false` when the amendment is active.
 */
class ValidPseudoAccounts
{
    std::vector<std::string> errors_;

public:
    /** Accumulate errors for any pseudo-account that violates its invariants.
     *
     *  @param isDelete Deletion events are skipped entirely.
     *  @param before Entry state before modification; used for sequence comparison.
     *  @param after Current account root state to validate.
     */
    void
    visitEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after);

    /** Log all accumulated errors and fail if the gating amendment is active.
     *
     *  @return `true` if no errors were accumulated or the amendment is not
     *      yet active; `false` with `fatal` log entries for each violation
     *      when `featureSingleAssetVault` is enabled.
     */
    bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&);
};

/** Invariant: certain fields on existing ledger entries must never be
 *  altered once the entry is created.
 *
 *  Creation and deletion are ignored (those events have their own checkers).
 *  For `ltLOAN_BROKER` and `ltLOAN` entries, a broad set of origination
 *  fields (`sfInterestRate`, `sfBorrower`, `sfStartDate`, etc.) are
 *  immutable. For all other entry types, `sfLedgerEntryType` and
 *  `sfLedgerIndex` are universally immutable.
 *
 *  @note Enforcement is gated on `featureLendingProtocol`: violations log
 *      and fire a debug `XRPL_ASSERT` unconditionally but only return
 *      `false` when that amendment is active — even for the universally
 *      immutable fields, because that is when this checker was introduced.
 */
class NoModifiedUnmodifiableFields
{
    // Pair is <before, after>.
    std::set<std::pair<SLE::const_pointer, SLE::const_pointer>> changedEntries_;

public:
    /** Record every modification (non-delete) for inspection in `finalize`.
     *
     *  @param isDelete Deletion and creation events are skipped.
     *  @param before Entry state before modification.
     *  @param after Entry state after modification.
     */
    void
    visitEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after);

    /** Scan all recorded modifications for immutable-field changes.
     *
     *  @return `true` if no immutable field was altered; `false` with a
     *      `fatal` log entry when a violation is found and
     *      `featureLendingProtocol` is active.
     */
    bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&);
};

/** Complete set of invariant checkers run after every transaction.
 *
 *  `ApplyContext::checkInvariantsHelper` unpacks this tuple at compile time
 *  via `std::index_sequence`, calling `visitEntry` on each element for
 *  every modified ledger entry and then `finalize` on each element once.
 *  Results from `finalize` are collected into a `std::array` rather than
 *  short-circuited with `&&`, so every failing checker produces its own
 *  `fatal`-level log entry before the combined verdict is returned.
 *
 *  To add a new invariant: declare its class (here or in a sibling header),
 *  include that header above, and append the class to this list.
 *
 *  @see InvariantChecker_PROTOTYPE for the interface each element must satisfy.
 *  @see ApplyContext::checkInvariants for the dispatch entry point.
 */
using InvariantChecks = std::tuple<
    TransactionFeeCheck,
    AccountRootsNotDeleted,
    AccountRootsDeletedClean,
    LedgerEntryTypesMatch,
    XRPBalanceChecks,
    XRPNotCreated,
    NoXRPTrustLines,
    NoDeepFreezeTrustLinesWithoutFreeze,
    TransfersNotFrozen,
    NoBadOffers,
    NoZeroEscrow,
    ValidNewAccountRoot,
    ValidNFTokenPage,
    NFTokenCountTracking,
    ValidClawback,
    ValidMPTIssuance,
    ValidPermissionedDomain,
    ValidPermissionedDEX,
    ValidAMM,
    NoModifiedUnmodifiableFields,
    ValidPseudoAccounts,
    ValidLoanBroker,
    ValidLoan,
    ValidVault,
    ValidMPTPayment>;

/** Construct a default-initialized `InvariantChecks` tuple.
 *
 *  Called by `ApplyContext::checkInvariantsHelper` at the start of each
 *  invariant-checking pass. Each checker accumulates state via `visitEntry`
 *  across all modified ledger entries before `finalize` renders its verdict.
 *
 *  @return A value-initialized tuple containing one instance of every
 *      registered invariant checker, ready for a new pass.
 *  @see InvariantChecker_PROTOTYPE
 */
inline InvariantChecks
getInvariantChecks()
{
    return InvariantChecks{};
}

}  // namespace xrpl
