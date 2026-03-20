/** Parser for the [telemetry] section of xrpld.cfg.

    Reads configuration values from the config file and populates a
    Telemetry::Setup struct. All options have sensible defaults so the
    section can be minimal or omitted entirely.

    See cfg/xrpld-example.cfg for the full list of available options.
*/

#include <xrpl/telemetry/Telemetry.h>

namespace xrpl {
namespace telemetry {

Telemetry::Setup
setup_Telemetry(
    Section const& section,
    std::string const& nodePublicKey,
    std::string const& version)
{
    Telemetry::Setup setup;

    setup.enabled = section.value_or<int>("enabled", 0) != 0;
    setup.serviceName = section.value_or<std::string>("service_name", "rippled");
    setup.serviceVersion = version;
    setup.serviceInstanceId = section.value_or<std::string>("service_instance_id", nodePublicKey);

    setup.exporterType = section.value_or<std::string>("exporter", "otlp_http");
    setup.exporterEndpoint =
        section.value_or<std::string>("endpoint", "http://localhost:4318/v1/traces");

    setup.useTls = section.value_or<int>("use_tls", 0) != 0;
    setup.tlsCertPath = section.value_or<std::string>("tls_ca_cert", "");

    setup.samplingRatio = section.value_or<double>("sampling_ratio", 1.0);

    setup.batchSize = section.value_or<std::uint32_t>("batch_size", 512u);
    setup.batchDelay =
        std::chrono::milliseconds{section.value_or<std::uint32_t>("batch_delay_ms", 5000u)};
    setup.maxQueueSize = section.value_or<std::uint32_t>("max_queue_size", 2048u);

    setup.traceTransactions = section.value_or<int>("trace_transactions", 1) != 0;
    setup.traceConsensus = section.value_or<int>("trace_consensus", 1) != 0;
    setup.traceRpc = section.value_or<int>("trace_rpc", 1) != 0;
    setup.tracePeer = section.value_or<int>("trace_peer", 0) != 0;
    setup.traceLedger = section.value_or<int>("trace_ledger", 1) != 0;

    return setup;
}

}  // namespace telemetry
}  // namespace xrpl
