#ifndef XRPL_TEST_JTX_ENVCONFIG_H_INCLUDED
#define XRPL_TEST_JTX_ENVCONFIG_H_INCLUDED

#include <xrpld/core/Config.h>

namespace ripple {
namespace test {

// frequently used macros defined here for convenience.
#define PORT_WS "port_ws"
#define PORT_RPC "port_rpc"
#define PORT_PEER "port_peer"

extern std::atomic<bool> envUseIPv4;

inline char const*
getEnvLocalhostAddr()
{
    return envUseIPv4 ? "127.0.0.1" : "::1";
}

/// @brief initializes a config object for use with jtx::Env
///
/// @param config the configuration object to be initialized
extern void
setupConfigForUnitTests(Config& config);

namespace jtx {

/// @brief creates and initializes a default
/// configuration for jtx::Env
///
/// @return unique_ptr to Config instance
inline std::unique_ptr<Config>
envconfig()
{
    auto p = std::make_unique<Config>();
    setupConfigForUnitTests(*p);
    return p;
}

/// @brief creates and initializes a default configuration for jtx::Env and
/// invokes the provided function/lambda with the configuration object.
///
/// @param modfunc callable function or lambda to modify the default config.
/// The first argument to the function must be std::unique_ptr to
/// ripple::Config. The function takes ownership of the unique_ptr and
/// relinquishes ownership by returning a unique_ptr.
///
/// @param args additional arguments that will be passed to
/// the config modifier function (optional)
///
/// @return unique_ptr to Config instance
template <class F, class... Args>
std::unique_ptr<Config>
envconfig(F&& modfunc, Args&&... args)
{
    return modfunc(envconfig(), std::forward<Args>(args)...);
}

/// @brief adjust config so no admin ports are enabled
///
/// this is intended for use with envconfig, as in
/// envconfig(no_admin)
///
/// @param cfg config instance to be modified
///
/// @return unique_ptr to Config instance
std::unique_ptr<Config> no_admin(std::unique_ptr<Config>);

std::unique_ptr<Config> secure_gateway(std::unique_ptr<Config>);

std::unique_ptr<Config> admin_localnet(std::unique_ptr<Config>);

std::unique_ptr<Config> secure_gateway_localnet(std::unique_ptr<Config>);

/// @brief adjust configuration with params needed to be a validator
///
/// this is intended for use with envconfig, as in
/// envconfig(validator, myseed)
///
/// @param cfg config instance to be modified
/// @param seed seed string for use in secret key generation. A fixed default
/// value will be used if this string is empty
///
/// @return unique_ptr to Config instance
std::unique_ptr<Config>
validator(std::unique_ptr<Config>, std::string const&);

/// @brief add a grpc address and port to config
///
/// This is intended for use with envconfig, for tests that require a grpc
/// server. If this function is not called, grpc server will not start
///
///
/// @param cfg config instance to be modified
std::unique_ptr<Config> addGrpcConfig(std::unique_ptr<Config>);

/// @brief add a grpc address, port and secure_gateway to config
///
/// This is intended for use with envconfig, for tests that require a grpc
/// server. If this function is not called, grpc server will not start
///
///
/// @param cfg config instance to be modified
std::unique_ptr<Config>
addGrpcConfigWithSecureGateway(
    std::unique_ptr<Config>,
    std::string const& secureGateway);

std::unique_ptr<Config>
makeConfig(
    std::map<std::string, std::string> extraTxQ = {},
    std::map<std::string, std::string> extraVoting = {});

}  // namespace jtx
}  // namespace test
}  // namespace ripple

#endif
