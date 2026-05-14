#pragma once

/** @file
 *  Rolling-average telemetry structures for the transaction reduce-relay feature.
 *
 *  Defines three cooperating types in `xrpl::metrics`:
 *  - `SingleMetrics` — primitive one-second amortised rolling average over a
 *    30-slot circular buffer, supporting both throughput rates and sample means.
 *  - `MultipleMetrics` — pairs two `SingleMetrics` instances (typically message
 *    count and byte size) behind a single `addMetrics` call site.
 *  - `TxMetrics` — top-level aggregator that owns one `MultipleMetrics` per
 *    tracked protocol message type and per-transaction relay-decision counters.
 *
 *  Neither `SingleMetrics` nor `MultipleMetrics` is thread-safe on its own.
 *  Thread safety is provided one level up: `TxMetrics::mutex` guards all reads
 *  and writes to their state.  In `OverlayImpl`, `txMetrics_` is additionally
 *  serialised via the overlay's Boost.Asio strand so that peer I/O threads do
 *  not race against each other on writes; RPC threads reading via `json()` rely
 *  solely on `mutex`.
 */

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/messages.h>

#include <boost/circular_buffer.hpp>

#include <chrono>
#include <mutex>

namespace xrpl::metrics {

/** One-second amortised rolling average over a 30-slot circular buffer.
 *
 *  On every `addMetrics(val)` call the value is accumulated into `accum` and
 *  the sample counter `N` is incremented.  When at least one second has elapsed
 *  since `intervalStart` the interval is closed: a representative value is
 *  pushed into the 30-entry `rollingAvgAggregate` buffer (displacing the oldest
 *  entry once full) and `rollingAvg` is recomputed as the arithmetic mean of
 *  all buffer entries.
 *
 *  The representative value for a closed interval depends on `perTimeUnit`:
 *  - `true` (default): `accum / elapsed_seconds` — a **throughput rate**
 *    (e.g., bytes per second, messages per second).
 *  - `false`: `accum / N` — a **per-observation average**
 *    (e.g., peers selected per transaction relay decision).
 *
 *  The buffer is pre-filled with zeros, so the published `rollingAvg` is
 *  dampened downward during the first 30 one-second windows — a deliberate
 *  smoothing artifact.  Intervals with no activity push `0` into the buffer
 *  and naturally dilute `rollingAvg` toward zero without an explicit decay step.
 *
 *  @note This struct is **not** independently thread-safe.  All callers must
 *      hold `TxMetrics::mutex` before invoking any method.
 */
struct SingleMetrics
{
    /** Construct a `SingleMetrics` instance.
     *
     *  @param ptu If `true`, the closed-interval representative value is
     *      `accum / elapsed_seconds` (throughput rate).  If `false`, it is
     *      `accum / N` (per-observation sample mean).  Defaults to `true`.
     */
    SingleMetrics(bool ptu = true) : perTimeUnit(ptu)
    {
    }

    using clock_type = std::chrono::steady_clock;

    /** Start of the current measurement interval. Reset after each close. */
    clock_type::time_point intervalStart{clock_type::now()};

    /** Running sum of all values added in the current open interval. */
    std::uint64_t accum{0};

    /** Published rolling average — arithmetic mean of `rollingAvgAggregate`. */
    std::uint64_t rollingAvg{0};

    /** Number of `addMetrics` calls in the current open interval. */
    std::uint32_t N{0};

    /** Selects how the closed-interval value is derived.
     *
     *  `true` → rate (`accum / elapsed_s`); `false` → mean (`accum / N`).
     */
    bool perTimeUnit{true};

    /** Circular buffer of the last 30 closed-interval values, pre-filled with
     *  zeros.  `rollingAvg` is always the mean of this buffer's current
     *  contents.
     */
    boost::circular_buffer<std::uint64_t> rollingAvgAggregate{30, 0ull};

