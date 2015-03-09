//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2015 Ripple Labs Inc.

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

#ifndef RIPPLE_INBOUNDTRANSACTIONS_H
#define RIPPLE_INBOUNDTRANSACTIONS_H

#include <ripple/overlay/Peer.h>
#include <ripple/shamap/SHAMap.h>
#include <beast/chrono/abstract_clock.h>
#include <beast/cxx14/memory.h> // <memory>
#include <beast/threads/Stoppable.h>

namespace ripple {

/** Manages the acquisition and lifetime of transaction sets.
*/

class InboundTransactions
{
public:
    typedef beast::abstract_clock <std::chrono::steady_clock> clock_type;

    InboundTransactions() = default;
    InboundTransactions(InboundTransactions const&) = delete;
    InboundTransactions& operator=(InboundTransactions const&) = delete;

    virtual ~InboundTransactions() = 0;

    /** Retrieves a transaction set by hash
    */
    virtual std::shared_ptr <SHAMap> getSet (
        uint256 const& setHash,
        bool acquire) = 0;

    /** Gives data to an inbound transaction set
    */
    virtual void gotData (uint256 const& setHash,
        std::shared_ptr <Peer>,
        std::shared_ptr <protocol::TMLedgerData>) = 0;

    /** Gives set to the container
    */
    virtual void giveSet (uint256 const& setHash,
        std::shared_ptr <SHAMap> const& set,
        bool acquired) = 0;

    /** Informs the container if a new consensus round
    */
    virtual void newRound (std::uint32_t seq) = 0;

    virtual Json::Value getInfo() = 0;

    virtual void onStop() = 0;
};

std::unique_ptr <InboundTransactions>
make_InboundTransactions (
    InboundTransactions::clock_type& clock,
    beast::Stoppable& parent,
    beast::insight::Collector::ptr const& collector,
    std::function
        <void (uint256 const&,
            std::shared_ptr <SHAMap> const&)> gotSet);


} // ripple

#endif
