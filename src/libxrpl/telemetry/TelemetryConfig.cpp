/** Parser for the [telemetry] section of xrpld.cfg.

    Reads configuration values from the config file and populates a
    Telemetry::Setup struct. All options have sensible defaults so the
    section can be minimal or omitted entirely.

    See cfg/xrpld-example.cfg for the full list of available options.
*/

#include <xrpl/basics/BasicConfig.h>
#include <xrpl/telemetry/Telemetry.h>

#include <algorithm>
#include <cstdint>
#include <string>

namespace xrpl::telemetry {

namespace {

/** Derive a human-readable network type label from the numeric network ID.
    @param networkId  The network identifier from [network_id] config.
    @return "mainnet", "testnet", "devnet", or "unknown" for other values.
*/
std::string
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
setupTelemetry(
    Section const& section,
    std::string const& nodePublicKey,
    std::string const& version,
    std::uint32_t networkId)
{
    Telemetry::Setup setup;

    setup.enabled = section.valueOr<int>("enabled", 0) != 0;
    setup.serviceName = section.valueOr<std::string>("service_name", "xrpld");
    setup.serviceVersion = version;
    setup.serviceInstanceId = section.valueOr<std::string>("service_instance_id", nodePublicKey);

    setup.exporterEndpoint =
        section.valueOr<std::string>("endpoint", "http://localhost:4318/v1/traces");

    setup.useTls = section.valueOr<int>("use_tls", 0) != 0;
    setup.tlsCertPath = section.valueOr<std::string>("tls_ca_cert", "");

    setup.samplingRatio = section.valueOr<double>("sampling_ratio", 1.0);
    setup.samplingRatio = std::clamp(setup.samplingRatio, 0.0, 1.0);

    setup.batchSize = section.valueOr<std::uint32_t>("batch_size", 512u);
    setup.batchDelay =
        std::chrono::milliseconds{section.valueOr<std::uint32_t>("batch_delay_ms", 5000u)};
    setup.maxQueueSize = section.valueOr<std::uint32_t>("max_queue_size", 2048u);

    setup.networkId = networkId;
    setup.networkType = networkTypeFromId(networkId);

    setup.traceTransactions = section.valueOr<int>("trace_transactions", 1) != 0;
    setup.traceConsensus = section.valueOr<int>("trace_consensus", 1) != 0;
    setup.traceRpc = section.valueOr<int>("trace_rpc", 1) != 0;
    setup.tracePeer = section.valueOr<int>("trace_peer", 0) != 0;
    setup.traceLedger = section.valueOr<int>("trace_ledger", 1) != 0;

    return setup;
}

}  // namespace xrpl::telemetry
