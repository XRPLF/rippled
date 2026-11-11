#include <xrpl/basics/Once.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <latch>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace xrpl;

struct OnceTest : ::testing::Test
{
    testing::StrictMock<testing::MockFunction<int()>> initMock;
};

TEST_F(OnceTest, init_is_called_on_first_access)
{
    Once<int> once;
    EXPECT_CALL(initMock, Call()).WillOnce(testing::Return(42));

    EXPECT_EQ(once.getOrInit(initMock.AsStdFunction()), 42);
}

TEST_F(OnceTest, init_is_called_only_once)
{
    Once<int> once;
    EXPECT_CALL(initMock, Call()).WillOnce(testing::Return(42));

    EXPECT_EQ(once.getOrInit(initMock.AsStdFunction()), 42);
    EXPECT_EQ(once.getOrInit(initMock.AsStdFunction()), 42);
    EXPECT_EQ(once.getOrInit([] { return 7; }), 42);
}

TEST_F(OnceTest, returns_reference_to_the_same_value)
{
    Once<std::string> once;

    auto& first = once.getOrInit([] { return std::string{"first"}; });
    auto& second = once.getOrInit([] { return std::string{"second"}; });

    EXPECT_EQ(&first, &second);
    EXPECT_EQ(first, "first");

    first = "modified";
    EXPECT_EQ(once.getOrInit([] { return std::string{"third"}; }), "modified");
}

TEST_F(OnceTest, init_is_retried_if_it_throws)
{
    Once<int> once;
    EXPECT_CALL(initMock, Call())
        .WillOnce(testing::Throw(std::runtime_error{"error"}))
        .WillOnce(testing::Return(42));

    EXPECT_THROW(once.getOrInit(initMock.AsStdFunction()), std::runtime_error);
    EXPECT_EQ(once.getOrInit(initMock.AsStdFunction()), 42);
    EXPECT_EQ(once.getOrInit(initMock.AsStdFunction()), 42);
}

TEST_F(OnceTest, works_with_move_only_and_non_default_constructible_types)
{
    struct NonDefaultConstructible
    {
        explicit NonDefaultConstructible(int v) : value{v}
        {
        }
        int value;
    };

    Once<std::unique_ptr<int>> moveOnly;
    EXPECT_EQ(*moveOnly.getOrInit([] { return std::make_unique<int>(42); }), 42);

    Once<NonDefaultConstructible> nonDefaultConstructible;
    EXPECT_EQ(
        nonDefaultConstructible.getOrInit([] { return NonDefaultConstructible{42}; }).value, 42);
}

TEST_F(OnceTest, concurrent_access_initializes_only_once)
{
    static constexpr std::size_t kNumThreads = 16;

    Once<std::shared_ptr<int>> once;
    std::atomic_size_t initCalls = 0;
    std::vector<std::shared_ptr<int>> results(kNumThreads);
    std::latch start{kNumThreads};
    std::vector<std::thread> threads;
    threads.reserve(kNumThreads);

    for (std::size_t i = 0; i < kNumThreads; ++i)
    {
        threads.emplace_back([&, i] {
            start.arrive_and_wait();
            results[i] = once.getOrInit([&initCalls, i] {
                ++initCalls;
                return std::make_shared<int>(static_cast<int>(i));
            });
        });
    }
    for (auto& thread : threads)
        thread.join();

    EXPECT_EQ(initCalls, 1);
    ASSERT_NE(results.front(), nullptr);
    for (auto const& result : results)
        EXPECT_EQ(result, results.front());
}
