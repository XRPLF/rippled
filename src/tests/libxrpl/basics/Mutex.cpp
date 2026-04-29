#include <xrpl/basics/Mutex.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using namespace xrpl;

struct MutexMakeTest : ::testing::Test
{
};

TEST_F(MutexMakeTest, default_constructor)
{
    auto m = Mutex<int>::make();
    auto lock = m.lock();
    EXPECT_EQ(*lock, 0);
}

TEST_F(MutexMakeTest, single_argument)
{
    auto m = Mutex<int>::make(42);
    auto lock = m.lock();
    EXPECT_EQ(*lock, 42);
}

TEST_F(MutexMakeTest, string_argument)
{
    auto m = Mutex<std::string>::make("test");
    auto lock = m.lock();
    EXPECT_EQ(*lock, "test");
}

TEST_F(MutexMakeTest, move_only_type)
{
    auto m = Mutex<std::unique_ptr<int>>::make(std::make_unique<int>(100));
    auto lock = m.lock();
    EXPECT_NE(lock->get(), nullptr);
    EXPECT_EQ(**lock, 100);
}

struct MutexDirectConstructionTest : ::testing::Test
{
};

TEST_F(MutexDirectConstructionTest, default_constructor)
{
    // Test default construction with a type that has a non-trivial
    // default constructor
    Mutex<std::string> m;
    auto lock = m.lock();
    EXPECT_TRUE(lock->empty());
}

TEST_F(MutexDirectConstructionTest, default_initialization)
{
    Mutex<int> m;
    auto lock = m.lock();
    EXPECT_EQ(*lock, 0);
}

TEST_F(MutexDirectConstructionTest, constructor_with_value)
{
    Mutex<int> m(42);
    auto lock = m.lock();
    EXPECT_EQ(*lock, 42);
}

TEST_F(MutexDirectConstructionTest, constructor_with_string)
{
    Mutex<std::string> m(std::string("hello"));
    auto lock = m.lock();
    EXPECT_EQ(*lock, "hello");
}

struct MutexLockNonConstTest : ::testing::Test
{
};

TEST_F(MutexLockNonConstTest, operator_star)
{
    Mutex<int> m(10);
    {
        auto lock = m.lock();
        EXPECT_EQ(*lock, 10);
        *lock = 20;
    }
    auto lock = m.lock();
    EXPECT_EQ(*lock, 20);
}

TEST_F(MutexLockNonConstTest, get_method)
{
    Mutex<int> m(10);
    {
        auto lock = m.lock();
        EXPECT_EQ(lock.get(), 10);
        lock.get() = 30;
    }
    auto lock = m.lock();
    EXPECT_EQ(lock.get(), 30);
}

TEST_F(MutexLockNonConstTest, operator_arrow)
{
    Mutex<std::string> m(std::string("test"));
    {
        auto lock = m.lock();
        EXPECT_EQ(lock->size(), 4);
        lock->append(" string");
    }
    auto lock = m.lock();
    EXPECT_EQ(*lock, "test string");
}

TEST_F(MutexLockNonConstTest, multiple_modifications)
{
    Mutex<int> m(10);
    {
        auto lock = m.lock();
        *lock = 20;
    }
    {
        auto lock = m.lock();
        EXPECT_EQ(lock.get(), 20);
        lock.get() = 30;
    }
    {
        auto lock = m.lock();
        EXPECT_EQ(*lock, 30);
    }
}

struct MutexLockConstTest : ::testing::Test
{
};

TEST_F(MutexLockConstTest, operator_star)
{
    Mutex<int> const m(42);
    auto lock = m.lock();
    static_assert(std::is_const_v<std::remove_reference_t<decltype(*lock)>>);
    EXPECT_EQ(*lock, 42);
}

TEST_F(MutexLockConstTest, get_method)
{
    Mutex<int> const m(42);
    auto lock = m.lock();
    static_assert(std::is_const_v<std::remove_reference_t<decltype(lock.get())>>);
    EXPECT_EQ(lock.get(), 42);
}

