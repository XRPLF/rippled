#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>

#include <map>
#include <vector>

namespace xrpl {

/**
 * @brief Invariant checker that prevents token transfers across frozen trust lines.
 *
 * Registered in the `InvariantChecks` tuple and executed after every transaction,
 * including failed ones. Implements the two-phase `InvariantChecker_PROTOTYPE`
 * contract: `visitEntry()` collects trust line balance changes, and `finalize()`
 * evaluates freeze rules across the full set of collected changes.
 *
 * A single-pass approach is insufficient because a trust line's freeze state
 * alone does not determine whether a transfer is forbidden — the check must be
 * performed end-to-end, comparing both sides of the transfer across potentially
 * different freeze states. The two-phase design makes this possible.
 *
 * Enforcement is gated on `featureDeepFreeze`: before the amendment activates,
 * violations are logged at `fatal` severity and fire `XRPL_ASSERT` in debug
 * builds, but do not invalidate the transaction in release builds. This
 * provides early warning without a consensus break, and the single `enforce`
 * variable is the only change needed if a fix amendment is introduced.
 *
 * @note `AMMClawback` transactions hold the `OverrideFreeze` privilege and may
 *     move funds across individually frozen or deep-frozen trust lines, but not
 *     when the issuer has set a global freeze.
 */
class TransfersNotFrozen
{
    /**
     * @brief A single trust line's participation in a balance change.
     *
     * `balanceChangeSign` is +1 if the balance increased (receiving) or -1 if
     * it decreased (sending), from the current issuer's perspective.
     */
    struct BalanceChange
    {
        std::shared_ptr<SLE const> const line;
        int const balanceChangeSign;
    };

    /**
     * @brief All balance changes for a single issuer's token, split by direction.
     *
     * When both `senders` and `receivers` are non-empty the transfer is
     * holder-to-holder and freeze rules apply. If either is empty the tokens
     * are moving to or from the issuer directly, which is always permitted.
     */
    struct IssuerChanges
    {
        std::vector<BalanceChange> senders;
        std::vector<BalanceChange> receivers;
    };

    /** Balance changes keyed by `Issue` (currency + issuer account). */
    using ByIssuer = std::map<Issue, IssuerChanges>;
    ByIssuer balanceChanges_;

    /**
     * @brief Cache of `ltACCOUNT_ROOT` SLEs observed during `visitEntry()`.
     *
     * `findIssuer()` checks this cache before falling back to `view.read()`,
     * avoiding a redundant ledger lookup in the common case where the issuer
     * account was already touched by the transaction.
     */
    std::map<AccountID, std::shared_ptr<SLE const> const> possibleIssuers_;

public:
    /**
     * @brief Collect balance changes from a single modified ledger entry.
     *
     * Skips non-trust-line entries, but caches any `ltACCOUNT_ROOT` entries
     * in `possibleIssuers_` for later use by `findIssuer()`. For trust lines,
     * records the net balance change under both sides' issuer keys so that
     * `finalize()` sees issuer-relative directionality regardless of which
     * side of the trust line an account sits on.
     *
     * @param isDelete True if the entry is being deleted; causes the final
     *     balance to be treated as zero so that trust-line deletion cannot
     *     transfer frozen funds to a third party.
     * @param before SLE state before the transaction; null for newly-created
     *     trust lines (balance treated as zero in that case).
     * @param after SLE state after the transaction; never null even on delete.
     */
    void
    visitEntry(bool, std::shared_ptr<SLE const> const&, std::shared_ptr<SLE const> const&);

    /**
     * @brief Validate that no collected balance change violates freeze rules.
     *
     * Iterates over every issuer in `balanceChanges_` and delegates to
     * `validateIssuerChanges()`. Only holder-to-holder transfers (both
     * `senders` and `receivers` non-empty) are evaluated for freeze
     * violations; issuance and redemption are unconditionally allowed.
     *
     * Enforcement is controlled by `featureDeepFreeze`: when disabled,
     * violations are logged and asserted in debug builds but do not cause the
     * method to return `false`.
     *
     * @param tx The transaction being applied (used for privilege checks and
     *     log correlation).
     * @param ter The transaction result (unused; invariant runs regardless).
     * @param fee The fee charged (unused by this checker).
     * @param view The post-transaction ledger view, used to look up issuers
     *     not cached in `possibleIssuers_`.
     * @param j Journal for diagnostic logging.
     * @return `true` if no frozen-fund movement is detected (or if
     *     `featureDeepFreeze` is disabled and a violation is found).
     *     `false` if a violation is found and the amendment is active.
     */
    bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&);

private:
    /**
     * @brief Return true if the entry should be processed for freeze checks.
     *
     * Caches `ltACCOUNT_ROOT` entries in `possibleIssuers_` as a side effect.
     * Returns `true` only for `ltRIPPLE_STATE` (trust line) entries where the
     * type has not changed between `before` and `after`.
     *
     * @param before Pre-transaction SLE; null for newly-created entries.
     * @param after Post-transaction SLE; must not be null.
     * @return `true` if the entry is a trust line eligible for balance-change
     *     recording.
     */
    bool
    isValidEntry(std::shared_ptr<SLE const> const& before, std::shared_ptr<SLE const> const& after);

