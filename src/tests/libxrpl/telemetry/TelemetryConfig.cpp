#include <xrpl/beast/utility/Journal.h>
#include <xrpl/config/BasicConfig.h>
#include <xrpl/telemetry/Telemetry.h>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

using namespace xrpl;

namespace {

/**
 * Batch-setting keys of the [telemetry] section.
 *
 * Spelled once so every case below matches what the parser reads. A
 * misspelling cannot hide: the accepting cases would see the default instead
 * of the value they wrote, and the rejecting cases would stop rejecting.
 */
namespace key {
constexpr char const* batchSize = "batch_size";
constexpr char const* batchDelayMs = "batch_delay_ms";
constexpr char const* maxQueueSize = "max_queue_size";
constexpr char const* consensusTraceStrategy = "consensus_trace_strategy";
}  // namespace key

/**
 * The upper bound quoted in the expected messages below.
 *
 * makeTelemetrySetup() derives it from std::uint32_t, so pin the literal to
 * that type here rather than repeating an unanchored number in 5 messages.
 */
static_assert(std::numeric_limits<std::uint32_t>::max() == 4294967295u);

using KeyValue = std::pair<char const*, char const*>;

/**
 * Parse a [telemetry] section holding only the given keys.
 *
 * A key that is not listed stays absent, so its default applies.
 *
 * @param values Key/value pairs to write into the section.
 * @return The populated Setup struct.
 */
telemetry::Telemetry::Setup
parseBatch(std::initializer_list<KeyValue> values)
{
    Section section;
    for (auto const& [name, value] : values)
        section.set(name, value);
    return telemetry::makeTelemetrySetup(section, "nHUtest123", "2.0.0", 0);
}

/**
 * Parse and return the rejection message.
 *
 * Only std::runtime_error is caught. A boost::bad_lexical_cast escaping the
 * parser derives from std::bad_cast, so it propagates and fails the test
 * instead of being mistaken for a clean rejection. That is the point of the
 * not-a-number cases.
 *
 * @param values Key/value pairs to write into the section.
 * @return The exception message, or "" if the parse succeeded.
 */
std::string
batchRejection(std::initializer_list<KeyValue> values)
{
    try
    {
        static_cast<void>(parseBatch(values));
        return {};
    }
    catch (std::runtime_error const& e)
    {
        return e.what();
    }
}

}  // namespace

TEST(TelemetryConfig, setup_defaults)
{
    telemetry::Telemetry::Setup const s;
    EXPECT_FALSE(s.enabled);
    EXPECT_EQ(s.serviceName, "xrpld");
    EXPECT_TRUE(s.serviceVersion.empty());
    EXPECT_TRUE(s.serviceInstanceId.empty());
    EXPECT_EQ(s.tracesEndpoint, "http://localhost:4318/v1/traces");
    EXPECT_FALSE(s.useTls);
    EXPECT_TRUE(s.tlsCertPath.empty());
    EXPECT_DOUBLE_EQ(s.samplingRatio, 1.0);
    EXPECT_EQ(s.batchSize, 512u);
    EXPECT_EQ(s.batchDelay, std::chrono::milliseconds{5000});
    EXPECT_EQ(s.maxQueueSize, 2048u);
    EXPECT_EQ(s.networkId, 0u);
    EXPECT_EQ(s.networkType, "mainnet");
    EXPECT_TRUE(s.traceTransactions);
    EXPECT_TRUE(s.traceConsensus);
    EXPECT_TRUE(s.traceRpc);
    EXPECT_TRUE(s.tracePeer);
    EXPECT_TRUE(s.traceLedger);
    EXPECT_EQ(s.consensusTraceStrategy, telemetry::ConsensusTraceStrategy::Deterministic);
}

