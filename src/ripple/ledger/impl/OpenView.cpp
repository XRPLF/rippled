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
#include <ripple/ledger/OpenView.h>

namespace ripple {

open_ledger_t const open_ledger{};

class OpenView::txs_iter_impl : public txs_type::iter_base
{
private:
    bool metadata_;
    txs_map::const_iterator iter_;

public:
    explicit txs_iter_impl(bool metadata, txs_map::const_iterator iter)
        : metadata_(metadata), iter_(iter)
    {
    }

    std::unique_ptr<base_type>
    copy() const override
    {
        return std::make_unique<txs_iter_impl>(metadata_, iter_);
    }

    bool
    equal(base_type const& impl) const override
    {
        auto const& other = dynamic_cast<txs_iter_impl const&>(impl);
        return iter_ == other.iter_;
    }

    void
    increment() override
    {
        ++iter_;
    }

    value_type
    dereference() const override
    {
        value_type result;
        {
            SerialIter sit(iter_->second.txn->slice());
            result.first = std::make_shared<STTx const>(sit);
        }
        if (metadata_)
        {
            SerialIter sit(iter_->second.meta->slice());
            result.second = std::make_shared<STObject const>(sit, sfMetadata);
        }
        return result;
    }
};

//------------------------------------------------------------------------------

OpenView::OpenView(OpenView const& rhs)
    : ReadView(rhs)
    , TxsRawView(rhs)
    , monotonic_resource_{std::make_unique<
          boost::container::pmr::monotonic_buffer_resource>(kilobytes(256))}
    , txs_{rhs.txs_, monotonic_resource_.get()}
    , rules_{rhs.rules_}
    , info_{rhs.info_}
    , base_{rhs.base_}
    , items_{rhs.items_}
    , hold_{rhs.hold_}
    , open_{rhs.open_} {};

OpenView::OpenView(
    open_ledger_t,
    ReadView const* base,
    Rules const& rules,
    std::shared_ptr<void const> hold)
    : monotonic_resource_{std::make_unique<
          boost::container::pmr::monotonic_buffer_resource>(kilobytes(256))}
    , txs_{monotonic_resource_.get()}
    , rules_(rules)
    , info_(base->info())
    , base_(base)
    , hold_(std::move(hold))
{
    info_.validated = false;
    info_.accepted = false;
    info_.seq = base_->info().seq + 1;
    info_.parentCloseTime = base_->info().closeTime;
    info_.parentHash = base_->info().hash;
}

OpenView::OpenView(ReadView const* base, std::shared_ptr<void const> hold)
    : monotonic_resource_{std::make_unique<
          boost::container::pmr::monotonic_buffer_resource>(kilobytes(256))}
    , txs_{monotonic_resource_.get()}
    , rules_(base->rules())
    , info_(base->info())
    , base_(base)
    , hold_(std::move(hold))
    , open_(base->open())
{
}

std::size_t
OpenView::txCount() const
{
    return txs_.size();
}

void
OpenView::apply(TxsRawView& to) const
{
    items_.apply(to);
    for (auto const& item : txs_)
        to.rawTxInsert(item.first, item.second.txn, item.second.meta);
}

//---

LedgerInfo const&
OpenView::info() const
{
    return info_;
}

Fees const&
OpenView::fees() const
{
    return base_->fees();
}

Rules const&
OpenView::rules() const
{
    return rules_;
}

bool
OpenView::exists(Keylet const& k) const
{
    return items_.exists(*base_, k);
}

auto
OpenView::succ(key_type const& key, boost::optional<key_type> const& last) const
    -> boost::optional<key_type>
{
    return items_.succ(*base_, key, last);
}

std::shared_ptr<SLE const>
OpenView::read(Keylet const& k) const
{
    return items_.read(*base_, k);
}

auto
OpenView::slesBegin() const -> std::unique_ptr<sles_type::iter_base>
{
    return items_.slesBegin(*base_);
}

auto
OpenView::slesEnd() const -> std::unique_ptr<sles_type::iter_base>
{
    return items_.slesEnd(*base_);
}

auto
OpenView::slesUpperBound(uint256 const& key) const
    -> std::unique_ptr<sles_type::iter_base>
{
    return items_.slesUpperBound(*base_, key);
}

auto
OpenView::txsBegin() const -> std::unique_ptr<txs_type::iter_base>
{
    return std::make_unique<txs_iter_impl>(!open(), txs_.cbegin());
}

auto
OpenView::txsEnd() const -> std::unique_ptr<txs_type::iter_base>
{
    return std::make_unique<txs_iter_impl>(!open(), txs_.cend());
}

bool
OpenView::txExists(key_type const& key) const
{
    return txs_.find(key) != txs_.end();
}

auto
OpenView::txRead(key_type const& key) const -> tx_type
{
    auto const iter = txs_.find(key);
    if (iter == txs_.end())
        return base_->txRead(key);
    auto const& item = iter->second;
    auto stx = std::make_shared<STTx const>(SerialIter{item.txn->slice()});
    decltype(tx_type::second) sto;
    if (item.meta)
        sto = std::make_shared<STObject const>(
            SerialIter{item.meta->slice()}, sfMetadata);
    else
        sto = nullptr;
    return {std::move(stx), std::move(sto)};
}

//---

void
OpenView::rawErase(std::shared_ptr<SLE> const& sle)
{
    items_.erase(sle);
}

void
OpenView::rawInsert(std::shared_ptr<SLE> const& sle)
{
    items_.insert(sle);
}

void
OpenView::rawReplace(std::shared_ptr<SLE> const& sle)
{
    items_.replace(sle);
}

void
OpenView::rawDestroyXRP(XRPAmount const& fee)
{
    items_.destroyXRP(fee);
    // VFALCO Deduct from info_.totalDrops ?
    //        What about child views?
}

//---

void
OpenView::rawTxInsert(
    key_type const& key,
    std::shared_ptr<Serializer const> const& txn,
    std::shared_ptr<Serializer const> const& metaData)
{
    auto const result = txs_.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(key),
        std::forward_as_tuple(txn, metaData));
    if (!result.second)
        LogicError("rawTxInsert: duplicate TX id" + to_string(key));
}

}  // namespace ripple
