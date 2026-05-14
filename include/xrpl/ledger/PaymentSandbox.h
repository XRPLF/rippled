#pragma once

#include <xrpl/ledger/RawView.h>
#include <xrpl/ledger/Sandbox.h>
#include <xrpl/ledger/detail/ApplyViewBase.h>
#include <xrpl/protocol/AccountID.h>

#include <map>
#include <utility>

namespace xrpl {

namespace detail {

// VFALCO TODO Inline this implementation
//        into the PaymentSandbox class itself
/** Bookkeeping ledger for credits deferred during payment execution.
 *
 *  Tracks every credit applied through a `PaymentSandbox` so that
 *  balance queries can subtract those credits before reporting available
 *  funds. This prevents circular-path liquidity: a credit arriving at an
 *  intermediate account mid-payment cannot be re-spent by an earlier step
 *  in the same path.
 *
 *  Two separate tables are maintained: `creditsIOU_` for IOU trust-line
 *  transfers (keyed by canonical `(lowAccount, highAccount, currency)`) and
 *  `creditsMPT_` for MPT issuances (keyed by `MPTID`). Owner-count
 *  maximums are stored in `ownerCounts_`.
 *
 *  @note This class is an implementation detail of `PaymentSandbox` and is
 *      not intended for direct use by other components.
 */
class DeferredCredits
{
private:
    using KeyIOU = std::tuple<AccountID, AccountID, Currency>;

    /** Per-trust-line record of accumulated debits and the pre-credit balance.
     *
     *  Debits are split by canonical endpoint: `lowAcctDebits` accumulates
     *  amounts sent by the account whose `AccountID` is lower; `highAcctDebits`
     *  accumulates amounts sent by the other endpoint. `lowAcctOrigBalance`
     *  holds the low-account's balance at the moment the first credit was
     *  recorded; it is never overwritten by subsequent credits.
     */
    struct ValueIOU
    {
        explicit ValueIOU() = default;
        STAmount lowAcctDebits;
        STAmount highAcctDebits;
        STAmount lowAcctOrigBalance;
    };

    /** Per-holder MPT debit record.
     *
     *  `debit` accumulates the total MPT amount sent by this holder during
     *  the payment. `origBalance` is the holder's balance at the time the
     *  first debit was recorded; it is never overwritten by subsequent debits.
     */
    struct HolderValueMPT
    {
        HolderValueMPT() = default;
        std::uint64_t debit = 0;
        std::uint64_t origBalance = 0;
    };

    /** Per-issuance MPT record aggregating credits and self-debits.
     *
     *  `holders` tracks per-holder debit entries. `credit` accumulates the
     *  total amount issued (i.e. credited to holders) during the payment.
     *  `origBalance` holds the issuer's `OutstandingAmount` at the time the
     *  first entry was recorded; it is never overwritten.
     *
     *  `selfDebit` handles the case where the MPT issuer owns a sell offer.
     *  Because the payment engine runs in reverse, crediting a holder first
     *  can transiently push `OutstandingAmount` above `MaximumAmount`. When
     *  the issuer's own sell offer is consumed in a later (reversed) step,
     *  the available issuance capacity must be reduced by the offer amount.
     *  `selfDebit` accumulates those offer amounts so that
     *  `balanceHookSelfIssueMPT` can correctly cap available issuance.
     */
    struct IssuerValueMPT
    {
        IssuerValueMPT() = default;
        std::map<AccountID, HolderValueMPT> holders;
        std::uint64_t credit = 0;
        std::int64_t origBalance = 0;
        std::uint64_t selfDebit = 0;
    };
    using AdjustmentMPT = IssuerValueMPT;

public:
    /** Query result for a single IOU trust-line adjustment.
     *
     *  Oriented from the perspective of the `main` account passed to
     *  `adjustmentsIOU()`: `debits` is what `main` has sent, `credits` is
     *  what `main` has received, and `origBalance` is `main`'s balance
     *  before the first credit in this sandbox was recorded.
     */
    struct AdjustmentIOU
    {
        AdjustmentIOU(STAmount d, STAmount c, STAmount b)
            : debits(std::move(d)), credits(std::move(c)), origBalance(std::move(b))
        {
        }
        STAmount debits;
        STAmount credits;
        STAmount origBalance;
    };

