//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2017 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef TEST_UNIT_TEST_MULTI_RUNNER_H
#define TEST_UNIT_TEST_MULTI_RUNNER_H

#include <boost/beast/core/static_string.hpp>
#include <beast/unit_test/global_suites.hpp>
#include <beast/unit_test/runner.hpp>

#include <boost/container/static_vector.hpp>
#include <boost/interprocess/ipc/message_queue.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/sync/interprocess_mutex.hpp>

#include <atomic>
#include <chrono>
#include <numeric>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>

namespace ripple {
namespace test {

namespace detail {

using clock_type = std::chrono::steady_clock;

struct case_results
{
    std::string name;
    std::size_t total = 0;
    std::size_t failed = 0;

    explicit case_results(std::string name_ = "") : name(std::move(name_))
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

    explicit suite_results(std::string name_ = "") : name(std::move(name_))
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
        // A parent process will periodically increment `keep_alive_`. The child
        // processes will check if `keep_alive_` is being incremented. If it is
        // not incremented for a sufficiently long time, the child will assume
        // the parent process has died.
        std::atomic<std::size_t> keep_alive_{0};

        mutable boost::interprocess::interprocess_mutex m_;
        detail::results results_;

        std::size_t
        checkout_job_index();

        std::size_t
        checkout_test_index();

        bool
        any_failed() const;

        void
        any_failed(bool v);

        void
        inc_keep_alive_count();

        std::size_t
        get_keep_alive_count();

        void
        add(results const& r);

        template <class S>
        void
        print_results(S& s);
    };

    static constexpr const char* shared_mem_name_ = "RippledUnitTestSharedMem";
    // name of the message queue a multi_runner_child will use to communicate
    // with multi_runner_parent
    static constexpr const char* message_queue_name_ =
        "RippledUnitTestMessageQueue";

    // `inner_` will be created in shared memory
    inner* inner_;
    // shared memory to use for the `inner` member
    boost::interprocess::shared_memory_object shared_mem_;
    boost::interprocess::mapped_region region_;

protected:
    std::unique_ptr<boost::interprocess::message_queue> message_queue_;

    enum class MessageType : std::uint8_t { test_start, test_end, log };
    void
    message_queue_send(MessageType mt, std::string const& s);

public:
    multi_runner_base();
    ~multi_runner_base();

    std::size_t
    checkout_test_index();

    std::size_t
    checkout_job_index();

    void
    any_failed(bool v);

    void
    add(results const& r);

    void
    inc_keep_alive_count();

    std::size_t
    get_keep_alive_count();

    template <class S>
    void
    print_results(S& s);

    bool
    any_failed() const;
};

}  // namespace detail

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
    any_failed() const;
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
    std::size_t num_jobs_{0};
    bool quiet_{false};
    bool print_log_{true};

    std::atomic<bool> continue_keep_alive_{true};
    std::thread keep_alive_thread_;

public:
    multi_runner_child(multi_runner_child const&) = delete;
    multi_runner_child&
    operator=(multi_runner_child const&) = delete;

    multi_runner_child(std::size_t num_jobs, bool quiet, bool print_log);
    ~multi_runner_child();

    template <class Pred>
    bool
    run_multi(Pred pred);

private:
    virtual void
    on_suite_begin(beast::unit_test::suite_info const& info) override;

    virtual void
    on_suite_end() override;

    virtual void
    on_case_begin(std::string const& name) override;

    virtual void
    on_case_end() override;

    virtual void
    on_pass() override;

    virtual void
    on_fail(std::string const& reason) override;

    virtual void
    on_log(std::string const& s) override;
};

//------------------------------------------------------------------------------

template <class Pred>
bool
multi_runner_child::run_multi(Pred pred)
{
    auto const& suite = beast::unit_test::global_suites();
    auto const num_tests = suite.size();
    bool failed = false;

    auto get_test = [&]() -> beast::unit_test::suite_info const* {
        auto const cur_test_index = checkout_test_index();
        if (cur_test_index >= num_tests)
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
            if (num_jobs_ <= 1)
                throw;  // a single process can die

            // inform the parent
            std::stringstream s;
            s << job_index_ << ">  failed Unhandled exception in test.\n";
            message_queue_send(MessageType::log, s.str());
            failed = true;
        }
    }
    any_failed(failed);
    return failed;
}

}  // namespace test
}  // namespace ripple

#endif
