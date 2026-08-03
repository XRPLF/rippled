#include <test/jtx/envconfig.h>

#include <test/jtx/amount.h>

#include <xrpld/core/Config.h>

#include <xrpl/config/Constants.h>

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
    cfg.fees.referenceFee = UNIT_TEST_REFERENCE_FEE;
    cfg.fees.accountReserve = XRP(200).value().xrp().drops();
    cfg.fees.ownerReserve = XRP(50).value().xrp().drops();

    // The Beta API (currently v2) is always available to tests
    cfg.betaRpcApi = true;

    cfg.overwrite(Sections::kNodeDatabase, Keys::kType, "memory");
    cfg.overwrite(Sections::kNodeDatabase, Keys::kPath, "main");
    cfg.deprecatedClearSection(Sections::kImportNodeDatabase);
    cfg.legacy(Sections::kDatabasePath, "");
    cfg.setupControl(true, true, true);
    cfg[Sections::kServer].append(Sections::kPortPeer);
    cfg[Sections::kPortPeer].set(Keys::kIp, getEnvLocalhostAddr());

    // Using port 0 asks the operating system to allocate an unused port, which
    // can be obtained after a "bind" call.
    // Works for all system (Linux, Windows, Unix, Mac).
    // Check https://man7.org/linux/man-pages/man7/ip.7.html
    // "ip_local_port_range" section for more info
    cfg[Sections::kPortPeer].set(Keys::kPort, "0");
    cfg[Sections::kPortPeer].set(Keys::kProtocol, "peer");

    cfg[Sections::kServer].append(Sections::kPortRpc);
    cfg[Sections::kPortRpc].set(Keys::kIp, getEnvLocalhostAddr());
    cfg[Sections::kPortRpc].set(Keys::kAdmin, getEnvLocalhostAddr());
    cfg[Sections::kPortRpc].set(Keys::kPort, "0");
    cfg[Sections::kPortRpc].set(Keys::kProtocol, "http,ws2");

    cfg[Sections::kServer].append(Sections::kPortWs);
    cfg[Sections::kPortWs].set(Keys::kIp, getEnvLocalhostAddr());
    cfg[Sections::kPortWs].set(Keys::kAdmin, getEnvLocalhostAddr());
    cfg[Sections::kPortWs].set(Keys::kPort, "0");
    cfg[Sections::kPortWs].set(Keys::kProtocol, "ws");
    cfg.sslVerify = false;
}

