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

#include <ripple/basics/contract.h>
#include <ripple/ledger/detail/ApplyViewBase.h>

namespace ripple {
namespace detail {

ApplyViewBase::ApplyViewBase(ReadView const* base, ApplyFlags flags)
    : flags_(flags), base_(base)
{
}

//---

bool
ApplyViewBase::open() const
{
    return base_->open();
}

LedgerInfo const&
ApplyViewBase::info() const
{
    return base_->info();
}

Fees const&
ApplyViewBase::fees() const
{
    return base_->fees();
}

Rules const&
ApplyViewBase::rules() const
{
    return base_->rules();
}

bool
ApplyViewBase::exists(Keylet const& k) const
{
    return items_.exists(*base_, k);
}

auto
ApplyViewBase::succ(key_type const& key, std::optional<key_type> const& last)
    const -> std::optional<key_type>
{
    return items_.succ(*base_, key, last);
}

std::shared_ptr<SLE const>
ApplyViewBase::read(Keylet const& k) const
{
    return items_.read(*base_, k);
}

auto
ApplyViewBase::slesBegin() const -> std::unique_ptr<sles_type::iter_base>
{
    return base_->slesBegin();
}

auto
ApplyViewBase::slesEnd() const -> std::unique_ptr<sles_type::iter_base>
{
    return base_->slesEnd();
}

auto
ApplyViewBase::slesUpperBound(uint256 const& key) const
    -> std::unique_ptr<sles_type::iter_base>
{
    return base_->slesUpperBound(key);
}

auto
ApplyViewBase::txsBegin() const -> std::unique_ptr<txs_type::iter_base>
{
    return base_->txsBegin();
}

auto
ApplyViewBase::txsEnd() const -> std::unique_ptr<txs_type::iter_base>
{
    return base_->txsEnd();
}

bool
ApplyViewBase::txExists(key_type const& key) const
{
    return base_->txExists(key);
}

auto
ApplyViewBase::txRead(key_type const& key) const -> tx_type
{
    return base_->txRead(key);
}

//---

ApplyFlags
ApplyViewBase::flags() const
{
    return flags_;
}

std::shared_ptr<SLE>
ApplyViewBase::peek(Keylet const& k)
{
    return items_.peek(*base_, k);
}

void
ApplyViewBase::erase(std::shared_ptr<SLE> const& sle)
{
    items_.erase(*base_, sle);
}

void
ApplyViewBase::insert(std::shared_ptr<SLE> const& sle)
{
    items_.insert(*base_, sle);
}

void
ApplyViewBase::update(std::shared_ptr<SLE> const& sle)
{
    items_.update(*base_, sle);
}

//---

void
ApplyViewBase::rawErase(std::shared_ptr<SLE> const& sle)
{
    items_.rawErase(*base_, sle);
}

void
ApplyViewBase::rawInsert(std::shared_ptr<SLE> const& sle)
{
    items_.insert(*base_, sle);
}

void
ApplyViewBase::rawReplace(std::shared_ptr<SLE> const& sle)
{
    items_.replace(*base_, sle);
}

void
ApplyViewBase::rawDestroyXRP(XRPAmount const& fee)
{
    items_.destroyXRP(fee);
}

std::map<std::tuple<AccountID, AccountID, Currency>, STAmount>
ApplyViewBase::balanceChanges(ReadView const& view) const
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
            assert(at == before->getType());
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
        auto const cur = newBalance.getCurrency();
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

}  // namespace detail
}  // namespace ripple
