#pragma once

#include <xrpl/beast/unit_test/global_suites.h>
#include <xrpl/beast/unit_test/runner.h>

#include <boost/beast/core/static_string.hpp>
#include <boost/container/static_vector.hpp>
#include <boost/interprocess/ipc/message_queue.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/sync/interprocess_mutex.hpp>

#include <atomic>
#include <chrono>
#include <numeric>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>

namespace xrpl {

namespace detail {

using clock_type = std::chrono::steady_clock;

struct case_results
{
    std::string name;
    std::size_t total = 0;
    std::size_t failed = 0;

    explicit case_results(std::string name = "") : name(std::move(name))
    {
    }
};

struct suite_results
{
    std::string name;
    std::size_t cases = 0;
    std::size_t total = 0;
    std::size_t failed = 0;
    typename clock_type::time_point start = clock_type::now();

    explicit suite_results(std::string name = "") : name(std::move(name))
    {
    }

    void
    add(case_results const& r);
};

struct results
{
    using static_string = boost::beast::static_string<256>;
    // results may be stored in shared memory. Use `static_string` to ensure
    // pointers from different memory spaces do not co-mingle
    using run_time = std::pair<static_string, typename clock_type::duration>;

    enum { max_top = 10 };

    std::size_t suites = 0;
    std::size_t cases = 0;
    std::size_t total = 0;
    std::size_t failed = 0;
    boost::container::static_vector<run_time, max_top> top;
    typename clock_type::time_point start = clock_type::now();

    void
    add(suite_results const& r);

    void
    merge(results const& r);

    template <class S>
    void
    print(S& s);
};

template <bool IsParent>
class multi_runner_base
{
    // `inner` will be created in shared memory. This is one way
    // multi_runner_parent and multi_runner_child object communicate. The other
    // way they communicate is through message queues.
    struct inner
    {
        std::atomic<std::size_t> job_index_{0};
        std::atomic<std::size_t> test_index_{0};
        std::atomic<bool> any_failed_{false};
        // A parent process will periodically kINCREMENT `keep_alive_`. The child
        // processes will check if `keep_alive_` is being incremented. If it is
        // not incremented for a sufficiently long time, the child will assume
        // the parent process has died.
        std::atomic<std::size_t> keep_alive_{0};

        mutable boost::interprocess::interprocess_mutex m_;
        detail::results results_;

        std::size_t
        checkoutJobIndex();

        std::size_t
        checkoutTestIndex();

        bool
        anyFailed() const;

        void
        anyFailed(bool v);

        std::size_t
        tests() const;

        std::size_t
        suites() const;

        void
        incKeepAliveCount();

        std::size_t
        getKeepAliveCount();

        void
        add(results const& r);

        template <class S>
        void
        print_results(S& s);
    };

    static constexpr char const* shared_mem_name__ = "RippledUnitTestSharedMem";
    // name of the message queue a multi_runner_child will use to communicate
    // with multi_runner_parent
    static constexpr char const* message_queue_name_ = "XrpldUnitTestMessageQueue";

    // `inner_` will be created in shared memory
    inner* inner_;
    // shared memory to use for the `inner` member
    boost::interprocess::shared_memory_object shared_mem_;
    boost::interprocess::mapped_region region_;

protected:
    std::unique_ptr<boost::interprocess::message_queue> message_queue_;

    enum class MessageType : std::uint8_t { test_start, test_end, log };
    void
    messageQueueSend(MessageType mt, std::string const& s);

public:
    multi_runner_base();
    ~multi_runner_base();

    std::size_t
    checkoutTestIndex();

    std::size_t
    checkoutJobIndex();

    void
    anyFailed(bool v);

    void
    add(results const& r);

    void
    incKeepAliveCount();

    std::size_t
    getKeepAliveCount();

    template <class S>
    void
    print_results(S& s);

    bool
    anyFailed() const;

    std::size_t
    tests() const;

    std::size_t
    suites() const;

    void
    addFailures(std::size_t failures);
};

}  // namespace detail

namespace test {

//------------------------------------------------------------------------------

/** Manager for children running unit tests
 */
class multi_runner_parent : private detail::multi_runner_base</*IsParent*/ true>
{
private:
    // message_queue_ is used to collect log messages from the children
    std::ostream& os_;
    std::atomic<bool> continue_message_queue_{true};
    std::thread message_queue_thread_;
    // track running suites so if a child crashes the culprit can be flagged
    std::set<std::string> running_suites_;

public:
    multi_runner_parent(multi_runner_parent const&) = delete;
    multi_runner_parent&
    operator=(multi_runner_parent const&) = delete;

    multi_runner_parent();
    ~multi_runner_parent();

    bool
    anyFailed() const;

    std::size_t
    tests() const;

    std::size_t
    suites() const;

    void
    addFailures(std::size_t failures);
};

//------------------------------------------------------------------------------

/** A class to run a subset of unit tests
 */
class multi_runner_child : public beast::unit_test::runner,
                           private detail::multi_runner_base</*IsParent*/ false>
{
private:
    std::size_t job_index_;
    detail::results results_;
    detail::suite_results suite_results_;
    detail::case_results case_results_;
    std::size_t num_jobs__{0};
    bool quiet_{false};
    bool print_log_{true};

    std::atomic<bool> continue_keep_alive_{true};
    std::thread keep_alive_thread_;

public:
    multi_runner_child(multi_runner_child const&) = delete;
    multi_runner_child&
    operator=(multi_runner_child const&) = delete;

    multi_runner_child(std::size_t num_jobs_, bool quiet, bool print_log);
    ~multi_runner_child();

    std::size_t
    tests() const;

    std::size_t
    suites() const;

    void
    addFailures(std::size_t failures);

    template <class Pred>
    bool
    run_multi(Pred pred);

private:
    virtual void
    onSuiteBegin(beast::unit_test::SuiteInfo const& info) override;

    virtual void
    onSuiteEnd() override;

    virtual void
    onCaseBegin(std::string const& name) override;

    virtual void
    onCaseEnd() override;

    virtual void
    onPass() override;

    virtual void
    onFail(std::string const& reason) override;

    virtual void
    onLog(std::string const& s) override;
};

//------------------------------------------------------------------------------

template <class Pred>
bool
multi_runner_child::run_multi(Pred pred)
{
    auto const& suite = beast::unit_test::global_suites();
    auto const num_tests_ = suite.size();
    bool failed = false;

    auto get_test = [&]() -> beast::unit_test::SuiteInfo const* {
        auto const cur_test_index = checkoutTestIndex();
        if (cur_test_index >= num_tests_)
            return nullptr;
        auto iter = suite.begin();
        std::advance(iter, cur_test_index);
        return &*iter;
    };
    while (auto t = get_test())
    {
        if (!pred(*t))
            continue;
        try
        {
            failed = run(*t) || failed;
        }
        catch (...)
        {
            if (num_jobs__ <= 1)
                throw;  // a single process can die

            // inform the parent
            std::stringstream s;
            s << job_index_ << ">  failed Unhandled exception in test.\n";
            messageQueueSend(MessageType::log, s.str());
            failed = true;
        }
    }
    anyFailed(failed);
    return failed;
}

}  // namespace test
}  // namespace xrpl
