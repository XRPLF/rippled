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

#ifndef RIPPLE_LEDGER_BATCHSANDBOX_H_INCLUDED
#define RIPPLE_LEDGER_BATCHSANDBOX_H_INCLUDED

#include <xrpld/ledger/RawView.h>
#include <xrpld/ledger/detail/ApplyViewBase.h>

namespace ripple {

/** Discardable, editable view to a ledger.

    The sandbox inherits the flags of the base.

    @note Presented as ApplyView to clients.
*/
class BatchSandbox : public detail::ApplyViewBase
{
public:
    BatchSandbox() = delete;
    BatchSandbox(BatchSandbox const&) = delete;
    BatchSandbox&
    operator=(BatchSandbox&&) = delete;
    BatchSandbox&
    operator=(BatchSandbox const&) = delete;

    BatchSandbox(BatchSandbox&&) = default;

    BatchSandbox(ReadView const* base, ApplyFlags flags) : ApplyViewBase(base, flags)
    {
    }

    BatchSandbox(ApplyView const* base) : BatchSandbox(base, base->flags())
    {
    }

    void
    apply(RawView& to)
    {
        items_.apply(to);
    }

    // //
    // // RawView
    // //

    // void
    // rawErase(std::shared_ptr<SLE> const& sle) override;

    // void
    // rawInsert(std::shared_ptr<SLE> const& sle) override;

    // void
    // rawErase(uint256 const& key);

    // void
    // rawReplace(std::shared_ptr<SLE> const& sle) override;

    // void
    // rawDestroyXRP(XRPAmount const& fee) override;

    // //
    // // TxsRawView
    // //

    // void
    // rawTxInsert(
    //     uint256 const& key,
    //     std::shared_ptr<Serializer const> const& txn,
    //     std::shared_ptr<Serializer const> const& metaData) override;

    // void
    // apply(OpenView& openView)
    // {
    //     items_.apply(openView);
    //     for (auto const& item : openView.txs_)
    //         openView.rawTxInsert(item.first, item.second.txn, item.second.meta);
    // }
};

}  // namespace ripple

#endif