    /**
     * @brief Compute the net balance change for a trust line.
     *
     * When `before` is null (trust line created mid-transaction, e.g., by a
     * payment crossing offers), the pre-existing balance is treated as zero.
     * When `isDelete` is true, the final balance is treated as zero so that
     * deletion cannot bypass frozen-transfer restrictions.
     *
     * @param before Pre-transaction SLE; null if the trust line was just created.
     * @param after Post-transaction SLE.
     * @param isDelete True when the entry is being deleted.
     * @return Signed `STAmount` representing `balanceAfter - balanceBefore`.
     */
    static STAmount
    calculateBalanceChange(
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after,
        bool isDelete);

    /**
     * @brief Insert a single `BalanceChange` into `balanceChanges_` under the given issue.
     *
     * Routes the change to `IssuerChanges::senders` when `balanceChangeSign < 0`,
     * or `IssuerChanges::receivers` when positive.
     *
     * @param issue The currency+issuer key for the change.
     * @param change The trust line and direction of the change.
     */
    void
    recordBalance(Issue const& issue, BalanceChange change);

    /**
     * @brief Record a trust line balance change from both sides' issuer perspectives.
     *
     * Because XRPL trust line balances are stored from the low account's
     * perspective, the same physical balance change must be inserted twice —
     * once for the high-limit account's issuer (using the raw sign) and once
     * for the low-limit account's issuer (with the sign inverted) — so that
     * `validateIssuerChanges()` sees consistent directionality from each
     * issuer's point of view.
     *
     * @param after Post-transaction trust line SLE.
     * @param balanceChange Net balance change; must be non-zero.
     */
    void
    recordBalanceChanges(std::shared_ptr<SLE const> const& after, STAmount const& balanceChange);

    /**
     * @brief Look up an issuer's `AccountRoot` SLE, using the local cache first.
     *
     * Checks `possibleIssuers_` (populated during `visitEntry()`) before
     * falling back to `view.read()`, so that issuers already modified by the
     * transaction do not require an additional ledger lookup.
     *
     * @param issuerID The account ID to look up.
     * @param view The post-transaction read-only ledger view.
     * @return The issuer's SLE, or nullptr if not found.
     */
    std::shared_ptr<SLE const>
    findIssuer(AccountID const& issuerID, ReadView const& view);

    /**
     * @brief Validate all balance changes for one issuer's token.
     *
     * Unconditionally allows issuance (no senders) and redemption (no
     * receivers). For holder-to-holder transfers, checks every sender and
     * receiver trust line against the issuer's global freeze flag and the
     * per-line freeze/deep-freeze flags via `validateFrozenState()`.
     *
     * @param issuer The issuer's `AccountRoot` SLE; must not be null.
     * @param changes All senders and receivers for this issuer's token.
     * @param tx The transaction being applied.
     * @param j Journal for diagnostic logging.
     * @param enforce When `false`, violations are logged but do not cause the
     *     method to return `false` (pre-`featureDeepFreeze` mode).
     * @return `true` if all changes are permitted; `false` on a freeze violation
     *     when `enforce` is `true`.
     */
    static bool
    validateIssuerChanges(
        std::shared_ptr<SLE const> const& issuer,
        IssuerChanges const& changes,
        STTx const& tx,
        beast::Journal const& j,
        bool enforce);

    /**
     * @brief Check whether a single trust line balance change violates freeze rules.
     *
     * Evaluates three layered freeze conditions in order:
     *  1. **Global freeze** (`lsfGlobalFreeze` on the issuer): freezes all trust
     *     lines with that issuer; no override is possible.
     *  2. **Deep freeze** (`lsfLowDeepFreeze`/`lsfHighDeepFreeze`): blocks all
     *     transfers regardless of direction; overrideable by `AMMClawback`.
     *  3. **Standard freeze** (`lsfLowFreeze`/`lsfHighFreeze`): only blocks
     *     outgoing transfers (`balanceChangeSign < 0`); overrideable by `AMMClawback`.
     *
     * @param change The trust line and direction of the balance change.
     * @param high `true` if the issuer is the high-limit account on the trust
     *     line (determines which freeze flag bits to examine).
     * @param tx The transaction being applied (for privilege and log checks).
     * @param j Journal for diagnostic logging.
     * @param enforce When `false`, violations log and assert but return `true`
     *     (pre-`featureDeepFreeze` behavior).
     * @param globalFreeze `true` if the issuer's `lsfGlobalFreeze` flag is set.
     * @return `true` if the transfer is permitted; `false` if it violates a
     *     freeze rule and `enforce` is `true`.
     */
    static bool
    validateFrozenState(
        BalanceChange const& change,
        bool high,
        STTx const& tx,
        beast::Journal const& j,
        bool enforce,
        bool globalFreeze);
};

}  // namespace xrpl
