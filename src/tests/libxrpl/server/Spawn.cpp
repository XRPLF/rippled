#include <xrpl/server/detail/Spawn.h>

#include <boost/asio/io_context.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/asio/strand.hpp>

#include <gtest/gtest.h>

#include <stdexcept>

using namespace xrpl;

namespace {

// util::spawn dispatches on whether its argument is already a strand. In-tree
// callers pass a strand, so these tests are what exercise the non-strand branch
// (which wraps the argument in a new strand): they force it to compile and run
// for a bare io_context and for an io_context executor. A coroutine spawned on
// each context form must execute exactly once while the io_context runs.

TEST(SpawnTest, runs_on_io_context_lvalue)
{
    boost::asio::io_context ioc;
    int runs = 0;
    util::spawn(ioc, [&](boost::asio::yield_context) { ++runs; });

    EXPECT_EQ(runs, 0);  // deferred until the context runs
    ioc.run();
    EXPECT_EQ(runs, 1);
}

TEST(SpawnTest, runs_on_executor_lvalue)
{
    boost::asio::io_context ioc;
    auto executor = ioc.get_executor();
    int runs = 0;
    util::spawn(executor, [&](boost::asio::yield_context) { ++runs; });

    EXPECT_EQ(runs, 0);
    ioc.run();
    EXPECT_EQ(runs, 1);
}

TEST(SpawnTest, runs_on_strand)
{
    boost::asio::io_context ioc;
    auto strand = boost::asio::make_strand(ioc);
    int runs = 0;
    util::spawn(strand, [&](boost::asio::yield_context) { ++runs; });

    EXPECT_EQ(runs, 0);
    ioc.run();
    EXPECT_EQ(runs, 1);
}

TEST(SpawnTest, propagates_exception_to_run)
{
    boost::asio::io_context ioc;
    util::spawn(
        ioc, [](boost::asio::yield_context) { throw std::runtime_error("spawned failure"); });

    // kPropagateExceptions must rethrow out of io_context::run(), preserving
    // both the type and the message rather than swallowing the exception.
    try
    {
        ioc.run();
        FAIL() << "expected the spawned exception to propagate";
    }
    catch (std::runtime_error const& e)
    {
        EXPECT_STREQ(e.what(), "spawned failure");
    }
}

}  // namespace
