//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#include <xrpld/app/misc/MPTUtils.h>
#include <xrpld/app/paths/detail/EitherAmount.h>
#include <xrpld/ledger/PaymentSandbox.h>
#include <xrpld/ledger/View.h>

#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/SField.h>

namespace ripple {

namespace detail {

auto
DeferredCredits::makeKeyIOU(
    AccountID const& a1,
    AccountID const& a2,
    Currency const& c) -> KeyIOU
{
    if (a1 < a2)
        return std::make_tuple(a1, a2, c);
    else
        return std::make_tuple(a2, a1, c);
}

void
DeferredCredits::creditIOU(
    AccountID const& sender,
    AccountID const& receiver,
    STAmount const& amount,
    STAmount const& preCreditSenderBalance)
{
    XRPL_ASSERT(
        sender != receiver,
        "ripple::detail::DeferredCredits::creditIOU : sender is not receiver");
    XRPL_ASSERT(
        !amount.negative(),
        "ripple::detail::DeferredCredits::creditIOU : positive amount");
    XRPL_ASSERT(
        amount.holds<Issue>(),
        "ripple::detail::DeferredCredits::creditIOU : amount is for Issue");

    auto const k = makeKeyIOU(sender, receiver, amount.get<Issue>().currency);
    auto i = creditsIOU_.find(k);
    if (i == creditsIOU_.end())
    {
        ValueIOU v;

        if (sender < receiver)
        {
            v.highAcctCredits = amount;
            v.lowAcctCredits = amount.zeroed();
            v.lowAcctOrigBalance = preCreditSenderBalance;
        }
        else
        {
            v.highAcctCredits = amount.zeroed();
            v.lowAcctCredits = amount;
            v.lowAcctOrigBalance = -preCreditSenderBalance;
        }

        creditsIOU_[k] = v;
    }
    else
    {
        // only record the balance the first time, do not record it here
        auto& v = i->second;
        if (sender < receiver)
            v.highAcctCredits += amount;
        else
            v.lowAcctCredits += amount;
    }
}

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
        "ripple::detail::DeferredCredits::creditMPT : amount is for MPTIssue");
    XRPL_ASSERT(
        !amount.negative(),
        "ripple::detail::DeferredCredits::creditMPT : positive amount");
    XRPL_ASSERT(
        sender != receiver,
        "ripple::detail::DeferredCredits::creditMPT : sender is not receiver");

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
            if (v.holders.find(receiver) == v.holders.end())
                v.holders[receiver].origBalance = preCreditBalanceHolder;
        }
        else
        {
            if (v.holders.find(sender) == v.holders.end())
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

void
DeferredCredits::ownerCount(
    AccountID const& id,
    std::uint32_t cur,
    std::uint32_t next)
{
    auto const v = std::max(cur, next);
    auto r = ownerCounts_.emplace(std::make_pair(id, v));
    if (!r.second)
    {
        auto& mapVal = r.first->second;
        mapVal = std::max(v, mapVal);
    }
}

std::optional<std::uint32_t>
DeferredCredits::ownerCount(AccountID const& id) const
{
    auto i = ownerCounts_.find(id);
    if (i != ownerCounts_.end())
        return i->second;
    return std::nullopt;
}

// Get the adjustments for the balance between main and other.
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
        result.emplace(
            v.highAcctCredits, v.lowAcctCredits, v.lowAcctOrigBalance);
    }
    else
    {
        result.emplace(
            v.lowAcctCredits, v.highAcctCredits, -v.lowAcctOrigBalance);
    }

    return result;
}

auto
DeferredCredits::adjustmentsMPT(ripple::MPTID const& mptID) const
    -> std::optional<AdjustmentMPT>
{
    auto i = creditsMPT_.find(mptID);
    if (i == creditsMPT_.end())
        return std::nullopt;
    return i->second;
}

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
            toVal.lowAcctCredits += fromVal.lowAcctCredits;
            toVal.highAcctCredits += fromVal.highAcctCredits;
            // Do not update the orig balance, it's already correct
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
                toVal.holders[k] = v;
            // Do not update the orig balance, it's already correct
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

