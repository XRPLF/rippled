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

#ifndef RIPPLE_TX_APPLYCONTEXT_H_INCLUDED
#define RIPPLE_TX_APPLYCONTEXT_H_INCLUDED

#include <ripple/ledger/ApplyViewImpl.h>
#include <ripple/core/Config.h>
#include <ripple/protocol/STTx.h>
#include <beast/utility/Journal.h>
#include <boost/optional.hpp>
#include <utility>

namespace ripple {

// tx_enable_test

/** State information when applying a tx. */
class ApplyContext
{
public:
    explicit
    ApplyContext (OpenView& base,
        STTx const& tx, ApplyFlags flags,
            Config const& config,
                beast::Journal = {});

    STTx const& tx;
    Config const& config;
    beast::Journal const journal;

    ApplyView&
    view()
    {
        return *view_;
    }

    ApplyView const&
    view() const
    {
        return *view_;
    }

    // VFALCO Unfortunately this is necessary
    RawView&
    rawView()
    {
        return *view_;
    }

    /** Sets the DeliveredAmount field in the metadata */
    void
    deliver (STAmount const& amount)
    {
        view_->deliver(amount);
    }

    /** Discard changes and start fresh. */
    void
    discard();

    /** Apply the transaction result to the base. */
    void
    apply (TER);

    void
    destroyXRP (std::uint64_t feeDrops)
    {
        view_->rawDestroyXRP(feeDrops);
    }

private:
    OpenView& base_;
    ApplyFlags flags_;
    boost::optional<ApplyViewImpl> view_;
};

} // ripple

#endif
