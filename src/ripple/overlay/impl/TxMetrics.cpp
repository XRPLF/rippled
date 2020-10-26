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

#include "ripple/overlay/impl/TxMetrics.h"
#include "ripple/protocol/jss.h"

#include <numeric>

namespace ripple {

namespace metrics {

void
TxMetrics::addMetrics(protocol::MessageType type, std::uint32_t val)
{
    auto add = [&](auto& m, std::uint32_t val) {
        std::lock_guard lock(mutex);
        m.addMetrics(val);
    };

    switch (type)
    {
        case protocol::MessageType::mtTRANSACTION:
            add(tx, val);
            break;
        case protocol::MessageType::mtHAVE_TRANSACTIONS:
            add(haveTx, val);
            break;
        case protocol::MessageType::mtGET_LEDGER:
            add(getLedger, val);
            break;
        case protocol::MessageType::mtLEDGER_DATA:
            add(ledgerData, val);
            break;
        case protocol::MessageType::mtTRANSACTIONS:
            add(transactions, val);
            break;
        default:
            return;
    }
}

void
TxMetrics::addMetrics(
    std::uint32_t selected,
    std::uint32_t suppressed,
    std::uint32_t notenabled)
{
    std::lock_guard lock(mutex);
    selectedPeers.addMetrics(selected);
    suppressedPeers.addMetrics(suppressed);
    notEnabled.addMetrics(notenabled);
}

void
TxMetrics::addMetrics(std::uint32_t missing)
{
    std::lock_guard lock(mutex);
    missingTx.addMetrics(missing);
}

void
MultipleMetrics::addMetrics(std::uint32_t val2)
{
    addMetrics(1, val2);
}

void
MultipleMetrics::addMetrics(std::uint32_t val1, std::uint32_t val2)
{
    m1.addMetrics(val1);
    m2.addMetrics(val2);
}

void
SingleMetrics::addMetrics(std::uint32_t val)
{
    using namespace std::chrono_literals;
    accum += val;
    N++;
    auto const timeElapsed = clock_type::now() - intervalStart;
    auto const timeElapsedInSecs =
        std::chrono::duration_cast<std::chrono::seconds>(timeElapsed);

    if (timeElapsedInSecs >= 1s)
    {
        auto const avg = accum / (perTimeUnit ? timeElapsedInSecs.count() : N);
        rollingAvgAggreg.push_back(avg);

        auto const total = std::accumulate(
            rollingAvgAggreg.begin(), rollingAvgAggreg.end(), 0ull);
        rollingAvg = total / rollingAvgAggreg.size();

        intervalStart = clock_type::now();
        accum = 0;
        N = 0;
    }
}

Json::Value
TxMetrics::json() const
{
    std::lock_guard l(mutex);

    Json::Value ret(Json::objectValue);

    ret[jss::txr_tx_cnt] = std::to_string(tx.m1.rollingAvg);
    ret[jss::txr_tx_sz] = std::to_string(tx.m2.rollingAvg);

    ret[jss::txr_have_txs_cnt] = std::to_string(haveTx.m1.rollingAvg);
    ret[jss::txr_have_txs_sz] = std::to_string(haveTx.m2.rollingAvg);

    ret[jss::txr_get_ledger_cnt] = std::to_string(getLedger.m1.rollingAvg);
    ret[jss::txr_get_ledger_sz] = std::to_string(getLedger.m2.rollingAvg);

    ret[jss::txr_ledger_data_cnt] = std::to_string(ledgerData.m1.rollingAvg);
    ret[jss::txr_ledger_data_sz] = std::to_string(ledgerData.m2.rollingAvg);

    ret[jss::txr_transactions_cnt] = std::to_string(transactions.m1.rollingAvg);
    ret[jss::txr_transactions_sz] = std::to_string(transactions.m2.rollingAvg);

    ret[jss::txr_selected_cnt] = std::to_string(selectedPeers.rollingAvg);

    ret[jss::txr_suppressed_cnt] = std::to_string(suppressedPeers.rollingAvg);

    ret[jss::txr_not_enabled_cnt] = std::to_string(notEnabled.rollingAvg);

    ret[jss::txr_missing_tx_freq] = std::to_string(missingTx.rollingAvg);

    return ret;
}

}  // namespace metrics

}  // namespace ripple