    /** Return the accumulated debit/credit adjustments for an IOU trust line.
     *
     *  The result is oriented from `main`'s perspective: `debits` contains
     *  what `main` has sent to `other`, `credits` contains what `other` has
     *  sent to `main`, and `origBalance` is `main`'s balance at the time the
     *  first credit for this pair was recorded.
     *
     *  @param main      The account whose perspective determines orientation.
     *  @param other     The counterparty account.
     *  @param currency  The currency of the trust line.
     *  @return Adjustment record, or `std::nullopt` if no credits have been
     *      recorded for this pair in this sandbox.
     */
    [[nodiscard]] std::optional<AdjustmentIOU>
    adjustmentsIOU(AccountID const& main, AccountID const& other, Currency const& currency) const;

    /** Return the accumulated MPT adjustments for a given issuance.
     *
     *  @param mptID  The unique identifier of the MPT issuance.
     *  @return Adjustment record, or `std::nullopt` if no credits have been
     *      recorded for this issuance in this sandbox.
     */
    [[nodiscard]] std::optional<AdjustmentMPT>
    adjustmentsMPT(MPTID const& mptID) const;

    /** Record an IOU credit from `sender` to `receiver`.
     *
     *  On the first call for a given `(sender, receiver, currency)` triple the
     *  pre-credit sender balance is saved as the original balance. Subsequent
     *  calls for the same triple accumulate debits without overwriting the
     *  original balance.
     *
     *  @param sender               Account sending the credit.
     *  @param receiver             Account receiving the credit.
     *  @param amount               Non-negative IOU amount being transferred.
     *  @param preCreditSenderBalance  Sender's balance immediately before
     *      this credit is applied; only stored on the first call.
     */
    void
    creditIOU(
        AccountID const& sender,
        AccountID const& receiver,
        STAmount const& amount,
        STAmount const& preCreditSenderBalance);

    /** Record an MPT credit from `sender` to `receiver`.
     *
     *  Distinguishes between issuer-to-holder transfers (which increment the
     *  aggregate `credit` counter) and holder-to-issuer redemptions (which
     *  increment the per-holder `debit` counter). The original balances are
     *  stored only on the first call for each holder/issuance combination.
     *
     *  @param sender                   Account sending the MPT.
     *  @param receiver                 Account receiving the MPT.
     *  @param amount                   Non-negative MPT amount being transferred.
     *  @param preCreditBalanceHolder   Holder's MPT balance before this credit.
     *  @param preCreditBalanceIssuer   Issuer's `OutstandingAmount` before this
     *      credit; only stored on the first call for this issuance.
     */
    void
    creditMPT(
        AccountID const& sender,
        AccountID const& receiver,
        STAmount const& amount,
        std::uint64_t preCreditBalanceHolder,
        std::int64_t preCreditBalanceIssuer);

    /** Record an MPT self-debit incurred by the issuer via a sell offer.
     *
     *  When the issuer owns a sell offer and it is consumed, the payment
     *  engine (running in reverse) may have already credited a holder,
     *  pushing `OutstandingAmount` transiently above `MaximumAmount`. This
     *  call registers the offer amount as a self-debit so that
     *  `balanceHookSelfIssueMPT` can cap available issuance correctly.
     *
     *  @param issue        The MPT issuance involved.
     *  @param amount       Amount of the issuer's sell offer that was consumed.
     *  @param origBalance  Issuer's `OutstandingAmount` before this entry; only
     *      stored on the first call for this issuance.
     */
    void
    issuerSelfDebitMPT(MPTIssue const& issue, std::uint64_t amount, std::int64_t origBalance);

