/**
 * GTest unit tests for the per-peer validation load helpers.
 *
 * The rate tracker, the ranking and the warning throttle are plain code with
 * no OTel dependency, so they run in every build. The last suite drives the
 * real gauge through an SDK pipeline, so it needs a telemetry build.
 *
 * @code
 *   readings --PeerRateTracker--> rates --rankValidationLoad--> ValidationLoad
 *                                                                   |
 *   PeerLogThrottle <-- busiest untrusted peer over the limit ------+
 *                                                                   |
 *   peer_validation_load, 10 points <--observeValidationLoad--------+
 * @endcode
 */

#include <xrpld/telemetry/PeerValidationLoad.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

#ifdef XRPL_ENABLE_TELEMETRY
#include <helpers/ManualMetricReader.h>
#include <opentelemetry/metrics/observer_result.h>
#include <opentelemetry/nostd/shared_ptr.h>
#include <opentelemetry/nostd/variant.h>
#include <opentelemetry/sdk/metrics/data/metric_data.h>
#include <opentelemetry/sdk/metrics/data/point_data.h>
#include <opentelemetry/sdk/metrics/export/metric_producer.h>
#include <opentelemetry/sdk/metrics/meter_provider.h>
#include <opentelemetry/sdk/metrics/meter_provider_factory.h>
#include <opentelemetry/sdk/metrics/metric_reader.h>

#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <utility>
#endif

