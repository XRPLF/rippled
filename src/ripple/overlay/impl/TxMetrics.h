//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2020 Ripple Labs Inc.

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

#ifndef RIPPLE_OVERLAY_TXMETRICS_H_INCLUDED
#define RIPPLE_OVERLAY_TXMETRICS_H_INCLUDED

#include "ripple/json/json_value.h"
#include "ripple/protocol/messages.h"

#include <boost/circular_buffer.hpp>

#include <chrono>
#include <mutex>

namespace ripple {

namespace metrics {

/** Run single metrics rolling average. Can be either average of a value
    per second or average of a value's sample per second. For instance,
    for transaction it makes sense to have transaction bytes and count
    per second, but for a number of selected peers to relay per transaction
    it makes sense to have sample's average.
 */
struct SingleMetrics
{
    /** Class constructor
       @param ptu if true then calculate metrics per second, otherwise
           sample's average
     */
    SingleMetrics(bool ptu = true) : perTimeUnit(ptu)
    {
    }
    using clock_type = std::chrono::steady_clock;
    clock_type::time_point intervalStart{clock_type::now()};
    std::uint64_t accum{0};
    std::uint64_t rollingAvg{0};
    std::uint32_t N{0};
    bool perTimeUnit{true};
    boost::circular_buffer<std::uint64_t> rollingAvgAggreg{30, 0ull};
    /** Add metrics value
     * @param val metrics value, either bytes or count
     */
    void
    addMetrics(std::uint32_t val);
};

/** Run two metrics. For instance message size and count for
    protocol messages. */
struct MultipleMetrics
{
    MultipleMetrics(bool ptu1 = true, bool ptu2 = true) : m1(ptu1), m2(ptu2)
    {
    }

    SingleMetrics m1;
    SingleMetrics m2;
    /** Add metrics to m2. m1 in this case aggregates the frequency.
       @param val2 m2 metrics value
     */
    void
    addMetrics(std::uint32_t val2);
    /** Add metrics to m1 and m2.
     * @param val1 m1 metrics value
     * @param val2 m2 metrics value
     */
    void
    addMetrics(std::uint32_t val1, std::uint32_t val2);
};

/** Run transaction reduce-relay feature related metrics */
struct TxMetrics
{
    mutable std::mutex mutex;
    // TMTransaction bytes and count per second
    MultipleMetrics tx;
    // TMHaveTransactions bytes and count per second
    MultipleMetrics haveTx;
    // TMGetLedger bytes and count per second
    MultipleMetrics getLedger;
    // TMLedgerData bytes and count per second
    MultipleMetrics ledgerData;
    // TMTransactions bytes and count per second
    MultipleMetrics transactions;
    // Peers selected to relay in each transaction sample average
    SingleMetrics selectedPeers{false};
    // Peers suppressed to relay in each transaction sample average
    SingleMetrics suppressedPeers{false};
    // Peers with tx reduce-relay feature not enabled
    SingleMetrics notEnabled{false};
    // TMTransactions number of transactions count per second
    SingleMetrics missingTx;
    /** Add protocol message metrics
       @param type protocol message type
       @param val message size in bytes
     */
    void
    addMetrics(protocol::MessageType type, std::uint32_t val);
    /** Add peers selected for relaying and suppressed peers metrics.
       @param selected number of selected peers to relay
       @param suppressed number of suppressed peers
       @param notEnabled number of peers with tx reduce-relay featured disabled
     */
    void
    addMetrics(
        std::uint32_t selected,
        std::uint32_t suppressed,
        std::uint32_t notEnabled);
    /** Add number of missing transactions that a node requested
       @param missing number of missing transactions
     */
    void
    addMetrics(std::uint32_t missing);
    /** Get json representation of the metrics
       @return json object
     */
    Json::Value
    json() const;
};

}  // namespace metrics

}  // namespace ripple

#endif