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

        // an unknown message type is categorized as unknown
        auto const unknown =
            TrafficCount::categorize(message, static_cast<protocol::MessageType>(99), false);
        BEAST_EXPECT(unknown == TrafficCount::Category::Unknown);
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
        testAddCount();
        testToString();
    }
};

BEAST_DEFINE_TESTSUITE(traffic_count, overlay, xrpl);

}  // namespace xrpl::test