namespace xrpl::telemetry {
namespace {

/**
 * Start of every test's timeline. Only the gaps between instants matter.
 */
constexpr std::chrono::steady_clock::time_point kValidationLoadStart{};

/**
 * Gap between two collections in these tests.
 */
constexpr std::chrono::seconds kValidationLoadPeriod{10};

/**
 * A validation count that, sent over one kValidationLoadPeriod, is one per
 * second above the production limit.
 */
constexpr std::uint64_t kValidationLoadFloodCount =
    (static_cast<std::uint64_t>(kValidationLoadPeerLimitPerSecond) + 1) * 10;

}  // namespace

// ---------------------------------------------------------------------------
// rankValidationLoad
// ---------------------------------------------------------------------------

// The three busiest rates, in order, and the busiest peer's share of the
// total. Catches a wrong sort direction, and a share taken over the top three
// (50/75) instead of over every peer (50/76).
TEST(PeerValidationLoad, ranks_top_three_and_share)
{
    std::vector<PeerRate> const peers{
        {.peerId = 1, .perSecond = 5.0},
        {.peerId = 2, .perSecond = 50.0},
        {.peerId = 3, .perSecond = 20.0},
        {.peerId = 4, .perSecond = 1.0}};

    auto const load = rankValidationLoad(peers, 200.0);

    EXPECT_EQ(load.topRates, (std::array{50.0, 20.0, 5.0}));
    EXPECT_DOUBLE_EQ(load.topShare, 50.0 / 76.0);
    EXPECT_EQ(load.overLimit, 0u);
    EXPECT_EQ(load.busiestPeer, std::optional<std::uint64_t>{2});
    EXPECT_EQ(load.busiestRate, 50.0);
}

// No peers: every value is zero and no peer is named. Catches a ranking that
// reads past an empty input.
TEST(PeerValidationLoad, zero_peers_gives_zeros)
{
    auto const load = rankValidationLoad({}, 200.0);

    EXPECT_EQ(load.topRates, (std::array{0.0, 0.0, 0.0}));
    EXPECT_EQ(load.topShare, 0.0);
    EXPECT_EQ(load.overLimit, 0u);
    EXPECT_FALSE(load.busiestPeer.has_value());
    EXPECT_EQ(load.busiestRate, 0.0);
}

// Connected peers that sent nothing: the share is zero, not NaN. Catches a
// share computed without the zero-total guard.
TEST(PeerValidationLoad, idle_peers_give_share_zero)
{
    std::vector<PeerRate> const peers{
        {.peerId = 1, .perSecond = 0.0}, {.peerId = 2, .perSecond = 0.0}};

    auto const load = rankValidationLoad(peers, 200.0);

    EXPECT_EQ(load.topRates, (std::array{0.0, 0.0, 0.0}));
    EXPECT_EQ(load.topShare, 0.0);
    EXPECT_EQ(load.overLimit, 0u);
}

// One peer above the limit carries all the load. Catches a share that is not
// 1 for a single peer, and an over-limit count that misses it.
TEST(PeerValidationLoad, one_peer_has_share_one)
{
    std::vector<PeerRate> const peers{{.peerId = 7, .perSecond = 300.0}};

    auto const load = rankValidationLoad(peers, 200.0);

    EXPECT_EQ(load.topRates, (std::array{300.0, 0.0, 0.0}));
    EXPECT_EQ(load.topShare, 1.0);
    EXPECT_EQ(load.overLimit, 1u);
    EXPECT_EQ(load.busiestPeer, std::optional<std::uint64_t>{7});
    EXPECT_EQ(load.busiestRate, 300.0);
}

// Equal rates rank by peer id, lowest first, for every input order. Catches a
// tie broken by input order, which would let the named peer change between
// collections with no change in traffic. Every order is tried because an
// unstable sort puts the lowest id first for some orders by chance.
TEST(PeerValidationLoad, ties_keep_lowest_peer_id_first)
{
    std::array<std::uint64_t, 4> order{4, 7, 9, 12};
    do
    {
        std::vector<PeerRate> peers;
        peers.reserve(order.size());
        for (auto const peerId : order)
            peers.push_back({.peerId = peerId, .perSecond = 40.0});

        auto const load = rankValidationLoad(peers, 200.0);

        EXPECT_EQ(load.busiestPeer, std::optional<std::uint64_t>{4})
            << "input order " << order[0] << ' ' << order[1] << ' ' << order[2] << ' ' << order[3];
        EXPECT_EQ(load.topRates, (std::array{40.0, 40.0, 40.0}));
        EXPECT_DOUBLE_EQ(load.topShare, 40.0 / 160.0);
    } while (std::ranges::next_permutation(order).found);
}

// A rate equal to the limit is not over it; the next double above it is.
// Catches >= in place of >.
TEST(PeerValidationLoad, only_rates_above_the_limit_count)
{
    double const limit = kValidationLoadPeerLimitPerSecond;
    std::vector<PeerRate> const peers{
        {.peerId = 1, .perSecond = limit},
        {.peerId = 2, .perSecond = std::nextafter(limit, 2 * limit)}};

    EXPECT_EQ(rankValidationLoad(peers, limit).overLimit, 1u);
}

// ---------------------------------------------------------------------------
// PeerRateTracker
// ---------------------------------------------------------------------------

// A peer seen for the first time has no earlier reading, so no rate. Catches
// a rate taken against an implicit zero, which would report a long-connected
// peer's lifetime count as one interval's traffic.
TEST(PeerRateTracker, first_reading_gives_no_rate)
{
    PeerRateTracker tracker;
    std::vector<PeerValidationReading> const readings{
        {.peerId = 1, .trusted = 100, .untrusted = 100}};

    EXPECT_TRUE(tracker.update(readings, kValidationLoadStart).empty());
}

// Two readings 10 s apart give the count difference per second, for each
// trust class. Catches a missing division by the elapsed time, and swapped
// trust fields.
TEST(PeerRateTracker, second_reading_gives_rate_per_second)
{
    PeerRateTracker tracker;
    std::vector<PeerValidationReading> const first{{.peerId = 1, .trusted = 100, .untrusted = 100}};
    std::vector<PeerValidationReading> const second{
        {.peerId = 1, .trusted = 1100, .untrusted = 600}};
    ASSERT_TRUE(tracker.update(first, kValidationLoadStart).empty());

    auto const rates = tracker.update(second, kValidationLoadStart + kValidationLoadPeriod);

    ASSERT_EQ(rates.size(), 1u);
    EXPECT_EQ(rates[0].peerId, 1u);
    EXPECT_EQ(rates[0].trustedPerSecond, 100.0);
    EXPECT_EQ(rates[0].untrustedPerSecond, 50.0);
}

// A peer missing from one round loses its state, so when it is back its first
// reading again gives no rate. Catches a tracker that never prunes: it would
// grow with every reconnect and, here, report peer 2 at 1000 / 10 s = 100/s,
// using a reading two rounds old over the 10 s since the last round.
TEST(PeerRateTracker, disconnected_peer_is_pruned)
{
    PeerRateTracker tracker;
    std::vector<PeerValidationReading> const both{
        {.peerId = 1, .trusted = 0, .untrusted = 0}, {.peerId = 2, .trusted = 0, .untrusted = 0}};
    std::vector<PeerValidationReading> const onlyFirst{
        {.peerId = 1, .trusted = 10, .untrusted = 10}};
    std::vector<PeerValidationReading> const backAgain{
        {.peerId = 1, .trusted = 20, .untrusted = 20},
        {.peerId = 2, .trusted = 1000, .untrusted = 1000}};
    ASSERT_TRUE(tracker.update(both, kValidationLoadStart).empty());
    ASSERT_EQ(tracker.update(onlyFirst, kValidationLoadStart + kValidationLoadPeriod).size(), 1u);

    auto const rates =
        tracker.update(backAgain, kValidationLoadStart + (2 * kValidationLoadPeriod));

    ASSERT_EQ(rates.size(), 1u);
    EXPECT_EQ(rates[0].peerId, 1u);
    EXPECT_EQ(rates[0].untrustedPerSecond, 1.0);
}

// A count lower than the last reading gives rate 0, not the wrapped unsigned
// difference. Catches a dropped guard, which would report about 1.8e18/s.
TEST(PeerRateTracker, lower_count_gives_zero_not_wrap)
{
    PeerRateTracker tracker;
    std::vector<PeerValidationReading> const before{{.peerId = 1, .trusted = 500, .untrusted = 10}};
    std::vector<PeerValidationReading> const after{{.peerId = 1, .trusted = 400, .untrusted = 30}};
    ASSERT_TRUE(tracker.update(before, kValidationLoadStart).empty());

    auto const rates = tracker.update(after, kValidationLoadStart + kValidationLoadPeriod);

    ASSERT_EQ(rates.size(), 1u);
    EXPECT_EQ(rates[0].trustedPerSecond, 0.0);
    EXPECT_EQ(rates[0].untrustedPerSecond, 2.0);
}

// Two readings at the same instant give rate 0, not infinity. Catches a
// division by a zero interval.
TEST(PeerRateTracker, same_instant_gives_zero_rate)
{
    PeerRateTracker tracker;
    std::vector<PeerValidationReading> const before{{.peerId = 1, .trusted = 0, .untrusted = 0}};
    std::vector<PeerValidationReading> const after{{.peerId = 1, .trusted = 50, .untrusted = 50}};
    ASSERT_TRUE(tracker.update(before, kValidationLoadStart).empty());

    auto const rates = tracker.update(after, kValidationLoadStart);

    ASSERT_EQ(rates.size(), 1u);
    EXPECT_EQ(rates[0].trustedPerSecond, 0.0);
    EXPECT_EQ(rates[0].untrustedPerSecond, 0.0);
}

// ---------------------------------------------------------------------------
// PeerLogThrottle
// ---------------------------------------------------------------------------

// One line per peer per interval, and each peer has its own window. Catches an
// inverted check, a window counted from the first line instead of the last,
// and one window shared by every peer.
TEST(PeerLogThrottle, one_line_per_peer_per_five_minutes)
{
    PeerLogThrottle throttle;
    auto const t0 = kValidationLoadStart;

    EXPECT_TRUE(throttle.shouldLog(1, t0));
    EXPECT_FALSE(
        throttle.shouldLog(1, t0 + kValidationLoadLogInterval - std::chrono::nanoseconds{1}));
    EXPECT_TRUE(throttle.shouldLog(2, t0 + std::chrono::minutes{1}));
    EXPECT_TRUE(throttle.shouldLog(1, t0 + kValidationLoadLogInterval));
    EXPECT_FALSE(throttle.shouldLog(1, t0 + kValidationLoadLogInterval + std::chrono::minutes{1}));
}

// After prune(), a disconnected peer can be named again at once, while a live
// peer keeps its window. Catches a prune that drops nothing, or everything.
TEST(PeerLogThrottle, prune_forgets_only_disconnected_peers)
{
    PeerLogThrottle throttle;
    ASSERT_TRUE(throttle.shouldLog(1, kValidationLoadStart));
    ASSERT_TRUE(throttle.shouldLog(2, kValidationLoadStart));
    std::array<std::uint64_t, 1> const live{2};

    throttle.prune(live);

    EXPECT_TRUE(throttle.shouldLog(1, kValidationLoadStart + std::chrono::seconds{1}));
    EXPECT_FALSE(throttle.shouldLog(2, kValidationLoadStart + std::chrono::seconds{1}));
}

// ---------------------------------------------------------------------------
// PeerValidationLoad
// ---------------------------------------------------------------------------

// The busiest untrusted peer over the limit is named once, with its rate, and
// held back while it keeps flooding inside the interval. Catches a missing
// throttle, and a warning naming the wrong peer or rate.
TEST(PeerValidationLoad, warns_once_for_the_busiest_untrusted_peer)
{
    PeerValidationLoad load;
    std::vector<PeerValidationReading> const quiet{
        {.peerId = 3, .trusted = 0, .untrusted = 0}, {.peerId = 5, .trusted = 0, .untrusted = 0}};
    std::vector<PeerValidationReading> const flooding{
        {.peerId = 3, .trusted = 0, .untrusted = 10},
        {.peerId = 5, .trusted = 0, .untrusted = kValidationLoadFloodCount}};
    std::vector<PeerValidationReading> const stillFlooding{
        {.peerId = 3, .trusted = 0, .untrusted = 20},
        {.peerId = 5, .trusted = 0, .untrusted = 2 * kValidationLoadFloodCount}};
    ASSERT_FALSE(load.sample(quiet, kValidationLoadStart).warning.has_value());

    auto const first = load.sample(flooding, kValidationLoadStart + kValidationLoadPeriod);
    auto const second =
        load.sample(stillFlooding, kValidationLoadStart + (2 * kValidationLoadPeriod));

    ASSERT_TRUE(first.warning.has_value());
    auto const warning = first.warning.value_or(ValidationLoadWarning{});
    EXPECT_EQ(warning.peerId, 5u);
    EXPECT_EQ(warning.untrustedPerSecond, kValidationLoadPeerLimitPerSecond + 1);
    EXPECT_FALSE(second.warning.has_value());
}

// Untrusted traffic exactly at the limit, and trusted traffic above it, raise
// no warning. Catches >= in place of >, and a warning taken from the trusted
// class.
TEST(PeerValidationLoad, no_warning_at_the_limit_or_for_trusted_traffic)
{
    PeerValidationLoad load;
    auto const atLimit = static_cast<std::uint64_t>(kValidationLoadPeerLimitPerSecond) * 10;
    ASSERT_EQ(static_cast<double>(atLimit) / 10.0, kValidationLoadPeerLimitPerSecond)
        << "the limit must be a whole number of validations per second for this fixture";
    std::vector<PeerValidationReading> const quiet{
        {.peerId = 1, .trusted = 0, .untrusted = 0}, {.peerId = 2, .trusted = 0, .untrusted = 0}};
    std::vector<PeerValidationReading> const busy{
        {.peerId = 1, .trusted = 0, .untrusted = atLimit},
        {.peerId = 2, .trusted = kValidationLoadFloodCount, .untrusted = 0}};
    ASSERT_FALSE(load.sample(quiet, kValidationLoadStart).warning.has_value());

    auto const sample = load.sample(busy, kValidationLoadStart + kValidationLoadPeriod);

    EXPECT_FALSE(sample.warning.has_value());
    EXPECT_EQ(sample.untrusted.overLimit, 0u);
    EXPECT_EQ(sample.trusted.overLimit, 1u);
}

// A warned peer that drops out for one collection loses its throttle window,
// so when it is back and flooding it is named again inside the interval.
// Catches a sample() that never prunes the throttle: the peer would stay
// silenced for the rest of the interval.
TEST(PeerValidationLoad, warns_again_after_the_peer_drops_out)
{
    PeerValidationLoad load;
    auto const at = [](int collection) {
        return kValidationLoadStart + (collection * kValidationLoadPeriod);
    };
    std::vector<PeerValidationReading> const quiet{{.peerId = 5, .trusted = 0, .untrusted = 0}};
    std::vector<PeerValidationReading> const flooding{
        {.peerId = 5, .trusted = 0, .untrusted = kValidationLoadFloodCount}};
    std::vector<PeerValidationReading> const nobody;
    std::vector<PeerValidationReading> const backAgain{
        {.peerId = 5, .trusted = 0, .untrusted = 2 * kValidationLoadFloodCount}};
    std::vector<PeerValidationReading> const stillFlooding{
        {.peerId = 5, .trusted = 0, .untrusted = 3 * kValidationLoadFloodCount}};
    ASSERT_LT(4 * kValidationLoadPeriod, kValidationLoadLogInterval)
        << "the second warning must fall inside the throttle interval";
    ASSERT_FALSE(load.sample(quiet, at(0)).warning.has_value());
    ASSERT_TRUE(load.sample(flooding, at(1)).warning.has_value());
    ASSERT_FALSE(load.sample(nobody, at(2)).warning.has_value());
    // The first reading since the peer came back: no rate yet.
    ASSERT_FALSE(load.sample(backAgain, at(3)).warning.has_value());

    auto const again = load.sample(stillFlooding, at(4));

    ASSERT_TRUE(again.warning.has_value());
    EXPECT_EQ(again.warning.value_or(ValidationLoadWarning{}).peerId, 5u);
}

// The warning line, exactly. The Validation Load Warnings panel filters on its
// start, and operators read the rest, so a reworded line silently empties the
// panel. The text is written out because the panel's query holds it. Catches
// a changed prefix, reordered fields, or a different number format.
TEST(PeerValidationLoad, warning_line_names_peer_key_and_rate)
{
    ValidationLoadWarning const warning{.peerId = 42, .untrustedPerSecond = 201.5};

    EXPECT_EQ(
        formatValidationLoadWarning(warning, "n9KeyOfThePeer"),
        "Validation load: peer 42 (n9KeyOfThePeer) sent 201.5 untrusted validations/s");
}

// ---------------------------------------------------------------------------
// The gauge, through a real SDK pipeline
// ---------------------------------------------------------------------------

#ifdef XRPL_ENABLE_TELEMETRY

namespace {

/**
 * Labels of one exported point, as key to string value.
 */
using ValidationLoadLabels = std::map<std::string, std::string>;

/**
 * What one collection exported for peer_validation_load.
 */
struct ValidationLoadPoints
{
    /**
     * Streams that carried the name. More than one means two instruments of
     * that name did not share storage.
     */
    std::size_t streams = 0;

