#pragma once

#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/detail/ApplyStateTable.h>
#include <xrpl/protocol/Book.h>
#include <xrpl/protocol/XRPAmount.h>

#include <unordered_set>
#include <utility>
#include <vector>

namespace xrpl::detail {

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
    [[nodiscard]] bool
    open() const override;

    [[nodiscard]] LedgerHeader const&
    header() const override;

    [[nodiscard]] Fees const&
    fees() const override;

    [[nodiscard]] Rules const&
    rules() const override;

    [[nodiscard]] bool
    exists(Keylet const& k) const override;

    [[nodiscard]] std::optional<key_type>
    succ(key_type const& key, std::optional<key_type> const& last = std::nullopt) const override;

    [[nodiscard]] std::shared_ptr<SLE const>
    read(Keylet const& k) const override;

    // Top-of-book cache hooks — delegated to the wrapped base view so
    // sandboxed views share the underlying open-ledger cache.

    [[nodiscard]] std::optional<uint256>
    topOfBookFirstPage(Book const& book) const override;

    void
    recordTopOfBook(Book const& book, uint256 const& firstPageKey) const override;

    void
    notifyOfferInserted(Book const& book, uint256 const& dirKey, uint256 const& offerKey)
        const override;

    void
    notifyOfferDeleted(Book const& book, uint256 const& dirKey, uint256 const& offerKey)
        const override;

    [[nodiscard]] std::optional<std::vector<uint256>>
    orderedBook(Book const& book) const override;

    [[nodiscard]] std::unique_ptr<SlesType::iter_base>
    slesBegin() const override;

    [[nodiscard]] std::unique_ptr<SlesType::iter_base>
    slesEnd() const override;

    [[nodiscard]] std::unique_ptr<SlesType::iter_base>
    slesUpperBound(uint256 const& key) const override;

    [[nodiscard]] std::unique_ptr<TxsType::iter_base>
    txsBegin() const override;

    [[nodiscard]] std::unique_ptr<TxsType::iter_base>
    txsEnd() const override;

    [[nodiscard]] bool
    txExists(key_type const& key) const override;

    [[nodiscard]] tx_type
    txRead(key_type const& key) const override;

    // ApplyView

    [[nodiscard]] ApplyFlags
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

    /** Flush buffered top-of-book notifications to the wrapped base view.

        Called by `Sandbox::apply` (and similar commit points) after the
        state table itself has been applied. Notifications buffered during
        the sandbox's lifetime are replayed against `base_` in insertion
        order so the parent cache only sees changes that actually commit.
    */
    void
    flushTopOfBookNotifications() const;

    /** Discard buffered notifications (e.g. when a sandbox is dropped
        without applying). Safe to call multiple times.
    */
    void
    discardTopOfBookNotifications() const noexcept;

protected:
    ApplyFlags flags_;
    ReadView const* base_;
    detail::ApplyStateTable items_;

    // Top-of-book cache notifications are buffered here for the lifetime
    // of the sandbox and only flushed to `base_` on `apply()`. This keeps
    // rolled-back transactions (e.g. FillOrKill via the sbCancel branch
    // of OfferCreate) from polluting the parent's cache.
    //
    // `dirtyBooks_` records every book mutated by buffered notifications;
    // reads against `topOfBookFirstPage` skip the cache for these books so
    // we never observe our own un-committed state. Outside of the dirty
    // set, the parent's cache is trusted as usual.
    struct OfferNote
    {
        Book book;
        uint256 dirKey;
        uint256 offerKey;
        bool isDelete;
    };
    mutable std::vector<OfferNote> pendingTopOfBookNotifications_;
    mutable std::unordered_set<Book> dirtyBooks_;
};

}  // namespace xrpl::detail
