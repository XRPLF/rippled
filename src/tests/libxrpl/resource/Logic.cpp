#include <xrpl/resource/detail/Logic.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/basics/random.h>
#include <xrpl/beast/insight/NullCollector.h>
#include <xrpl/beast/net/IPAddressV4.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/resource/Charge.h>
#include <xrpl/resource/Consumer.h>
#include <xrpl/resource/Disposition.h>
#include <xrpl/resource/Gossip.h>
#include <xrpl/resource/detail/Tuning.h>

#include <boost/utility/base_from_member.hpp>

#include <gtest/gtest.h>
#include <helpers/TestSink.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <ranges>
#include <string>
#include <utility>

namespace xrpl::Resource {

class ResourceManagerTest : public ::testing::Test
{
protected:
    beast::Journal const j_{TestSink::instance()};

    class TestLogic : private boost::base_from_member<TestStopwatch>, public Logic
    {
    private:
        using clock_type = boost::base_from_member<TestStopwatch>;

    public:
        explicit TestLogic(beast::Journal journal)
            : Logic(beast::insight::NullCollector::make(), member, journal)
        {
        }

        void
        advance()
        {
            ++member;
        }

        TestStopwatch&
        clock()
        {
            return member;
        }
    };

    //--------------------------------------------------------------------------

    static Gossip
    makeGossip()
    {
        Gossip gossip;
        std::uint8_t const v(10 + randInt(9));
        std::uint8_t const n(10 + randInt(9));
        gossip.items.reserve(n);
        for (std::uint8_t i = 0; i < n; ++i)
        {
            Gossip::Item item;
            item.balance = 100 + randInt(499);
            beast::IP::AddressV4::bytes_type const d = {{
                192,
                0,
                2,
                static_cast<std::uint8_t>(v + i),
            }};
            item.address = beast::IP::Endpoint{beast::IP::AddressV4{d}};
            gossip.items.push_back(std::move(item));
        }
        return gossip;
    }
};

TEST_F(ResourceManagerTest, limited_warn_drop)
{
    TestLogic logic{j_};

    Charge const fee{kDropThreshold + 1};
    beast::IP::Endpoint const addr{beast::IP::Endpoint::fromString("192.0.2.2")};

    {
        Consumer c{logic.newInboundEndpoint(addr)};

        // Create load until we get a warning
        auto n = 10000;
        auto warned = false;

        while (--n >= 0)
        {
            if (c.charge(fee) == Disposition::Warn)
            {
                warned = true;
                break;
            }
            logic.advance();
        }

        ASSERT_TRUE(warned) << "Loop count exceeded without warning";

        // Create load until we get dropped
        bool dropped = false;
        while (--n >= 0)
        {
            if (c.charge(fee) == Disposition::Drop)
            {
                dropped = true;
                // Disconnect abusive Consumer
                EXPECT_TRUE(c.disconnect(j_));
                break;
            }
            logic.advance();
        }

        ASSERT_TRUE(dropped) << "Loop count exceeded without dropping";
    }

    // Make sure the consumer is on the blacklist for a while.
    {
        Consumer const c{logic.newInboundEndpoint(addr)};
        logic.periodicActivity();
        EXPECT_EQ(c.disposition(), Disposition::Drop) << "Dropped consumer not put on blacklist";
    }

    // Makes sure the Consumer is eventually removed from blacklist
    bool readmitted = false;
    {
        using namespace std::chrono_literals;
        // Give Consumer time to become readmitted.  Should never
        // exceed expiration time.
        auto n = kSecondsUntilExpiration + 1s;
        while (--n > 0s)
        {
            logic.advance();
            logic.periodicActivity();
            Consumer const c{logic.newInboundEndpoint(addr)};
            if (c.disposition() != Disposition::Drop)
            {
                readmitted = true;
                break;
            }
        }
    }
    EXPECT_TRUE(readmitted) << "Dropped Consumer left on blacklist too long";
}

TEST_F(ResourceManagerTest, unlimited_warn_drop)
{
    TestLogic logic{j_};

    Charge const fee{kDropThreshold + 1};
    beast::IP::Endpoint const addr{beast::IP::Endpoint::fromString("192.0.2.2")};
    Consumer c{logic.newUnlimitedEndpoint(addr)};

    // Create load until we get a warning
    int n = 10000;
    bool warned = false;

    while (--n >= 0)
    {
        if (c.charge(fee) == Disposition::Warn)
        {
            warned = true;
            break;
        }
        logic.advance();
    }

    EXPECT_FALSE(warned) << "Should loop forever with no warning";
}

TEST_F(ResourceManagerTest, charges)
{
    static constexpr auto kDecayTicks = 128uz;

    TestLogic logic{j_};

    {
        beast::IP::Endpoint const address{beast::IP::Endpoint::fromString("192.0.2.1")};
        Consumer c{logic.newInboundEndpoint(address)};
        Charge const fee{1000};
        JLOG(j_.info()) << "Charging " << c.toString() << " " << fee << " per second";
        c.charge(fee);
        for (auto tick = 0uz; tick < kDecayTicks; ++tick)
        {
            JLOG(j_.info()) << "Time= " << logic.clock().now().time_since_epoch().count()
                            << ", Balance = " << c.balance();
            logic.advance();
        }
    }

    {
        beast::IP::Endpoint const address{beast::IP::Endpoint::fromString("192.0.2.2")};
        Consumer c{logic.newInboundEndpoint(address)};
        Charge const fee{1000};
        JLOG(j_.info()) << "Charging " << c.toString() << " " << fee << " per second";
        for (auto tick = 0uz; tick < kDecayTicks; ++tick)
        {
            c.charge(fee);
            JLOG(j_.info()) << "Time= " << logic.clock().now().time_since_epoch().count()
                            << ", Balance = " << c.balance();
            logic.advance();
        }
    }
}

TEST_F(ResourceManagerTest, imports)
{
    TestLogic logic{j_};

    static constexpr auto kGossipSources = 5uz;
    std::ranges::for_each(std::views::iota(0uz, kGossipSources), [&](auto const i) {
        logic.importConsumers(std::to_string(i), makeGossip());
    });
}

TEST_F(ResourceManagerTest, import)
{
    TestLogic logic{j_};

    Gossip g;
    Gossip::Item item;
    item.balance = 100;
    beast::IP::AddressV4::bytes_type const d = {{
        192,
        0,
        2,
        1,
    }};
    item.address = beast::IP::Endpoint{beast::IP::AddressV4{d}};
    g.items.push_back(std::move(item));

    logic.importConsumers("g", g);
}

}  // namespace xrpl::Resource