STAmount
PaymentSandbox::balanceHookIOU(
    AccountID const& account,
    AccountID const& issuer,
    STAmount const& amount) const
{
    XRPL_ASSERT(amount.holds<Issue>(), "balanceHookIOU: amount is for Issue");

    /*
    There are two algorithms here. The pre-switchover algorithm takes the
    current amount and subtracts the recorded credits. The post-switchover
    algorithm remembers the original balance, and subtracts the debits. The
    post-switchover algorithm should be more numerically stable. Consider a
    large credit with a small initial balance. The pre-switchover algorithm
    computes (B+C)-C (where B+C will the amount passed in). The
    post-switchover algorithm returns B. When B and C differ by large
    magnitudes, (B+C)-C may not equal B.
    */

    auto const& currency = amount.get<Issue>().currency;

    auto delta = amount.zeroed();
    auto lastBal = amount;
    auto minBal = amount;
    for (auto curSB = this; curSB; curSB = curSB->ps_)
    {
        if (auto adj = curSB->tab_.adjustmentsIOU(account, issuer, currency))
        {
            delta += adj->debits;
            lastBal = adj->origBalance;
            if (lastBal < minBal)
                minBal = lastBal;
        }
    }

    // The adjusted amount should never be larger than the balance. In
    // some circumstances, it is possible for the deferred credits table
    // to compute usable balance just slightly above what the ledger
    // calculates (but always less than the actual balance).
    auto adjustedAmt = std::min({amount, lastBal - delta, minBal});
    if (amount.holds<Issue>())
        adjustedAmt.setIssuer(amount.getIssuer());

    if (isXRP(issuer) && adjustedAmt < beast::zero)
        // A calculated negative XRP balance is not an error case. Consider a
        // payment snippet that credits a large XRP amount and then debits the
        // same amount. The credit can't be used, but we subtract the debit and
        // calculate a negative value. It's not an error case.
        adjustedAmt.clear();

    return adjustedAmt;
}