namespace jtx {

std::unique_ptr<Config>
noAdmin(std::unique_ptr<Config> cfg)
{
    (*cfg)[Sections::kPortRpc].set(Keys::kAdmin, "");
    (*cfg)[Sections::kPortWs].set(Keys::kAdmin, "");
    return cfg;
}

std::unique_ptr<Config>
secureGateway(std::unique_ptr<Config> cfg)
{
    (*cfg)[Sections::kPortRpc].set(Keys::kAdmin, "");
    (*cfg)[Sections::kPortWs].set(Keys::kAdmin, "");
    (*cfg)[Sections::kPortRpc].set(Keys::kSecureGateway, getEnvLocalhostAddr());
    return cfg;
}

std::unique_ptr<Config>
adminLocalnet(std::unique_ptr<Config> cfg)
{
    (*cfg)[Sections::kPortRpc].set(Keys::kAdmin, "127.0.0.0/8");
    (*cfg)[Sections::kPortWs].set(Keys::kAdmin, "127.0.0.0/8");
    return cfg;
}

std::unique_ptr<Config>
secureGatewayLocalnet(std::unique_ptr<Config> cfg)
{
    (*cfg)[Sections::kPortRpc].set(Keys::kAdmin, "");
    (*cfg)[Sections::kPortWs].set(Keys::kAdmin, "");
    (*cfg)[Sections::kPortRpc].set(Keys::kSecureGateway, "127.0.0.0/8");
    (*cfg)[Sections::kPortWs].set(Keys::kSecureGateway, "127.0.0.0/8");
    return cfg;
}
std::unique_ptr<Config>
singleThreadIo(std::unique_ptr<Config> cfg)
{
    cfg->ioWorkers = 1;
    return cfg;
}

constexpr auto kDefaultSeed = "shUwVw52ofnCUX5m7kPTKzJdr4HEH";

std::unique_ptr<Config>
validator(std::unique_ptr<Config> cfg, std::string const& seed)
{
    // If the config has valid validation keys then we run as a validator.
    cfg->section(Sections::kValidationSeed)
        .append(std::vector<std::string>{seed.empty() ? kDefaultSeed : seed});
    return cfg;
}

std::unique_ptr<Config>
addGrpcConfig(std::unique_ptr<Config> cfg)
{
    (*cfg)[Sections::kPortGrpc].set(Keys::kIp, getEnvLocalhostAddr());
    (*cfg)[Sections::kPortGrpc].set(Keys::kPort, "0");
    return cfg;
}

std::unique_ptr<Config>
addGrpcConfigWithSecureGateway(std::unique_ptr<Config> cfg, std::string const& secureGateway)
{
    (*cfg)[Sections::kPortGrpc].set(Keys::kIp, getEnvLocalhostAddr());

    // Check https://man7.org/linux/man-pages/man7/ip.7.html
    // "ip_local_port_range" section for using 0 ports
    (*cfg)[Sections::kPortGrpc].set(Keys::kPort, "0");
    (*cfg)[Sections::kPortGrpc].set(Keys::kSecureGateway, secureGateway);
    return cfg;
}

std::unique_ptr<Config>
addGrpcConfigWithTLS(
    std::unique_ptr<Config> cfg,
    std::string const& certPath,
    std::string const& keyPath)
{
    (*cfg)[Sections::kPortGrpc].set(Keys::kIp, getEnvLocalhostAddr());
    (*cfg)[Sections::kPortGrpc].set(Keys::kPort, "0");
    (*cfg)[Sections::kPortGrpc].set(Keys::kSslCert, certPath);
    (*cfg)[Sections::kPortGrpc].set(Keys::kSslKey, keyPath);
    return cfg;
}

std::unique_ptr<Config>
addGrpcConfigWithTLSAndClientCA(
    std::unique_ptr<Config> cfg,
    std::string const& certPath,
    std::string const& keyPath,
    std::string const& clientCAPath)
{
    (*cfg)[Sections::kPortGrpc].set(Keys::kIp, getEnvLocalhostAddr());
    (*cfg)[Sections::kPortGrpc].set(Keys::kPort, "0");
    (*cfg)[Sections::kPortGrpc].set(Keys::kSslCert, certPath);
    (*cfg)[Sections::kPortGrpc].set(Keys::kSslKey, keyPath);
    (*cfg)[Sections::kPortGrpc].set(Keys::kSslClientCa, clientCAPath);
    return cfg;
}

std::unique_ptr<Config>
addGrpcConfigWithTLSAndCertChain(
    std::unique_ptr<Config> cfg,
    std::string const& certPath,
    std::string const& keyPath,
    std::string const& certChainPath)
{
    (*cfg)[Sections::kPortGrpc].set(Keys::kIp, getEnvLocalhostAddr());
    (*cfg)[Sections::kPortGrpc].set(Keys::kPort, "0");
    (*cfg)[Sections::kPortGrpc].set(Keys::kSslCert, certPath);
    (*cfg)[Sections::kPortGrpc].set(Keys::kSslKey, keyPath);
    (*cfg)[Sections::kPortGrpc].set(Keys::kSslCertChain, certChainPath);
    return cfg;
}

std::unique_ptr<Config>
makeConfig(
    std::map<std::string, std::string> extraTxQ,
    std::map<std::string, std::string> extraVoting)
{
    auto p = test::jtx::envconfig();
    auto& section = p->section(Sections::kTransactionQueue);
    section.set(Keys::kLedgersInQueue, "2");
    section.set(Keys::kMinimumQueueSize, "2");
    section.set(Keys::kMinLedgersToComputeSizeLimit, "3");
    section.set(Keys::kMaxLedgerCountsToStore, "100");
    section.set(Keys::kRetrySequencePercent, "25");
    section.set(Keys::kNormalConsensusIncreasePercent, "0");

    for (auto const& [k, v] : extraTxQ)
        section.set(k, v);

    // Some tests specify different fee settings that are enabled by
    // a FeeVote
    if (!extraVoting.empty())
    {
        auto& votingSection = p->section(Sections::kVoting);
        for (auto const& [k, v] : extraVoting)
        {
            votingSection.set(k, v);
        }

        // In order for the vote to occur, we must run as a validator
        p->section(Sections::kValidationSeed).legacy("shUwVw52ofnCUX5m7kPTKzJdr4HEH");
    }
    return p;
}

}  // namespace jtx
}  // namespace xrpl::test
