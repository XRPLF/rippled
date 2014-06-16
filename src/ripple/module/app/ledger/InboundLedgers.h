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

#ifndef RIPPLE_INBOUNDLEDGERS_H
#define RIPPLE_INBOUNDLEDGERS_H

namespace ripple {

/** Manages the lifetime of inbound ledgers.

    @see InboundLedger
*/
class InboundLedgers
{
public:
    typedef beast::abstract_clock <std::chrono::seconds> clock_type;

    virtual ~InboundLedgers() = 0;

    // VFALCO TODO Make this a free function outside the class:
    //             std::unique_ptr <InboundLedger> make_InboundLedgers (...)
    //
    static InboundLedgers* New (clock_type& clock, beast::Stoppable& parent,
                                beast::insight::Collector::ptr const& collector);


    // VFALCO TODO Should this be called findOrAdd ?
    //
    virtual InboundLedger::pointer findCreate (uint256 const& hash, 
        std::uint32_t seq, InboundLedger::fcReason) = 0;

    virtual InboundLedger::pointer find (LedgerHash const& hash) = 0;

    virtual bool hasLedger (LedgerHash const& ledgerHash) = 0;

    virtual void dropLedger (LedgerHash const& ledgerHash) = 0;

    // VFALCO TODO Why is hash passed by value?
    // VFALCO TODO Remove the dependency on the Peer object.
    //
    virtual bool gotLedgerData (LedgerHash const& ledgerHash,
        std::shared_ptr<Peer>,
        std::shared_ptr <protocol::TMLedgerData>) = 0;

    virtual void doLedgerData (Job&, LedgerHash hash) = 0;

    virtual void gotStaleData (
        std::shared_ptr <protocol::TMLedgerData> packet) = 0;

    virtual int getFetchCount (int& timeoutCount) = 0;

    virtual void logFailure (uint256 const& h) = 0;

    virtual bool isFailure (uint256 const& h) = 0;

    virtual void clearFailures() = 0;

    virtual Json::Value getInfo() = 0;

    virtual void gotFetchPack (Job&) = 0;
    virtual void sweep () = 0;

    virtual void onStop() = 0;
};

} // ripple

#endif
