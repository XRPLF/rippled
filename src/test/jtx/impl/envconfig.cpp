#include <test/jtx/envconfig.h>

#include <test/jtx/amount.h>

#include <xrpld/core/Config.h>

#include <xrpl/basics/BasicConfig.h>

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

    cfg.overwrite(kSECTION_NODE_DATABASE, kKEY_TYPE, "memory");
    cfg.overwrite(kSECTION_NODE_DATABASE, kKEY_PATH, "main");
    cfg.deprecatedClearSection(kSECTION_IMPORT_NODE_DATABASE);
    cfg.legacy(kSECTION_DATABASE_PATH, "");
    cfg.setupControl(true, true, true);
    cfg[kSECTION_SERVER].append(kSECTION_PORT_PEER);
    cfg[kSECTION_PORT_PEER].set(kKEY_IP, getEnvLocalhostAddr());

    // Using port 0 asks the operating system to allocate an unused port, which
    // can be obtained after a "bind" call.
    // Works for all system (Linux, Windows, Unix, Mac).
    // Check https://man7.org/linux/man-pages/man7/ip.7.html
    // "ip_local_port_range" section for more info
    cfg[kSECTION_PORT_PEER].set(kKEY_PORT, "0");
    cfg[kSECTION_PORT_PEER].set(kKEY_PROTOCOL, "peer");

    cfg[kSECTION_SERVER].append(kSECTION_PORT_RPC);
    cfg[kSECTION_PORT_RPC].set(kKEY_IP, getEnvLocalhostAddr());
    cfg[kSECTION_PORT_RPC].set(kKEY_ADMIN, getEnvLocalhostAddr());
    cfg[kSECTION_PORT_RPC].set(kKEY_PORT, "0");
    cfg[kSECTION_PORT_RPC].set(kKEY_PROTOCOL, "http,ws2");

    cfg[kSECTION_SERVER].append(kSECTION_PORT_WS);
    cfg[kSECTION_PORT_WS].set(kKEY_IP, getEnvLocalhostAddr());
    cfg[kSECTION_PORT_WS].set(kKEY_ADMIN, getEnvLocalhostAddr());
    cfg[kSECTION_PORT_WS].set(kKEY_PORT, "0");
    cfg[kSECTION_PORT_WS].set(kKEY_PROTOCOL, "ws");
    cfg.SSL_VERIFY = false;
}

