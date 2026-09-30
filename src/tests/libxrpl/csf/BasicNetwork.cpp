#include <csf/BasicNetwork.h>

#include <csf/Scheduler.h>
#include <gtest/gtest.h>

#include <set>
#include <vector>

namespace xrpl::test {

namespace {

struct Peer
{
    int id;
    std::set<int> set;

    Peer(Peer const&) = default;
    Peer(Peer&&) = default;

    explicit Peer(int id) : id(id)
    {
    }

    template <class Net>
    void
    start(csf::Scheduler& scheduler, Net& net)
    {
        using namespace std::chrono_literals;
        auto t = scheduler.in(1s, [&] { set.insert(0); });
        if (id == 0)
        {
            for (auto const link : net.links(this))
            {
                net.send(this, link.target, [&, to = link.target] { to->receive(net, this, 1); });
            }
        }
        else
        {
            scheduler.cancel(t);
        }
    }

    template <class Net>
    void
    receive(Net& net, Peer* from, int m)
    {
        set.insert(m);
        ++m;
        if (m < 5)
        {
            for (auto const link : net.links(this))
            {
                net.send(this, link.target, [&, mm = m, to = link.target] {
                    to->receive(net, this, mm);
                });
            }
        }
    }
};

}  // namespace

TEST(BasicNetworkTest, network)
{
    using namespace std::chrono_literals;
    std::vector<Peer> pv;
    pv.emplace_back(0);
    pv.emplace_back(1);
    pv.emplace_back(2);
    csf::Scheduler scheduler;
    csf::BasicNetwork<Peer*> net(scheduler);
    EXPECT_TRUE(!net.connect(&pv[0], &pv[0]));
    EXPECT_TRUE(net.connect(&pv[0], &pv[1], 1s));
    EXPECT_TRUE(net.connect(&pv[1], &pv[2], 1s));
    EXPECT_TRUE(!net.connect(&pv[0], &pv[1]));
    for (auto& peer : pv)
        peer.start(scheduler, net);
    EXPECT_TRUE(scheduler.stepFor(0s));
    EXPECT_TRUE(scheduler.stepFor(1s));
    EXPECT_TRUE(scheduler.step());
    EXPECT_TRUE(!scheduler.step());
    EXPECT_TRUE(!scheduler.stepFor(1s));
    net.send(&pv[0], &pv[1], [] {});
    net.send(&pv[1], &pv[0], [] {});
    EXPECT_TRUE(net.disconnect(&pv[0], &pv[1]));
    EXPECT_TRUE(!net.disconnect(&pv[0], &pv[1]));
    for (;;)
    {
        auto const links = net.links(&pv[1]);
        if (links.empty())
            break;
        EXPECT_TRUE(net.disconnect(&pv[1], links[0].target));
    }
    EXPECT_TRUE(pv[0].set == std::set<int>({0, 2, 4}));
    EXPECT_TRUE(pv[1].set == std::set<int>({1, 3}));
    EXPECT_TRUE(pv[2].set == std::set<int>({2, 4}));
}

TEST(BasicNetworkTest, disconnect)
{
    using namespace std::chrono_literals;
    csf::Scheduler scheduler;
    csf::BasicNetwork<int> net(scheduler);
    EXPECT_TRUE(net.connect(0, 1, 1s));
    EXPECT_TRUE(net.connect(0, 2, 2s));

    std::set<int> delivered;
    net.send(0, 1, [&]() { delivered.insert(1); });
    net.send(0, 2, [&]() { delivered.insert(2); });

    scheduler.in(1000ms, [&]() { EXPECT_TRUE(net.disconnect(0, 2)); });
    scheduler.in(1100ms, [&]() { EXPECT_TRUE(net.connect(0, 2)); });

    scheduler.step();

    // only the first message is delivered because the disconnect at 1 s
    // purges all pending messages from 0 to 2
    EXPECT_TRUE(delivered == std::set<int>({1}));
}

}  // namespace xrpl::test
