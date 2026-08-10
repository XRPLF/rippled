#include <csf/Scheduler.h>

#include <gtest/gtest.h>

#include <set>

namespace xrpl::test {

TEST(SchedulerTest, scheduler)
{
    using namespace std::chrono_literals;
    csf::Scheduler scheduler;
    std::set<int> seen;

    scheduler.in(1s, [&] { seen.insert(1); });
    scheduler.in(2s, [&] { seen.insert(2); });
    auto token = scheduler.in(3s, [&] { seen.insert(3); });
    scheduler.at(scheduler.now() + 4s, [&] { seen.insert(4); });
    scheduler.at(scheduler.now() + 8s, [&] { seen.insert(8); });

    auto start = scheduler.now();

    // Process first event
    EXPECT_TRUE(seen.empty());
    EXPECT_TRUE(scheduler.stepOne());
    EXPECT_TRUE(seen == std::set<int>({1}));
    EXPECT_TRUE(scheduler.now() == (start + 1s));

    // No processing if stepping until current time
    EXPECT_TRUE(scheduler.stepUntil(scheduler.now()));
    EXPECT_TRUE(seen == std::set<int>({1}));
    EXPECT_TRUE(scheduler.now() == (start + 1s));

    // Process next event
    EXPECT_TRUE(scheduler.stepFor(1s));
    EXPECT_TRUE(seen == std::set<int>({1, 2}));
    EXPECT_TRUE(scheduler.now() == (start + 2s));

    // Don't process cancelled event, but advance clock
    scheduler.cancel(token);
    EXPECT_TRUE(scheduler.stepFor(1s));
    EXPECT_TRUE(seen == std::set<int>({1, 2}));
    EXPECT_TRUE(scheduler.now() == (start + 3s));

    // Process until 3 seen ints
    EXPECT_TRUE(scheduler.stepWhile([&]() { return seen.size() < 3; }));
    EXPECT_TRUE(seen == std::set<int>({1, 2, 4}));
    EXPECT_TRUE(scheduler.now() == (start + 4s));

    // Process the rest
    EXPECT_TRUE(scheduler.step());
    EXPECT_TRUE(seen == std::set<int>({1, 2, 4, 8}));
    EXPECT_TRUE(scheduler.now() == (start + 8s));

    // Process the rest again doesn't advance
    EXPECT_TRUE(!scheduler.step());
    EXPECT_TRUE(seen == std::set<int>({1, 2, 4, 8}));
    EXPECT_TRUE(scheduler.now() == (start + 8s));
}

}  // namespace xrpl::test