TEST(TelemetryConfig, parse_empty_section)
{
    Section const section;
    auto setup = telemetry::makeTelemetrySetup(section, "nHUtest123", "2.0.0", 0);

    EXPECT_FALSE(setup.enabled);
    EXPECT_EQ(setup.serviceName, "xrpld");
    EXPECT_EQ(setup.serviceVersion, "2.0.0");
    EXPECT_EQ(setup.serviceInstanceId, "nHUtest123");
    EXPECT_DOUBLE_EQ(setup.samplingRatio, 1.0);
    // An absent key takes the documented default. setup_defaults covers the
    // struct's own initializers; these three cover the parser applying them.
    EXPECT_EQ(setup.batchSize, 512u);
    EXPECT_EQ(setup.batchDelay, std::chrono::milliseconds{5000});
    EXPECT_EQ(setup.maxQueueSize, 2048u);
    EXPECT_TRUE(setup.traceRpc);
    EXPECT_TRUE(setup.traceTransactions);
    EXPECT_TRUE(setup.traceConsensus);
    EXPECT_TRUE(setup.tracePeer);
    EXPECT_TRUE(setup.traceLedger);
}

TEST(TelemetryConfig, parse_full_section)
{
    Section section;
    section.set("enabled", "1");
    section.set("service_name", "my-rippled");
    section.set("service_instance_id", "custom-id");
    section.set("exporter", "otlp_http");
    section.set("traces_endpoint", "http://collector:4318/v1/traces");
    section.set("use_tls", "1");
    section.set("tls_ca_cert", "/etc/ssl/ca.pem");
    section.set("batch_size", "256");
    section.set("batch_delay_ms", "3000");
    section.set("max_queue_size", "4096");
    section.set("trace_transactions", "0");
    section.set("trace_consensus", "0");
    section.set("trace_rpc", "1");
    section.set("trace_peer", "1");
    section.set("trace_ledger", "0");

    auto setup = telemetry::makeTelemetrySetup(section, "nHUtest123", "2.0.0", 1);

    EXPECT_TRUE(setup.enabled);
    EXPECT_EQ(setup.serviceName, "my-rippled");
    EXPECT_EQ(setup.serviceInstanceId, "custom-id");
    EXPECT_EQ(setup.tracesEndpoint, "http://collector:4318/v1/traces");
    EXPECT_TRUE(setup.useTls);
    EXPECT_EQ(setup.tlsCertPath, "/etc/ssl/ca.pem");
    EXPECT_EQ(setup.batchSize, 256u);
    EXPECT_EQ(setup.batchDelay, std::chrono::milliseconds{3000});
    EXPECT_EQ(setup.maxQueueSize, 4096u);
    EXPECT_FALSE(setup.traceTransactions);
    EXPECT_FALSE(setup.traceConsensus);
    EXPECT_TRUE(setup.traceRpc);
    EXPECT_TRUE(setup.tracePeer);
    EXPECT_FALSE(setup.traceLedger);
}

TEST(TelemetryConfig, batch_settings_accept_the_lower_bound_exactly)
{
    auto const setup =
        parseBatch({{key::batchSize, "1"}, {key::batchDelayMs, "1"}, {key::maxQueueSize, "1"}});
    EXPECT_EQ(setup.batchSize, 1u);
    EXPECT_EQ(setup.batchDelay, std::chrono::milliseconds{1});
    EXPECT_EQ(setup.maxQueueSize, 1u);
}

TEST(TelemetryConfig, batch_settings_accept_the_upper_bound_exactly)
{
    auto const setup = parseBatch(
        {{key::batchSize, "4294967295"},
         {key::batchDelayMs, "4294967295"},
         {key::maxQueueSize, "4294967295"}});
    EXPECT_EQ(setup.batchSize, 4294967295u);
    EXPECT_EQ(setup.batchDelay, std::chrono::milliseconds{4294967295});
    EXPECT_EQ(setup.maxQueueSize, 4294967295u);
}

TEST(TelemetryConfig, batch_size_zero_is_rejected)
{
    EXPECT_EQ(
        batchRejection({{key::batchSize, "0"}}),
        "Invalid value 'batch_size' in [telemetry]: must be between 1 and 4294967295.");
}

