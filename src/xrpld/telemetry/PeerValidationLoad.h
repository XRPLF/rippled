#pragma once

/**
 * Per-peer validation load: the rates, ranks and warning throttle behind the
 * `peer_validation_load` gauge.
 *
 * The node counts the validations each peer delivers. On every collection the
 * gauge turns those counts into per-second rates, ranks them, and reports only
 * the ranked values, so no label value comes from a peer. The busiest untrusted
 * peer over the limit is named in a throttled warning line instead.
 *
 * @code
 *   AppMetricGauges callback (one collection)
 *     |  one PeerValidationReading per live peer
 *     v
 *   PeerValidationLoad::sample()
 *     +-- PeerRateTracker::update()    counts -> validations/s, forgets gone peers
 *     +-- rankValidationLoad(), x2     top 3 rates, top share, peers over limit
 *     +-- PeerLogThrottle              one warning per connection per interval
 *     v
 *   ValidationLoadSample --observeValidationLoad()--> 10 gauge points
 *                        --warning -----------------> one warning line
 * @endcode
 *
 * Only observeValidationLoad() and createValidationLoadGauge() touch OTel, so
 * they are the only part inside XRPL_ENABLE_TELEMETRY. The rest is plain code
 * that the tests run in every build.
 */

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef XRPL_ENABLE_TELEMETRY
#include <xrpl/telemetry/PeerValidationLoadMetricNames.h>

#include <opentelemetry/metrics/async_instruments.h>
#include <opentelemetry/metrics/meter.h>
#include <opentelemetry/metrics/observer_result.h>
#include <opentelemetry/nostd/shared_ptr.h>
#include <opentelemetry/nostd/variant.h>
#endif

namespace xrpl::telemetry {

/**
 * Validations per second above which one peer counts as over the limit.
 *
 * Counted per trust class in `peers_over_limit`. The busiest untrusted peer
 * above it can be named in a warning. Only telemetry reads it: nothing is
 * dropped or charged when a peer goes over it.
 */
inline constexpr double kValidationLoadPeerLimitPerSecond = 200.0;

/**
 * Shortest gap between two warnings that name the same connection. A
 * reconnect gets a new connection id, so it starts a new gap.
 */
inline constexpr std::chrono::minutes kValidationLoadLogInterval{5};

/**
 * Start of every validation-load warning line. The Validation Load Warnings
 * panel on the Peer Quality dashboard filters on this text, so changing it
 * empties that panel.
 */
inline constexpr std::string_view kValidationLoadLogPrefix = "Validation load: peer";

/**
 * One peer's running validation counts, read at one collection.
 *
 * @code
 *   Peer::validationCounts() --> PeerValidationReading --> PeerRateTracker::update()
 * @endcode
 */
struct PeerValidationReading
{
    /**
     * Connection id of the peer, from Peer::id().
     */
    std::uint64_t peerId = 0;

    /**
     * Trusted validations the peer delivered since it connected.
     */
    std::uint64_t trusted = 0;

    /**
     * Untrusted validations the peer delivered since it connected.
     */
    std::uint64_t untrusted = 0;
};

/**
 * One peer's validation rates between two collections.
 *
 * @code
 *   PeerRateTracker::update() --> PeerValidationRate --> PeerValidationLoad::sample()
 * @endcode
 */
struct PeerValidationRate
{
    /**
     * Connection id of the peer.
     */
    std::uint64_t peerId = 0;

    /**
     * Trusted validations per second. Never negative.
     */
    double trustedPerSecond = 0.0;

    /**
     * Untrusted validations per second. Never negative.
     */
    double untrustedPerSecond = 0.0;
};

/**
 * One peer's rate for one trust class: the input to rankValidationLoad().
 *
 * @code
 *   PeerValidationRate --split by trust--> PeerRate --> rankValidationLoad()
 * @endcode
 */
struct PeerRate
{
    /**
     * Connection id of the peer.
     */
    std::uint64_t peerId = 0;

