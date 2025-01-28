//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2017 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <test/jtx/envconfig.h>

#include <test/jtx/amount.h>
#include <xrpld/core/ConfigSections.h>

namespace ripple {
namespace test {

std::atomic<bool> envUseIPv4{false};

void
setupConfigForUnitTests(Config& cfg)
{
    using namespace jtx;
    // Default fees to old values, so tests don't have to worry about changes in
    // Config.h
    cfg.FEES.reference_fee = 10;
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
no_admin(std::unique_ptr<Config> cfg)
{
    (*cfg)[PORT_RPC].set("admin", "");
    (*cfg)[PORT_WS].set("admin", "");
    return cfg;
}

std::unique_ptr<Config>
secure_gateway(std::unique_ptr<Config> cfg)
{
    (*cfg)[PORT_RPC].set("admin", "");
    (*cfg)[PORT_WS].set("admin", "");
    (*cfg)[PORT_RPC].set("secure_gateway", getEnvLocalhostAddr());
    return cfg;
}

std::unique_ptr<Config>
admin_localnet(std::unique_ptr<Config> cfg)
{
    (*cfg)[PORT_RPC].set("admin", "127.0.0.0/8");
    (*cfg)[PORT_WS].set("admin", "127.0.0.0/8");
    return cfg;
}

std::unique_ptr<Config>
secure_gateway_localnet(std::unique_ptr<Config> cfg)
{
    (*cfg)[PORT_RPC].set("admin", "");
    (*cfg)[PORT_WS].set("admin", "");
    (*cfg)[PORT_RPC].set("secure_gateway", "127.0.0.0/8");
    (*cfg)[PORT_WS].set("secure_gateway", "127.0.0.0/8");
    return cfg;
}

auto constexpr defaultseed = "shUwVw52ofnCUX5m7kPTKzJdr4HEH";

std::unique_ptr<Config>
validator(std::unique_ptr<Config> cfg, std::string const& seed)
{
    // If the config has valid validation keys then we run as a validator.
    cfg->section(SECTION_VALIDATION_SEED)
        .append(std::vector<std::string>{seed.empty() ? defaultseed : seed});
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
addGrpcConfigWithSecureGateway(
    std::unique_ptr<Config> cfg,
    std::string const& secureGateway)
{
    (*cfg)[SECTION_PORT_GRPC].set("ip", getEnvLocalhostAddr());

    // Check https://man7.org/linux/man-pages/man7/ip.7.html
    // "ip_local_port_range" section for using 0 ports
    (*cfg)[SECTION_PORT_GRPC].set("port", "0");
    (*cfg)[SECTION_PORT_GRPC].set("secure_gateway", secureGateway);
    return cfg;
}

}  // namespace jtx
}  // namespace test
}  // namespace ripple