    /** Record an owner-count transition for `account`.
     *
     *  Stores the maximum of `cur` and `next`, and takes the maximum with any
     *  previously recorded value. Because payments only ever decrease owner
     *  counts, the highest observed count is the conservative bound that
     *  prevents a transient low count from bypassing reserve checks mid-payment.
     *
     *  @param id    Account whose owner count is changing.
     *  @param cur   Current owner count before the transition.
     *  @param next  Owner count after the transition.
     */
    void
    ownerCount(AccountID const& id, std::uint32_t cur, std::uint32_t next);

    /** Return the maximum owner count observed for `account` in this sandbox.
     *
     *  Since payments only decrease owner counts, the maximum is the correct
     *  conservative bound for reserve checks.
     *
     *  @param id  Account to query.
     *  @return The peak owner count, or `std::nullopt` if no transition has
     *      been recorded for this account.
     */
    [[nodiscard]] std::optional<std::uint32_t>
    ownerCount(AccountID const& id) const;

    /** Merge this sandbox's deferred credits into a parent sandbox.
     *
     *  Debit accumulators and self-debit fields are summed; original balances
     *  are never overwritten (the parent's earlier record takes precedence).
     *  Owner-count maximums are taken across both sandboxes.
     *
     *  @param to  The parent `DeferredCredits` table to merge into.
     */
    void
    apply(DeferredCredits& to);

private:
    /** Produce a canonical `KeyIOU` by ordering the two accounts. */
    static KeyIOU
    makeKeyIOU(AccountID const& a1, AccountID const& a2, Currency const& currency);

    std::map<KeyIOU, ValueIOU> creditsIOU_;
    std::map<MPTID, IssuerValueMPT> creditsMPT_;
    std::map<AccountID, std::uint32_t> ownerCounts_;
};

}  // namespace detail

//------------------------------------------------------------------------------

/** Speculative ledger view that hides in-flight credits from balance queries.
 *
 *  The XRPL payment engine processes multi-hop paths where value flows through
 *  chains of trust lines, order books, and AMM pools. Without a guard, a
 *  credit arriving at an intermediate account mid-path could immediately
 *  appear as spendable liquidity for a later step in the same path — allowing
 *  phantom value to be created. `PaymentSandbox` prevents this by intercepting
 *  every credit via the hook protocol defined in `ApplyView` and recording it
 *  in a `DeferredCredits` table. Balance queries then subtract those deferred
 *  credits so freshly-received funds are invisible to outgoing transfer checks
 *  until the entire transaction commits.
 *
 *  `PaymentSandbox` can be stacked: constructing one on top of another via the
 *  pointer constructors creates a child sandbox whose deferred credits chain to
 *  the parent. The pathfinding engine uses this to evaluate each candidate
 *  strand in a disposable child, committing to the parent only on success.
 *
 *  @note When constructing on top of an existing `PaymentSandbox`, you **must**
 *      use the explicit pointer constructors. Using the plain `ApplyView*`
 *      constructor would bypass deferred-credit propagation and break invariants.
 *
 *  @note Presented as `ApplyView` to clients.
 */
class PaymentSandbox final : public detail::ApplyViewBase
{
public:
    PaymentSandbox() = delete;
    PaymentSandbox(PaymentSandbox const&) = delete;
    PaymentSandbox&
    operator=(PaymentSandbox&&) = delete;
    PaymentSandbox&
    operator=(PaymentSandbox const&) = delete;

    PaymentSandbox(PaymentSandbox&&) = default;

    /** Construct a root payment sandbox over a read-only base view.
     *
     *  @param base   The underlying ledger state to layer mutations on top of.
     *  @param flags  Transaction-processing flags forwarded to `ApplyViewBase`.
     */
    PaymentSandbox(ReadView const* base, ApplyFlags flags) : ApplyViewBase(base, flags)
    {
    }

