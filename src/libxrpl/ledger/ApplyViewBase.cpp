#include <xrpl/ledger/detail/ApplyViewBase.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/Fees.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerHeader.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/XRPAmount.h>

#include <memory>
#include <optional>

namespace xrpl::detail {

ApplyViewBase::ApplyViewBase(ReadView const* base, ApplyFlags flags) : flags_(flags), base_(base)
{
}

//---

bool
ApplyViewBase::open() const
{
    return base_->open();
}

LedgerHeader const&
ApplyViewBase::header() const
{
    return base_->header();
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
ApplyViewBase::succ(key_type const& key, std::optional<key_type> const& last) const
    -> std::optional<key_type>
{
    return items_.succ(*base_, key, last);
}

SLE::const_pointer
ApplyViewBase::read(Keylet const& k) const
{
    return items_.read(*base_, k);
}

auto
ApplyViewBase::slesBegin() const -> std::unique_ptr<SlesType::iter_base>
{
    return base_->slesBegin();
}

auto
ApplyViewBase::slesEnd() const -> std::unique_ptr<SlesType::iter_base>
{
    return base_->slesEnd();
}

auto
ApplyViewBase::slesUpperBound(uint256 const& key) const -> std::unique_ptr<SlesType::iter_base>
{
    return base_->slesUpperBound(key);
}

auto
ApplyViewBase::txsBegin() const -> std::unique_ptr<TxsType::iter_base>
{
    return base_->txsBegin();
}

auto
ApplyViewBase::txsEnd() const -> std::unique_ptr<TxsType::iter_base>
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

SLE::pointer
ApplyViewBase::peek(Keylet const& k)
{
    return items_.peek(*base_, k);
}

void
ApplyViewBase::erase(SLE::ref sle)
{
    items_.erase(*base_, sle);
}

void
ApplyViewBase::insert(SLE::ref sle)
{
    items_.insert(*base_, sle);
}

void
ApplyViewBase::update(SLE::ref sle)
{
    items_.update(*base_, sle);
}

//---

void
ApplyViewBase::rawErase(SLE::ref sle)
{
    items_.rawErase(*base_, sle);
}

void
ApplyViewBase::rawInsert(SLE::ref sle)
{
    items_.insert(*base_, sle);
}

void
ApplyViewBase::rawReplace(SLE::ref sle)
{
    items_.replace(*base_, sle);
}

void
ApplyViewBase::rawDestroyXRP(XRPAmount const& fee)
{
    items_.destroyXRP(fee);
}

}  // namespace xrpl::detail
