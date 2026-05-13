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
#include <xrpl/resource/detail/Logic.h>
#include <xrpl/resource/detail/Tuning.h>

#include <boost/utility/base_from_member.hpp>

#include <gtest/gtest.h>
#include <helpers/TestSink.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>

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

    static void
    createGossip(Gossip& gossip)
    {
        std::uint8_t const v(10 + randInt(9));
        std::uint8_t const n(10 + randInt(9));
        gossip.items.reserve(n);
        for (std::uint8_t i = 0; i < n; ++i)
        {
            Gossip::Item item;
            item.balance = 100 + randInt(499);
            beast::IP::AddressV4::bytes_type const d = {
                {192, 0, 2, static_cast<std::uint8_t>(v + i)}};
            item.address = beast::IP::Endpoint{beast::IP::AddressV4{d}};
            gossip.items.push_back(item);
        }
    }

    //--------------------------------------------------------------------------

    void
    testDrop(bool limited)
    {
        TestLogic logic(j_);

        Charge const fee(kDROP_THRESHOLD + 1);
        beast::IP::Endpoint const addr(beast::IP::Endpoint::fromString("192.0.2.2"));

        std::function<Consumer(beast::IP::Endpoint)> const ep = limited
            ? std::bind(&TestLogic::newInboundEndpoint, &logic, std::placeholders::_1)
            : std::bind(&TestLogic::newUnlimitedEndpoint, &logic, std::placeholders::_1);

        {
            Consumer c(ep(addr));

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
                ++logic.clock();
            }

            if (!limited)
            {
                EXPECT_FALSE(warned) << "Should loop forever with no warning";
                return;
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
                ++logic.clock();
            }

            ASSERT_TRUE(dropped) << "Loop count exceeded without dropping";
        }

        // Make sure the consumer is on the blacklist for a while.
        {
            Consumer const c(logic.newInboundEndpoint(addr));
            logic.periodicActivity();
            EXPECT_EQ(c.disposition(), Disposition::Drop)
                << "Dropped consumer not put on blacklist";
        }

        // Makes sure the Consumer is eventually removed from blacklist
        bool readmitted = false;
        {
            using namespace std::chrono_literals;
            // Give Consumer time to become readmitted.  Should never
            // exceed expiration time.
            auto n = kSECONDS_UNTIL_EXPIRATION + 1s;
            while (--n > 0s)
            {
                ++logic.clock();
                logic.periodicActivity();
                Consumer const c(logic.newInboundEndpoint(addr));
                if (c.disposition() != Disposition::Drop)
                {
                    readmitted = true;
                    break;
                }
            }
        }
        EXPECT_TRUE(readmitted) << "Dropped Consumer left on blacklist too long";
    }

    void
    testImports()
    {
        TestLogic logic(j_);

        Gossip g[5];

        for (int i = 0; i < 5; ++i)
            createGossip(g[i]);

        for (int i = 0; i < 5; ++i)
            logic.importConsumers(std::to_string(i), g[i]);
    }

    void
    testImport()
    {
        TestLogic logic(j_);

        Gossip g;
        Gossip::Item item;
        item.balance = 100;
        beast::IP::AddressV4::bytes_type const d = {{192, 0, 2, 1}};
        item.address = beast::IP::Endpoint{beast::IP::AddressV4{d}};
        g.items.push_back(item);

        logic.importConsumers("g", g);
    }

    void
    testCharges()
    {
        TestLogic logic(j_);

        {
            beast::IP::Endpoint const address(beast::IP::Endpoint::fromString("192.0.2.1"));
            Consumer c(logic.newInboundEndpoint(address));
            Charge const fee(1000);
            JLOG(j_.info()) << "Charging " << c.toString() << " " << fee << " per second";
            c.charge(fee);
            for (int i = 0; i < 128; ++i)
            {
                JLOG(j_.info()) << "Time= " << logic.clock().now().time_since_epoch().count()
                                << ", Balance = " << c.balance();
                logic.advance();
            }
        }

        {
            beast::IP::Endpoint const address(beast::IP::Endpoint::fromString("192.0.2.2"));
            Consumer c(logic.newInboundEndpoint(address));
            Charge const fee(1000);
            JLOG(j_.info()) << "Charging " << c.toString() << " " << fee << " per second";
            for (int i = 0; i < 128; ++i)
            {
                c.charge(fee);
                JLOG(j_.info()) << "Time= " << logic.clock().now().time_since_epoch().count()
                                << ", Balance = " << c.balance();
                logic.advance();
            }
        }
    }
};

TEST_F(ResourceManagerTest, limited_warn_drop)
{
    testDrop(true);
}

TEST_F(ResourceManagerTest, unlimited_warn_drop)
{
    testDrop(false);
}

TEST_F(ResourceManagerTest, charges)
{
    testCharges();
}

TEST_F(ResourceManagerTest, imports)
{
    testImports();
}

TEST_F(ResourceManagerTest, import)
{
    testImport();
}

}  // namespace xrpl::Resource
