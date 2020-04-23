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

#ifndef RIPPLE_APP_LEDGER_INBOUNDTRANSACTIONS_H_INCLUDED
#define RIPPLE_APP_LEDGER_INBOUNDTRANSACTIONS_H_INCLUDED

#include <ripple/beast/clock/abstract_clock.h>
#include <ripple/core/Stoppable.h>
#include <ripple/overlay/Peer.h>
#include <ripple/shamap/SHAMap.h>
#include <memory>

namespace ripple {

class Application;

/** Manages the acquisition and lifetime of transaction sets.
 */

class InboundTransactions
{
public:
    using clock_type = beast::abstract_clock<std::chrono::steady_clock>;

    InboundTransactions() = default;
    InboundTransactions(InboundTransactions const&) = delete;
    InboundTransactions&
    operator=(InboundTransactions const&) = delete;

    virtual ~InboundTransactions() = 0;

    /** Find and return a transaction set, or nullptr if it is missing.
     *
     * @param setHash The transaction set ID (digest of the SHAMap root node).
     * @param acquire Whether to fetch the transaction set from the network if
     * it is missing.
     * @return The transaction set with ID setHash, or nullptr if it is
     * missing.
     */
    virtual std::shared_ptr<SHAMap>
    getSet(uint256 const& setHash, bool acquire) = 0;

    /** Add a transaction set from a LedgerData message.
     *
     * @param setHash The transaction set ID (digest of the SHAMap root node).
     * @param peer The peer that sent the message.
     * @param message The LedgerData message.
     */
    virtual void
    gotData(
        uint256 const& setHash,
        std::shared_ptr<Peer> peer,
        std::shared_ptr<protocol::TMLedgerData> message) = 0;

    /** Add a transaction set.
     *
     * @param setHash The transaction set ID (should match set.getHash()).
     * @param set The transaction set.
     * @param acquired Whether this transaction set was acquired from a peer,
     * or constructed by ourself during consensus.
     */
    virtual void
    giveSet(
        uint256 const& setHash,
        std::shared_ptr<SHAMap> const& set,
        bool acquired) = 0;

    /** Informs the container if a new consensus round
     */
    virtual void
    newRound(std::uint32_t seq) = 0;
};

std::unique_ptr<InboundTransactions>
make_InboundTransactions(
    Application& app,
    Stoppable& parent,
    beast::insight::Collector::ptr const& collector,
    std::function<void(std::shared_ptr<SHAMap> const&, bool)> gotSet);

}  // namespace ripple

#endif
