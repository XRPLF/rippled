#include <xrpl/ledger/OpenView.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/ledger/RawView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/Fees.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/basics/TraceLog.h>

#include <boost/container/pmr/monotonic_buffer_resource.hpp>

#include <cstddef>
#include <memory>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace xrpl {

class OpenView::TxsIterImpl : public TxsType::iter_base
{
private:
    bool metadata_;
    txs_map::const_iterator iter_;

public:
    explicit TxsIterImpl(bool metadata, txs_map::const_iterator iter)
        : metadata_(metadata), iter_(iter)
    {
    }

    [[nodiscard]] std::unique_ptr<base_type>
    copy() const override
    {
    TRACE_FUNC();
        return std::make_unique<TxsIterImpl>(metadata_, iter_);
    }

    [[nodiscard]] bool
    equal(base_type const& impl) const override
    {
    TRACE_FUNC();
        if (auto const p = dynamic_cast<TxsIterImpl const*>(&impl))
            return iter_ == p->iter_;
        return false;
    }

    void
    increment() override
    {
    TRACE_FUNC();
        ++iter_;
    }

    [[nodiscard]] value_type
    dereference() const override
    {
    TRACE_FUNC();
        value_type result;
        {
    TRACE_FUNC();
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
    , monotonic_resource_{std::make_unique<boost::container::pmr::monotonic_buffer_resource>(
          kINITIAL_BUFFER_SIZE)}
    , txs_{rhs.txs_, monotonic_resource_.get()}
    , rules_{rhs.rules_}
    , header_{rhs.header_}
    , base_{rhs.base_}
    , items_{rhs.items_}
    , hold_{rhs.hold_}
    , open_{rhs.open_} {};

OpenView::OpenView(OpenLedgerT, ReadView const* base, Rules rules, std::shared_ptr<void const> hold)
    : monotonic_resource_{
          std::make_unique<boost::container::pmr::monotonic_buffer_resource>(kINITIAL_BUFFER_SIZE)}
    , txs_{monotonic_resource_.get()}
    , rules_(std::move(rules))
    , header_(base->header())
    , base_(base)
    , hold_(std::move(hold))
{
    TRACE_FUNC();
    header_.validated = false;
    header_.accepted = false;
    header_.seq = base_->header().seq + 1;
    header_.parentCloseTime = base_->header().closeTime;
    header_.parentHash = base_->header().hash;
}

OpenView::OpenView(ReadView const* base, std::shared_ptr<void const> hold)
    : monotonic_resource_{
          std::make_unique<boost::container::pmr::monotonic_buffer_resource>(kINITIAL_BUFFER_SIZE)}
    , txs_{monotonic_resource_.get()}
    , rules_(base->rules())
    , header_(base->header())
    , base_(base)
    , hold_(std::move(hold))
    , open_(base->open())
{
}

std::size_t
OpenView::txCount() const
{
    TRACE_FUNC();
    return baseTxCount_ + txs_.size();
}

void
OpenView::apply(TxsRawView& to) const
{
    TRACE_FUNC();
    items_.apply(to);
    for (auto const& item : txs_)
        to.rawTxInsert(item.first, item.second.txn, item.second.meta);
}

//---

LedgerHeader const&
OpenView::header() const
{
    TRACE_FUNC();
    return header_;
}

Fees const&
OpenView::fees() const
{
    TRACE_FUNC();
    return base_->fees();
}

Rules const&
OpenView::rules() const
{
    TRACE_FUNC();
    return rules_;
}

bool
OpenView::exists(Keylet const& k) const
{
    TRACE_FUNC();
    return items_.exists(*base_, k);
}

auto
OpenView::succ(key_type const& key, std::optional<key_type> const& last) const
    -> std::optional<key_type>
{
    TRACE_FUNC();
    return items_.succ(*base_, key, last);
}

std::shared_ptr<SLE const>
OpenView::read(Keylet const& k) const
{
    TRACE_FUNC();
    return items_.read(*base_, k);
}

auto
OpenView::slesBegin() const -> std::unique_ptr<SlesType::iter_base>
{
    TRACE_FUNC();
    return items_.slesBegin(*base_);
}

auto
OpenView::slesEnd() const -> std::unique_ptr<SlesType::iter_base>
{
    TRACE_FUNC();
    return items_.slesEnd(*base_);
}

auto
OpenView::slesUpperBound(uint256 const& key) const -> std::unique_ptr<SlesType::iter_base>
{
    TRACE_FUNC();
    return items_.slesUpperBound(*base_, key);
}

auto
OpenView::txsBegin() const -> std::unique_ptr<TxsType::iter_base>
{
    TRACE_FUNC();
    return std::make_unique<TxsIterImpl>(!open(), txs_.cbegin());
}

auto
OpenView::txsEnd() const -> std::unique_ptr<TxsType::iter_base>
{
    TRACE_FUNC();
    return std::make_unique<TxsIterImpl>(!open(), txs_.cend());
}

bool
OpenView::txExists(key_type const& key) const
{
    TRACE_FUNC();
    return txs_.contains(key);
}

auto
OpenView::txRead(key_type const& key) const -> tx_type
{
    TRACE_FUNC();
    auto const iter = txs_.find(key);
    if (iter == txs_.end())
        return base_->txRead(key);
    auto const& item = iter->second;
    auto stx = std::make_shared<STTx const>(SerialIter{item.txn->slice()});
    decltype(tx_type::second) sto;
    if (item.meta)
    {
        sto = std::make_shared<STObject const>(SerialIter{item.meta->slice()}, sfMetadata);
    }
    else
    {
        sto = nullptr;
    }
    return {std::move(stx), std::move(sto)};
}

//---

void
OpenView::rawErase(std::shared_ptr<SLE> const& sle)
{
    TRACE_FUNC();
    items_.erase(sle);
}

void
OpenView::rawInsert(std::shared_ptr<SLE> const& sle)
{
    TRACE_FUNC();
    items_.insert(sle);
}

void
OpenView::rawReplace(std::shared_ptr<SLE> const& sle)
{
    TRACE_FUNC();
    items_.replace(sle);
}

void
OpenView::rawDestroyXRP(XRPAmount const& fee)
{
    TRACE_FUNC();
    items_.destroyXRP(fee);
    // VFALCO Deduct from header_.totalDrops ?
    //        What about child views?
}

//---

void
OpenView::rawTxInsert(
    key_type const& key,
    std::shared_ptr<Serializer const> const& txn,
    std::shared_ptr<Serializer const> const& metaData)
{
    TRACE_FUNC();
    auto const result = txs_.emplace(
        std::piecewise_construct, std::forward_as_tuple(key), std::forward_as_tuple(txn, metaData));
    if (!result.second)
        Throw<std::logic_error>("rawTxInsert: duplicate TX id: " + to_string(key));
}

}  // namespace xrpl