    /** Accumulate a sample value and, once per second, condense the interval
     *  into the rolling average.
     *
     *  @param val Sample value to accumulate.  Depending on context this is a
     *      byte count, message count, or peer count.
     *  @note Not thread-safe; callers must hold `TxMetrics::mutex`.
     */
    void
    addMetrics(std::uint32_t val);
};

/** Pairs two `SingleMetrics` instances for simultaneous count and byte-size
 *  tracking of a single protocol message type.
 *
 *  In practice `m1` tracks message count per second and `m2` tracks byte
 *  throughput per second.  The two-argument overload records both explicitly;
 *  the one-argument overload synthesises a count of 1 for `m1` — convenient
 *  for inbound message paths where the count per call is always one.
 *
 *  @note Not independently thread-safe; callers must hold `TxMetrics::mutex`.
 */
struct MultipleMetrics
{
    /** Construct a `MultipleMetrics` instance.
     *
     *  @param ptu1 `perTimeUnit` flag forwarded to `m1` (default `true`).
     *  @param ptu2 `perTimeUnit` flag forwarded to `m2` (default `true`).
     */
    MultipleMetrics(bool ptu1 = true, bool ptu2 = true) : m1(ptu1), m2(ptu2)
    {
    }

    /** Message-count rolling average (`perTimeUnit=true` → messages/second). */
    SingleMetrics m1;

    /** Byte-size rolling average (`perTimeUnit=true` → bytes/second). */
    SingleMetrics m2;

    /** Record a single message of `val2` bytes.
     *
     *  Forwards `val2` to `m2` and synthesises a count of 1 for `m1`.  Use
     *  this overload on inbound message paths where each call represents exactly
     *  one protocol message.
     *
     *  @param val2 Message size in bytes.
     */
    void
    addMetrics(std::uint32_t val2);

    /** Record an explicit count and byte size.
     *
     *  @param val1 Value for `m1` (typically a message count).
     *  @param val2 Value for `m2` (typically a byte count).
     */
    void
    addMetrics(std::uint32_t val1, std::uint32_t val2);
};

/** Top-level rolling-average aggregator for transaction reduce-relay telemetry.
 *
 *  Holds one `MultipleMetrics` instance per tracked protocol message type
 *  (recording both message-count and byte-size rolling averages) and three
 *  `SingleMetrics` instances that capture the relay-decision outcome per
 *  transaction: how many peers were selected for full relay, how many were
 *  suppressed, and how many did not have the reduce-relay feature enabled.
 *  A fourth `SingleMetrics` (`missingTx`) measures how often `TMTransactions`
 *  bundles arrive carrying transactions the local node was missing — a proxy
 *  for reactive-request load induced by the feature.
 *
 *  ## Protocol message fields
 *
 *  | Field          | Protocol Message          |
 *  |----------------|---------------------------|
 *  | `tx`           | `mtTRANSACTION`           |
 *  | `haveTx`       | `mtHAVE_TRANSACTIONS`     |
 *  | `getLedger`    | `mtGET_LEDGER`            |
 *  | `ledgerData`   | `mtLEDGER_DATA`           |
 *  | `transactions` | `mtTRANSACTIONS`          |
 *
 *  ## Concurrency
 *
 *  All `addMetrics` overloads and `json()` acquire `mutex` for the duration of
 *  their operation.  In `OverlayImpl`, `txMetrics_` is additionally serialised
 *  by the overlay's Boost.Asio strand so that peer I/O threads do not race each
 *  other on writes; RPC threads reading via `json()` rely solely on `mutex`.
 */
struct TxMetrics
{
    /** Guards all reads and writes to subordinate `SingleMetrics` and
     *  `MultipleMetrics` state.  Declared `mutable` so that `json() const`
     *  can acquire it.
     */
    mutable std::mutex mutex;

    /** Rolling count and byte-size averages for `mtTRANSACTION` messages. */
    MultipleMetrics tx;

    /** Rolling count and byte-size averages for `mtHAVE_TRANSACTIONS` messages. */
    MultipleMetrics haveTx;