    /**
     * Value of each point, by its labels.
     */
    std::map<ValidationLoadLabels, double> values;
};

/**
 * Peer readings a test hands the gauge, standing in for the overlay.
 */
struct ValidationLoadFeed
{
    /**
     * The production state the gauge keeps between collections.
     */
    PeerValidationLoad load;

    /**
     * The readings the next collection sees.
     */
    std::vector<PeerValidationReading> readings;

    /**
     * The instant the next collection runs at.
     */
    std::chrono::steady_clock::time_point now;
};

/**
 * Gauge callback for the tests: the production sample and observe steps over
 * the feed's readings.
 *
 * @param result Where the SDK collects this pass's points.
 * @param state  The ValidationLoadFeed the test registered.
 */
void
observeValidationLoadFeed(opentelemetry::metrics::ObserverResult result, void* state)
{
    auto& feed = *static_cast<ValidationLoadFeed*>(state);
    observeValidationLoad(result, feed.load.sample(feed.readings, feed.now));
}

/**
 * Add one exported point to @p points. A point that is not a double last
 * value with string labels fails the running test instead of being dropped.
 *
 * @param points Where the point goes.
 * @param point  The point as the SDK exported it.
 */
void
addValidationLoadPoint(
    ValidationLoadPoints& points,
    opentelemetry::sdk::metrics::PointDataAttributes const& point)
{
    auto const* const lastValue =
        opentelemetry::nostd::get_if<opentelemetry::sdk::metrics::LastValuePointData>(
            &point.point_data);
    auto const* const value =
        lastValue != nullptr ? opentelemetry::nostd::get_if<double>(&lastValue->value_) : nullptr;
    if (value == nullptr)
    {
        ADD_FAILURE() << "peer_validation_load exported a point that is not a double gauge";
        return;
    }

    ValidationLoadLabels labels;
    for (auto const& [key, attribute] : point.attributes)
    {
        auto const* const text = opentelemetry::nostd::get_if<std::string>(&attribute);
        if (text == nullptr)
        {
            ADD_FAILURE() << "label is not a string: " << key;
            continue;
        }
        labels.emplace(key, *text);
    }
    points.values.emplace(std::move(labels), *value);
}

/**
 * Run one collection through @p reader and keep what peer_validation_load
 * exported.
 *
 * @param reader A reader attached to a provider that still exists.
 * @return The streams and points of peer_validation_load.
 */
[[nodiscard]] ValidationLoadPoints
collectValidationLoadPoints(opentelemetry::sdk::metrics::MetricReader& reader)
{
    ValidationLoadPoints points;
    bool const collected =
        reader.Collect([&points](opentelemetry::sdk::metrics::ResourceMetrics& data) {
            for (auto const& scope : data.scope_metric_data_)
            {
                for (auto const& metric : scope.metric_data_)
                {
                    if (metric.instrument_descriptor.name_ != "peer_validation_load")
                        continue;
                    ++points.streams;
                    for (auto const& point : metric.point_data_attr_)
                        addValidationLoadPoint(points, point);
                }
            }
            return true;
        });
    EXPECT_TRUE(collected) << "the reader is not attached to a live pipeline";
    return points;
}

}  // namespace

// Two collections 10 s apart over three peers export exactly 10 points, each
// with a label pair from the fixed lists. The first holds zeros, so the
// series exist from the first collection; the second holds the rates. Catches
// a missing or extra point (the 10-series budget), a renamed label, and
// swapped trust classes. The label strings are written out because they are
// what the dashboards query.
TEST(PeerValidationLoadGauge, exports_ten_points_with_rates)
{
    auto const reader = std::make_shared<xrpl::test::ManualMetricReader>();
    std::shared_ptr<opentelemetry::sdk::metrics::MeterProvider> const provider =
        opentelemetry::sdk::metrics::MeterProviderFactory::Create();
    provider->AddMetricReader(reader);
    auto const meter = provider->GetMeter("xrpld_test", "1.0.0");

    ValidationLoadFeed feed{
        .load = {},
        .readings = {{.peerId = 1}, {.peerId = 2}, {.peerId = 3}},
        .now = kValidationLoadStart};
    auto const gauge = createValidationLoadGauge(*meter);
    gauge->AddCallback(observeValidationLoadFeed, &feed);

    auto const first = collectValidationLoadPoints(*reader);

    std::map<ValidationLoadLabels, double> const zeros{
        {{{"metric", "top1_rate"}, {"trust", "trusted"}}, 0.0},
        {{{"metric", "top2_rate"}, {"trust", "trusted"}}, 0.0},
        {{{"metric", "top3_rate"}, {"trust", "trusted"}}, 0.0},
        {{{"metric", "top_share"}, {"trust", "trusted"}}, 0.0},
        {{{"metric", "peers_over_limit"}, {"trust", "trusted"}}, 0.0},
        {{{"metric", "top1_rate"}, {"trust", "untrusted"}}, 0.0},
        {{{"metric", "top2_rate"}, {"trust", "untrusted"}}, 0.0},
        {{{"metric", "top3_rate"}, {"trust", "untrusted"}}, 0.0},
        {{{"metric", "top_share"}, {"trust", "untrusted"}}, 0.0},
        {{{"metric", "peers_over_limit"}, {"trust", "untrusted"}}, 0.0}};
    EXPECT_EQ(first.streams, 1u);
    EXPECT_EQ(first.values, zeros);

    // Untrusted: 100/s, 1/s, 0/s. Trusted: 3/s, 7/s and one peer one per
    // second over the limit.
    feed.readings = {
        {.peerId = 1, .trusted = 30, .untrusted = 1000},
        {.peerId = 2, .trusted = 70, .untrusted = 10},
        {.peerId = 3, .trusted = kValidationLoadFloodCount, .untrusted = 0}};
    feed.now = kValidationLoadStart + kValidationLoadPeriod;

    auto const second = collectValidationLoadPoints(*reader);

    double const overLimit = kValidationLoadPeerLimitPerSecond + 1;
    std::map<ValidationLoadLabels, double> const rates{
        {{{"metric", "top1_rate"}, {"trust", "trusted"}}, overLimit},
        {{{"metric", "top2_rate"}, {"trust", "trusted"}}, 7.0},
        {{{"metric", "top3_rate"}, {"trust", "trusted"}}, 3.0},
        {{{"metric", "top_share"}, {"trust", "trusted"}}, overLimit / (overLimit + 10.0)},
        {{{"metric", "peers_over_limit"}, {"trust", "trusted"}}, 1.0},
        {{{"metric", "top1_rate"}, {"trust", "untrusted"}}, 100.0},
        {{{"metric", "top2_rate"}, {"trust", "untrusted"}}, 1.0},
        {{{"metric", "top3_rate"}, {"trust", "untrusted"}}, 0.0},
        {{{"metric", "top_share"}, {"trust", "untrusted"}}, 100.0 / 101.0},
        {{{"metric", "peers_over_limit"}, {"trust", "untrusted"}}, 0.0}};
    EXPECT_EQ(second.streams, 1u);
    EXPECT_EQ(second.values, rates);
}

#endif  // XRPL_ENABLE_TELEMETRY

}  // namespace xrpl::telemetry
