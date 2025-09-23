//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2025 Ripple Labs Inc.

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

#ifndef RIPPLE_LEDGER_OPENVIEWSANDBOX_H_INCLUDED
#define RIPPLE_LEDGER_OPENVIEWSANDBOX_H_INCLUDED

#include <xrpl/ledger/OpenView.h>

#include <memory>

namespace ripple {

class OpenViewSandbox
{
private:
    OpenView& parent_;
    std::unique_ptr<OpenView> sandbox_;

public:
    using key_type = ReadView::key_type;

    OpenViewSandbox(OpenView& parent)
        : parent_(parent)
        , sandbox_(std::make_unique<OpenView>(batch_view, parent))
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

}  // namespace ripple

#endif