TEST_F(MutexLockConstTest, operator_arrow)
{
    Mutex<std::string> const m(std::string("test"));
    auto lock = m.lock();
    static_assert(std::is_const_v<std::remove_reference_t<decltype(*lock)>>);
    EXPECT_EQ(lock->size(), 4);
    EXPECT_EQ(lock->at(0), 't');
}

struct MutexConstCorrectnessTest : ::testing::Test
{
};

TEST_F(MutexConstCorrectnessTest, non_const_allows_modification)
{
    Mutex<std::vector<int>> m({1, 2, 3, 4, 5});
    {
        auto lock = m.lock();
        EXPECT_EQ(lock->size(), 5);
        lock->push_back(6);
    }
    auto lock = m.lock();
    EXPECT_EQ(lock->size(), 6);
    EXPECT_EQ(lock->back(), 6);
}

TEST_F(MutexConstCorrectnessTest, const_reference_provides_const_access)
{
    Mutex<std::vector<int>> const m({1, 2, 3, 4, 5, 6});
    Mutex<std::vector<int>> const& const_ref = m;
    auto lock = const_ref.lock();
    static_assert(std::is_const_v<std::remove_reference_t<decltype(*lock)>>);
    EXPECT_EQ(lock->size(), 6);
    EXPECT_EQ(lock->at(5), 6);
}

struct MutexDifferentLockTypesTest : ::testing::Test
{
};

TEST_F(MutexDifferentLockTypesTest, scoped_lock)
{
    Mutex<int> m(0);
    {
        auto lock = m.lock();
        *lock = 1;
    }
    auto lock = m.lock();
    EXPECT_EQ(*lock, 1);
}

TEST_F(MutexDifferentLockTypesTest, unique_lock)
{
    Mutex<int> m(0);
    auto lock = m.lock<std::unique_lock>();
    EXPECT_EQ(*lock, 0);
    *lock = 2;

    std::unique_lock<std::mutex>& ul = lock;
    ul.unlock();
    ul.lock();
    EXPECT_EQ(*lock, 2);
}

struct MutexSharedMutexTest : ::testing::Test
{
};

TEST_F(MutexSharedMutexTest, shared_lock_for_const_access)
{
    Mutex<int, std::shared_mutex> const m(100);
    Mutex<int, std::shared_mutex> const& const_ref = m;
    auto lock = const_ref.lock<std::shared_lock>();
    EXPECT_EQ(*lock, 100);
}

TEST_F(MutexSharedMutexTest, unique_lock_for_mutable_access)
{
    Mutex<int, std::shared_mutex> m(100);
    {
        auto lock = m.lock<std::unique_lock>();
        *lock = 200;
    }
    auto lock = m.lock<std::shared_lock>();
    EXPECT_EQ(*lock, 200);
}

struct MutexComplexTypeTest : ::testing::Test
{
    struct Data
    {
        int x;
        std::string y;

        Data(int x_, std::string y_) : x(x_), y(std::move(y_))
        {
        }
    };
};

TEST_F(MutexComplexTypeTest, construct_and_access)
{
    auto m = Mutex<Data>::make(42, "hello");
    auto lock = m.lock();
    EXPECT_EQ(lock->x, 42);
    EXPECT_EQ(lock->y, "hello");
}

TEST_F(MutexComplexTypeTest, modify_fields)
{
    auto m = Mutex<Data>::make(42, "hello");
    {
        auto lock = m.lock();
        lock->x = 100;
        lock->y = "world";
    }
    {
        auto lock = m.lock();
        EXPECT_EQ(lock->x, 100);
        EXPECT_EQ(lock->y, "world");
    }
}

TEST_F(MutexComplexTypeTest, const_access_to_fields)
{
    auto const m = Mutex<Data>::make(42, "hello");
    auto lock = m.lock();
    static_assert(std::is_const_v<std::remove_reference_t<decltype(*lock)>>);
    EXPECT_EQ(lock->x, 42);
    EXPECT_EQ(lock->y, "hello");
}
