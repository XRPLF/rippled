//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2024 Ripple Labs Inc.

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
#include <ripple/core/Config.h>
#include <ripple/core/ConfigSections.h>
#include <ripple/overlay/Overlay.h>
#include <ripple/overlay/impl/ProtocolVersion.h>
#include <ripple/rpc/ServerHandler.h>
#include <test/jtx.h>

namespace ripple {
namespace test {

class ProtocolStart_test : public beast::unit_test::suite
{
public:
    std::unique_ptr<ripple::Config>
    getConfig(
        std::string const& addr,
        std::uint16_t port_peer,
        std::vector<std::string> const& ipsfixed,
        std::optional<std::vector<ProtocolVersion>> const& pvs = std::nullopt)
    {
        auto cfg = std::make_unique<ripple::Config>();
        using namespace jtx;
        // Default fees to old values, so tests don't have to worry about
        // changes in Config.h
        cfg->FEES.reference_fee = 10;
        cfg->FEES.account_reserve = XRP(200).value().xrp().drops();
        cfg->FEES.owner_reserve = XRP(50).value().xrp().drops();
        // The Beta API (currently v2) is always available to tests
        cfg->BETA_RPC_API = true;

        cfg->overwrite(ConfigSection::nodeDatabase(), "type", "memory");
        cfg->overwrite(ConfigSection::nodeDatabase(), "path", "main");
        cfg->deprecatedClearSection(ConfigSection::importNodeDatabase());
        cfg->legacy("database_path", "");
        cfg->setupControl(true, true, true);
        (*cfg)["server"].append("port_peer");
        (*cfg)["port_peer"].set("ip", addr);
        (*cfg)["port_peer"].set("port", std::to_string(port_peer));
        (*cfg)["port_peer"].set("protocol", "peer");

        (*cfg)["server"].append("port_rpc");
        (*cfg)["port_rpc"].set("ip", addr);
        (*cfg)["port_rpc"].set("admin", addr);
        (*cfg)["port_rpc"].set("port", std::to_string(port_peer + 1));
        (*cfg)["port_rpc"].set("protocol", "http");
        (*cfg).SSL_VERIFY = false;

        for (auto ips : ipsfixed)
            cfg->IPS_FIXED.push_back(ips);

        cfg->ALLOW_OVERLAY = true;
        cfg->PROTOCOL_VERSIONS = pvs;

        return cfg;
    }

    void
    testProtocolStart()
    {
        using namespace jtx;
        using PVS = std::optional<std::vector<ProtocolVersion>>;

        // this lambda establishes an overlay with two virtual nodes and
        // verifies that the ping messages are sent/received by both peers
        auto doTest = [&](PVS const& pvs,
                          PVS const& pvs1,
                          std::string const& expProtocol) {
            std::stringstream str;
            str << "Protocol Start: "
                << (pvs ? toProtocolVersionStr(*pvs) : "2.1-2.3") << " - "
                << (pvs1 ? toProtocolVersionStr(*pvs1) : "2.1-2.3");
            testcase(str.str());
            auto cfg = getConfig("127.0.0.1", 8000, {}, pvs);
            Env env(*this, std::move(cfg));

            cfg = getConfig("0.0.0.0", 9000, {"127.0.0.1 8000"}, pvs1);
            Env env1(*this, std::move(cfg));

            auto ping = [&](Env& env) {
                env.app().overlay().foreach(
                    [](std::shared_ptr<Peer> const& peer) {
                        protocol::TMPing message;
                        message.set_type(protocol::TMPing::ptPING);
                        message.set_seq(rand_int<std::uint32_t>());
                        peer->send(std::make_shared<Message>(
                            message, protocol::mtPING));
                    });
            };

            auto metrics =
                [&](Env& env) -> std::pair<std::uint16_t, std::uint16_t> {
                std::uint16_t recv = 0;
                std::uint16_t sent = 0;
                env.app().overlay().foreach(
                    [&](std::shared_ptr<Peer> const& peer) {
                        auto const& j = peer->json();
                        BEAST_EXPECT(j["protocol"].asString() == expProtocol);
                        recv = j["metrics"]["total_bytes_recv"].asUInt();
                        sent = j["metrics"]["total_bytes_sent"].asUInt();
                    });
                return {recv, sent};
            };

            bool havePeers = false;
            // timeout after 5 sec (5000 msec)
            std::uint16_t constexpr maxTimer = 5000;
            std::uint16_t timer = 0;
            auto peersEmpty = [](Env& env) {
                return env.app().overlay().getActivePeers().empty();
            };

            do
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                timer += 100;
                // a peer is only created if connected
                havePeers = !peersEmpty(env) && !peersEmpty(env1);
            } while (!havePeers && timer <= maxTimer);

            if (havePeers)
            {
                auto const before = metrics(env);
                auto const before1 = metrics(env1);
                ping(env);
                ping(env1);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                auto const after = metrics(env);
                auto const after1 = metrics(env1);
                auto gt = [](auto const& p1, auto const& p2) {
                    return p1.first > p2.first && p1.second > p2.second;
                };
                // verify protocol messages are sent and received
                BEAST_EXPECT(gt(after, before) && gt(after1, before1));
            }

            BEAST_EXPECT(havePeers);
        };

        // peers have the same protocol: 2.3 - 2.3, negotiate 2.3
        doTest(std::nullopt, std::nullopt, "XRPL/2.3");
        // inbound peer has 2.2 and outbound peer has 2.3, negotiate 2.2
        doTest({{{2, 1}, {2, 2}}}, std::nullopt, "XRPL/2.2");
        // outbound peer has 2.3 and inbound peer has 2.2, negotiate 2.2
        doTest(std::nullopt, {{{2, 1}, {2, 2}}}, "XRPL/2.2");
    }

    void
    run() override
    {
        testProtocolStart();
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(ProtocolStart, app, ripple, 1);

}  // namespace test
}  // namespace ripple