    /** Construct a payment sandbox over an existing `ApplyView`.
     *
     *  Inherits the flags of the base view. Use the explicit pointer
     *  constructors instead if `base` is itself a `PaymentSandbox`.
     *
     *  @param base  The mutable view to build on top of.
     */
    PaymentSandbox(ApplyView const* base) : ApplyViewBase(base, base->flags())
    {
    }

    /** Construct a child payment sandbox on top of an existing `PaymentSandbox`.
     *
     *  The child's deferred-credit table chains to the parent so that balance
     *  adjustments aggregate correctly across the sandbox stack. Changes are
     *  not visible in the parent until `apply(PaymentSandbox&)` is called.
     *
     *  @param parent  Non-null pointer to the parent sandbox. A pointer is
     *      used rather than a reference to prevent confusion with copy
     *      construction.
     *
     *  @note This overload set **must** be used whenever building on top of
     *      a `PaymentSandbox` or derived class. The plain `ApplyView*`
     *      constructor does not propagate deferred credits.
     */
    /** @{ */
    explicit PaymentSandbox(PaymentSandbox const* base)
        : ApplyViewBase(base, base->flags()), ps_(base)
    {
    }

    explicit PaymentSandbox(PaymentSandbox* base) : ApplyViewBase(base, base->flags()), ps_(base)
    {
    }
    /** @} */

    /** Return the IOU balance adjusted for deferred credits.
     *
     *  Walks the sandbox chain (this → parent → … ) and accumulates total
     *  debits from all ancestor tables. Returns
     *  `min(amount, origBalance - totalDebits, minObservedBalance)` to
     *  handle edge cases where rounding in the deferred table could otherwise
     *  overestimate usable funds. A computed negative XRP result is clamped
     *  to zero (it is not an error — it arises when a large credit is
     *  followed by the same debit within the path).
     *
     *  @param account  The account whose perspective determines orientation.
     *  @param issuer   The IOU issuer (doubles as the currency issuer).
     *  @param amount   The raw balance as reported by the underlying ledger.
     *  @return Adjusted balance with deferred credits hidden.
     */
    [[nodiscard]] STAmount
    balanceHookIOU(AccountID const& account, AccountID const& issuer, STAmount const& amount)
        const override;

    /** Return the MPT holder or issuer balance adjusted for deferred credits.
     *
     *  Walks the sandbox chain accumulating per-holder debits (if `account`
     *  is a holder) or the aggregate issuer credit (if `account` is the
     *  issuer). Returns `min(amount, origBalance - totalAdjustment,
     *  minObservedBalance)`, clamped to zero.
     *
     *  @param account  The account being queried (holder or issuer).
     *  @param issue    The MPT issuance.
     *  @param amount   The raw balance as reported by the underlying ledger.
     *  @return Adjusted balance with deferred credits hidden.
     */
    [[nodiscard]] STAmount
    balanceHookMPT(AccountID const& account, MPTIssue const& issue, std::int64_t amount)
        const override;

    /** Return the issuer's available MPT issuance capacity, net of self-debits.
     *
     *  When the issuer owns sell offers and the payment engine (running in
     *  reverse) has already consumed some of them, those amounts are recorded
     *  as self-debits. This hook caps available issuance at
     *  `origOutstandingAmount - totalSelfDebits`, returning zero if the result
     *  is non-positive.
     *
     *  @param issue   The MPT issuance.
     *  @param amount  The raw `OutstandingAmount` from the underlying ledger.
     *  @return Available issuance capacity after subtracting self-debits.
     */
    [[nodiscard]] STAmount
    balanceHookSelfIssueMPT(MPTIssue const& issue, std::int64_t amount) const override;

    /** Record an IOU credit in the deferred-credits table.
     *
     *  Called by ledger mutation helpers at every IOU transfer. The recorded
     *  debit is used by `balanceHookIOU` to hide this credit from future
     *  balance queries within the same payment path.
     *
     *  @param from              Account sending the credit.
     *  @param to                Account receiving the credit.
     *  @param amount            Non-negative IOU amount being transferred.
     *  @param preCreditBalance  Sender's balance immediately before this credit.
     */
    void
    creditHookIOU(
        AccountID const& from,
        AccountID const& to,
        STAmount const& amount,
        STAmount const& preCreditBalance) override;

