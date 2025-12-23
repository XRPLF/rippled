#pragma once

#include <xrpl/ledger/OpenView.h>

#include <memory>

namespace xrpl {

class OpenViewSandbox
{
private:
    OpenView& parent_;
    std::unique_ptr<OpenView> sandbox_;

public:
    using key_type = ReadView::key_type;

    OpenViewSandbox(OpenView& parent)
        : parent_(parent), sandbox_(std::make_unique<OpenView>(batch_view, parent))
    {
    }

    void
    rawErase(std::shared_ptr<SLE> const& sle)
    {
        sandbox_->rawErase(sle);
    }

    void
    rawInsert(std::shared_ptr<SLE> const& sle)
    {
        sandbox_->rawInsert(sle);
    }

    void
    rawReplace(std::shared_ptr<SLE> const& sle)
    {
        sandbox_->rawReplace(sle);
    }

    void
    rawDestroyXRP(XRPAmount const& fee)
    {
        sandbox_->rawDestroyXRP(fee);
    }

    void
    rawTxInsert(
        key_type const& key,
        std::shared_ptr<Serializer const> const& txn,
        std::shared_ptr<Serializer const> const& metaData)
    {
        sandbox_->rawTxInsert(key, txn, metaData);
    }

    void
    commit()
    {
        sandbox_->apply(parent_);
        sandbox_ = std::make_unique<OpenView>(batch_view, parent_);
    }

    void
    discard()
    {
        sandbox_ = std::make_unique<OpenView>(batch_view, parent_);
    }

    OpenView const&
    view() const
    {
        return *sandbox_;
    }

    OpenView&
    view()
    {
        return *sandbox_;
    }
};

}  // namespace xrpl
