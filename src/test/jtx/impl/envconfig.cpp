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
    cfg.FEES.reference_fee = UNIT_TEST_REFERENCE_FEE;
    cfg.FEES.account_reserve = XRP(200).value().xrp().drops();
    cfg.FEES.owner_reserve = XRP(50).value().xrp().drops();

    // The Beta API (currently v2) is always available to tests
    cfg.BETA_RPC_API = true;

    cfg.overwrite(Sections::kNODE_DATABASE, Keys::kTYPE, "memory");
    cfg.overwrite(Sections::kNODE_DATABASE, Keys::kPATH, "main");
    cfg.deprecatedClearSection(Sections::kIMPORT_NODE_DATABASE);
    cfg.legacy(Sections::kDATABASE_PATH, "");
    cfg.setupControl(true, true, true);
    cfg[Sections::kSERVER].append(Sections::kPORT_PEER);
    cfg[Sections::kPORT_PEER].set(Keys::kIP, getEnvLocalhostAddr());

    // Using port 0 asks the operating system to allocate an unused port, which
    // can be obtained after a "bind" call.
    // Works for all system (Linux, Windows, Unix, Mac).
    // Check https://man7.org/linux/man-pages/man7/ip.7.html
    // "ip_local_port_range" section for more info
    cfg[Sections::kPORT_PEER].set(Keys::kPORT, "0");
    cfg[Sections::kPORT_PEER].set(Keys::kPROTOCOL, "peer");

    cfg[Sections::kSERVER].append(Sections::kPORT_RPC);
    cfg[Sections::kPORT_RPC].set(Keys::kIP, getEnvLocalhostAddr());
    cfg[Sections::kPORT_RPC].set(Keys::kADMIN, getEnvLocalhostAddr());
    cfg[Sections::kPORT_RPC].set(Keys::kPORT, "0");
    cfg[Sections::kPORT_RPC].set(Keys::kPROTOCOL, "http,ws2");

    cfg[Sections::kSERVER].append(Sections::kPORT_WS);
    cfg[Sections::kPORT_WS].set(Keys::kIP, getEnvLocalhostAddr());
    cfg[Sections::kPORT_WS].set(Keys::kADMIN, getEnvLocalhostAddr());
    cfg[Sections::kPORT_WS].set(Keys::kPORT, "0");
    cfg[Sections::kPORT_WS].set(Keys::kPROTOCOL, "ws");
    cfg.SSL_VERIFY = false;
}

