/**
 * Parser for the [telemetry] section of xrpld.cfg.
 *
 * Reads configuration values from the config file and populates a
 * Telemetry::Setup struct. All options have sensible defaults so the
 * section can be minimal or omitted entirely.
 *
 * See cfg/xrpld-example.cfg for the full list of available options.
 */

#include <xrpl/config/BasicConfig.h>
#include <xrpl/telemetry/Telemetry.h>

#include <chrono>
#include <cstdint>
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
constexpr char const* endpoint = "endpoint";
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
constexpr char const* endpoint = "http://localhost:4318/v1/traces";
constexpr std::uint32_t batchSize = 512u;
constexpr std::uint32_t batchDelayMs = 5000u;
constexpr std::uint32_t maxQueueSize = 2048u;
}  // namespace dflt

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

    setup.exporterEndpoint = section.valueOr<std::string>(key::endpoint, dflt::endpoint);

    setup.useTls = section.valueOr<int>(key::useTls, 0) != 0;
    setup.tlsCertPath = section.valueOr<std::string>(key::tlsCaCert, "");

    // Head sampling is intentionally fixed at 1.0 (sample everything) and is
    // not read from config. A per-node ratio would let nodes make divergent
    // keep/drop decisions for the same distributed trace, producing broken
    // traces; volume reduction is delegated to the collector's tail sampling.
    // setup.samplingRatio is a const member fixed at 1.0; nothing to parse.

    setup.batchSize = section.valueOr<std::uint32_t>(key::batchSize, dflt::batchSize);
    setup.batchDelay = std::chrono::milliseconds{
        section.valueOr<std::uint32_t>(key::batchDelayMs, dflt::batchDelayMs)};
    setup.maxQueueSize = section.valueOr<std::uint32_t>(key::maxQueueSize, dflt::maxQueueSize);

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