TEST(TelemetryConfig, batch_delay_ms_zero_is_rejected)
{
    EXPECT_EQ(
        batchRejection({{key::batchDelayMs, "0"}}),
        "Invalid value 'batch_delay_ms' in [telemetry]: must be between 1 and 4294967295.");
}

TEST(TelemetryConfig, max_queue_size_zero_is_rejected)
{
    EXPECT_EQ(
        batchRejection({{key::maxQueueSize, "0"}}),
        "Invalid value 'max_queue_size' in [telemetry]: must be between 1 and 4294967295.");
}

TEST(TelemetryConfig, batch_size_not_a_number_is_rejected_as_runtime_error)
{
    // Section::get() reaches boost::lexical_cast, which throws a std::bad_cast.
    // Catching only std::runtime_error is the point: this fails unless the
    // parser turned that into a message naming the key.
    EXPECT_EQ(
        batchRejection({{key::batchSize, "abc"}}),
        "Invalid value 'batch_size' in [telemetry]: must be a whole number.");
}

TEST(TelemetryConfig, batch_delay_ms_not_a_number_is_rejected_as_runtime_error)
{
    EXPECT_EQ(
        batchRejection({{key::batchDelayMs, "abc"}}),
        "Invalid value 'batch_delay_ms' in [telemetry]: must be a whole number.");
}

TEST(TelemetryConfig, max_queue_size_not_a_number_is_rejected_as_runtime_error)
{
    EXPECT_EQ(
        batchRejection({{key::maxQueueSize, "abc"}}),
        "Invalid value 'max_queue_size' in [telemetry]: must be a whole number.");
}

TEST(TelemetryConfig, batch_size_fractional_is_rejected)
{
    // A batch counts spans, so "512.5" must not silently truncate to 512.
    EXPECT_EQ(
        batchRejection({{key::batchSize, "512.5"}}),
        "Invalid value 'batch_size' in [telemetry]: must be a whole number.");
}

TEST(TelemetryConfig, batch_settings_reject_negative_rather_than_wrapping)
{
    // boost::lexical_cast to an unsigned type turns "-1" into 4294967295
    // instead of failing, so a negative must land on the range check.
    EXPECT_EQ(
        batchRejection({{key::batchSize, "-1"}}),
        "Invalid value 'batch_size' in [telemetry]: must be between 1 and 4294967295.");
    EXPECT_EQ(
        batchRejection({{key::batchDelayMs, "-1"}}),
        "Invalid value 'batch_delay_ms' in [telemetry]: must be between 1 and 4294967295.");
    EXPECT_EQ(
        batchRejection({{key::maxQueueSize, "-1"}}),
        "Invalid value 'max_queue_size' in [telemetry]: must be between 1 and 4294967295.");
}

TEST(TelemetryConfig, max_queue_size_above_the_upper_bound_is_rejected)
{
    EXPECT_EQ(
        batchRejection({{key::maxQueueSize, "4294967296"}}),
        "Invalid value 'max_queue_size' in [telemetry]: must be between 1 and 4294967295.");
}

TEST(TelemetryConfig, batch_size_above_max_queue_size_is_rejected)
{
    // The OTel SDK documents max_export_batch_size <= max_queue_size as a
    // precondition and does not enforce it, so the parser must.
    EXPECT_EQ(
        batchRejection({{key::batchSize, "600"}, {key::maxQueueSize, "512"}}),
        "Invalid value 'batch_size' in [telemetry]: must not exceed 'max_queue_size' (512).");
}

TEST(TelemetryConfig, batch_size_above_a_lowered_max_queue_size_is_rejected)
{
    // The likely operator mistake: lowering only max_queue_size and leaving
    // batch_size at its 512 default.
    EXPECT_EQ(
        batchRejection({{key::maxQueueSize, "256"}}),
        "Invalid value 'batch_size' in [telemetry]: must not exceed 'max_queue_size' (256).");
}

