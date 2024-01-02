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
#include <ripple/basics/random.h>
#include <ripple/core/Config.h>
#include <ripple/core/ConfigSections.h>
#include <ripple/overlay/Overlay.h>
#include <ripple/overlay/impl/ProtocolVersion.h>
#include <ripple/protocol/messages.h>
#include <ripple/rpc/ServerHandler.h>
#include <test/jtx.h>

namespace ripple {
namespace test {

class ProtocolStart_test : public beast::unit_test::suite
{
    // received/sent message statistics
    struct MSG
    {
        std::uint16_t start = 0;
        std::uint16_t ping = 0;
    };
    struct STATS
    {
        MSG sent;
        MSG recv;
    };

public:
    /* set Config parameters
     */
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

    /* send mtPING message
     */
    void
    sendPing(jtx::Env& env)
    {
        env.app().overlay().foreach([](std::shared_ptr<Peer> const& peer) {
            protocol::TMPing message;
            message.set_type(protocol::TMPing::ptPING);
            message.set_seq(rand_int<std::uint32_t>());
            peer->send(std::make_shared<Message>(message, protocol::mtPING));
        });
    }

    /* check the negotiated protocol matches the expected protocol
       return number of sent/recv mtSTART_PROTOCOL and mtPING messages
     */
    STATS
    getMetrics(jtx::Env& env, std::string const& expProtocol)
    {
        static const std::string mstart(
            std::to_string(protocol::mtSTART_PROTOCOL));
        static const std::string mping(std::to_string(protocol::mtPING));
        STATS stats;
        env.app().overlay().foreach([&](std::shared_ptr<Peer> const& peer) {
            auto const& j = peer->json();
            BEAST_EXPECT(j[jss::protocol].asString() == expProtocol);
            auto collect = [&](MSG& msg, Json::Value const& types) {
                if (types.isMember(mstart))
                    msg.start = std::atoi(types[mstart].asCString());
                if (types.isMember(mping))
                    msg.ping = std::atoi(types[mping].asCString());
            };
            collect(stats.sent, j[jss::metrics][jss::message_type_sent]);
            collect(stats.recv, j[jss::metrics][jss::message_type_recv]);
        });
        return stats;
    }

    void
    testProtocolStart()
    {
        using namespace jtx;
        using PVS = std::vector<ProtocolVersion>;

        // this lambda establishes an overlay with two virtual nodes and
        // verifies that start protocol messages are sent/received if
        // the negotiated protocol supports it and that
        // ping messages can be sent/received by both peers once the peers
        // are connected
        auto doTest = [&](std::uint16_t port,
                          std::uint16_t port1,
                          PVS const& pvs,
                          PVS const& pvs1,
                          std::string const& expProtocol,
                          bool expectStart) {
            std::stringstream str;
            str << "Protocol Start: " << toProtocolVersionStr(pvs) << " - "
                << toProtocolVersionStr(pvs1);
            testcase(str.str());
            str.str("");
            auto cfg = getConfig("127.0.0.1", port, {}, pvs);
            // inbound virtual peer
            Env env(*this, std::move(cfg));

            str << "127.0.0.1 " << port;
            cfg = getConfig("0.0.0.0", port1, {str.str()}, pvs1);
            // outbound virtual peer
            Env env1(*this, std::move(cfg));

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
                auto const before = getMetrics(env, expProtocol);
                auto const before1 = getMetrics(env1, expProtocol);
                // inbound receives start protocol if supported
                BEAST_EXPECT(before.recv.start == expectStart);
                // inbound never sends start protocol
                BEAST_EXPECT(before.sent.start == 0);
                // outbound sends start protocol if supported
                BEAST_EXPECT(before1.sent.start == expectStart);
                // outbound never receives start protocol
                BEAST_EXPECT(before1.recv.start == 0);
                // no ping initially
                BEAST_EXPECT(before.sent.ping == 0 && before.recv.ping == 0);
                BEAST_EXPECT(before1.sent.ping == 0 && before1.recv.ping == 0);
                sendPing(env);
                sendPing(env1);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                auto const after = getMetrics(env, expProtocol);
                auto const after1 = getMetrics(env1, expProtocol);
                // protocol messages are sent and received
                BEAST_EXPECT(after.sent.ping > 0 && after.recv.ping > 0);
                BEAST_EXPECT(after1.sent.ping > 0 && after1.recv.ping > 0);
            }

            BEAST_EXPECT(havePeers);
        };

        // latest protocols support start protocol
        auto const supportedProtocols =
            parseProtocolVersions(supportedProtocolVersions());
        // old protocols don't support start protocol
        auto const oldProtocols = PVS{{2, 1}, {2, 2}};
        // largest supported protocol
        auto const negotiateProtocol = [](PVS const& pvs, PVS const& pvs1) {
            if (auto const negotiated =
                    negotiateProtocolVersion(toProtocolVersionStr(pvs), pvs1))
            {
                std::stringstream str;
                str << "XRPL/" << negotiated->first << "."
                    << negotiated->second;
                return str.str();
            }
            return std::string("");
        };

        // both peers have the latest protocol: supported - supported,
        // negotiate supported
        doTest(
            9000,
            10000,
            supportedProtocols,
            supportedProtocols,
            negotiateProtocol(supportedProtocols, supportedProtocols),
            true);
        // inbound peer has 2.2 and outbound peer has the latest, negotiate 2.2
        doTest(
            9100,
            10100,
            oldProtocols,
            supportedProtocols,
            negotiateProtocol(oldProtocols, supportedProtocols),
            false);
        // outbound peer has the latest and inbound peer has 2.2, negotiate 2.2
        doTest(
            9200,
            10200,
            supportedProtocols,
            oldProtocols,
            negotiateProtocol(supportedProtocols, oldProtocols),
            false);
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
