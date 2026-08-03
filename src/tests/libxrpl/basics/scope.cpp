#include <xrpl/basics/scope.h>

#include <gtest/gtest.h>

#include <utility>

using namespace xrpl;

TEST(scope, ScopeExit)
{
    // ScopeExit always executes the functor on destruction,
    // unless release() is called
    int i = 0;
    {
        ScopeExit const x{[&i]() { i = 1; }};
    }
    EXPECT_EQ(i, 1);
    {
        ScopeExit x{[&i]() { i = 2; }};
        x.release();
    }
    EXPECT_EQ(i, 1);
    {
        ScopeExit x{[&i]() { i += 2; }};
        auto x2 = std::move(x);
    }
    EXPECT_EQ(i, 3);
    {
        ScopeExit x{[&i]() { i = 4; }};
        x.release();
        auto x2 = std::move(x);
    }
    EXPECT_EQ(i, 3);
    {
        try
        {
            ScopeExit const x{[&i]() { i = 5; }};
            throw 1;
        }
        catch (...)  // NOLINT(bugprone-empty-catch)
        {
        }
    }
    EXPECT_EQ(i, 5);
    {
        try
        {
            ScopeExit x{[&i]() { i = 6; }};
            x.release();
            throw 1;
        }
        catch (...)  // NOLINT(bugprone-empty-catch)
        {
        }
    }
    EXPECT_EQ(i, 5);
}

TEST(scope, ScopeFail)
{
    // ScopeFail executes the functor on destruction only
    // if an exception is unwinding, unless release() is called
    int i = 0;
    {
        ScopeFail const x{[&i]() { i = 1; }};
    }
    EXPECT_EQ(i, 0);
    {
        ScopeFail x{[&i]() { i = 2; }};
        x.release();
    }
    EXPECT_EQ(i, 0);
    {
        ScopeFail x{[&i]() { i = 3; }};
        auto x2 = std::move(x);
    }
    EXPECT_EQ(i, 0);
    {
        ScopeFail x{[&i]() { i = 4; }};
        x.release();
        auto x2 = std::move(x);
    }
    EXPECT_EQ(i, 0);
    {
        try
        {
            ScopeFail const x{[&i]() { i = 5; }};
            throw 1;
        }
        catch (...)  // NOLINT(bugprone-empty-catch)
        {
        }
    }
    EXPECT_EQ(i, 5);
    {
        try
        {
            ScopeFail x{[&i]() { i = 6; }};
            x.release();
            throw 1;
        }
        catch (...)  // NOLINT(bugprone-empty-catch)
        {
        }
    }
    EXPECT_EQ(i, 5);
}

TEST(scope, ScopeSuccess)
{
    // ScopeSuccess executes the functor on destruction only
    // if an exception is not unwinding, unless release() is called
    int i = 0;
    {
        ScopeSuccess const x{[&i]() { i = 1; }};
    }
    EXPECT_EQ(i, 1);
    {
        ScopeSuccess x{[&i]() { i = 2; }};
        x.release();
    }
    EXPECT_EQ(i, 1);
    {
        ScopeSuccess x{[&i]() { i += 2; }};
        auto x2 = std::move(x);
    }
    EXPECT_EQ(i, 3);
    {
        ScopeSuccess x{[&i]() { i = 4; }};
        x.release();
        auto x2 = std::move(x);
    }
    EXPECT_EQ(i, 3);
    {
        try
        {
            ScopeSuccess const x{[&i]() { i = 5; }};
            throw 1;
        }
        catch (...)  // NOLINT(bugprone-empty-catch)
        {
        }
    }
    EXPECT_EQ(i, 3);
    {
        try
        {
            ScopeSuccess x{[&i]() { i = 6; }};
            x.release();
            throw 1;
        }
        catch (...)  // NOLINT(bugprone-empty-catch)
        {
        }
    }
    EXPECT_EQ(i, 3);
}