    /**
     * Validations per second.
     */
    double perSecond = 0.0;
};

/**
 * The ranked view of one trust class. Holds the five values the gauge
 * exports for that class, plus the busiest peer for the warning.
 *
 * @code
 *   rankValidationLoad() --> ValidationLoad --> observeValidationLoad()
 * @endcode
 */
struct ValidationLoad
{
    /**
     * Rates of the busiest, second and third busiest peer, in validations per
     * second. 0 where there are fewer peers.
     */
    std::array<double, 3> topRates{};

    /**
     * The busiest peer's share of the class total, from 0 to 1. 0 when the
     * total is 0.
     */
    double topShare = 0.0;

    /**
     * How many peers are above the limit.
     */
    std::size_t overLimit = 0;

    /**
     * Connection id of the busiest peer. Empty when there are no peers.
     */
    std::optional<std::uint64_t> busiestPeer = std::nullopt;

    /**
     * The busiest peer's rate, in validations per second.
     */
    double busiestRate = 0.0;
};

/**
 * A peer to name in a warning line.
 *
 * @code
 *   PeerValidationLoad::sample() --> ValidationLoadWarning --> AppMetricGauges (JLOG)
 * @endcode
 */
struct ValidationLoadWarning
{
    /**
     * Connection id of the peer.
     */
    std::uint64_t peerId = 0;

    /**
     * Its untrusted validations per second.
     */
    double untrustedPerSecond = 0.0;
};

/**
 * The warning line that names a flooding peer.
 *
 * @param warning   The peer and its untrusted rate.
 * @param publicKey The peer's node public key, in base58.
 * @return The line, for example
 * `Validation load: peer 42 (n9...) sent 201.5 untrusted validations/s`. It
 * carries no address: the peers admin command lists the address beside the
 * public key while the peer is connected.
 */
[[nodiscard]] inline std::string
formatValidationLoadWarning(ValidationLoadWarning const& warning, std::string_view publicKey)
{
    std::ostringstream line;
    line << kValidationLoadLogPrefix << ' ' << warning.peerId << " (" << publicKey << ") sent "
         << warning.untrustedPerSecond << " untrusted validations/s";
    return line.str();
}

/**
 * Everything one collection produces: both ranked trust classes and, at
 * most, one warning.
 *
 * @code
 *   PeerValidationLoad::sample() --> ValidationLoadSample --> observeValidationLoad()
 * @endcode
 */
struct ValidationLoadSample
{
    /**
     * Ranked rates of validations from trusted signers.
     */
    ValidationLoad trusted;

    /**
     * Ranked rates of validations from untrusted signers.
     */
    ValidationLoad untrusted;

