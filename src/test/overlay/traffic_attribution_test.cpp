#include <test/jtx/Env.h>
#include <test/jtx/envconfig.h>
#include <test/overlay/CapturePeer.h>

#include <xrpld/overlay/Cluster.h>
#include <xrpld/overlay/detail/TrafficCount.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/JsonPropertyStream.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SecretKey.h>

#include <xrpl.pb.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace xrpl::test {

using namespace jtx;

/**
 * Covers the traffic attribution in `PeerImp::onMessageBegin`.
 *
 * The `traffic_count` suite covers what `TrafficCount::attribute` returns. This
 * suite covers which counter a real peer's bytes reach, which is what an
 * operator reads. A call site that passed the wrong argument, or reported the
 * category it held before attributing it, would satisfy the first and fail this
 * one.
 */
class traffic_attribution_test : public beast::unit_test::Suite
{
    // The assertions compare deltas, so any size works. A size other than 1
    // keeps a byte count from passing as a message count.
    static constexpr std::size_t kFrameSize = 37;

    struct Counts
    {
        std::uint64_t clusterMessages{0};
        std::uint64_t clusterBytes{0};
        std::uint64_t unknownMessages{0};
        std::uint64_t unknownBytes{0};
    };

    /**
     * Reads the cluster and unknown counters the way an operator reads them,
     * through the overlay's property stream rather than through `TrafficCount`.
     *
     * @param env The environment owning the overlay.
     * @return The inbound messages and bytes counted under each of the two
     *         categories, or zeroes for a category the stream omits.
     */
    static Counts
    read(Env& env)
    {
        JsonPropertyStream stream;
        env.app().getOverlay().writeOne(stream);

        // `TrafficCount::toString` names the categories, so a renamed counter
        // cannot leave this lookup silently matching nothing.
        auto const clusterName = TrafficCount::toString(TrafficCount::Category::Cluster);
        auto const unknownName = TrafficCount::toString(TrafficCount::Category::Unknown);

        Counts counts;
        for (auto const& entry : stream.top()["peers"]["traffic"])
        {
            auto const name = entry["category"].asString();
            auto const messages = std::stoull(entry["messages_in"].asString());
            auto const bytes = std::stoull(entry["bytes_in"].asString());

            if (name == clusterName)
            {
                counts.clusterMessages = messages;
                counts.clusterBytes = bytes;
            }
            else if (name == unknownName)
            {
                counts.unknownMessages = messages;
                counts.unknownBytes = bytes;
            }
        }
        return counts;
    }

    void
    testInboundCluster()
    {
        testcase("inbound cluster attribution");

        Env env{*this, envconfig()};

        // The outsider takes a fresh random key, which no `[cluster_nodes]`
        // entry names. The member's key is registered before the peer is built,
        // since `cluster()` asks the cluster map on every message.
        auto const outsider = makeCapturePeer(env);
        PublicKey const memberKey = randomKeyPair(KeyType::Ed25519).first;
        BEAST_EXPECT(env.app().getCluster().update(memberKey, "test-member"));
        auto const member = makeCapturePeer(env, memberKey);

        auto const message = std::make_shared<protocol::TMCluster>();
        auto const before = read(env);

        // Any peer can send this type, so a sender outside the cluster is
        // counted as unknown, leaving the cluster counter to mean traffic
        // between configured members.
        outsider->onMessageBegin(protocol::mtCLUSTER, message, kFrameSize, kFrameSize, false);
        auto const afterOutsider = read(env);
        BEAST_EXPECT(afterOutsider.unknownMessages == before.unknownMessages + 1);
        BEAST_EXPECT(afterOutsider.unknownBytes == before.unknownBytes + kFrameSize);
        BEAST_EXPECT(afterOutsider.clusterMessages == before.clusterMessages);
        BEAST_EXPECT(afterOutsider.clusterBytes == before.clusterBytes);

        // The same frame from a configured member is counted as cluster
        // traffic.
        member->onMessageBegin(protocol::mtCLUSTER, message, kFrameSize, kFrameSize, false);
        auto const afterMember = read(env);
        BEAST_EXPECT(afterMember.clusterMessages == afterOutsider.clusterMessages + 1);
        BEAST_EXPECT(afterMember.clusterBytes == afterOutsider.clusterBytes + kFrameSize);
        BEAST_EXPECT(afterMember.unknownMessages == afterOutsider.unknownMessages);
        BEAST_EXPECT(afterMember.unknownBytes == afterOutsider.unknownBytes);
    }

public:
    void
    run() override
    {
        testInboundCluster();
    }
};

BEAST_DEFINE_TESTSUITE(traffic_attribution, overlay, xrpl);

}  // namespace xrpl::test