TEST(TelemetryConfig, batch_size_equal_to_max_queue_size_is_accepted)
{
    // The cross-check rejects only batchSize > maxQueueSize, so equal passes.
    auto const setup = parseBatch({{key::batchSize, "512"}, {key::maxQueueSize, "512"}});
    EXPECT_EQ(setup.batchSize, 512u);
    EXPECT_EQ(setup.maxQueueSize, 512u);
}

TEST(TelemetryConfig, consensus_trace_strategy_names_match_the_config_spellings)
{
    // strategyName() feeds both the parser and the trace_strategy span
    // attribute, so these two strings are the whole public vocabulary.
    EXPECT_STREQ(
        telemetry::strategyName(telemetry::ConsensusTraceStrategy::Deterministic), "deterministic");
    EXPECT_STREQ(telemetry::strategyName(telemetry::ConsensusTraceStrategy::Random), "random");
}

TEST(TelemetryConfig, consensus_trace_strategy_defaults_to_deterministic)
{
    // The key is absent, so the default applies. Deterministic is the only
    // strategy in use, and a default of Random would break cross-node
    // correlation on every node that omits the key.
    EXPECT_EQ(
        parseBatch({}).consensusTraceStrategy, telemetry::ConsensusTraceStrategy::Deterministic);
}

TEST(TelemetryConfig, consensus_trace_strategy_accepts_deterministic)
{
    EXPECT_EQ(
        parseBatch({{key::consensusTraceStrategy, "deterministic"}}).consensusTraceStrategy,
        telemetry::ConsensusTraceStrategy::Deterministic);
}

TEST(TelemetryConfig, consensus_trace_strategy_accepts_random)
{
    // Random is experimental and unused, but it is a documented spelling, so
    // the parser must still map it to its own enumerator rather than reject it
    // or fold it into the default.
    EXPECT_EQ(
        parseBatch({{key::consensusTraceStrategy, "random"}}).consensusTraceStrategy,
        telemetry::ConsensusTraceStrategy::Random);
}

TEST(TelemetryConfig, consensus_trace_strategy_empty_value_is_the_default)
{
    // `consensus_trace_strategy=` with nothing after it. An empty value means
    // the operator wrote the key and no value, which is the default, not a typo.
    EXPECT_EQ(
        parseBatch({{key::consensusTraceStrategy, ""}}).consensusTraceStrategy,
        telemetry::ConsensusTraceStrategy::Deterministic);
}

TEST(TelemetryConfig, consensus_trace_strategy_rejects_an_undocumented_value)
{
    // "attribute" is not a spelling this parser accepts. Rejecting rather than
    // defaulting is the point: a silent fallback would leave the operator
    // believing a setting took effect.
    EXPECT_EQ(
        batchRejection({{key::consensusTraceStrategy, "attribute"}}),
        "Invalid value 'consensus_trace_strategy' in [telemetry]: must be 'deterministic' or "
        "'random'.");
}

TEST(TelemetryConfig, consensus_trace_strategy_matching_is_case_sensitive)
{
    // Every other value in this section is matched exactly, so "Random" is a
    // typo and must be reported as one.
    EXPECT_EQ(
        batchRejection({{key::consensusTraceStrategy, "Random"}}),
        "Invalid value 'consensus_trace_strategy' in [telemetry]: must be 'deterministic' or "
        "'random'.");
}

TEST(TelemetryConfig, null_telemetry_factory)
{
    telemetry::Telemetry::Setup setup;
    setup.enabled = false;

    beast::Journal::Sink& sink = beast::Journal::getNullSink();
    beast::Journal const j(sink);
    auto tel = telemetry::makeTelemetry(setup, j);
    EXPECT_TRUE(tel != nullptr);
    EXPECT_FALSE(tel->isEnabled());
    EXPECT_FALSE(tel->shouldTraceRpc());
    EXPECT_FALSE(tel->shouldTraceTransactions());
    EXPECT_FALSE(tel->shouldTraceConsensus());
    EXPECT_FALSE(tel->shouldTracePeer());
    EXPECT_FALSE(tel->shouldTraceLedger());

    // start/stop should be no-ops without crashing
    tel->start();
    tel->stop();
}
