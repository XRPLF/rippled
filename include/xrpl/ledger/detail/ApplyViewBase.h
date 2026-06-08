#pragma once

#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/detail/ApplyStateTable.h>
#include <xrpl/protocol/XRPAmount.h>

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

    SLE::pointer
    peek(Keylet const& k) override;

    void
    erase(SLE::ref sle) override;

    void
    insert(SLE::ref sle) override;

    void
    update(SLE::ref sle) override;

    // RawView

    void
    rawErase(SLE::ref sle) override;

    void
    rawInsert(SLE::ref sle) override;

    void
    rawReplace(SLE::ref sle) override;

    void
    rawDestroyXRP(XRPAmount const& feeDrops) override;

protected:
    ApplyFlags flags_;
    ReadView const* base_;
    detail::ApplyStateTable items_;
};

}  // namespace xrpl::detail