    /**
     * The busiest untrusted peer, when it is over the limit and the throttle
     * lets it be named.
     */
    std::optional<ValidationLoadWarning> warning = std::nullopt;
};

/**
 * Turns each peer's running validation counts into per-second rates between
 * two collections.
 *
 * @code
 *   previous call            this call (10 s later)
 *   peer 7: 1000 / 40  --->  peer 7: 2000 / 90   =>  100/s trusted, 5/s untrusted
 *   peer 9:   50 / 50        (gone)              =>  no rate, state dropped
 *                            peer 12: 30 / 30    =>  no rate yet, state kept
 * @endcode
 *
 * @code
 * // Primary use: the first call stores the counts, the next gives rates.
 * PeerRateTracker tracker;
 * auto const none = tracker.update(first, t0);  // empty: nothing to compare
 * auto const rates = tracker.update(second, t0 + std::chrono::seconds{10});
 *
 * // Edge case: a peer missing from one call is forgotten, so when it is
 * // back its first reading again gives no rate.
 * auto const seen = tracker.update(withPeer, t0 + std::chrono::seconds{20});
 * auto const gone = tracker.update(withoutPeer, t0 + std::chrono::seconds{30});
 * auto const back = tracker.update(withPeer, t0 + std::chrono::seconds{40});  // no rate for it
 *
 * // Edge case: a count lower than the stored one gives rate 0, not a
 * // wrapped unsigned difference.
 * auto const zero = tracker.update(lowerCounts, t0 + std::chrono::seconds{50});
 * @endcode
 *
 * @note Not thread-safe. The gauge calls it only from its own callback, and
 * the OTel SDK runs observable callbacks one at a time.
 * @note A peer needs two readings before it has a rate, so a peer that
 * connects and floods within one interval shows up one interval late.
 */
class PeerRateTracker
{
public:
    /**
     * Stores this collection's readings and returns the rates of the peers
     * that were also in the previous call. Every peer missing from
     * @p readings is forgotten, so a disconnected peer leaves no state.
     *
     * @param readings One reading per live peer. Peer ids must be unique.
     * @param now      When the readings were taken.
     * @return One rate per peer present now and in the previous call, in the
     * order of @p readings. A count lower than the stored one, or no time
     * passed since the previous call, gives a rate of 0.
     */
    [[nodiscard]] std::vector<PeerValidationRate>
    update(
        std::span<PeerValidationReading const> readings,
        std::chrono::steady_clock::time_point now)
    {
        double const elapsedSeconds = std::chrono::duration<double>(now - previousTime_).count();
        std::vector<PeerValidationRate> rates;
        rates.reserve(readings.size());
        std::unordered_map<std::uint64_t, PeerValidationReading> current;
        current.reserve(readings.size());
        for (auto const& reading : readings)
        {
            auto const before = previous_.find(reading.peerId);
            if (before != previous_.end())
            {
                rates.push_back(
                    {.peerId = reading.peerId,
                     .trustedPerSecond =
                         ratePerSecond(reading.trusted, before->second.trusted, elapsedSeconds),
                     .untrustedPerSecond = ratePerSecond(
                         reading.untrusted, before->second.untrusted, elapsedSeconds)});
            }
            current.insert_or_assign(reading.peerId, reading);
        }
        previous_ = std::move(current);
        previousTime_ = now;
        return rates;
    }

private:
    /**
     * Count difference per second, guarded against a lower count and against
     * no elapsed time.
     *
     * @param now            The count read now.
     * @param before         The count stored by the previous call.
     * @param elapsedSeconds Seconds between the two readings.
     * @return Validations per second, or 0 when @p now is below @p before or
     * @p elapsedSeconds is not positive.
     */
    [[nodiscard]] static double
    ratePerSecond(std::uint64_t now, std::uint64_t before, double elapsedSeconds)
    {
        if (now < before || elapsedSeconds <= 0.0)
            return 0.0;
        return static_cast<double>(now - before) / elapsedSeconds;
    }

    /**
     * The previous call's reading of each peer, by connection id. Holds only
     * peers that were live at that call.
     */
    std::unordered_map<std::uint64_t, PeerValidationReading> previous_;