    /** Rolling count and byte-size averages for `mtGET_LEDGER` messages. */
    MultipleMetrics getLedger;

    /** Rolling count and byte-size averages for `mtLEDGER_DATA` messages. */
    MultipleMetrics ledgerData;

    /** Rolling count and byte-size averages for `mtTRANSACTIONS` messages. */
    MultipleMetrics transactions;

    /** Per-transaction sample average of peers selected to receive a full relay.
     *
     *  Uses `perTimeUnit=false` — the interesting signal is the ratio of
     *  selected to suppressed peers per decision, not the volume over time.
     */
    SingleMetrics selectedPeers{false};

    /** Per-transaction sample average of peers skipped because they were judged
     *  to already have the transaction (suppress path).
     *
     *  Uses `perTimeUnit=false`.
     */
    SingleMetrics suppressedPeers{false};

    /** Per-transaction sample average of peers that did not negotiate the
     *  reduce-relay feature and therefore always receive full relays.
     *
     *  Uses `perTimeUnit=false`.
     */
    SingleMetrics notEnabled{false};

    /** Throughput rate (count per second) of transactions in `TMTransactions`
     *  bundles that the local node was missing at receipt time.
     *
     *  A rising value indicates that peers are sending hash announcements for
     *  transactions the node does not yet have, generating reactive-request
     *  load — a side-effect of aggressive relay suppression.
     *
     *  Uses `perTimeUnit=true`.
     */
    SingleMetrics missingTx;

    /** Record byte size of an inbound protocol message by type.
     *
     *  Routes `val` to the appropriate `MultipleMetrics` bucket.  Only the
     *  five message types relevant to reduce-relay are tracked; all other types
     *  are silently ignored.  Called from `PeerImp::onMessageBegin()` for every
     *  tracked inbound message.
     *
     *  @param type Protocol message type from the protobuf enum.
     *  @param val  Message size in bytes.
     */
    void
    addMetrics(protocol::MessageType type, std::uint32_t val);

    /** Record the relay-decision outcome for a single transaction event.
     *
     *  Updates the three per-transaction sample averages: `selectedPeers`,
     *  `suppressedPeers`, and `notEnabled`.  Called from the relay-dispatch
     *  path in `OverlayImpl` once per transaction after the peer set has been
     *  partitioned.
     *
     *  @param selected    Number of peers chosen to receive the full transaction.
     *  @param suppressed  Number of peers skipped (hash-only or no send).
     *  @param notEnabled  Number of peers without the reduce-relay feature.
     */
    void
    addMetrics(
        std::uint32_t selected,
        std::uint32_t suppressed,
        std::uint32_t notEnabled);

    /** Record missing-transaction count from a `TMTransactions` bundle.
     *
     *  Feeds `missing` into `missingTx` as a throughput rate (transactions per
     *  second over the rolling 30-second window).  Called from
     *  `PeerImp::onMessage(TMTransactions)` whenever a bundle arrives carrying
     *  data the local node did not already have.
     *
     *  @param missing Number of transactions in the bundle not already held
     *      by the local node.
     */
    void
    addMetrics(std::uint32_t missing);

    /** Serialize all rolling averages to a JSON object for RPC consumers.
     *
     *  Emits thirteen `jss::txr_*`-keyed fields covering the five message-type
     *  count/size pairs and the four peer-selection diagnostics.  All values are
     *  serialized as JSON strings via `std::to_string` rather than JSON
     *  integers; callers must parse them if numeric comparison is needed.
     *
     *  Acquires `mutex` for the duration of the snapshot so the returned object
     *  is internally consistent even when writer strands are concurrently active.
     *
     *  @return A `Json::Value` object containing the current rolling-average
     *      snapshot.  Values reflect the trailing ~30-second window; metrics
     *      with no recent activity report `"0"` as older non-zero slots age out.
     */
    json::Value
    json() const;
};

}  // namespace xrpl::metrics
