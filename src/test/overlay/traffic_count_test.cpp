//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2025 Ripple Labs Inc.

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

#include <xrpld/overlay/Message.h>
#include <xrpld/overlay/detail/TrafficCount.h>

#include <xrpl/beast/unit_test.h>
#include <xrpl/protocol/messages.h>

namespace ripple {

namespace test {

class traffic_count_test : public beast::unit_test::suite
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
        const auto known =
            TrafficCount::categorize(message, protocol::mtPING, false);
        BEAST_EXPECT(known == TrafficCount::category::base);

        // an unknown message type is categorized as unknown
        const auto unknown = TrafficCount::categorize(
            message, static_cast<protocol::MessageType>(99), false);
        BEAST_EXPECT(unknown == TrafficCount::category::unknown);
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
            TrafficCount m_traffic;

            auto const counts = m_traffic.getCounts();
            std::for_each(counts.begin(), counts.end(), [&](auto& pair) {
                for (auto i = 0; i < tc.messageCount; ++i)
                    m_traffic.addCount(pair.first, tc.inbound, tc.size);
            });

            auto const counts_new = m_traffic.getCounts();
            std::for_each(
                counts_new.begin(), counts_new.end(), [&](auto& pair) {
                    BEAST_EXPECT(
                        pair.second.bytesIn.load() == tc.expectedBytesIn);
                    BEAST_EXPECT(
                        pair.second.bytesOut.load() == tc.expectedBytesOut);
                    BEAST_EXPECT(
                        pair.second.messagesIn.load() == tc.expectedMessagesIn);
                    BEAST_EXPECT(
                        pair.second.messagesOut.load() ==
                        tc.expectedMessagesOut);
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

        for (const auto& tc : testcases)
            run(tc);
    }

    void
    run() override
    {
        testCategorize();
        testAddCount();
    }
};

BEAST_DEFINE_TESTSUITE(traffic_count, overlay, ripple);

}  // namespace test
}  // namespace ripple