STAmount
PaymentSandbox::balanceHookMPT(
    AccountID const& account,
    MPTIssue const& issue,
    std::int64_t amount) const
{
    auto const& issuer = issue.getIssuer();
    bool const accountIsHolder = account != issuer;

    std::int64_t delta = 0;
    std::int64_t lastBal = amount;
    std::int64_t minBal = amount;
    for (auto curSB = this; curSB; curSB = curSB->ps_)
    {
        if (auto adj = curSB->tab_.adjustmentsMPT(issue))
        {
            if (accountIsHolder)
            {
                if (auto const i = adj->holders.find(account);
                    i != adj->holders.end())
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
            if (lastBal < minBal)
                minBal = lastBal;
        }
    }

    // The adjusted amount should never be larger than the balance.

    auto const adjustedAmt = std::min({amount, lastBal - delta, minBal});

    return adjustedAmt > 0 ? STAmount{issue, adjustedAmt} : STAmount{issue};
}

STAmount
PaymentSandbox::balanceHookSelfIssueMPT(
    ripple::MPTIssue const& issue,
    std::int64_t amount) const
{
    std::int64_t selfDebited = 0;
    std::int64_t lastBal = amount;
    for (auto curSB = this; curSB; curSB = curSB->ps_)
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

std::uint32_t
PaymentSandbox::ownerCountHook(AccountID const& account, std::uint32_t count)
    const
{
    std::uint32_t result = count;
    for (auto curSB = this; curSB; curSB = curSB->ps_)
    {
        if (auto adj = curSB->tab_.ownerCount(account))
            result = std::max(result, *adj);
    }
    return result;
}

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

void
PaymentSandbox::creditHookMPT(
    AccountID const& from,
    AccountID const& to,
    STAmount const& amount,
    std::uint64_t preCreditBalanceHolder,
    std::int64_t preCreditBalanceIssuer)
{
    XRPL_ASSERT(
        amount.holds<MPTIssue>(), "creditHookMPT: amount is for MPTIssue");

    tab_.creditMPT(
        from, to, amount, preCreditBalanceHolder, preCreditBalanceIssuer);
}

void
PaymentSandbox::issuerSelfDebitHookMPT(
    MPTIssue const& issue,
    std::uint64_t amount,
    std::int64_t origBalance)
{
    tab_.issuerSelfDebitMPT(issue, amount, origBalance);
}

void
PaymentSandbox::adjustOwnerCountHook(
    AccountID const& account,
    std::uint32_t cur,
    std::uint32_t next)
{
    tab_.ownerCount(account, cur, next);
}

void
PaymentSandbox::apply(RawView& to)
{
    XRPL_ASSERT(!ps_, "ripple::PaymentSandbox::apply : non-null sandbox");
    items_.apply(to);
}

void
PaymentSandbox::apply(PaymentSandbox& to)
{
    XRPL_ASSERT(ps_ == &to, "ripple::PaymentSandbox::apply : matching sandbox");
    items_.apply(to);
    tab_.apply(to.tab_);
}

std::map<std::tuple<AccountID, AccountID, Currency>, STAmount>
PaymentSandbox::balanceChanges(ReadView const& view) const
{
    using key_t = std::tuple<AccountID, AccountID, Currency>;
    // Map of delta trust lines. As a special case, when both ends of the trust
    // line are the same currency, then it's delta currency for that issuer. To
    // get the change in XRP balance, Account == root, issuer == root, currency
    // == XRP
    std::map<key_t, STAmount> result;

    // populate a dictionary with low/high/currency/delta. This can be
    // compared with the other versions payment code.
    auto each = [&result](
                    uint256 const& key,
                    bool isDelete,
                    std::shared_ptr<SLE const> const& before,
                    std::shared_ptr<SLE const> const& after) {
        STAmount oldBalance;
        STAmount newBalance;
        AccountID lowID;
        AccountID highID;

        // before is read from prev view
        if (isDelete)
        {
            if (!before)
                return;

            auto const bt = before->getType();
            switch (bt)
            {
                case ltACCOUNT_ROOT:
                    lowID = xrpAccount();
                    highID = (*before)[sfAccount];
                    oldBalance = (*before)[sfBalance];
                    newBalance = oldBalance.zeroed();
                    break;
                case ltRIPPLE_STATE:
                    lowID = (*before)[sfLowLimit].getIssuer();
                    highID = (*before)[sfHighLimit].getIssuer();
                    oldBalance = (*before)[sfBalance];
                    newBalance = oldBalance.zeroed();
                    break;
                case ltOFFER:
                    // TBD
                    break;
                default:
                    break;
            }
        }
        else if (!before)
        {
            // insert
            auto const at = after->getType();
            switch (at)
            {
                case ltACCOUNT_ROOT:
                    lowID = xrpAccount();
                    highID = (*after)[sfAccount];
                    newBalance = (*after)[sfBalance];
                    oldBalance = newBalance.zeroed();
                    break;
                case ltRIPPLE_STATE:
                    lowID = (*after)[sfLowLimit].getIssuer();
                    highID = (*after)[sfHighLimit].getIssuer();
                    newBalance = (*after)[sfBalance];
                    oldBalance = newBalance.zeroed();
                    break;
                case ltOFFER:
                    // TBD
                    break;
                default:
                    break;
            }
        }
        else
        {
            // modify
            auto const at = after->getType();
            XRPL_ASSERT(
                at == before->getType(),
                "ripple::PaymentSandbox::balanceChanges : after and before "
                "types matching");
            switch (at)
            {
                case ltACCOUNT_ROOT:
                    lowID = xrpAccount();
                    highID = (*after)[sfAccount];
                    oldBalance = (*before)[sfBalance];
                    newBalance = (*after)[sfBalance];
                    break;
                case ltRIPPLE_STATE:
                    lowID = (*after)[sfLowLimit].getIssuer();
                    highID = (*after)[sfHighLimit].getIssuer();
                    oldBalance = (*before)[sfBalance];
                    newBalance = (*after)[sfBalance];
                    break;
                case ltOFFER:
                    // TBD
                    break;
                default:
                    break;
            }
        }
        // The following are now set, put them in the map
        auto delta = newBalance - oldBalance;
        auto const cur = newBalance.get<Issue>().currency;
        result[std::make_tuple(lowID, highID, cur)] = delta;
        auto r = result.emplace(std::make_tuple(lowID, lowID, cur), delta);
        if (r.second)
        {
            r.first->second += delta;
        }

        delta.negate();
        r = result.emplace(std::make_tuple(highID, highID, cur), delta);
        if (r.second)
        {
            r.first->second += delta;
        }
    };
    items_.visit(view, each);
    return result;
}

XRPAmount
PaymentSandbox::xrpDestroyed() const
{
    return items_.dropsDestroyed();
}

}  // namespace ripple