namespace jtx {

std::unique_ptr<Config>
noAdmin(std::unique_ptr<Config> cfg)
{
    (*cfg)[kSECTION_PORT_RPC].set(kKEY_ADMIN, "");
    (*cfg)[kSECTION_PORT_WS].set(kKEY_ADMIN, "");
    return cfg;
}

std::unique_ptr<Config>
secureGateway(std::unique_ptr<Config> cfg)
{
    (*cfg)[kSECTION_PORT_RPC].set(kKEY_ADMIN, "");
    (*cfg)[kSECTION_PORT_WS].set(kKEY_ADMIN, "");
    (*cfg)[kSECTION_PORT_RPC].set(kKEY_SECURE_GATEWAY, getEnvLocalhostAddr());
    return cfg;
}

std::unique_ptr<Config>
adminLocalnet(std::unique_ptr<Config> cfg)
{
    (*cfg)[kSECTION_PORT_RPC].set(kKEY_ADMIN, "127.0.0.0/8");
    (*cfg)[kSECTION_PORT_WS].set(kKEY_ADMIN, "127.0.0.0/8");
    return cfg;
}

std::unique_ptr<Config>
secureGatewayLocalnet(std::unique_ptr<Config> cfg)
{
    (*cfg)[kSECTION_PORT_RPC].set(kKEY_ADMIN, "");
    (*cfg)[kSECTION_PORT_WS].set(kKEY_ADMIN, "");
    (*cfg)[kSECTION_PORT_RPC].set(kKEY_SECURE_GATEWAY, "127.0.0.0/8");
    (*cfg)[kSECTION_PORT_WS].set(kKEY_SECURE_GATEWAY, "127.0.0.0/8");
    return cfg;
}
std::unique_ptr<Config>
singleThreadIo(std::unique_ptr<Config> cfg)
{
    cfg->IO_WORKERS = 1;
    return cfg;
}

auto constexpr kDEFAULTSEED = "shUwVw52ofnCUX5m7kPTKzJdr4HEH";

std::unique_ptr<Config>
validator(std::unique_ptr<Config> cfg, std::string const& seed)
{
    // If the config has valid validation keys then we run as a validator.
    cfg->section(kSECTION_VALIDATION_SEED)
        .append(std::vector<std::string>{seed.empty() ? kDEFAULTSEED : seed});
    return cfg;
}

std::unique_ptr<Config>
addGrpcConfig(std::unique_ptr<Config> cfg)
{
    (*cfg)[kSECTION_PORT_GRPC].set(kKEY_IP, getEnvLocalhostAddr());
    (*cfg)[kSECTION_PORT_GRPC].set(kKEY_PORT, "0");
    return cfg;
}

std::unique_ptr<Config>
addGrpcConfigWithSecureGateway(std::unique_ptr<Config> cfg, std::string const& secureGateway)
{
    (*cfg)[kSECTION_PORT_GRPC].set(kKEY_IP, getEnvLocalhostAddr());

    // Check https://man7.org/linux/man-pages/man7/ip.7.html
    // "ip_local_port_range" section for using 0 ports
    (*cfg)[kSECTION_PORT_GRPC].set(kKEY_PORT, "0");
    (*cfg)[kSECTION_PORT_GRPC].set(kKEY_SECURE_GATEWAY, secureGateway);
    return cfg;
}

std::unique_ptr<Config>
addGrpcConfigWithTLS(
    std::unique_ptr<Config> cfg,
    std::string const& certPath,
    std::string const& keyPath)
{
    (*cfg)[kSECTION_PORT_GRPC].set(kKEY_IP, getEnvLocalhostAddr());
    (*cfg)[kSECTION_PORT_GRPC].set(kKEY_PORT, "0");
    (*cfg)[kSECTION_PORT_GRPC].set(kKEY_SSL_CERT, certPath);
    (*cfg)[kSECTION_PORT_GRPC].set(kKEY_SSL_KEY, keyPath);
    return cfg;
}

std::unique_ptr<Config>
addGrpcConfigWithTLSAndClientCA(
    std::unique_ptr<Config> cfg,
    std::string const& certPath,
    std::string const& keyPath,
    std::string const& clientCAPath)
{
    (*cfg)[kSECTION_PORT_GRPC].set(kKEY_IP, getEnvLocalhostAddr());
    (*cfg)[kSECTION_PORT_GRPC].set(kKEY_PORT, "0");
    (*cfg)[kSECTION_PORT_GRPC].set(kKEY_SSL_CERT, certPath);
    (*cfg)[kSECTION_PORT_GRPC].set(kKEY_SSL_KEY, keyPath);
    (*cfg)[kSECTION_PORT_GRPC].set(kKEY_SSL_CLIENT_CA, clientCAPath);
    return cfg;
}

std::unique_ptr<Config>
addGrpcConfigWithTLSAndCertChain(
    std::unique_ptr<Config> cfg,
    std::string const& certPath,
    std::string const& keyPath,
    std::string const& certChainPath)
{
    (*cfg)[kSECTION_PORT_GRPC].set(kKEY_IP, getEnvLocalhostAddr());
    (*cfg)[kSECTION_PORT_GRPC].set(kKEY_PORT, "0");
    (*cfg)[kSECTION_PORT_GRPC].set(kKEY_SSL_CERT, certPath);
    (*cfg)[kSECTION_PORT_GRPC].set(kKEY_SSL_KEY, keyPath);
    (*cfg)[kSECTION_PORT_GRPC].set(kKEY_SSL_CERT_CHAIN, certChainPath);
    return cfg;
}

std::unique_ptr<Config>
makeConfig(
    std::map<std::string, std::string> extraTxQ,
    std::map<std::string, std::string> extraVoting)
{
    auto p = test::jtx::envconfig();
    auto& section = p->section(kSECTION_TRANSACTION_QUEUE);
    section.set(kKEY_LEDGERS_IN_QUEUE, "2");
    section.set(kKEY_MINIMUM_QUEUE_SIZE, "2");
    section.set(kKEY_MIN_LEDGERS_TO_COMPUTE_SIZE_LIMIT, "3");
    section.set(kKEY_MAX_LEDGER_COUNTS_TO_STORE, "100");
    section.set(kKEY_RETRY_SEQUENCE_PERCENT, "25");
    section.set(kKEY_NORMAL_CONSENSUS_INCREASE_PERCENT, "0");

    for (auto const& [k, v] : extraTxQ)
        section.set(k, v);

    // Some tests specify different fee settings that are enabled by
    // a FeeVote
    if (!extraVoting.empty())
    {
        auto& votingSection = p->section(kSECTION_VOTING);
        for (auto const& [k, v] : extraVoting)
        {
            votingSection.set(k, v);
        }

        // In order for the vote to occur, we must run as a validator
        p->section(kSECTION_VALIDATION_SEED).legacy("shUwVw52ofnCUX5m7kPTKzJdr4HEH");
    }
    return p;
}

}  // namespace jtx
}  // namespace xrpl::test
