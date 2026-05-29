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

std::optional<uint256>
ApplyViewBase::topOfBookFirstPage(Book const& book) const
{
    // Reads inside a sandbox that has already mutated `book` cannot use
    // the parent's cache: the parent's view of the top doesn't reflect
    // our buffered changes yet. Fall back to succ() in that case.
    if (dirtyBooks_.find(book) != dirtyBooks_.end())
        return std::nullopt;
    return base_->topOfBookFirstPage(book);
}

void
ApplyViewBase::recordTopOfBook(Book const& book, uint256 const& firstPageKey) const
{
    // Don't populate the parent cache from inside a dirty sandbox view —
    // our succ() result may reflect uncommitted mutations from the parent's
    // perspective.
    if (dirtyBooks_.find(book) != dirtyBooks_.end())
        return;
    base_->recordTopOfBook(book, firstPageKey);
}

void
ApplyViewBase::notifyOfferInserted(Book const& book, uint256 const& dirKey, uint256 const& offerKey)
    const
{
    dirtyBooks_.insert(book);
    pendingTopOfBookNotifications_.emplace_back(book, dirKey, offerKey, /*isDelete=*/false);
}

void
ApplyViewBase::notifyOfferDeleted(Book const& book, uint256 const& dirKey, uint256 const& offerKey)
    const
{
    dirtyBooks_.insert(book);
    pendingTopOfBookNotifications_.emplace_back(book, dirKey, offerKey, /*isDelete=*/true);
}

std::optional<std::vector<uint256>>
ApplyViewBase::orderedBook(Book const& book) const
{
    // Unlike topOfBookFirstPage, do NOT skip dirty books: the cursor is taken
    // once and iterated locally, and it self-heals any offer this sandbox has
    // buffered-deleted via peek()-null-skip in BookTip. So always delegate to
    // the (immutable-for-this-crossing) base index.
    return base_->orderedBook(book);
}

void
ApplyViewBase::flushTopOfBookNotifications() const
{
    for (auto const& note : pendingTopOfBookNotifications_)
    {
        if (note.isDelete)
            base_->notifyOfferDeleted(note.book, note.dirKey, note.offerKey);
        else
            base_->notifyOfferInserted(note.book, note.dirKey, note.offerKey);
    }
    pendingTopOfBookNotifications_.clear();
    dirtyBooks_.clear();
}

void
ApplyViewBase::discardTopOfBookNotifications() const noexcept
{
    pendingTopOfBookNotifications_.clear();
    dirtyBooks_.clear();
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
