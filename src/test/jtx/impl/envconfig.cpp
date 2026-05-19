#include <test/jtx/envconfig.h>

#include <test/jtx/amount.h>

#include <xrpld/core/Config.h>
#include <xrpld/core/ConfigSections.h>

#include <atomic>
#include <map>
#include <memory>
#include <vector>

namespace xrpl::test {

std::atomic<bool> gEnvUseIPv4{false};

void
setupConfigForUnitTests(Config& cfg)
{
    using namespace jtx;
    // Default fees to old values, so tests don't have to worry about changes in
    // Config.h
    cfg.FEES.reference_fee = UNIT_TEST_REFERENCE_FEE;
    cfg.FEES.account_reserve = XRP(200).value().xrp().drops();
    cfg.FEES.owner_reserve = XRP(50).value().xrp().drops();

    // The Beta API (currently v2) is always available to tests
    cfg.BETA_RPC_API = true;

    cfg.overwrite(ConfigSection::nodeDatabase(), "type", "memory");
    cfg.overwrite(ConfigSection::nodeDatabase(), "path", "main");
    cfg.deprecatedClearSection(ConfigSection::importNodeDatabase());
    cfg.legacy("database_path", "");
    cfg.setupControl(true, true, true);
    cfg["server"].append(PORT_PEER);
    cfg[PORT_PEER].set("ip", getEnvLocalhostAddr());

    // Using port 0 asks the operating system to allocate an unused port, which
    // can be obtained after a "bind" call.
    // Works for all system (Linux, Windows, Unix, Mac).
    // Check https://man7.org/linux/man-pages/man7/ip.7.html
    // "ip_local_port_range" section for more info
    cfg[PORT_PEER].set("port", "0");
    cfg[PORT_PEER].set("protocol", "peer");

    cfg["server"].append(PORT_RPC);
    cfg[PORT_RPC].set("ip", getEnvLocalhostAddr());
    cfg[PORT_RPC].set("admin", getEnvLocalhostAddr());
    cfg[PORT_RPC].set("port", "0");
    cfg[PORT_RPC].set("protocol", "http,ws2");

    cfg["server"].append(PORT_WS);
    cfg[PORT_WS].set("ip", getEnvLocalhostAddr());
    cfg[PORT_WS].set("admin", getEnvLocalhostAddr());
    cfg[PORT_WS].set("port", "0");
    cfg[PORT_WS].set("protocol", "ws");
    cfg.SSL_VERIFY = false;
}

namespace jtx {

std::unique_ptr<Config>
noAdmin(std::unique_ptr<Config> cfg)
{
    (*cfg)[PORT_RPC].set("admin", "");
    (*cfg)[PORT_WS].set("admin", "");
    return cfg;
}

std::unique_ptr<Config>
secureGateway(std::unique_ptr<Config> cfg)
{
    (*cfg)[PORT_RPC].set("admin", "");
    (*cfg)[PORT_WS].set("admin", "");
    (*cfg)[PORT_RPC].set("secure_gateway", getEnvLocalhostAddr());
    return cfg;
}

std::unique_ptr<Config>
adminLocalnet(std::unique_ptr<Config> cfg)
{
    (*cfg)[PORT_RPC].set("admin", "127.0.0.0/8");
    (*cfg)[PORT_WS].set("admin", "127.0.0.0/8");
    return cfg;
}

std::unique_ptr<Config>
secureGatewayLocalnet(std::unique_ptr<Config> cfg)
{
    (*cfg)[PORT_RPC].set("admin", "");
    (*cfg)[PORT_WS].set("admin", "");
    (*cfg)[PORT_RPC].set("secure_gateway", "127.0.0.0/8");
    (*cfg)[PORT_WS].set("secure_gateway", "127.0.0.0/8");
    return cfg;
}
std::unique_ptr<Config>
singleThreadIo(std::unique_ptr<Config> cfg)
{
    cfg->IO_WORKERS = 1;
    return cfg;
}

constexpr auto kDefaultSeed = "shUwVw52ofnCUX5m7kPTKzJdr4HEH";

std::unique_ptr<Config>
validator(std::unique_ptr<Config> cfg, std::string const& seed)
{
    // If the config has valid validation keys then we run as a validator.
    cfg->section(SECTION_VALIDATION_SEED)
        .append(std::vector<std::string>{seed.empty() ? kDefaultSeed : seed});
    return cfg;
}

std::unique_ptr<Config>
addGrpcConfig(std::unique_ptr<Config> cfg)
{
    (*cfg)[SECTION_PORT_GRPC].set("ip", getEnvLocalhostAddr());
    (*cfg)[SECTION_PORT_GRPC].set("port", "0");
    return cfg;
}

std::unique_ptr<Config>
addGrpcConfigWithSecureGateway(std::unique_ptr<Config> cfg, std::string const& secureGateway)
{
    (*cfg)[SECTION_PORT_GRPC].set("ip", getEnvLocalhostAddr());

    // Check https://man7.org/linux/man-pages/man7/ip.7.html
    // "ip_local_port_range" section for using 0 ports
    (*cfg)[SECTION_PORT_GRPC].set("port", "0");
    (*cfg)[SECTION_PORT_GRPC].set("secure_gateway", secureGateway);
    return cfg;
}

std::unique_ptr<Config>
addGrpcConfigWithTLS(
    std::unique_ptr<Config> cfg,
    std::string const& certPath,
    std::string const& keyPath)
{
    (*cfg)[SECTION_PORT_GRPC].set("ip", getEnvLocalhostAddr());
    (*cfg)[SECTION_PORT_GRPC].set("port", "0");
    (*cfg)[SECTION_PORT_GRPC].set("ssl_cert", certPath);
    (*cfg)[SECTION_PORT_GRPC].set("ssl_key", keyPath);
    return cfg;
}

std::unique_ptr<Config>
addGrpcConfigWithTLSAndClientCA(
    std::unique_ptr<Config> cfg,
    std::string const& certPath,
    std::string const& keyPath,
    std::string const& clientCAPath)
{
    (*cfg)[SECTION_PORT_GRPC].set("ip", getEnvLocalhostAddr());
    (*cfg)[SECTION_PORT_GRPC].set("port", "0");
    (*cfg)[SECTION_PORT_GRPC].set("ssl_cert", certPath);
    (*cfg)[SECTION_PORT_GRPC].set("ssl_key", keyPath);
    (*cfg)[SECTION_PORT_GRPC].set("ssl_client_ca", clientCAPath);
    return cfg;
}

std::unique_ptr<Config>
addGrpcConfigWithTLSAndCertChain(
    std::unique_ptr<Config> cfg,
    std::string const& certPath,
    std::string const& keyPath,
    std::string const& certChainPath)
{
    (*cfg)[SECTION_PORT_GRPC].set("ip", getEnvLocalhostAddr());
    (*cfg)[SECTION_PORT_GRPC].set("port", "0");
    (*cfg)[SECTION_PORT_GRPC].set("ssl_cert", certPath);
    (*cfg)[SECTION_PORT_GRPC].set("ssl_key", keyPath);
    (*cfg)[SECTION_PORT_GRPC].set("ssl_cert_chain", certChainPath);
    return cfg;
}

std::unique_ptr<Config>
makeConfig(
    std::map<std::string, std::string> extraTxQ,
    std::map<std::string, std::string> extraVoting)
{
    auto p = test::jtx::envconfig();
    auto& section = p->section("transaction_queue");
    section.set("ledgers_in_queue", "2");
    section.set("minimum_queue_size", "2");
    section.set("min_ledgers_to_compute_size_limit", "3");
    section.set("max_ledger_counts_to_store", "100");
    section.set("retry_sequence_percent", "25");
    section.set("normal_consensus_increase_percent", "0");

    for (auto const& [k, v] : extraTxQ)
        section.set(k, v);

    // Some tests specify different fee settings that are enabled by
    // a FeeVote
    if (!extraVoting.empty())
    {
        auto& votingSection = p->section("voting");
        for (auto const& [k, v] : extraVoting)
        {
            votingSection.set(k, v);
        }

        // In order for the vote to occur, we must run as a validator
        p->section("validation_seed").legacy("shUwVw52ofnCUX5m7kPTKzJdr4HEH");
    }
    return p;
}

}  // namespace jtx
}  // namespace xrpl::test
