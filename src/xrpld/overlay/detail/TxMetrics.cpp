/** @file
 *  Implements rolling-average metrics for the transaction reduce-relay feature.
 *
 *  Three cooperating types are defined in `xrpl::metrics`: `SingleMetrics`
 *  (1-second-amortized rolling average over a 30-slot circular buffer),
 *  `MultipleMetrics` (pairs two `SingleMetrics` for count + byte size), and
 *  `TxMetrics` (top-level aggregator covering five protocol message types plus
 *  peer-selection diagnostics). All mutable state in `TxMetrics` is guarded by
 *  its own `mutex`; subordinate types carry no locks of their own.
 */
#include <xrpld/overlay/detail/TxMetrics.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/jss.h>

#include <xrpl.pb.h>

#include <chrono>
#include <cstdint>
#include <mutex>
#include <numeric>
#include <string>

namespace xrpl::metrics {

/** Dispatch an inbound protocol message to the appropriate `MultipleMetrics`
 *  bucket and record its byte size.
 *
 *  Only the five message types directly relevant to reduce-relay decisions
 *  are tracked. All other types are silently ignored via `default: return`
 *  so that future protocol additions do not produce errors or assertions.
 *
 *  @param type Protocol message type from the protobuf enum.
 *  @param val  Message size in bytes.
 */
void
TxMetrics::addMetrics(protocol::MessageType type, std::uint32_t val)
{
    auto add = [&](auto& m, std::uint32_t val) {
        std::scoped_lock const lock(mutex);
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
TxMetrics::addMetrics(std::uint32_t selected, std::uint32_t suppressed, std::uint32_t notenabled)
{
    std::scoped_lock const lock(mutex);
    selectedPeers.addMetrics(selected);
    suppressedPeers.addMetrics(suppressed);
    notEnabled.addMetrics(notenabled);
}

void
TxMetrics::addMetrics(std::uint32_t missing)
{
    std::scoped_lock const lock(mutex);
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

/** Accumulate a sample and, once per second, condense the interval into the
 *  30-slot circular rolling average.
 *
 *  Each call adds `val` to `accum` and increments the sample counter `N`.
 *  When at least one second has elapsed since `intervalStart`, the interval
 *  is closed: a single representative value is computed and pushed into the
 *  30-entry `rollingAvgAggregate` circular buffer (displacing the oldest
 *  entry when full), then `rollingAvg` is recalculated as the arithmetic
 *  mean of all buffer entries.
 *
 *  The representative value for the closed interval depends on `perTimeUnit`:
 *  - `true` (default): `accum / elapsed_seconds` — a **rate per second**,
 *      suitable for cumulative quantities such as message counts or byte totals.
 *  - `false`: `accum / N` — a **per-observation average**, suitable for
 *      quantities like "peers selected per transaction" where each call
 *      represents one independent decision.
 *
 *  Intervals with no activity push `0` implicitly through the circular buffer,
 *  naturally diluting `rollingAvg` toward zero over the trailing 30 seconds
 *  without any explicit decay step.
 *
 *  @param val Sample value to accumulate (message count, byte size, or peer
 *      count depending on context).
 *  @note This method is not thread-safe; callers must hold `TxMetrics::mutex`
 *      before invoking it.
 */
void
SingleMetrics::addMetrics(std::uint32_t val)
{
    using namespace std::chrono_literals;
    accum += val;
    N++;
    auto const timeElapsed = clock_type::now() - intervalStart;
    auto const timeElapsedInSecs = std::chrono::duration_cast<std::chrono::seconds>(timeElapsed);

    if (timeElapsedInSecs >= 1s)
    {
        auto const avg = accum / (perTimeUnit ? timeElapsedInSecs.count() : N);
        rollingAvgAggregate.push_back(avg);

        auto const total =
            std::accumulate(rollingAvgAggregate.begin(), rollingAvgAggregate.end(), 0ull);
        rollingAvg = total / rollingAvgAggregate.size();

        intervalStart = clock_type::now();
        accum = 0;
        N = 0;
    }
}

/** Serialize all rolling averages to a JSON object for RPC consumers.
 *
 *  Emits thirteen fields (five message-type count/size pairs plus four
 *  peer-selection diagnostics) keyed by `jss::txr_*` constants. All values
 *  are serialized as strings via `std::to_string` rather than JSON integers;
 *  callers must parse them accordingly.
 *
 *  Acquires `mutex` for the duration of the snapshot so the returned object
 *  is self-consistent even when writer strands are active concurrently.
 *
 *  @return A JSON object containing the current rolling-average snapshot.
 *  @note The values reflect the trailing 30-second window; a metric with no
 *      recent activity reports `"0"` as older non-zero slots age out.
 */
json::Value
TxMetrics::json() const
{
    std::scoped_lock const l(mutex);

    json::Value ret(json::ValueType::Object);

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

}  // namespace xrpl::metrics
