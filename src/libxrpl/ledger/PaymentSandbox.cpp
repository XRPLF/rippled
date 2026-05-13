/** @file
 *  Implements `detail::DeferredCredits` and `PaymentSandbox` — the deferred
 *  credit accounting layer that makes multi-hop IOU and MPT payments safe.
 *
 *  The core invariant: a credit recorded during one step of a payment must
 *  not become visible as spendable balance to any subsequent step in the same
 *  payment. `DeferredCredits` enforces this by intercepting every credit via
 *  `creditIOU`/`creditMPT` and returning a pre-credit balance whenever a step
 *  calls `balanceHookIOU`/`balanceHookMPT`.
 */
#include <xrpl/ledger/PaymentSandbox.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/RawView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/XRPAmount.h>

#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <tuple>
#include <utility>

namespace xrpl {

namespace detail {

/** Build a canonical, direction-independent map key for an IOU trust-line pair.
 *
 *  The lower `AccountID` is always placed first so that the pair (A→B) and
 *  (B→A) share a single `creditsIOU_` entry, mirroring the ledger's own
 *  bidirectional `RippleState` storage convention.
 *
 *  @param a1 One endpoint of the trust line.
 *  @param a2 The other endpoint.
 *  @param c  The currency in question.
 *  @return A `(min, max, currency)` tuple suitable as a `creditsIOU_` key.
 */
auto
DeferredCredits::makeKeyIOU(AccountID const& a1, AccountID const& a2, Currency const& c) -> KeyIOU
{
    if (a1 < a2)
    {
        return std::make_tuple(a1, a2, c);
    }

    return std::make_tuple(a2, a1, c);
}

/** Record a deferred IOU credit from `sender` to `receiver`.
 *
 *  On the first call for a given (sender, receiver, currency) triple the
 *  sender's pre-credit balance snapshot is captured in `lowAcctOrigBalance`.
 *  Subsequent calls for the same triple accumulate the credit amount but do
 *  **not** update the snapshot — this is the post-switchover invariant that
 *  avoids floating-point cancellation when `amount` is large relative to the
 *  original balance.
 *
 *  @param sender               Account delivering the funds.
 *  @param receiver             Account receiving the funds.
 *  @param amount               Positive IOU amount being credited.
 *  @param preCreditSenderBalance  Sender's balance on the trust line before
 *      this credit is applied; captured only on the first call per pair.
 */
void
DeferredCredits::creditIOU(
    AccountID const& sender,
    AccountID const& receiver,
    STAmount const& amount,
    STAmount const& preCreditSenderBalance)
{
    XRPL_ASSERT(
        sender != receiver, "xrpl::detail::DeferredCredits::creditIOU : sender is not receiver");
    XRPL_ASSERT(!amount.negative(), "xrpl::detail::DeferredCredits::creditIOU : positive amount");
    XRPL_ASSERT(
        amount.holds<Issue>(), "xrpl::detail::DeferredCredits::creditIOU : amount is for Issue");

    auto const k = makeKeyIOU(sender, receiver, amount.get<Issue>().currency);
    auto i = creditsIOU_.find(k);
    if (i == creditsIOU_.end())
    {
        ValueIOU v;

        if (sender < receiver)
        {
            v.lowAcctDebits = amount;
            v.highAcctDebits = amount.zeroed();
            v.lowAcctOrigBalance = preCreditSenderBalance;
        }
        else
        {
            v.lowAcctDebits = amount.zeroed();
            v.highAcctDebits = amount;
            v.lowAcctOrigBalance = -preCreditSenderBalance;
        }

        creditsIOU_[k] = v;
    }
    else
    {
        // only record the balance the first time, do not record it here
        auto& v = i->second;
        if (sender < receiver)
        {
            v.lowAcctDebits += amount;
        }
        else
        {
            v.highAcctDebits += amount;
        }
    }
}

/** Record a deferred MPT credit from `sender` to `receiver`.
 *
 *  Maintains per-holder debits and, for issuer→holder transfers, a running
 *  `credit` total against the issuer's `OutstandingAmount`.  The issuer's
 *  original `OutstandingAmount` (`preCreditBalanceIssuer`) and each holder's
 *  original balance (`preCreditBalanceHolder`) are captured only on the first
 *  call for each MPTID / holder pair; later calls accumulate debits without
 *  overwriting snapshots.
 *
 *  @param sender                   Account delivering the MPT.
 *  @param receiver                 Account receiving the MPT.
 *  @param amount                   Positive MPT amount being credited.
 *  @param preCreditBalanceHolder   Receiver's MPToken balance before this
 *      credit; captured only on first call per holder.
 *  @param preCreditBalanceIssuer   Issuer's `OutstandingAmount` before this
 *      credit (may temporarily exceed `MaximumAmount` during reverse-path
 *      execution); captured only on first call per MPTID.
 */
void
DeferredCredits::creditMPT(
    AccountID const& sender,
    AccountID const& receiver,
    STAmount const& amount,
    std::uint64_t preCreditBalanceHolder,
    std::int64_t preCreditBalanceIssuer)
{
    XRPL_ASSERT(
        amount.holds<MPTIssue>(),
        "xrpl::detail::DeferredCredits::creditMPT : amount is for MPTIssue");
    XRPL_ASSERT(!amount.negative(), "xrpl::detail::DeferredCredits::creditMPT : positive amount");
    XRPL_ASSERT(
        sender != receiver, "xrpl::detail::DeferredCredits::creditMPT : sender is not receiver");

    auto const mptAmtVal = amount.mpt().value();
    auto const& issuer = amount.getIssuer();
    auto const& mptIssue = amount.get<MPTIssue>();
    auto const& mptID = mptIssue.getMptID();
    bool const isSenderIssuer = sender == issuer;

    auto i = creditsMPT_.find(mptID);
    if (i == creditsMPT_.end())
    {
        IssuerValueMPT v;
        if (isSenderIssuer)
        {
            v.credit = mptAmtVal;
            v.holders[receiver].origBalance = preCreditBalanceHolder;
        }
        else
        {
            v.holders[sender].debit = mptAmtVal;
            v.holders[sender].origBalance = preCreditBalanceHolder;
        }
        v.origBalance = preCreditBalanceIssuer;
        creditsMPT_.emplace(mptID, std::move(v));
    }
    else
    {
        // only record the balance the first time, do not record it here
        auto& v = i->second;
        if (isSenderIssuer)
        {
            v.credit += mptAmtVal;
            if (!v.holders.contains(receiver))
            {
                v.holders[receiver].origBalance = preCreditBalanceHolder;
            }
        }
        else
        {
            if (!v.holders.contains(sender))
            {
                v.holders[sender].debit = mptAmtVal;
                v.holders[sender].origBalance = preCreditBalanceHolder;
            }
            else
            {
                v.holders[sender].debit += mptAmtVal;
            }
        }
    }
}

/** Record an MPT issuer self-debit arising from a sell offer owned by the issuer.
 *
 *  The payment engine executes paths in reverse (credit before debit).  When
 *  the issuer owns a sell offer, the credit step temporarily inflates
 *  `OutstandingAmount` beyond `MaximumAmount`.  `selfDebit` accumulates the
 *  total amount the issuer has self-debited so that `balanceHookSelfIssueMPT`
 *  can cap issuable supply to `origBalance - selfDebit`.
 *
 *  @param issue        Identifies the MPT issuance.
 *  @param amount       Positive amount the issuer is self-debiting this step.
 *  @param origBalance  Issuer's `OutstandingAmount` before any path execution;
 *      captured only on the first call per MPTID.
 */
void
DeferredCredits::issuerSelfDebitMPT(
    MPTIssue const& issue,
    std::uint64_t amount,
    std::int64_t origBalance)
{
    auto const& mptID = issue.getMptID();
    auto i = creditsMPT_.find(mptID);

    if (i == creditsMPT_.end())
    {
        IssuerValueMPT v;
        v.origBalance = origBalance;
        v.selfDebit = amount;
        creditsMPT_.emplace(mptID, std::move(v));
    }
    else
    {
        i->second.selfDebit += amount;
    }
}

/** Update the peak owner count recorded for `id`.
 *
 *  Stores `max(cur, next)` and then takes the running maximum with any
 *  previously recorded value.  Using the maximum rather than the final count
 *  ensures that reserve checks during the payment reflect the highest
 *  obligation incurred at any point, even if trust lines created mid-payment
 *  are subsequently deleted.
 *
 *  @param id   Account whose owner count is changing.
 *  @param cur  Owner count before the adjustment.
 *  @param next Owner count after the adjustment.
 */
void
DeferredCredits::ownerCount(AccountID const& id, std::uint32_t cur, std::uint32_t next)
{
    auto const v = std::max(cur, next);
    auto r = ownerCounts_.emplace(id, v);
    if (!r.second)
    {
        auto& mapVal = r.first->second;
        mapVal = std::max(v, mapVal);
    }
}

/** Return the peak owner count recorded for `id`, if any.
 *
 *  @param id Account to query.
 *  @return The maximum owner count seen for this account during the payment,
 *      or `std::nullopt` if no adjustment has been recorded for `id`.
 */
std::optional<std::uint32_t>
DeferredCredits::ownerCount(AccountID const& id) const
{
    auto i = ownerCounts_.find(id);
    if (i != ownerCounts_.end())
        return i->second;
    return std::nullopt;
}

/** Return the recorded IOU adjustments for the trust line between `main` and `other`.
 *
 *  The returned `AdjustmentIOU` is oriented from `main`'s perspective:
 *  `debits` is what `main` has sent, `credits` is what `main` has received,
 *  and `origBalance` is `main`'s balance before any deferred credit was
 *  recorded.
 *
 *  @param main     The account whose perspective the adjustment is expressed in.
 *  @param other    The counterparty on the trust line.
 *  @param currency The IOU currency.
 *  @return Adjustments from `main`'s perspective, or `std::nullopt` if no
 *      credit has been recorded for this triple.
 */
auto
DeferredCredits::adjustmentsIOU(
    AccountID const& main,
    AccountID const& other,
    Currency const& currency) const -> std::optional<AdjustmentIOU>
{
    std::optional<AdjustmentIOU> result;

    KeyIOU const k = makeKeyIOU(main, other, currency);
    auto i = creditsIOU_.find(k);
    if (i == creditsIOU_.end())
        return result;

    auto const& v = i->second;

    if (main < other)
    {
        result.emplace(v.lowAcctDebits, v.highAcctDebits, v.lowAcctOrigBalance);
        return result;
    }

    result.emplace(v.highAcctDebits, v.lowAcctDebits, -v.lowAcctOrigBalance);
    return result;
}

/** Return the recorded MPT adjustments for the given issuance.
 *
 *  @param mptID Identifier of the MPT issuance.
 *  @return The full `IssuerValueMPT` record (aliased as `AdjustmentMPT`)
 *      containing per-holder debits and the issuer's credit/self-debit totals,
 *      or `std::nullopt` if no credit has been recorded for this MPTID.
 */
auto
DeferredCredits::adjustmentsMPT(xrpl::MPTID const& mptID) const -> std::optional<AdjustmentMPT>
{
    auto i = creditsMPT_.find(mptID);
    if (i == creditsMPT_.end())
        return std::nullopt;
    return i->second;
}

/** Merge this sandbox's deferred-credit tables into a parent `DeferredCredits`.
 *
 *  Credits and debits are accumulated additively into `to`.  Existing
 *  `origBalance` snapshots in `to` are **never** overwritten — the parent
 *  recorded the true pre-payment balance first, and that must remain
 *  authoritative.  Owner counts are merged by taking the per-account maximum.
 *
 *  @param to Destination table, typically belonging to a parent `PaymentSandbox`.
 */
void
DeferredCredits::apply(DeferredCredits& to)
{
    for (auto const& i : creditsIOU_)
    {
        auto r = to.creditsIOU_.emplace(i);
        if (!r.second)
        {
            auto& toVal = r.first->second;
            auto const& fromVal = i.second;
            toVal.lowAcctDebits += fromVal.lowAcctDebits;
            toVal.highAcctDebits += fromVal.highAcctDebits;
        }
    }

    for (auto const& i : creditsMPT_)
    {
        auto r = to.creditsMPT_.emplace(i);
        if (!r.second)
        {
            auto& toVal = r.first->second;
            auto const& fromVal = i.second;
            toVal.credit += fromVal.credit;
            toVal.selfDebit += fromVal.selfDebit;
            for (auto& [k, v] : fromVal.holders)
            {
                if (!toVal.holders.contains(k))
                {
                    toVal.holders[k] = v;
                }
                else
                {
                    toVal.holders[k].debit += v.debit;
                }
            }
        }
    }

    for (auto const& i : ownerCounts_)
    {
        auto r = to.ownerCounts_.emplace(i);
        if (!r.second)
        {
            auto& toVal = r.first->second;
            auto const& fromVal = i.second;
            toVal = std::max(toVal, fromVal);
        }
    }
}

}  // namespace detail

/** Return `account`'s IOU balance adjusted for deferred credits recorded in
 *  this sandbox and all ancestor sandboxes.
 *
 *  Uses the post-switchover algorithm: rather than computing `(B+C) - C`
 *  (which suffers catastrophic cancellation when the credit `C` dwarfs the
 *  original balance `B`), the first `creditIOU` call for a pair captures `B`
 *  directly.  This function walks the `ps_` chain accumulating the total
 *  debit `delta` and the earliest-seen original balance `lastBal`, then
 *  returns `min(amount, lastBal - delta, minBal)` to prevent the adjusted
 *  amount from ever exceeding any ancestor's pre-credit snapshot.
 *
 *  @param account Account whose available IOU balance is being queried.
 *  @param issuer  Issuer of the IOU currency.
 *  @param amount  Post-credit balance as reported by the underlying ledger
 *      view (i.e. `B+C`).
 *  @return Pre-credit available balance, clamped to zero for XRP when the
 *      arithmetic produces a negative value due to large mid-payment credits.
 *
 *  @note A calculated negative XRP result is not an error: it can arise when
 *      a large XRP credit is recorded and then debited within the same path.
 *      The negative value is clamped to zero rather than treated as a fault.
 */
STAmount
PaymentSandbox::balanceHookIOU(
    AccountID const& account,
    AccountID const& issuer,
    STAmount const& amount) const
{
    XRPL_ASSERT(amount.holds<Issue>(), "balanceHookIOU: amount is for Issue");

    auto const& currency = amount.get<Issue>().currency;

    auto delta = amount.zeroed();
    auto lastBal = amount;
    auto minBal = amount;
    for (auto curSB = this; curSB != nullptr; curSB = curSB->ps_)
    {
        if (auto adj = curSB->tab_.adjustmentsIOU(account, issuer, currency))
        {
            delta += adj->debits;
            lastBal = adj->origBalance;
            if (lastBal < minBal)
                minBal = lastBal;
        }
    }

    auto adjustedAmt = std::min({amount, lastBal - delta, minBal});
    adjustedAmt.get<Issue>().account = amount.getIssuer();

    if (isXRP(issuer) && adjustedAmt < beast::kZERO)
    {
        adjustedAmt.clear();
    }

    return adjustedAmt;
}

/** Return `account`'s MPT balance adjusted for deferred credits in this sandbox chain.
 *
 *  Walks the `ps_` ancestor chain accumulating total debits (`delta`) and the
 *  earliest pre-credit snapshot (`lastBal`), then returns
 *  `min(amount, lastBal - delta, minBal)`.  For holders `delta` is the sum of
 *  per-holder debits; for the issuer `delta` is the running `credit` total
 *  against `OutstandingAmount`.
 *
 *  @param account Account being queried (holder or issuer).
 *  @param issue   The MPT issuance.
 *  @param amount  Current raw balance as seen by the underlying ledger view.
 *  @return Pre-credit available balance as an `STAmount`, or zero if the
 *      adjusted amount would be non-positive.
 */
STAmount
PaymentSandbox::balanceHookMPT(AccountID const& account, MPTIssue const& issue, std::int64_t amount)
    const
{
    auto const& issuer = issue.getIssuer();
    bool const accountIsHolder = account != issuer;

    std::int64_t delta = 0;
    std::int64_t lastBal = amount;
    std::int64_t minBal = amount;
    for (auto curSB = this; curSB != nullptr; curSB = curSB->ps_)
    {
        if (auto adj = curSB->tab_.adjustmentsMPT(issue))
        {
            if (accountIsHolder)
            {
                if (auto const i = adj->holders.find(account); i != adj->holders.end())
                {
                    delta += i->second.debit;
                    lastBal = i->second.origBalance;
                }
            }
            else
            {
                delta += adj->credit;
                lastBal = adj->origBalance;
            }
            minBal = std::min(lastBal, minBal);
        }
    }

    auto const adjustedAmt = std::min({amount, lastBal - delta, minBal});

    return adjustedAmt > 0 ? STAmount{issue, adjustedAmt} : STAmount{issue};
}

/** Return the issuer's available MPT issuance capacity adjusted for self-debits.
 *
 *  When the issuer owns a sell offer, the payment engine credits the buyer
 *  before debiting the issuer, which can transiently inflate
 *  `OutstandingAmount` beyond `MaximumAmount`.  This hook caps the issuer's
 *  available issuance to `origBalance - selfDebit`, where `selfDebit`
 *  accumulates across the sandbox chain via `issuerSelfDebitMPT`.
 *
 *  @param issue  The MPT issuance for which the issuer's capacity is queried.
 *  @param amount Current `OutstandingAmount` as reported by the underlying
 *      ledger view.
 *  @return Adjusted available issuance, or zero if `selfDebit >= origBalance`.
 */
STAmount
PaymentSandbox::balanceHookSelfIssueMPT(xrpl::MPTIssue const& issue, std::int64_t amount) const
{
    std::int64_t selfDebited = 0;
    std::int64_t lastBal = amount;
    for (auto curSB = this; curSB != nullptr; curSB = curSB->ps_)
    {
        if (auto adj = curSB->tab_.adjustmentsMPT(issue))
        {
            selfDebited += adj->selfDebit;
            lastBal = adj->origBalance;
        }
    }

    if (lastBal > selfDebited)
        return STAmount{issue, lastBal - selfDebited};

    return STAmount{issue};
}

/** Return the peak owner count for `account` seen across the sandbox chain.
 *
 *  Walks all ancestor sandboxes and returns the maximum of `count` and any
 *  recorded peak, ensuring reserve checks reflect the highest owner count
 *  incurred at any point during the payment even if trust lines created
 *  mid-payment have since been deleted.
 *
 *  @param account Account being queried.
 *  @param count   Current owner count from the underlying ledger view.
 *  @return Maximum of `count` and the peak recorded across all sandboxes.
 */
std::uint32_t
PaymentSandbox::ownerCountHook(AccountID const& account, std::uint32_t count) const
{
    std::uint32_t result = count;
    for (auto curSB = this; curSB != nullptr; curSB = curSB->ps_)
    {
        if (auto adj = curSB->tab_.ownerCount(account))
            result = std::max(result, *adj);
    }
    return result;
}

/** Intercept an IOU credit and record it in the deferred-credit table.
 *
 *  Called by the payment engine whenever an IOU amount flows from `from` to
 *  `to`.  Forwards directly to `tab_.creditIOU` so the credit is deferred
 *  and invisible to subsequent balance queries within the same payment.
 *
 *  @param from              Sending account.
 *  @param to                Receiving account.
 *  @param amount            Positive IOU amount being credited.
 *  @param preCreditBalance  Sender's trust-line balance before this credit;
 *      used as the original-balance snapshot on first call per pair.
 */
void
PaymentSandbox::creditHookIOU(
    AccountID const& from,
    AccountID const& to,
    STAmount const& amount,
    STAmount const& preCreditBalance)
{
    XRPL_ASSERT(amount.holds<Issue>(), "creditHookIOU: amount is for Issue");

    tab_.creditIOU(from, to, amount, preCreditBalance);
}

/** Intercept an MPT credit and record it in the deferred-credit table.
 *
 *  Called by the payment engine whenever an MPT amount flows from `from` to
 *  `to`.  Forwards directly to `tab_.creditMPT`.
 *
 *  @param from                    Sending account (issuer or holder).
 *  @param to                      Receiving account.
 *  @param amount                  Positive MPT amount being credited.
 *  @param preCreditBalanceHolder  Receiver's MPToken balance before this credit.
 *  @param preCreditBalanceIssuer  Issuer's `OutstandingAmount` before this credit.
 */
void
PaymentSandbox::creditHookMPT(
    AccountID const& from,
    AccountID const& to,
    STAmount const& amount,
    std::uint64_t preCreditBalanceHolder,
    std::int64_t preCreditBalanceIssuer)
{
    XRPL_ASSERT(amount.holds<MPTIssue>(), "creditHookMPT: amount is for MPTIssue");

    tab_.creditMPT(from, to, amount, preCreditBalanceHolder, preCreditBalanceIssuer);
}

/** Intercept an MPT issuer self-debit and record it in the deferred-credit table.
 *
 *  Called when the issuer executes a sell offer for their own MPT.  Forwards
 *  to `tab_.issuerSelfDebitMPT` so `balanceHookSelfIssueMPT` can later cap
 *  available issuance correctly.
 *
 *  @param issue       Identifies the MPT issuance.
 *  @param amount      Positive amount the issuer is self-debiting.
 *  @param origBalance Issuer's `OutstandingAmount` before path execution began.
 */
void
PaymentSandbox::issuerSelfDebitHookMPT(
    MPTIssue const& issue,
    std::uint64_t amount,
    std::int64_t origBalance)
{
    XRPL_ASSERT(amount > 0, "PaymentSandbox::issuerSelfDebitHookMPT: amount must be > 0");

    tab_.issuerSelfDebitMPT(issue, amount, origBalance);
}

/** Record an owner-count change for `account` in this sandbox's peak table.
 *
 *  Forwards to `tab_.ownerCount(account, cur, next)`, which stores
 *  `max(cur, next)` and retains the running maximum across calls.
 *
 *  @param account Account whose owner count is changing.
 *  @param cur     Owner count before the adjustment.
 *  @param next    Owner count after the adjustment.
 */
void
PaymentSandbox::adjustOwnerCountHook(
    AccountID const& account,
    std::uint32_t cur,
    std::uint32_t next)
{
    tab_.ownerCount(account, cur, next);
}

/** Commit this sandbox's ledger state changes to the underlying raw view.
 *
 *  Terminal form: asserts that `ps_ == nullptr`, confirming this is the
 *  outermost sandbox with no unresolved parent.  The deferred-credit table
 *  is not forwarded here; only `items_` (ledger object mutations) are flushed.
 *
 *  @param to Destination raw view (typically the `ApplyView` for the tx).
 */
void
PaymentSandbox::apply(RawView& to)
{
    XRPL_ASSERT(!ps_, "xrpl::PaymentSandbox::apply : non-null sandbox");
    items_.apply(to);
}

/** Merge this sandbox into a parent `PaymentSandbox`.
 *
 *  Propagates both ledger state changes (`items_`) and the deferred-credit
 *  tables (`tab_`) into the parent.  Asserts that `ps_ == &to`, enforcing
 *  that only the direct parent may be the merge target.
 *
 *  @param to Parent sandbox into which this sandbox's state is merged.
 */
void
PaymentSandbox::apply(PaymentSandbox& to)
{
    XRPL_ASSERT(ps_ == &to, "xrpl::PaymentSandbox::apply : matching sandbox");
    items_.apply(to);
    tab_.apply(to.tab_);
}

/** Return the total XRP destroyed (burned as fees) within this sandbox.
 *
 *  @return Drop count forwarded from `items_.dropsDestroyed()`.
 */
XRPAmount
PaymentSandbox::xrpDestroyed() const
{
    return items_.dropsDestroyed();
}

}  // namespace xrpl
