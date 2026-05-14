/** @file
 *  Implements `TransfersNotFrozen`, the post-transaction invariant checker
 *  that prevents token balances from moving across frozen trust lines.
 *
 *  The checker operates in two phases mandated by the `InvariantChecker_PROTOTYPE`
 *  contract. During `visitEntry()` every modified `ltRIPPLE_STATE` and
 *  `ltACCOUNT_ROOT` entry is processed to accumulate balance-change records
 *  keyed by issuer (`balanceChanges_`) and a lightweight issuer cache
 *  (`possibleIssuers_`). During `finalize()` those records are validated against
 *  the three freeze tiers — global freeze, deep freeze, and directional freeze —
 *  and the `AMMClawback` privilege exemption is applied where applicable.
 *
 *  Enforcement is gated on the `featureDeepFreeze` amendment via the `enforce`
 *  flag. Before the amendment activates, violations are logged at `fatal`
 *  severity and fire `XRPL_ASSERT` in debug builds (providing early warning to
 *  operators and developers) but do not invalidate the transaction in release
 *  builds. When the amendment — or any future fix amendment — is added, only
 *  the single `enforce =` line in `finalize()` needs to change.
 *
 *  @note The `XRPL_ASSERT(enforce, ...)` calls throughout this file use
 *      the counterintuitive pattern where the assert fires when `enforce`
 *      is *false* and a violation is detected. This is intentional: in debug
 *      builds it crashes the process to catch developer mistakes in tests that
 *      exercise the invariant without activating the amendment. In release
 *      builds the assert is a no-op and the `!enforce` return value lets the
 *      transaction through.
 */
#include <xrpl/tx/invariants/FreezeInvariant.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/invariants/InvariantCheckPrivilege.h>

#include <memory>
#include <utility>

