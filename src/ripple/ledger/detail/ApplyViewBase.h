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

#ifndef RIPPLE_LEDGER_APPLYVIEWBASE_H_INCLUDED
#define RIPPLE_LEDGER_APPLYVIEWBASE_H_INCLUDED

#include <ripple/basics/XRPAmount.h>
#include <ripple/ledger/ApplyView.h>
#include <ripple/ledger/OpenView.h>
#include <ripple/ledger/ReadView.h>
#include <ripple/ledger/detail/ApplyStateTable.h>

namespace ripple {
namespace detail {

class ApplyViewBase : public ApplyView, public RawView
{
public:
    ApplyViewBase() = delete;
    ApplyViewBase(ApplyViewBase const&) = delete;
    ApplyViewBase&
    operator=(ApplyViewBase&&) = delete;
    ApplyViewBase&
    operator=(ApplyViewBase const&) = delete;

    ApplyViewBase(ApplyViewBase&&) = default;

    ApplyViewBase(ReadView const* base, ApplyFlags flags);

    // ReadView
    bool
    open() const override;

    LedgerInfo const&
    info() const override;

    Fees const&
    fees() const override;

    Rules const&
    rules() const override;

    bool
    exists(Keylet const& k) const override;

    std::optional<key_type>
    succ(
        key_type const& key,
        std::optional<key_type> const& last = std::nullopt) const override;

    std::shared_ptr<SLE const>
    read(Keylet const& k) const override;

    std::unique_ptr<sles_type::iter_base>
    slesBegin() const override;

    std::unique_ptr<sles_type::iter_base>
    slesEnd() const override;

    std::unique_ptr<sles_type::iter_base>
    slesUpperBound(uint256 const& key) const override;

    std::unique_ptr<txs_type::iter_base>
    txsBegin() const override;

    std::unique_ptr<txs_type::iter_base>
    txsEnd() const override;

    bool
    txExists(key_type const& key) const override;

    tx_type
    txRead(key_type const& key) const override;

    // ApplyView

    ApplyFlags
    flags() const override;

    std::shared_ptr<SLE>
    peek(Keylet const& k) override;

    void
    erase(std::shared_ptr<SLE> const& sle) override;

    void
    insert(std::shared_ptr<SLE> const& sle) override;

    void
    update(std::shared_ptr<SLE> const& sle) override;

    // RawView

    void
    rawErase(std::shared_ptr<SLE> const& sle) override;

    void
    rawInsert(std::shared_ptr<SLE> const& sle) override;

    void
    rawReplace(std::shared_ptr<SLE> const& sle) override;

    void
    rawDestroyXRP(XRPAmount const& feeDrops) override;
    
    // Map of delta trust lines. As a special case, when both ends of the trust
    // line are the same currency, then it's delta currency for that issuer. To
    // get the change in XRP balance, Account == root, issuer == root, currency
    // == XRP
    std::map<std::tuple<AccountID, AccountID, Currency>, STAmount>                                                         
    balanceChanges(ReadView const& view) const;

protected:
    ApplyFlags flags_;
    ReadView const* base_;
    detail::ApplyStateTable items_;
};

}  // namespace detail
}  // namespace ripple

#endif
