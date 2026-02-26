#include <xrpl/basics/scope.h>

#include <gtest/gtest.h>

using namespace xrpl;

TEST(scope, scope_exit)
{
    // scope_exit always executes the functor on destruction,
    // unless release() is called
    int i = 0;
    {
        scope_exit x{[&i]() { i = 1; }};
    }
    EXPECT_EQ(i, 1);
    {
        scope_exit x{[&i]() { i = 2; }};
        x.release();
    }
    EXPECT_EQ(i, 1);
    {
        scope_exit x{[&i]() { i += 2; }};
        auto x2 = std::move(x);
    }
    EXPECT_EQ(i, 3);
    {
        scope_exit x{[&i]() { i = 4; }};
        x.release();
        auto x2 = std::move(x);
    }
    EXPECT_EQ(i, 3);
    {
        try
        {
            scope_exit x{[&i]() { i = 5; }};
            throw 1;
        }
        catch (...)
        {
        }
    }
    EXPECT_EQ(i, 5);
    {
        try
        {
            scope_exit x{[&i]() { i = 6; }};
            x.release();
            throw 1;
        }
        catch (...)
        {
        }
    }
    EXPECT_EQ(i, 5);
}

TEST(scope, scope_fail)
{
    // scope_fail executes the functor on destruction only
    // if an exception is unwinding, unless release() is called
    int i = 0;
    {
        scope_fail x{[&i]() { i = 1; }};
    }
    EXPECT_EQ(i, 0);
    {
        scope_fail x{[&i]() { i = 2; }};
        x.release();
    }
    EXPECT_EQ(i, 0);
    {
        scope_fail x{[&i]() { i = 3; }};
        auto x2 = std::move(x);
    }
    EXPECT_EQ(i, 0);
    {
        scope_fail x{[&i]() { i = 4; }};
        x.release();
        auto x2 = std::move(x);
    }
    EXPECT_EQ(i, 0);
    {
        try
        {
            scope_fail x{[&i]() { i = 5; }};
            throw 1;
        }
        catch (...)
        {
        }
    }
    EXPECT_EQ(i, 5);
    {
        try
        {
            scope_fail x{[&i]() { i = 6; }};
            x.release();
            throw 1;
        }
        catch (...)
        {
        }
    }
    EXPECT_EQ(i, 5);
}

TEST(scope, scope_success)
{
    // scope_success executes the functor on destruction only
    // if an exception is not unwinding, unless release() is called
    int i = 0;
    {
        scope_success x{[&i]() { i = 1; }};
    }
    EXPECT_EQ(i, 1);
    {
        scope_success x{[&i]() { i = 2; }};
        x.release();
    }
    EXPECT_EQ(i, 1);
    {
        scope_success x{[&i]() { i += 2; }};
        auto x2 = std::move(x);
    }
    EXPECT_EQ(i, 3);
    {
        scope_success x{[&i]() { i = 4; }};
        x.release();
        auto x2 = std::move(x);
    }
    EXPECT_EQ(i, 3);
    {
        try
        {
            scope_success x{[&i]() { i = 5; }};
            throw 1;
        }
        catch (...)
        {
        }
    }
    EXPECT_EQ(i, 3);
    {
        try
        {
            scope_success x{[&i]() { i = 6; }};
            x.release();
            throw 1;
        }
        catch (...)
        {
        }
    }
    EXPECT_EQ(i, 3);
}
