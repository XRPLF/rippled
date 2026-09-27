#include <xrpld/overlay/detail/TrafficCount.h>

#include <xrpl/beast/unit_test/suite.h>

#include <xrpl.pb.h>

#include <algorithm>
#include <cstdint>

namespace xrpl::test {

class traffic_count_test : public beast::unit_test::Suite
{
public:
    traffic_count_test() = default;

    void
    testCategorize()
    {
        testcase("categorize");
        protocol::TMPing message;
        message.set_type(protocol::TMPing::ptPING);

        // a known message is categorized to a proper category
        auto const known = TrafficCount::categorize(message, protocol::mtPING, false);
        BEAST_EXPECT(known == TrafficCount::Category::Base);

        // cluster messages have a category of their own, in both directions; the same lookup
        // serves outbound traffic, so a missing entry would hide them from a node's report
        // whichever way they traveled
        BEAST_EXPECT(
            TrafficCount::categorize(message, protocol::mtCLUSTER, true) ==
            TrafficCount::Category::Cluster);
        BEAST_EXPECT(
            TrafficCount::categorize(message, protocol::mtCLUSTER, false) ==
            TrafficCount::Category::Cluster);

        // an unknown message type is categorized as unknown
        auto const unknown =
            TrafficCount::categorize(message, static_cast<protocol::MessageType>(99), false);
        BEAST_EXPECT(unknown == TrafficCount::Category::Unknown);
    }

    void
    testAttribute()
    {
        testcase("attribute");

        auto const outsider = [] { return false; };
        auto const member = [] { return true; };

        // Cluster is read as traffic between configured cluster members, and any
        // peer can send that type, so a sender outside the cluster is held to
        // unknown rather than counted as one.
        BEAST_EXPECT(
            TrafficCount::attribute(TrafficCount::Category::Cluster, outsider) ==
            TrafficCount::Category::Unknown);
        BEAST_EXPECT(
            TrafficCount::attribute(TrafficCount::Category::Cluster, member) ==
            TrafficCount::Category::Cluster);

        // Every other category is derived from the message alone, so membership
        // does not enter into it either way.
        for (auto const cat : {TrafficCount::Category::Base, TrafficCount::Category::Unknown})
        {
            BEAST_EXPECT(TrafficCount::attribute(cat, outsider) == cat);
            BEAST_EXPECT(TrafficCount::attribute(cat, member) == cat);
        }

        // Answering the question takes a lock, so it must not be asked for a
        // category that does not depend on the answer.
        auto asked = 0;
        auto const counted = [&asked] {
            ++asked;
            return true;
        };
        BEAST_EXPECT(
            TrafficCount::attribute(TrafficCount::Category::Base, counted) ==
            TrafficCount::Category::Base);
        BEAST_EXPECT(asked == 0);
        BEAST_EXPECT(
            TrafficCount::attribute(TrafficCount::Category::Cluster, counted) ==
            TrafficCount::Category::Cluster);
        BEAST_EXPECT(asked == 1);
    }

    struct TestCase
    {
        std::string name;
        int size;
        bool inbound;
        int messageCount;
        std::uint64_t expectedBytesIn;
        std::uint64_t expectedBytesOut;
        std::uint64_t expectedMessagesIn;
        std::uint64_t expectedMessagesOut;
    };

    void
    testAddCount()
    {
        auto run = [&](TestCase const& tc) {
            testcase(tc.name);
            TrafficCount traffic;

            auto const counts = traffic.getCounts();
            std::ranges::for_each(counts, [&](auto const& pair) {
                for (auto i = 0; i < tc.messageCount; ++i)
                    traffic.addCount(pair.first, tc.inbound, tc.size);
            });

            auto const countsNew = traffic.getCounts();
            std::ranges::for_each(countsNew, [&](auto const& pair) {
                BEAST_EXPECT(pair.second.bytesIn.load() == tc.expectedBytesIn);
                BEAST_EXPECT(pair.second.bytesOut.load() == tc.expectedBytesOut);
                BEAST_EXPECT(pair.second.messagesIn.load() == tc.expectedMessagesIn);
                BEAST_EXPECT(pair.second.messagesOut.load() == tc.expectedMessagesOut);
            });
        };

        auto const testcases = {
            TestCase{
                .name = "zero-counts",
                .size = 0,
                .inbound = false,
                .messageCount = 0,
                .expectedBytesIn = 0,
                .expectedBytesOut = 0,
                .expectedMessagesIn = 0,
                .expectedMessagesOut = 0,
            },
            TestCase{
                .name = "inbound-counts",
                .size = 10,
                .inbound = true,
                .messageCount = 10,
                .expectedBytesIn = 100,
                .expectedBytesOut = 0,
                .expectedMessagesIn = 10,
                .expectedMessagesOut = 0,
            },
            TestCase{
                .name = "outbound-counts",
                .size = 10,
                .inbound = false,
                .messageCount = 10,
                .expectedBytesIn = 0,
                .expectedBytesOut = 100,
                .expectedMessagesIn = 0,
                .expectedMessagesOut = 10,
            },
        };

        for (auto const& tc : testcases)
            run(tc);
    }

    void
    testToString()
    {
        testcase("category-to-string");

        // known category returns known string value
        BEAST_EXPECT(TrafficCount::toString(TrafficCount::Category::Total) == "total");

        // return "unknown" for unknown categories
        BEAST_EXPECT(
            TrafficCount::toString(static_cast<TrafficCount::Category>(1000)) == "unknown");
    }

    void
    run() override
    {
        testCategorize();
        testAttribute();
        testAddCount();
        testToString();
    }
};

BEAST_DEFINE_TESTSUITE(traffic_count, overlay, xrpl);

}  // namespace xrpl::test