namespace jtx {

std::unique_ptr<Config>
noAdmin(std::unique_ptr<Config> cfg)
{
    (*cfg)[Sections::kPORT_RPC].set(Keys::kADMIN, "");
    (*cfg)[Sections::kPORT_WS].set(Keys::kADMIN, "");
    return cfg;
}

std::unique_ptr<Config>
secureGateway(std::unique_ptr<Config> cfg)
{
    (*cfg)[Sections::kPORT_RPC].set(Keys::kADMIN, "");
    (*cfg)[Sections::kPORT_WS].set(Keys::kADMIN, "");
    (*cfg)[Sections::kPORT_RPC].set(Keys::kSECURE_GATEWAY, getEnvLocalhostAddr());
    return cfg;
}

std::unique_ptr<Config>
adminLocalnet(std::unique_ptr<Config> cfg)
{
    (*cfg)[Sections::kPORT_RPC].set(Keys::kADMIN, "127.0.0.0/8");
    (*cfg)[Sections::kPORT_WS].set(Keys::kADMIN, "127.0.0.0/8");
    return cfg;
}

std::unique_ptr<Config>
secureGatewayLocalnet(std::unique_ptr<Config> cfg)
{
    (*cfg)[Sections::kPORT_RPC].set(Keys::kADMIN, "");
    (*cfg)[Sections::kPORT_WS].set(Keys::kADMIN, "");
    (*cfg)[Sections::kPORT_RPC].set(Keys::kSECURE_GATEWAY, "127.0.0.0/8");
    (*cfg)[Sections::kPORT_WS].set(Keys::kSECURE_GATEWAY, "127.0.0.0/8");
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
    cfg->section(Sections::kVALIDATION_SEED)
        .append(std::vector<std::string>{seed.empty() ? kDEFAULTSEED : seed});
    return cfg;
}

std::unique_ptr<Config>
addGrpcConfig(std::unique_ptr<Config> cfg)
{
    (*cfg)[Sections::kPORT_GRPC].set(Keys::kIP, getEnvLocalhostAddr());
    (*cfg)[Sections::kPORT_GRPC].set(Keys::kPORT, "0");
    return cfg;
}

std::unique_ptr<Config>
addGrpcConfigWithSecureGateway(std::unique_ptr<Config> cfg, std::string const& secureGateway)
{
    (*cfg)[Sections::kPORT_GRPC].set(Keys::kIP, getEnvLocalhostAddr());

    // Check https://man7.org/linux/man-pages/man7/ip.7.html
    // "ip_local_port_range" section for using 0 ports
    (*cfg)[Sections::kPORT_GRPC].set(Keys::kPORT, "0");
    (*cfg)[Sections::kPORT_GRPC].set(Keys::kSECURE_GATEWAY, secureGateway);
    return cfg;
}

std::unique_ptr<Config>
addGrpcConfigWithTLS(
    std::unique_ptr<Config> cfg,
    std::string const& certPath,
    std::string const& keyPath)
{
    (*cfg)[Sections::kPORT_GRPC].set(Keys::kIP, getEnvLocalhostAddr());
    (*cfg)[Sections::kPORT_GRPC].set(Keys::kPORT, "0");
    (*cfg)[Sections::kPORT_GRPC].set(Keys::kSSL_CERT, certPath);
    (*cfg)[Sections::kPORT_GRPC].set(Keys::kSSL_KEY, keyPath);
    return cfg;
}

std::unique_ptr<Config>
addGrpcConfigWithTLSAndClientCA(
    std::unique_ptr<Config> cfg,
    std::string const& certPath,
    std::string const& keyPath,
    std::string const& clientCAPath)
{
    (*cfg)[Sections::kPORT_GRPC].set(Keys::kIP, getEnvLocalhostAddr());
    (*cfg)[Sections::kPORT_GRPC].set(Keys::kPORT, "0");
    (*cfg)[Sections::kPORT_GRPC].set(Keys::kSSL_CERT, certPath);
    (*cfg)[Sections::kPORT_GRPC].set(Keys::kSSL_KEY, keyPath);
    (*cfg)[Sections::kPORT_GRPC].set(Keys::kSSL_CLIENT_CA, clientCAPath);
    return cfg;
}

std::unique_ptr<Config>
addGrpcConfigWithTLSAndCertChain(
    std::unique_ptr<Config> cfg,
    std::string const& certPath,
    std::string const& keyPath,
    std::string const& certChainPath)
{
    (*cfg)[Sections::kPORT_GRPC].set(Keys::kIP, getEnvLocalhostAddr());
    (*cfg)[Sections::kPORT_GRPC].set(Keys::kPORT, "0");
    (*cfg)[Sections::kPORT_GRPC].set(Keys::kSSL_CERT, certPath);
    (*cfg)[Sections::kPORT_GRPC].set(Keys::kSSL_KEY, keyPath);
    (*cfg)[Sections::kPORT_GRPC].set(Keys::kSSL_CERT_CHAIN, certChainPath);
    return cfg;
}

std::unique_ptr<Config>
makeConfig(
    std::map<std::string, std::string> extraTxQ,
    std::map<std::string, std::string> extraVoting)
{
    auto p = test::jtx::envconfig();
    auto& section = p->section(Sections::kTRANSACTION_QUEUE);
    section.set(Keys::kLEDGERS_IN_QUEUE, "2");
    section.set(Keys::kMINIMUM_QUEUE_SIZE, "2");
    section.set(Keys::kMIN_LEDGERS_TO_COMPUTE_SIZE_LIMIT, "3");
    section.set(Keys::kMAX_LEDGER_COUNTS_TO_STORE, "100");
    section.set(Keys::kRETRY_SEQUENCE_PERCENT, "25");
    section.set(Keys::kNORMAL_CONSENSUS_INCREASE_PERCENT, "0");

    for (auto const& [k, v] : extraTxQ)
        section.set(k, v);

    // Some tests specify different fee settings that are enabled by
    // a FeeVote
    if (!extraVoting.empty())
    {
        auto& votingSection = p->section(Sections::kVOTING);
        for (auto const& [k, v] : extraVoting)
        {
            votingSection.set(k, v);
        }

        // In order for the vote to occur, we must run as a validator
        p->section(Sections::kVALIDATION_SEED).legacy("shUwVw52ofnCUX5m7kPTKzJdr4HEH");
    }
    return p;
}

}  // namespace jtx
}  // namespace xrpl::test