    /**
     * When the previous call's readings were taken.
     */
    std::chrono::steady_clock::time_point previousTime_;
};

/**
 * Ranks one trust class of per-peer rates.
 *
 * @param peers          The rate of each peer, for one trust class.
 * @param limitPerSecond Rate above which a peer counts in `overLimit`.
 * @return The top three rates, the busiest peer's share of the total, the
 * number of peers above the limit, and the busiest peer. All zeros and no
 * busiest peer when @p peers is empty; share 0 when every rate is 0. Equal
 * rates rank the lower peer id first, so the order is stable.
 */
[[nodiscard]] inline ValidationLoad
rankValidationLoad(std::span<PeerRate const> peers, double limitPerSecond)
{
    ValidationLoad load;
    if (peers.empty())
        return load;

    std::vector<PeerRate> ranked(peers.begin(), peers.end());
    auto const top = std::min(ranked.size(), load.topRates.size());
    auto const busierFirst = [](PeerRate const& lhs, PeerRate const& rhs) {
        if (lhs.perSecond != rhs.perSecond)
            return lhs.perSecond > rhs.perSecond;
        return lhs.peerId < rhs.peerId;
    };
    std::ranges::partial_sort(
        ranked, ranked.begin() + static_cast<std::ptrdiff_t>(top), busierFirst);
    std::ranges::transform(
        std::span(ranked).first(top), load.topRates.begin(), &PeerRate::perSecond);

    double total = 0.0;
    for (auto const& peer : peers)
        total += peer.perSecond;

    load.busiestPeer = ranked.front().peerId;
    load.busiestRate = ranked.front().perSecond;
    load.topShare = total > 0.0 ? load.busiestRate / total : 0.0;
    load.overLimit = static_cast<std::size_t>(std::ranges::count_if(
        peers, [limitPerSecond](PeerRate const& peer) { return peer.perSecond > limitPerSecond; }));
    return load;
}

/**
 * Lets one warning line per connection through per kValidationLoadLogInterval.
 *
 * @code
 *   shouldLog(peer) --first time, or interval passed--> true, window restarts
 *                   --inside the interval-------------> false
 *   prune(live)     --peer not live--> its window is forgotten
 * @endcode
 *
 * @code
 * // Primary use: name a flooding peer at most once per interval.
 * PeerLogThrottle throttle;
 * if (throttle.shouldLog(peerId, now))
 *     JLOG(journal.warn()) << formatValidationLoadWarning(warning, publicKey);
 *
 * // Edge case: two peers have separate windows, so naming one never
 * // silences the other.
 * PeerLogThrottle pair;
 * bool const first = pair.shouldLog(1, now);   // true
 * bool const second = pair.shouldLog(2, now);  // also true
 *
 * // Edge case: a peer that is not connected any more is forgotten, so its
 * // window does not outlive the connection.
 * throttle.prune(liveIds);
 * @endcode
 *
 * @note Not thread-safe. The gauge calls it only from its own callback, and
 * the OTel SDK runs observable callbacks one at a time.
 * @note A peer that reconnects gets a new connection id, and so a new window.
 */
class PeerLogThrottle
{
public:
    /**
     * Says whether a line may name this peer now. A true answer restarts the
     * peer's window; a false one changes nothing.
     *
     * @param peerId Connection id of the peer.
     * @param now    The current time.
     * @return true when the peer was never named, or was last named at least
     * kValidationLoadLogInterval ago.
     */
    [[nodiscard]] bool
    shouldLog(std::uint64_t peerId, std::chrono::steady_clock::time_point now)
    {
        auto const [entry, inserted] = lastLogged_.try_emplace(peerId, now);
        if (inserted)
            return true;
        if (now - entry->second < kValidationLoadLogInterval)
            return false;
        entry->second = now;
        return true;
    }