    /** Record an MPT credit in the deferred-credits table.
     *
     *  Called by ledger mutation helpers at every MPT transfer. The recorded
     *  debit is used by `balanceHookMPT` to hide this credit from future
     *  balance queries within the same payment path.
     *
     *  @param from                     Account sending the MPT.
     *  @param to                       Account receiving the MPT.
     *  @param amount                   Non-negative MPT amount being transferred.
     *  @param preCreditBalanceHolder   Holder's MPT balance before this credit.
     *  @param preCreditBalanceIssuer   Issuer's `OutstandingAmount` before this
     *      credit.
     */
    void
    creditHookMPT(
        AccountID const& from,
        AccountID const& to,
        STAmount const& amount,
        std::uint64_t preCreditBalanceHolder,
        std::int64_t preCreditBalanceIssuer) override;

    /** Record an MPT issuer self-debit arising from a consumed sell offer.
     *
     *  Called when the MPT issuer's own sell offer is consumed during
     *  payment processing. Accumulates the offer amount in the
     *  `DeferredCredits` self-debit field so that `balanceHookSelfIssueMPT`
     *  can correctly limit further issuance capacity.
     *
     *  @param issue       The MPT issuance.
     *  @param amount      Amount consumed from the issuer's sell offer.
     *  @param origBalance Issuer's `OutstandingAmount` before this entry.
     */
    void
    issuerSelfDebitHookMPT(MPTIssue const& issue, std::uint64_t amount, std::int64_t origBalance)
        override;

    /** Record an owner-count transition for reserve-check purposes.
     *
     *  Stores the maximum of `cur` and `next` in the deferred-credits table.
     *  Because payments only decrease owner counts, the peak value is the
     *  conservative bound that prevents a transient low count from bypassing
     *  reserve checks mid-payment.
     *
     *  @param account  Account whose owner count is changing.
     *  @param cur      Owner count before the transition.
     *  @param next     Owner count after the transition.
     */
    void
    adjustOwnerCountHook(AccountID const& account, std::uint32_t cur, std::uint32_t next) override;

    /** Return the peak owner count observed for `account` in this sandbox chain.
     *
     *  Walks the sandbox chain and returns the maximum recorded count across
     *  all ancestors, or `count` if no transition has been recorded.
     *
     *  @param account  Account to query.
     *  @param count    Baseline count from the underlying ledger.
     *  @return The peak owner count seen across the sandbox chain.
     */
    [[nodiscard]] std::uint32_t
    ownerCountHook(AccountID const& account, std::uint32_t count) const override;

    /** Commit changes to a base view.
     *
     *  The two overloads serve different commit targets:
     *  - `apply(RawView&)` is the terminal commit: asserts this sandbox has
     *    no parent (`ps_ == nullptr`) and flushes the state journal to the
     *    raw ledger. The `RawView` must contain state identical to the view
     *    passed at construction, otherwise behavior is undefined.
     *  - `apply(PaymentSandbox&)` asserts that `&to == ps_` (you can only
     *    apply to your direct parent) and propagates both the state journal
     *    and the deferred-credits table into the parent sandbox.
     *
     *  @param to  The target view to flush changes into.
     */
    /** @{ */
    void
    apply(RawView& to);

    void
    apply(PaymentSandbox& to);
    /** @} */

    /** Return the amount of XRP destroyed (as fees) during this payment.
     *
     *  Delegates to `items_.dropsDestroyed()`. Distinct from transferred XRP.
     */
    [[nodiscard]] XRPAmount
    xrpDestroyed() const;

private:
    detail::DeferredCredits tab_;
    PaymentSandbox const* ps_ = nullptr;
};

}  // namespace xrpl