namespace xrpl {

/** Accumulate one modified ledger entry into the freeze-check state.
 *
 *  A trust line's freeze flags alone cannot determine whether a transfer is
 *  forbidden — the check must span all affected trust lines because both
 *  sides of a transfer can carry different freeze states and directionality
 *  matters. Balance changes are therefore accumulated here and all freeze
 *  policy decisions are deferred to `finalize()` / `validateIssuerChanges()`.
 *
 *  As a side effect, `ltACCOUNT_ROOT` entries are cached in `possibleIssuers_`
 *  so that `findIssuer()` can avoid an extra ledger lookup for issuers that
 *  were already touched by the transaction. Non-trust-line, non-account-root
 *  entries are silently ignored.
 */
void
TransfersNotFrozen::visitEntry(
    bool isDelete,
    std::shared_ptr<SLE const> const& before,
    std::shared_ptr<SLE const> const& after)
{
    if (!isValidEntry(before, after))
    {
        return;
    }

    auto const balanceChange = calculateBalanceChange(before, after, isDelete);
    if (balanceChange.signum() == 0)
    {
        return;
    }

    recordBalanceChanges(after, balanceChange);
}

/** Validate all collected trust-line balance changes against freeze rules.
 *
 *  Iterates `balanceChanges_` and calls `validateIssuerChanges()` for each
 *  issuer. The `enforce` flag — controlled by `featureDeepFreeze` — decides
 *  whether a detected violation causes this method to return `false` (hard
 *  enforcement) or merely logs and asserts (monitoring-only mode). To add a
 *  fix amendment in the future, append `|| view.rules().enabled(fixFreezeExploit)`
 *  to the single line that sets `enforce`; no other code needs to change.
 *
 *  It is considered impossible for an issuer account that owns a trust line
 *  to be absent from the ledger, but the missing-issuer path is guarded
 *  defensively to prevent a crash in release builds.
 *
 *  @return `true` if no frozen-fund movement is detected, or if enforcement
 *      is disabled (`featureDeepFreeze` not yet active). `false` if a freeze
 *      violation is found and the amendment is active.
 */
bool
TransfersNotFrozen::finalize(
    STTx const& tx,
    TER const ter,
    XRPAmount const fee,
    ReadView const& view,
    beast::Journal const& j)
{
    [[maybe_unused]] bool const enforce = view.rules().enabled(featureDeepFreeze);

    for (auto const& [issue, changes] : balanceChanges_)
    {
        auto const issuerSle = findIssuer(issue.account, view);
        // It should be impossible for the issuer to not be found, but check
        // just in case so xrpld doesn't crash in release.
        if (!issuerSle)
        {
            XRPL_ASSERT(
                enforce,
                "xrpl::TransfersNotFrozen::finalize : enforce "
                "invariant.");
            if (enforce)
            {
                return false;
            }
            continue;
        }

        if (!validateIssuerChanges(issuerSle, changes, tx, j, enforce))
        {
            return false;
        }
    }

    return true;
}

/** Return true if the entry is a trust line eligible for balance-change recording.
 *
 *  `ltACCOUNT_ROOT` entries are silently cached in `possibleIssuers_` and
 *  excluded from further processing (returns `false`). All other entry types
 *  return `false` and are ignored.
 *
 *  The explicit type guard against `ltRIPPLE_STATE` is necessary even though
 *  the `LedgerEntryTypesMatch` invariant also checks types, because all
 *  invariants run independently regardless of previous failures and a type
 *  mismatch here could cause undefined behaviour in subsequent processing.
 *
 *  @param before Pre-transaction SLE; null for newly-created entries.
 *  @param after Post-transaction SLE; must not be null.
 *  @return `true` only for `ltRIPPLE_STATE` entries whose type is unchanged.
 */
bool
TransfersNotFrozen::isValidEntry(
    std::shared_ptr<SLE const> const& before,
    std::shared_ptr<SLE const> const& after)
{
    // `after` can never be null, even if the trust line is deleted.
    XRPL_ASSERT(after, "xrpl::TransfersNotFrozen::isValidEntry : valid after.");
    if (!after)
    {
        return false;
    }

    if (after->getType() == ltACCOUNT_ROOT)
    {
        possibleIssuers_.emplace(after->at(sfAccount), after);
        return false;
    }

    /* While LedgerEntryTypesMatch invariant also checks types, all invariants
     * are processed regardless of previous failures.
     *
     * This type check is still necessary here because it prevents potential
     * issues in subsequent processing.
     */
    return after->getType() == ltRIPPLE_STATE && (!before || before->getType() == ltRIPPLE_STATE);
}

/** Compute the net balance change for a trust line, handling creation and deletion.
 *
 *  Two edge cases require special treatment to close freeze-bypass loopholes:
 *
 *  - **Created mid-transaction** (`before` is null): a `Payment` or
 *    `OfferCreate` that crosses offers can create a trust line on the fly.
 *    Such a line is not created frozen, but the sender's line may be.
 *    Treating the pre-existing balance as zero ensures the full post-transaction
 *    balance counts as the change, so the sender's freeze state is still checked.
 *
 *  - **Deleted mid-transaction** (`isDelete` is true): the final balance is
 *    treated as zero so that deleting a trust line cannot be used to transfer
 *    frozen funds to a third party while appearing to "clear" a balance.
 *
 *  @param before Pre-transaction SLE; null if the trust line was just created.
 *  @param after Post-transaction SLE.
 *  @param isDelete True when the entry is being deleted.
 *  @return Signed `STAmount` representing `balanceAfter - balanceBefore`.
 */
STAmount
TransfersNotFrozen::calculateBalanceChange(
    std::shared_ptr<SLE const> const& before,
    std::shared_ptr<SLE const> const& after,
    bool isDelete)
{
    auto const getBalance = [](auto const& line, auto const& other, bool zero) {
        STAmount const amt = line ? line->at(sfBalance) : other->at(sfBalance).zeroed();
        return zero ? amt.zeroed() : amt;
    };

    /* Trust lines can be created dynamically by other transactions such as
     * Payment and OfferCreate that cross offers. Such trust line won't be
     * created frozen, but the sender might be, so the starting balance must be
     * treated as zero.
     */
    auto const balanceBefore = getBalance(before, after, false);

    /* Same as above, trust lines can be dynamically deleted, and for frozen
     * trust lines, payments not involving the issuer must be blocked. This is
     * achieved by treating the final balance as zero when isDelete=true to
     * ensure frozen line restrictions are enforced even during deletion.
     */
    auto const balanceAfter = getBalance(after, before, isDelete);

    return balanceAfter - balanceBefore;
}

/** Insert a `BalanceChange` into `balanceChanges_` under the given issue.
 *
 *  Routes the change to `IssuerChanges::senders` when `balanceChangeSign < 0`
 *  (balance decreased from this issuer's perspective) or to
 *  `IssuerChanges::receivers` when positive. A zero sign is invalid and
 *  triggers an assertion — callers must filter out zero-change entries first.
 *
 *  @param issue Currency and issuer account identifying the `balanceChanges_` bucket.
 *  @param change Trust line SLE reference and directional sign; must be non-zero.
 */
void
TransfersNotFrozen::recordBalance(Issue const& issue, BalanceChange change)
{
    XRPL_ASSERT(
        change.balanceChangeSign,
        "xrpl::TransfersNotFrozen::recordBalance : valid trustline "
        "balance sign.");
    auto& changes = balanceChanges_[issue];
    if (change.balanceChangeSign < 0)
    {
        changes.senders.emplace_back(std::move(change));
    }
    else
    {
        changes.receivers.emplace_back(std::move(change));
    }
}

/** Record a trust line balance change from both sides' issuer perspectives.
 *
 *  XRPL stores trust line balances from the low account's perspective
 *  (the account with the numerically lower `AccountID`). The same physical
 *  balance movement therefore looks like opposite-signed changes when viewed
 *  from the high account's side. To give `validateIssuerChanges()` consistent
 *  issuer-relative directionality, the change is inserted twice: once for the
 *  high-limit account's issuer using the raw sign, and once for the low-limit
 *  account's issuer with the sign inverted.
 *
 *  @param after Post-transaction trust line SLE.
 *  @param balanceChange Net signed balance change; must be non-zero.
 */
void
TransfersNotFrozen::recordBalanceChanges(
    std::shared_ptr<SLE const> const& after,
    STAmount const& balanceChange)
{
    auto const balanceChangeSign = balanceChange.signum();
    auto const currency = after->at(sfBalance).get<Issue>().currency;

    // Change from low account's perspective, which is trust line default
    recordBalance(
        {currency, after->at(sfHighLimit).getIssuer()},
        {.line = after, .balanceChangeSign = balanceChangeSign});

    // Change from high account's perspective, which reverses the sign.
    recordBalance(
        {currency, after->at(sfLowLimit).getIssuer()},
        {.line = after, .balanceChangeSign = -balanceChangeSign});
}

/** Look up an issuer's `AccountRoot` SLE, using the transaction-local cache first.
 *
 *  Checks `possibleIssuers_` (populated during `visitEntry()`) before
 *  falling back to `view.read()`. This avoids a redundant ledger lookup in
 *  the common case where the issuer account was already modified by the
 *  transaction being validated.
 *
 *  @param issuerID Account to look up.
 *  @param view Post-transaction read-only ledger view used as the fallback.
 *  @return The issuer's `AccountRoot` SLE, or nullptr if not found.
 */
std::shared_ptr<SLE const>
TransfersNotFrozen::findIssuer(AccountID const& issuerID, ReadView const& view)
{
    if (auto it = possibleIssuers_.find(issuerID); it != possibleIssuers_.end())
    {
        return it->second;
    }

    return view.read(keylet::account(issuerID));
}

/** Validate all balance changes for one issuer's token against freeze rules.
 *
 *  Issuance (no senders) and redemption (no receivers) are unconditionally
 *  allowed regardless of freeze flags — freeze restrictions apply only to
 *  holder-to-holder transfers, where both `changes.senders` and
 *  `changes.receivers` are non-empty. If either collection is empty,
 *  tokens are flowing directly to or from the issuer. The holder may still
 *  carry contradicting freeze flags for peer-to-peer transfers, but those
 *  are validated when the holder is processed as an issuer in its own
 *  `balanceChanges_` entry.
 *
 *  For holder-to-holder transfers, every sender and receiver trust line is
 *  checked by `validateFrozenState()` against the three freeze tiers.
 *
 *  @param issuer The issuer's `AccountRoot` SLE; must not be null.
 *  @param changes All senders and receivers for this issuer's token.
 *  @param tx The transaction being applied.
 *  @param j Journal for diagnostic logging.
 *  @param enforce When `false`, violations log and assert but do not cause
 *      this method to return `false` (pre-`featureDeepFreeze` mode).
 *  @return `true` if all changes are permitted; `false` on a freeze violation
 *      when `enforce` is `true`.
 */
bool
TransfersNotFrozen::validateIssuerChanges(
    std::shared_ptr<SLE const> const& issuer,
    IssuerChanges const& changes,
    STTx const& tx,
    beast::Journal const& j,
    bool enforce)
{
    if (!issuer)
    {
        return false;
    }

    bool const globalFreeze = issuer->isFlag(lsfGlobalFreeze);
    if (changes.receivers.empty() || changes.senders.empty())
    {
        return true;
    }

    for (auto const& actors : {changes.senders, changes.receivers})
    {
        for (auto const& change : actors)
        {
            bool const high = change.line->at(sfLowLimit).getIssuer() == issuer->at(sfAccount);

            if (!validateFrozenState(change, high, tx, j, enforce, globalFreeze))
            {
                return false;
            }
        }
    }
    return true;
}

/** Check whether a single trust line balance change violates freeze rules.
 *
 *  Evaluates three layered freeze conditions, any of which is sufficient to
 *  block the transfer:
 *
 *  1. **Global freeze** (`lsfGlobalFreeze` on the issuer): all trust lines with
 *     that issuer are frozen; no override is possible.
 *  2. **Deep freeze** (`lsfLowDeepFreeze`/`lsfHighDeepFreeze`): blocks both
 *     inbound and outbound movement regardless of directionality.
 *  3. **Standard freeze** (`lsfLowFreeze`/`lsfHighFreeze`): direction-sensitive —
 *     only blocks outgoing transfers (`balanceChangeSign < 0`).
 *
 *  The `high` parameter indicates whether the issuer under scrutiny is the
 *  high-limit account on this trust line (numerically larger `AccountID`),
 *  which determines which freeze-flag bits to inspect on the SLE.
 *
 *  **`AMMClawback` exception**: when `hasPrivilege(tx, OverrideFreeze)` is true,
 *  the invariant permits movement across individually frozen or deep-frozen AMM
 *  pool trust lines (`lsfAMMNode`). A global freeze is never overrideable, and
 *  regular (non-AMM) trust lines cannot be clawed back even with the privilege.
 *
 *  When `enforce` is `false` (amendment not yet active), a detected violation
 *  logs at `fatal` severity and fires `XRPL_ASSERT` in debug builds, but
 *  returns `true` to allow the transaction through. See the `@file` docstring
 *  for the rationale behind this pattern.
 *
 *  @param change Trust line SLE and direction of the balance change.
 *  @param high `true` if the issuer is the high-limit account on this trust line.
 *  @param tx The transaction being applied.
 *  @param j Journal for diagnostic logging.
 *  @param enforce When `false`, violations are logged but do not fail the check.
 *  @param globalFreeze `true` if the issuer's `lsfGlobalFreeze` flag is set.
 *  @return `true` if the transfer is permitted; `false` if it violates a freeze
 *      rule and `enforce` is `true`.
 */
bool
TransfersNotFrozen::validateFrozenState(
    BalanceChange const& change,
    bool high,
    STTx const& tx,
    beast::Journal const& j,
    bool enforce,
    bool globalFreeze)
{
    bool const freeze =
        change.balanceChangeSign < 0 && change.line->isFlag(high ? lsfLowFreeze : lsfHighFreeze);
    bool const deepFreeze = change.line->isFlag(high ? lsfLowDeepFreeze : lsfHighDeepFreeze);
    bool const frozen = globalFreeze || deepFreeze || freeze;

    bool const isAMMLine = change.line->isFlag(lsfAMMNode);

    if (!frozen)
    {
        return true;
    }

    // AMMClawbacks are allowed to override some freeze rules
    if ((!isAMMLine || globalFreeze) && hasPrivilege(tx, OverrideFreeze))
    {
        JLOG(j.debug()) << "Invariant check allowing funds to be moved "
                        << (change.balanceChangeSign > 0 ? "to" : "from")
                        << " a frozen trustline for AMMClawback " << tx.getTransactionID();
        return true;
    }

    JLOG(j.fatal()) << "Invariant failed: Attempting to move frozen funds for "
                    << tx.getTransactionID();
    XRPL_ASSERT(
        enforce,
        "xrpl::TransfersNotFrozen::validateFrozenState : enforce "
        "invariant.");

    return !enforce;
}

}  // namespace xrpl
