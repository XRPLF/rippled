/** @file
 *  Implements `ApplyViewBase`, the shared concrete base for all mutable
 *  ledger views that buffer per-transaction state changes.
 *
 *  The file contains only thin delegation: every `ReadView` query that does
 *  not need awareness of pending writes forwards directly to `base_`, while
 *  every change-aware read and every mutation routes through `items_` (an
 *  `ApplyStateTable`).
 *
 *  Two design choices here are non-obvious:
 *  - `slesBegin`, `slesEnd`, and `slesUpperBound` bypass `items_` and forward
 *    to `base_` directly.  The apply phase never needs to iterate over its own
 *    pending inserts; bypassing the buffer keeps SLE iteration consistent with
 *    the base snapshot and avoids materialising the full merged key space.
 *  - `rawInsert` and `rawErase` use *different* `ApplyStateTable` entry points.
 *    `rawInsert` calls `items_.insert()` — the same validated path as the
 *    high-level `insert()`.  `rawErase` calls `items_.rawErase()`, which
 *    bypasses the pointer-identity ownership check enforced by `items_.erase()`.
 *    This asymmetry exists because `RawView` callers (e.g., `Sandbox::apply`)
 *    flush changes from another view's table and cannot satisfy the
 *    same-pointer ownership invariant.
 */
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

std::shared_ptr<SLE const>
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

}  // namespace xrpl::detail
