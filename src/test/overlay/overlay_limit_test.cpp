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
 * Tests for `Overlay::limit()`, the configured peer allowance.
 *
 * The value feeds `ApplicationImp::fdRequired()`, which reserves two file
 * descriptors per allowed peer at startup, so an understated limit leaves the
 * process short of descriptors and an overstated one can push the required
 * count past the system limit and abort startup.
 *
 * A unit test configuration declares its peer port as zero and never binds
 * one, so incoming connections are disabled throughout and every limit here is
 * an outbound-only allowance. The inbound cases live alongside
 * `peer_finder::Config::makeConfig`, which takes the port as a parameter.
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
