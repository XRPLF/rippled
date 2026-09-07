/**
 * Parser for the [telemetry] section of xrpld.cfg.
 *
 * Reads configuration values from the config file and populates a
 * Telemetry::Setup struct. All options have sensible defaults so the
 * section can be minimal or omitted entirely.
 *
 * See cfg/xrpld-example.cfg for the full list of available options.
 */

#include <xrpl/basics/contract.h>
#include <xrpl/config/BasicConfig.h>
#include <xrpl/telemetry/Telemetry.h>

#include <chrono>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>

namespace xrpl::telemetry {

namespace {

/**
 * Config key names for the [telemetry] section.
 *
 * Each must match the corresponding option documented in
 * cfg/xrpld-example.cfg verbatim. Defined as `char const*` so they
 * pass to Section::valueOr() (which takes `std::string const&`)
 * without an explicit conversion, exactly as a literal would.
 */
namespace key {
constexpr char const* enabled = "enabled";
constexpr char const* serviceName = "service_name";
constexpr char const* serviceInstanceId = "service_instance_id";
constexpr char const* tracesEndpoint = "traces_endpoint";
constexpr char const* useTls = "use_tls";
constexpr char const* tlsCaCert = "tls_ca_cert";
constexpr char const* batchSize = "batch_size";
constexpr char const* batchDelayMs = "batch_delay_ms";
constexpr char const* maxQueueSize = "max_queue_size";
constexpr char const* traceTransactions = "trace_transactions";
constexpr char const* traceConsensus = "trace_consensus";
constexpr char const* traceRpc = "trace_rpc";
constexpr char const* tracePeer = "trace_peer";
constexpr char const* traceLedger = "trace_ledger";
}  // namespace key

/**
 * Default values applied when a key is absent from the config.
 *
 * @note serviceName mirrors SystemParameters' systemName() ("xrpld") but
 * is duplicated here as a literal: the telemetry module deliberately does
 * not link xrpl.libxrpl.protocol, so including SystemParameters.h would
 * introduce an undeclared cross-module dependency.
 */
namespace dflt {
constexpr char const* serviceName = "xrpld";
constexpr char const* tracesEndpoint = "http://localhost:4318/v1/traces";
constexpr std::uint32_t batchSize = 512u;
constexpr std::uint32_t batchDelayMs = 5000u;
constexpr std::uint32_t maxQueueSize = 2048u;
}  // namespace dflt

/**
 * Smallest accepted value for the three batch settings.
 *
 * All three size a queue or a timer, so zero is meaningless for every one of
 * them. The OTel BatchSpanProcessor takes them as given and does not validate,
 * so the config parser is the only place a nonsense value can be rejected.
 */
constexpr std::uint32_t kMinBatchSetting = 1u;

/**
 * Section name used in error messages, so the operator knows where to look.
 */
constexpr char const* kSectionLabel = "[telemetry]";

/**
 * Read a config value and reject anything outside minValue..UINT32_MAX.
 *
 * Section::get() lets boost::bad_lexical_cast escape. That derives from
 * std::bad_cast, not std::runtime_error, so a mistyped value gives the operator
 * a bare "bad cast" naming no key. Wrap it and rethrow with the key name.
 *
 * @param section The [telemetry] section to read from.
 * @param name Key to read, as documented in cfg/xrpld-example.cfg.
 * @param absentValue Value returned when the key is absent.
 * @param minValue Smallest accepted value.
 * @return The configured value, or absentValue if the key is absent.
 * @note Throws std::runtime_error for a value that is not a whole number, and
 * for one out of range, with a different message for each.
 */
[[nodiscard]] std::uint32_t
readBounded(
    Section const& section,
    char const* name,
    std::uint32_t absentValue,
    std::uint32_t minValue)
{
    // Read as signed. boost::lexical_cast to an unsigned type wraps a leading
    // minus instead of failing ("-1" yields 4294967295), so reading signed is
    // the only way to see a negative value and reject it below.
    std::optional<std::int64_t> parsed;
    try
    {
        parsed = section.get<std::int64_t>(name);
    }
    catch (...)
    {
        Throw<std::runtime_error>(
            std::string("Invalid value '") + name + "' in " + kSectionLabel +
            ": must be a whole number.");
    }

    if (!parsed)
        return absentValue;

    constexpr auto maxValue = static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max());
    if (*parsed < static_cast<std::int64_t>(minValue) || *parsed > maxValue)
    {
        Throw<std::runtime_error>(
            std::string("Invalid value '") + name + "' in " + kSectionLabel + ": must be between " +
            std::to_string(minValue) + " and " + std::to_string(maxValue) + ".");
    }

