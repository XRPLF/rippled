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

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

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
constexpr char const* tlsClientCert = "tls_client_cert";
constexpr char const* tlsClientKey = "tls_client_key";
constexpr char const* batchSize = "batch_size";
constexpr char const* batchDelayMs = "batch_delay_ms";
constexpr char const* maxQueueSize = "max_queue_size";
constexpr char const* traceTransactions = "trace_transactions";
constexpr char const* traceConsensus = "trace_consensus";
constexpr char const* traceRpc = "trace_rpc";
constexpr char const* tracePeer = "trace_peer";
constexpr char const* traceLedger = "trace_ledger";
constexpr char const* consensusTraceStrategy = "consensus_trace_strategy";
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

/**
 * Throw unless the given path names a regular file this process can read.
 *
 * An empty path means the option is unset, which every caller allows. Opening
 * the file proves it is present and that the read permission check passes,
 * without loading any of its contents — one of these paths names a private key.
 * Nothing here checks that the contents parse as PEM.
 *
 * A path that is not a regular file is rejected before the open, because
 * opening a FIFO waits for a writer.
 *
 * @param path       Path taken from the config, possibly empty.
 * @param configKey  Config key the path came from, named in the message. Not
 * called `key`, which would hide the `key` namespace above.
 * @throws std::runtime_error  If the path is non-empty and cannot be read.
 */
void
requireReadableFile(std::string const& path, char const* configKey)
{
    if (path.empty())
        return;

    // Each branch sets the reason and stops. The two that come from the
    // operating system reuse its message; the middle one has no errno to read.
    std::error_code ec;
    std::string reason;
    auto const fileStatus = std::filesystem::status(path, ec);
    if (ec)
    {
        reason = ec.message();
    }
    else if (!std::filesystem::is_regular_file(fileStatus))
    {
        reason = "not a regular file";
    }
    else if (std::ifstream stream{path, std::ios::in}; !stream)
    {
        reason = std::error_code{errno, std::generic_category()}.message();
    }

    if (!reason.empty())
    {
        Throw<std::runtime_error>(
            std::string{"[telemetry] "} + configKey + " cannot be read: " + path + " - " + reason);
    }
}

/**
 * Throw unless an endpoint URL is one the client certificate can be used on.
 *
 * The OTLP/HTTP exporter turns TLS on from the URL scheme alone, and matches
 * "https:" exactly and case-sensitively. So a client certificate only reaches
 * the collector on an https endpoint, and this check is what holds that
 * invariant: with a client certificate configured, the endpoint is an https URL.
 * "https://" is required in full, which is stricter than the exporter's own
 * test, so anything this accepts the exporter also treats as TLS.
 *
 * @param endpoint   Endpoint URL from the config, or the built-in default.
 * @param configKey  Config key the URL came from, named in the message.
 * @throws std::runtime_error  If the URL does not begin with "https://".
 */
void
requireHttpsEndpoint(std::string const& endpoint, char const* configKey)
{
    constexpr std::string_view kHttpsPrefix{"https://"};

    if (std::string_view{endpoint}.starts_with(kHttpsPrefix))
        return;

    Throw<std::runtime_error>(
        std::string("Invalid value '") + configKey + "' in " + kSectionLabel +
        ": must start with '" + std::string{kHttpsPrefix} + "' when " + key::tlsClientCert +
        " is set, but is '" + endpoint + "'.");
}

/**
 * Map a `consensus_trace_strategy` value onto its enumerator.
 *
 * Only the two documented spellings are accepted. A typo would otherwise pick
 * the default silently, and the operator would never learn the setting had no
 * effect. Matching is exact and case-sensitive, like every other value in this
 * section.
 *
 * @param value  Raw config value; empty means the key was absent.
 * @return The matching strategy, or Deterministic when the key was absent.
 * @throws std::runtime_error  If the value is neither documented spelling.
 */
[[nodiscard]] ConsensusTraceStrategy
readConsensusTraceStrategy(std::string const& value)
{
    if (value.empty() || value == strategyName(ConsensusTraceStrategy::Deterministic))
        return ConsensusTraceStrategy::Deterministic;

    if (value == strategyName(ConsensusTraceStrategy::Random))
        return ConsensusTraceStrategy::Random;

    Throw<std::runtime_error>(
        std::string("Invalid value '") + key::consensusTraceStrategy + "' in " + kSectionLabel +
        ": must be '" + strategyName(ConsensusTraceStrategy::Deterministic) + "' or '" +
        strategyName(ConsensusTraceStrategy::Random) + "'.");
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
    setup.tlsClientCertPath = section.valueOr<std::string>(key::tlsClientCert, "");
    setup.tlsClientKeyPath = section.valueOr<std::string>(key::tlsClientKey, "");

    // The mutual TLS (mTLS) checks below are fatal, so gate them on the one
    // thing this parser can know: `enabled` is 1. With `enabled` 0 a leftover
    // cert line must never stop the node from booting.
    //
    // The predicate is only that config switch, not whether an exporter can
    // exist. This file has no preprocessor guard, so both checks also run in a
    // -Dtelemetry=OFF build, where makeTelemetry() returns the null
    // implementation whatever `enabled` says.
    if (setup.enabled)
    {
        // mTLS needs both the client certificate and its private key.
        // Supplying only one fails later with a cryptic SSL handshake error, so
        // reject the partial configuration here with an actionable message.
        if (setup.tlsClientCertPath.empty() != setup.tlsClientKeyPath.empty())
        {
            Throw<std::runtime_error>(
                "[telemetry] tls_client_cert and tls_client_key must be set together "
                "(set both for mutual TLS, or neither for one-way TLS).");
        }

        // Still inside the enabled branch. mTLS only takes effect when TLS is
        // on, so a client certificate set with use_tls=0 would be ignored and
        // any exporter that did run would connect in plaintext. Reject that
        // contradiction instead of failing open. tls_ca_cert is deliberately
        // not checked this way.
        if (!setup.tlsClientCertPath.empty() && !setup.useTls)
        {
            Throw<std::runtime_error>(
                "[telemetry] tls_client_cert/tls_client_key require use_tls=1 "
                "(set use_tls=1 to enable mutual TLS, or remove the cert paths).");
        }

        // Still inside the enabled branch, and checked before the files are
        // opened so a scheme problem is not hidden behind a path problem. The
        // exporter reads TLS off the endpoint scheme, so a client certificate is
        // only presented on an https endpoint. tls_ca_cert is left out of this
        // check: it only names a trust store, while a client certificate is this
        // node's own identity and has to reach the collector to mean anything.
        if (!setup.tlsClientCertPath.empty())
            requireHttpsEndpoint(setup.tracesEndpoint, key::tracesEndpoint);

        // Still inside the enabled branch. The exporter opens these files only
        // when TLS is on, so check them only then: a bad path behind use_tls=0
        // stops nothing. Checking here turns what would otherwise surface much
        // later as an opaque handshake failure into a startup error naming the
        // key. Each path is optional; an empty one is skipped.
        if (setup.useTls)
        {
            requireReadableFile(setup.tlsCertPath, key::tlsCaCert);
            requireReadableFile(setup.tlsClientCertPath, key::tlsClientCert);
            requireReadableFile(setup.tlsClientKeyPath, key::tlsClientKey);
        }
    }

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
        readConsensusTraceStrategy(section.valueOr<std::string>(key::consensusTraceStrategy, ""));

    return setup;
}

}  // namespace xrpl::telemetry