    /**
     * Forgets every peer not in @p live, so a disconnected peer leaves no
     * state.
     *
     * @param live Connection ids of the peers connected now.
     */
    void
    prune(std::span<std::uint64_t const> live)
    {
        std::erase_if(lastLogged_, [live](auto const& entry) {
            return std::ranges::find(live, entry.first) == live.end();
        });
    }

private:
    /**
     * When each peer was last named, by connection id.
     */
    std::unordered_map<std::uint64_t, std::chrono::steady_clock::time_point> lastLogged_;
};

/**
 * The state the `peer_validation_load` gauge keeps between collections, and
 * the step that turns one collection's readings into its sample.
 *
 * @code
 *   PeerValidationLoad
 *     +-- PeerRateTracker tracker_    last reading of each live peer
 *     +-- PeerLogThrottle throttle_   last warning of each live peer
 * @endcode
 *
 * @code
 * // Primary use, once per collection: publish the points, then log.
 * auto const sample = load.sample(readings, std::chrono::steady_clock::now());
 * observeValidationLoad(result, sample);
 * if (sample.warning)
 *     JLOG(journal.warn()) << formatValidationLoadWarning(*sample.warning, publicKey);
 *
 * // Edge case: the first collection has no rates yet. Every value is 0 and
 * // there is no warning, but all 10 points are still published.
 * auto const first = load.sample(readings, t0);
 * observeValidationLoad(result, first);
 * @endcode
 *
 * @note Not thread-safe. AppMetricGauges calls sample() only from the gauge
 * callback, and the OTel SDK runs observable callbacks one at a time.
 * @note Only the busiest untrusted peer can be named in one collection. A
 * second flooding peer is named only in a collection where it is the busiest.
 */
class PeerValidationLoad
{
public:
    /**
     * Turns one collection's readings into rates, ranks both trust classes,
     * and decides whether to name the busiest untrusted peer.
     *
     * @param readings One reading per live peer. Peer ids must be unique.
     * @param now      When the readings were taken.
     * @return Both ranked classes, and a warning when the busiest untrusted
     * peer is above kValidationLoadPeerLimitPerSecond and was not named
     * within kValidationLoadLogInterval.
     */
    [[nodiscard]] ValidationLoadSample
    sample(
        std::span<PeerValidationReading const> readings,
        std::chrono::steady_clock::time_point now)
    {
        auto const rates = tracker_.update(readings, now);
        std::vector<PeerRate> trusted;
        std::vector<PeerRate> untrusted;
        trusted.reserve(rates.size());
        untrusted.reserve(rates.size());
        for (auto const& rate : rates)
        {
            trusted.push_back({.peerId = rate.peerId, .perSecond = rate.trustedPerSecond});
            untrusted.push_back({.peerId = rate.peerId, .perSecond = rate.untrustedPerSecond});
        }

        ValidationLoadSample result{
            .trusted = rankValidationLoad(trusted, kValidationLoadPeerLimitPerSecond),
            .untrusted = rankValidationLoad(untrusted, kValidationLoadPeerLimitPerSecond),
            .warning = std::nullopt};

        std::vector<std::uint64_t> live;
        live.reserve(readings.size());
        std::ranges::transform(readings, std::back_inserter(live), &PeerValidationReading::peerId);
        throttle_.prune(live);

        auto const& busiest = result.untrusted;
        if (busiest.overLimit > 0 && busiest.busiestPeer.has_value() &&
            throttle_.shouldLog(*busiest.busiestPeer, now))
        {
            result.warning = ValidationLoadWarning{
                .peerId = *busiest.busiestPeer, .untrustedPerSecond = busiest.busiestRate};
        }
        return result;
    }

private:
    /**
     * Last reading of each live peer.
     */
    PeerRateTracker tracker_;

    /**
     * Last warning of each live peer.
     */
    PeerLogThrottle throttle_;
};

#ifdef XRPL_ENABLE_TELEMETRY

/**
 * Creates the `peer_validation_load` observable gauge.
 *
 * @param meter The meter to create it on.
 * @return The instrument handle. Its callbacks stay registered only while it
 * lives.
 */
[[nodiscard]] inline opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
createValidationLoadGauge(opentelemetry::metrics::Meter& meter)
{
    return meter.CreateDoubleObservableGauge(metric::kPeerValidationLoad, kPeerValidationLoadDesc);
}

/**
 * Publishes one sample as the gauge's 10 points: five `metric` values for
 * each of the two `trust` values. Every label value is a constant, so the
 * point set never depends on the peers.
 *
 * @param result The observer result the SDK passed to the gauge callback.
 * @param sample The sample to publish.
 * @throws opentelemetry::nostd::bad_variant_access when @p result is not a
 * double observer, which means the callback is not on a gauge made by
 * createValidationLoadGauge().
 */
inline void
observeValidationLoad(
    opentelemetry::metrics::ObserverResult const& result,
    ValidationLoadSample const& sample)
{
    auto& doubles = *opentelemetry::nostd::get<
        opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObserverResultT<double>>>(result);
    auto const observeClass = [&doubles](ValidationLoad const& load, char const* trust) {
        std::array const points{
            std::pair{kLoadTop1Rate, load.topRates[0]},
            std::pair{kLoadTop2Rate, load.topRates[1]},
            std::pair{kLoadTop3Rate, load.topRates[2]},
            std::pair{kLoadTopShare, load.topShare},
            std::pair{kLoadPeersOverLimit, static_cast<double>(load.overLimit)}};
        for (auto const& [metric, value] : points)
            doubles.Observe(value, {{kLabelMetric, metric}, {kLabelTrust, trust}});
    };
    observeClass(sample.trusted, kTrustTrusted);
    observeClass(sample.untrusted, kTrustUntrusted);
}

#endif  // XRPL_ENABLE_TELEMETRY

}  // namespace xrpl::telemetry