    return static_cast<std::uint32_t>(*parsed);
}

/**
 * Derive a human-readable network type label from the numeric network ID.
 * @param networkId  The network identifier from [network_id] config.
 * @return "mainnet", "testnet", "devnet", or "unknown" for other values.
 */
[[nodiscard]] std::string
networkTypeFromId(std::uint32_t networkId)
{
    switch (networkId)
    {
        case 0:
            return "mainnet";
        case 1:
            return "testnet";
        case 2:
            return "devnet";
        default:
            return "unknown";
    }
}

}  // namespace

Telemetry::Setup
makeTelemetrySetup(
    Section const& section,
    std::string const& nodePublicKey,
    std::string const& version,
    std::uint32_t networkId)
{
    Telemetry::Setup setup;

    setup.enabled = section.valueOr<int>(key::enabled, 0) != 0;
    setup.serviceName = section.valueOr<std::string>(key::serviceName, dflt::serviceName);
    setup.serviceVersion = version;
    setup.serviceInstanceId = section.valueOr<std::string>(key::serviceInstanceId, nodePublicKey);

    setup.tracesEndpoint = section.valueOr<std::string>(key::tracesEndpoint, dflt::tracesEndpoint);

    setup.useTls = section.valueOr<int>(key::useTls, 0) != 0;
    setup.tlsCertPath = section.valueOr<std::string>(key::tlsCaCert, "");

    // Head sampling is intentionally fixed at 1.0 (sample everything) and is
    // not read from config. A per-node ratio would let nodes make divergent
    // keep/drop decisions for the same distributed trace, producing broken
    // traces; volume reduction is delegated to the collector's tail sampling.
    // setup.samplingRatio is a const member fixed at 1.0; nothing to parse.

    setup.batchSize = readBounded(section, key::batchSize, dflt::batchSize, kMinBatchSetting);
    setup.batchDelay = std::chrono::milliseconds{
        readBounded(section, key::batchDelayMs, dflt::batchDelayMs, kMinBatchSetting)};
    setup.maxQueueSize =
        readBounded(section, key::maxQueueSize, dflt::maxQueueSize, kMinBatchSetting);

    // The OTel SDK documents max_export_batch_size <= max_queue_size as a
    // precondition of BatchSpanProcessorOptions and does not enforce it, so
    // reject the pair here rather than hand the SDK a state it forbids.
    if (setup.batchSize > setup.maxQueueSize)
    {
        Throw<std::runtime_error>(
            std::string("Invalid value '") + key::batchSize + "' in " + kSectionLabel +
            ": must not exceed '" + key::maxQueueSize + "' (" + std::to_string(setup.maxQueueSize) +
            ").");
    }

    setup.networkId = networkId;
    setup.networkType = networkTypeFromId(networkId);

    setup.traceTransactions = section.valueOr<int>(key::traceTransactions, 1) != 0;
    setup.traceConsensus = section.valueOr<int>(key::traceConsensus, 1) != 0;
    setup.traceRpc = section.valueOr<int>(key::traceRpc, 1) != 0;
    setup.tracePeer = section.valueOr<int>(key::tracePeer, 1) != 0;
    setup.traceLedger = section.valueOr<int>(key::traceLedger, 1) != 0;

    setup.consensusTraceStrategy =
        section.valueOr<std::string>("consensus_trace_strategy", "deterministic");

    return setup;
}

}  // namespace xrpl::telemetry
