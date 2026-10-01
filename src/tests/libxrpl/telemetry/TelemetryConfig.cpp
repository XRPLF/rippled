#include <xrpl/beast/utility/Journal.h>
#include <xrpl/config/BasicConfig.h>
#include <xrpl/telemetry/Telemetry.h>

#include <gtest/gtest.h>

#include <array>
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
}  // namespace key

using Setup = telemetry::Telemetry::Setup;

/**
 * An on/off key of the [telemetry] section and the Setup field it sets.
 *
 * @code
 *   name=<text>  -->  setup.*field holds the parsed value
 *   name absent  -->  setup.*field == absentValue
 * @endcode
 *
 * absentValue is the default that cfg/xrpld-example.cfg documents.
 */
struct FlagKey
{
    /**
     * Key name, as written in the config file.
     */
    char const* name;

    /**
     * Setup field the key sets.
     */
    bool Setup::* field;

    /**
     * Value the field takes when the key is absent.
     */
    bool absentValue;
};

/**
 * The on/off keys the parser reads, with the field each one sets.
 *
 * The names repeat the parser's own constants, which are private to
 * TelemetryConfig.cpp. With a misspelled name, at least one accepting case
 * fails, and so does every rejecting case.
 *
 * @note The rows pair keys with fields by hand. A new on/off key needs its
 * own row, or these tests do not cover it.
 */
constexpr auto kFlagKeys = std::to_array<FlagKey>({
    {.name = "enabled", .field = &Setup::enabled, .absentValue = false},
    {.name = "use_tls", .field = &Setup::useTls, .absentValue = false},
    {.name = "trace_transactions", .field = &Setup::traceTransactions, .absentValue = true},
    {.name = "trace_consensus", .field = &Setup::traceConsensus, .absentValue = true},
    {.name = "trace_rpc", .field = &Setup::traceRpc, .absentValue = true},
    {.name = "trace_peer", .field = &Setup::tracePeer, .absentValue = true},
    {.name = "trace_ledger", .field = &Setup::traceLedger, .absentValue = true},
});

/**
 * Values an on/off key accepts, each with the bool it means.
 *
 * TRUE and False check that case is ignored.
 */
constexpr auto kAcceptedFlagValues = std::to_array<std::pair<char const*, bool>>({
    {"0", false},
    {"1", true},
    {"true", true},
    {"false", false},
    {"TRUE", true},
    {"False", false},
});

/**
 * Values an on/off key rejects.
 *
 * They are other numbers and other words for true. None may turn a key on.
 */
constexpr auto kRejectedFlagValues = std::to_array<char const*>({"yes", "2", "-1", "on"});

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

TEST(TelemetryConfig, on_off_keys_accept_0_1_true_and_false_ignoring_case)
{
    for (auto const& flag : kFlagKeys)
    {
        for (auto const& [text, expected] : kAcceptedFlagValues)
        {
            SCOPED_TRACE(std::string(flag.name) + "=" + text);
            auto const setup = parseBatch({{flag.name, text}});

            // Only the key that was set may leave its default, so a key
            // wired to the wrong field fails here.
            for (auto const& other : kFlagKeys)
            {
                SCOPED_TRACE(other.name);
                bool const want = other.field == flag.field ? expected : other.absentValue;
                EXPECT_EQ(setup.*other.field, want);
            }
        }
    }
}

TEST(TelemetryConfig, on_off_keys_reject_other_values_naming_the_key)
{
    for (auto const& flag : kFlagKeys)
    {
        std::string const message = std::string("Invalid value '") + flag.name +
            "' in [telemetry]: must be 0, 1, true or false.";
        for (auto const* text : kRejectedFlagValues)
        {
            SCOPED_TRACE(std::string(flag.name) + "=" + text);
            EXPECT_EQ(batchRejection({{flag.name, text}}), message);
        }
    }
}

TEST(TelemetryConfig, absent_on_off_keys_take_their_defaults)
{
    auto const setup = parseBatch({});
    for (auto const& flag : kFlagKeys)
    {
        SCOPED_TRACE(flag.name);
        EXPECT_EQ(setup.*flag.field, flag.absentValue);
    }
}
