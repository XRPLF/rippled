#include <test/jtx/Env.h>
#include <test/jtx/envconfig.h>

#include <xrpld/app/main/Application.h>
#include <xrpld/overlay/Overlay.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/peerfinder/detail/Tuning.h>

#include <utility>

namespace xrpl::test {

using namespace jtx;

/**
 * Tests for `Overlay::limit()`, the configured peer allowance reported once
 * `OverlayImpl::start()` applies the computed `peer_finder::Config`.
 *
 * `ApplicationImp::fdRequired()` runs before `OverlayImpl::start()` does, so it
 * always sees the peer finder manager's default-constructed configuration and
 * never this value; `Overlay::limit()` instead surfaces through the PeerFinder
 * property stream and other post-startup callers.
 *
 * `jtx::Env` runs standalone, and `ServerHandler` strips the `peer` protocol
 * from every configured port under `config.standalone()`, so the peer port
 * declared here is never bound and incoming connections are disabled
 * throughout; every limit in this suite is an outbound-only allowance. The
 * inbound cases live alongside `peer_finder::Config::makeConfig`, which takes
 * the port as a parameter.
 */
class OverlayLimit_test : public beast::unit_test::Suite
{
    void
    testLegacyPeersMax()
    {
        testcase("Legacy peers_max is reported");

        auto config = jtx::envconfig();
        config->peersMax = 40;

        Env env(*this, std::move(config));
        BEAST_EXPECT(env.app().getOverlay().limit() == 40);
    }

    void
    testPerDirectionPeerLimits()
    {
        testcase("Per-direction peer limits are reported");

        // With incoming connections disabled the 50 inbound slots are dropped
        // and only the outbound allowance remains, so neither zero (the value
        // `maxPeers` used to hold in this branch of makeConfig) nor 70 (the
        // unconditional sum of both directions) is correct.
        auto config = jtx::envconfig();
        config->peersInMax = 50;
        config->peersOutMax = 20;

        Env env(*this, std::move(config));
        BEAST_EXPECT(env.app().getOverlay().limit() == 20);
    }

    void
    testDefaultConfig()
    {
        testcase("A default configuration reports the default limit");

        Env env(*this);
        BEAST_EXPECT(env.app().getOverlay().limit() == peer_finder::tuning::kDefaultMaxPeers);
    }

    void
    run() override
    {
        testLegacyPeersMax();
        testPerDirectionPeerLimits();
        testDefaultConfig();
    }
};

BEAST_DEFINE_TESTSUITE(OverlayLimit, overlay, xrpl);

}  // namespace xrpl::test
