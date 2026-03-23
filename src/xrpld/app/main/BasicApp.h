#pragma once

#include <boost/asio/io_context.hpp>

#include <optional>
#include <thread>
#include <vector>

/** Manages an io_context and its worker threads.
 *
 *  Ensures the io_context outlives all derived classes by joining worker
 *  threads in the destructor. Supports immediate or deferred thread startup.
 *
 *  @note Thread-safe after construction completes. The deferred-start
 *        constructor and startIOThreads() must be called from a single thread.
 */
class BasicApp
{
private:
    std::optional<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>> work_;
    std::vector<std::thread> threads_;
    boost::asio::io_context io_context_;

    /// Number of IO worker threads to create.
    std::size_t numberOfThreads_;

public:
    BasicApp(std::size_t numberOfThreads);
    ~BasicApp();

    boost::asio::io_context&
    get_io_context()
    {
        return io_context_;
    }

    size_t
    get_number_of_threads() const
    {
        return threads_.size();
    }

protected:
    /** Tag type for deferred thread startup.
     *
     *  Pass to the protected constructor to construct without starting IO
     *  threads. The derived class must call startIOThreads() once its own
     *  construction is complete.
     */
    struct DeferStart
    {
    };

    /** Construct without starting IO threads.
     *
     *  @param numberOfThreads Desired number of IO worker threads.
     *  @param  Tag to select deferred startup.
     */
    BasicApp(std::size_t numberOfThreads, DeferStart);

    /** Start the IO worker threads.
     *
     *  @note Must be called exactly once after the deferred-start constructor.
     */
    void
    startIOThreads();
};
