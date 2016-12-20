//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2016 Ripple Labs Inc.

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

#ifndef RIPPLE_APP_CONSENSUS_RCLCXLEDGER_H_INCLUDED
#define RIPPLE_APP_CONSENSUS_RCLCXLEDGER_H_INCLUDED

#include <ripple/app/ledger/Ledger.h>
#include <ripple/ledger/ReadView.h>
#include <ripple/app/ledger/LedgerToJson.h>
#include <ripple/protocol/RippleLedgerHash.h>
#include <memory>

namespace ripple {

/** Represents a ledger in RCLConsensus.

    RCLCxLedger is a thin wrapper over `std::shared_ptr<Ledger const>`.
*/
class RCLCxLedger
{
public:
    //! Unique identifier of a ledger
    using ID = LedgerHash;

    /** Default constructor

        TODO: This may not be needed if we ensure RCLConsensus is handed a valid
        ledger in its constructor.  Its bad now because other members are not
        checking whether the ledger is valid.
    */
    RCLCxLedger() = default;

    /** Constructor

        @param l The ledger to wrap.
    */
    RCLCxLedger(std::shared_ptr<Ledger const> const & l) : ledger_{ l } {}

    //! Sequence number of the ledger.
    auto const &
    seq() const
    {
        return ledger_->info().seq;
    }

    //! Unique identifier (hash) of this ledger.
    auto const &
    id() const
    {
        return ledger_->info().hash;
    }

    //! Unique identifier (hash) of this ledger's parent.
    auto const &
    parentID() const
    {
        return ledger_->info().parentHash;
    }

    //! Resolution used when calculating this ledger's close time.
    auto
    closeTimeResolution() const
    {
        return ledger_->info().closeTimeResolution;
    }

    //! Whether consensus process agreed on close time of the ledger.
    bool
    closeAgree() const
    {
        return ripple::getCloseAgree(ledger_->info());
    }

    //! The close time of this ledger
    auto
    closeTime() const
    {
        return ledger_->info().closeTime;
    }

    //! The close time of this ledger's parent.
    auto
    parentCloseTime() const
    {
        return ledger_->info().parentCloseTime;
    }

    //! JSON representation of this ledger.
    Json::Value
    getJson() const
    {
        return ripple::getJson(*ledger_);
    }

    /** The ledger instance.

        TODO: Make this shared_ptr<ReadView const> .. requires ability to create
        a new ledger from a readView?
    */
    std::shared_ptr<Ledger const> ledger_;

};

}
#endif
