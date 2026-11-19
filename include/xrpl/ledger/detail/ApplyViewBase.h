#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/RawView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/detail/ApplyStateTable.h>
#include <xrpl/protocol/Fees.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerHeader.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/XRPAmount.h>

#include <memory>
#include <optional>
#include <set>

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

    [[nodiscard]] SLE::const_pointer
    read(Keylet const& k) const override;

    [[nodiscard]] std::unique_ptr<SlesType::IterBase>
    slesBegin() const override;

    [[nodiscard]] std::unique_ptr<SlesType::IterBase>
    slesEnd() const override;

    [[nodiscard]] std::unique_ptr<SlesType::IterBase>
    slesUpperBound(UInt256 const& key) const override;

    [[nodiscard]] std::unique_ptr<TxsType::IterBase>
    txsBegin() const override;

    [[nodiscard]] std::unique_ptr<TxsType::IterBase>
    txsEnd() const override;

    [[nodiscard]] bool
    txExists(key_type const& key) const override;

    [[nodiscard]] TxType
    txRead(key_type const& key) const override;

    // ApplyView

    [[nodiscard]] ApplyFlags
    flags() const override;

    SLE::pointer
    peek(Keylet const& k) override;

    void
    erase(SLE::Ref sle) override;

    void
    erasePersistent(SLE::Ref sle) override;

    void
    insert(SLE::Ref sle) override;

    void
    update(SLE::Ref sle) override;

    // RawView

    void
    rawErase(SLE::Ref sle) override;

    void
    rawInsert(SLE::Ref sle) override;

    void
    rawReplace(SLE::Ref sle) override;

    void
    rawDestroyXRP(XRPAmount const& feeDrops) override;

    /**
     * Record a key as a persistent delete without erasing anything.
     *
     * Use this when the entry was already erased in this view by a child
     * view that was applied to it.
     */
    void
    markPersistent(uint256 const& key);

    /**
     * Keys recorded as persistent deletes in this view.
     */
    [[nodiscard]] std::set<uint256> const&
    persistentKeys() const
    {
        return persistent_;
    }

protected:
    /**
     * Add the persistent delete keys of this view to `to`, if `to` is
     * also an ApplyViewBase. Call this from apply().
     */
    void
    applyPersistent(RawView& to) const;

    ApplyFlags flags_;
    ReadView const* base_;
    detail::ApplyStateTable items_;
    std::set<uint256> persistent_;
};

}  // namespace xrpl::detail
