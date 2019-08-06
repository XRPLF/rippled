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

#include <test/unit_test/multi_runner.h>

#include <beast/unit_test/amount.hpp>

#include <boost/lexical_cast.hpp>

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace ripple {
namespace test {

extern void
incPorts();

namespace detail {

std::string
fmtdur(typename clock_type::duration const& d)
{
    using namespace std::chrono;
    auto const ms = duration_cast<milliseconds>(d);
    if (ms < seconds{1})
        return boost::lexical_cast<std::string>(ms.count()) + "ms";
    std::stringstream ss;
    ss << std::fixed << std::setprecision(1) << (ms.count() / 1000.) << "s";
    return ss.str();
}

//------------------------------------------------------------------------------

void
suite_results::add(case_results const& r)
{
    ++cases;
    total += r.total;
    failed += r.failed;
}

//------------------------------------------------------------------------------

void
results::add(suite_results const& r)
{
    ++suites;
    total += r.total;
    cases += r.cases;
    failed += r.failed;
    auto const elapsed = clock_type::now() - r.start;
    if (elapsed >= std::chrono::seconds{1})
    {
        auto const iter = std::lower_bound(
            top.begin(),
            top.end(),
            elapsed,
            [](run_time const& t1, typename clock_type::duration const& t2) {
                return t1.second > t2;
            });

        if (iter != top.end())
        {
            if (top.size() == max_top && iter == top.end() - 1)
            {
                // avoid invalidating the iterator
                *iter = run_time{
                    static_string{static_string::string_view_type{r.name}},
                    elapsed};
            }
            else
            {
                if (top.size() == max_top)
                    top.resize(top.size() - 1);
                top.emplace(
                    iter,
                    static_string{static_string::string_view_type{r.name}},
                    elapsed);
            }
        }
        else if (top.size() < max_top)
        {
            top.emplace_back(
                static_string{static_string::string_view_type{r.name}},
                elapsed);
        }
    }
}

void
results::merge(results const& r)
{
    suites += r.suites;
    total += r.total;
    cases += r.cases;
    failed += r.failed;

    // combine the two top collections
    boost::container::static_vector<run_time, 2 * max_top> top_result;
    top_result.resize(top.size() + r.top.size());
    std::merge(
        top.begin(),
        top.end(),
        r.top.begin(),
        r.top.end(),
        top_result.begin(),
        [](run_time const& t1, run_time const& t2) {
            return t1.second > t2.second;
        });

    if (top_result.size() > max_top)
        top_result.resize(max_top);

    top = top_result;
}

template <class S>
void
results::print(S& s)
{
    using namespace beast::unit_test;

    if (top.size() > 0)
    {
        s << "Longest suite times:\n";
        for (auto const& [name, dur] : top)
            s << std::setw(8) << fmtdur(dur) << " " << name << '\n';
    }

    auto const elapsed = clock_type::now() - start;
    s << fmtdur(elapsed) << ", " << amount{suites, "suite"} << ", "
      << amount{cases, "case"} << ", " << amount{total, "test"} << " total, "
      << amount{failed, "failure"} << std::endl;
}

//------------------------------------------------------------------------------

template <bool IsParent>
std::size_t
multi_runner_base<IsParent>::inner::checkout_job_index()
{
    return job_index_++;
}

template <bool IsParent>
std::size_t
multi_runner_base<IsParent>::inner::checkout_test_index()
{
    return test_index_++;
}

template <bool IsParent>
bool
multi_runner_base<IsParent>::inner::any_failed() const
{
    return any_failed_;
}

template <bool IsParent>
void
multi_runner_base<IsParent>::inner::any_failed(bool v)
{
    any_failed_ = any_failed_ || v;
}

template <bool IsParent>
void
multi_runner_base<IsParent>::inner::inc_keep_alive_count()
{
    ++keep_alive_;
}

template <bool IsParent>
std::size_t
multi_runner_base<IsParent>::inner::get_keep_alive_count()
{
    return keep_alive_;
}

template <bool IsParent>
void
multi_runner_base<IsParent>::inner::add(results const& r)
{
    std::lock_guard l{m_};
    results_.merge(r);
}

template <bool IsParent>
template <class S>
void
multi_runner_base<IsParent>::inner::print_results(S& s)
{
    std::lock_guard l{m_};
    results_.print(s);
}

template <bool IsParent>
multi_runner_base<IsParent>::multi_runner_base()
{
    try
    {
        if (IsParent)
        {
            // cleanup any leftover state for any previous failed runs
            boost::interprocess::shared_memory_object::remove(shared_mem_name_);
            boost::interprocess::message_queue::remove(message_queue_name_);
        }

        shared_mem_ = boost::interprocess::shared_memory_object{
            std::conditional_t<
                IsParent,
                boost::interprocess::create_only_t,
                boost::interprocess::open_only_t>{},
            shared_mem_name_,
            boost::interprocess::read_write};

        if (IsParent)
        {
            shared_mem_.truncate(sizeof(inner));
            message_queue_ =
                std::make_unique<boost::interprocess::message_queue>(
                    boost::interprocess::create_only,
                    message_queue_name_,
                    /*max messages*/ 16,
                    /*max message size*/ 1 << 20);
        }
        else
        {
            message_queue_ =
                std::make_unique<boost::interprocess::message_queue>(
                    boost::interprocess::open_only, message_queue_name_);
        }

        region_ = boost::interprocess::mapped_region{
            shared_mem_, boost::interprocess::read_write};
        if (IsParent)
            inner_ = new (region_.get_address()) inner{};
        else
            inner_ = reinterpret_cast<inner*>(region_.get_address());
    }
    catch (...)
    {
        if (IsParent)
        {
            boost::interprocess::shared_memory_object::remove(shared_mem_name_);
            boost::interprocess::message_queue::remove(message_queue_name_);
        }
        throw;
    }
}

template <bool IsParent>
multi_runner_base<IsParent>::~multi_runner_base()
{
    if (IsParent)
    {
        inner_->~inner();
        boost::interprocess::shared_memory_object::remove(shared_mem_name_);
        boost::interprocess::message_queue::remove(message_queue_name_);
    }
}

template <bool IsParent>
std::size_t
multi_runner_base<IsParent>::checkout_test_index()
{
    return inner_->checkout_test_index();
}

template <bool IsParent>
std::size_t
multi_runner_base<IsParent>::checkout_job_index()
{
    return inner_->checkout_job_index();
}

template <bool IsParent>
bool
multi_runner_base<IsParent>::any_failed() const
{
    return inner_->any_failed();
}

template <bool IsParent>
void
multi_runner_base<IsParent>::any_failed(bool v)
{
    return inner_->any_failed(v);
}

template <bool IsParent>
void
multi_runner_base<IsParent>::add(results const& r)
{
    inner_->add(r);
}

template <bool IsParent>
void
multi_runner_base<IsParent>::inc_keep_alive_count()
{
    inner_->inc_keep_alive_count();
}

template <bool IsParent>
std::size_t
multi_runner_base<IsParent>::get_keep_alive_count()
{
    return inner_->get_keep_alive_count();
}

template <bool IsParent>
template <class S>
void
multi_runner_base<IsParent>::print_results(S& s)
{
    inner_->print_results(s);
}

template <bool IsParent>
void
multi_runner_base<IsParent>::message_queue_send(MessageType mt, std::string const& s)
{
    // must use a mutex since the two "sends" must happen in order
    std::lock_guard l{inner_->m_};
    message_queue_->send(&mt, sizeof(mt), /*priority*/ 0);
    message_queue_->send(s.c_str(), s.size(), /*priority*/ 0);
}

template <bool IsParent>
constexpr const char* multi_runner_base<IsParent>::shared_mem_name_;
template <bool IsParent>
constexpr const char* multi_runner_base<IsParent>::message_queue_name_;

}  // detail

//------------------------------------------------------------------------------

multi_runner_parent::multi_runner_parent()
    : os_(std::cout)
{
    message_queue_thread_ = std::thread([this] {
        std::vector<char> buf(1 << 20);
        while (this->continue_message_queue_ ||
               this->message_queue_->get_num_msg())
        {
            // let children know the parent is still alive
            this->inc_keep_alive_count();
            if (!this->message_queue_->get_num_msg())
            {
                // If a child does not see the keep alive count incremented,
                // it will assume the parent has died. This sleep time needs
                // to be small enough so the child will see increments from
                // a live parent.
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            try
            {
                std::size_t recvd_size = 0;
                unsigned int priority = 0;
                this->message_queue_->receive(
                    buf.data(), buf.size(), recvd_size, priority);
                if (!recvd_size)
                    continue;
                assert (recvd_size == 1);
                MessageType mt{*reinterpret_cast<MessageType*>(buf.data())};

                this->message_queue_->receive(
                    buf.data(), buf.size(), recvd_size, priority);
                if (recvd_size)
                {
                    std::string s{buf.data(), recvd_size};
                    switch (mt)
                    {
                        case MessageType::log:
                            this->os_ << s;
                            this->os_.flush();
                            break;
                        case MessageType::test_start:
                            running_suites_.insert(std::move(s));
                            break;
                        case MessageType::test_end:
                            running_suites_.erase(s);
                            break;
                        default:
                            assert(0);  // unknown message type
                    }
                }
            }
            catch (std::exception const& e)
            {
                std::cerr << "Error: " << e.what()
                          << " reading unit test message queue.\n";
                return;
            }
            catch (...)
            {
                std::cerr << "Unknown error reading unit test message queue.\n";
                return;
            }
        }
    });
}

multi_runner_parent::~multi_runner_parent()
{
    using namespace beast::unit_test;

    continue_message_queue_ = false;
    message_queue_thread_.join();

    print_results(os_);

    for (auto const& s : running_suites_)
    {
        os_ << "\nSuite: " << s
            << " failed to complete. The child process may have crashed.\n";
    }
}

bool
multi_runner_parent::any_failed() const
{
    return multi_runner_base<true>::any_failed();
}

//------------------------------------------------------------------------------

multi_runner_child::multi_runner_child(
    std::size_t num_jobs,
    bool quiet,
    bool print_log)
    : job_index_{checkout_job_index()}
    , num_jobs_{num_jobs}
    , quiet_{quiet}
    , print_log_{!quiet || print_log}
{
    // incPort twice (2*jobIndex_) because some tests need two envs
    for (std::size_t i = 0; i < 2 * job_index_; ++i)
        test::incPorts();

    if (num_jobs_ > 1)
    {
        keep_alive_thread_ = std::thread([this] {
            std::size_t last_count = get_keep_alive_count();
            while (this->continue_keep_alive_)
            {
                // Use a small sleep time so in the normal case the child
                // process may shutdown quickly. However, to protect against
                // false alarms, use a longer sleep time later on.
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                auto cur_count = this->get_keep_alive_count();
                if (cur_count == last_count)
                {
                    // longer sleep time to protect against false alarms
                    std::this_thread::sleep_for(std::chrono::seconds(2));
                    cur_count = this->get_keep_alive_count();
                    if (cur_count == last_count)
                    {
                        // assume parent process is no longer alive
                        std::cerr << "multi_runner_child " << job_index_
                                  << ": Assuming parent died, exiting.\n";
                        std::exit(EXIT_FAILURE);
                    }
                }
                last_count = cur_count;
            }
        });
    }
}

multi_runner_child::~multi_runner_child()
{
    if (num_jobs_ > 1)
    {
        continue_keep_alive_ = false;
        keep_alive_thread_.join();
    }

    add(results_);
}

void
multi_runner_child::on_suite_begin(beast::unit_test::suite_info const& info)
{
    suite_results_ = detail::suite_results{info.full_name()};
    message_queue_send(MessageType::test_start, suite_results_.name);
}

void
multi_runner_child::on_suite_end()
{
    results_.add(suite_results_);
    message_queue_send(MessageType::test_end, suite_results_.name);
}

void
multi_runner_child::on_case_begin(std::string const& name)
{
    case_results_ = detail::case_results(name);

    if (quiet_)
        return;

    std::stringstream s;
    if (num_jobs_ > 1)
        s << job_index_ << "> ";
    s << suite_results_.name
      << (case_results_.name.empty() ? "" : (" " + case_results_.name)) << '\n';
    message_queue_send(MessageType::log, s.str());
}

void
multi_runner_child::on_case_end()
{
    suite_results_.add(case_results_);
}

void
multi_runner_child::on_pass()
{
    ++case_results_.total;
}

void
multi_runner_child::on_fail(std::string const& reason)
{
    ++case_results_.failed;
    ++case_results_.total;
    std::stringstream s;
    if (num_jobs_ > 1)
        s << job_index_ << "> ";
    s << "#" << case_results_.total << " failed" << (reason.empty() ? "" : ": ")
      << reason << '\n';
    message_queue_send(MessageType::log, s.str());
}

void
multi_runner_child::on_log(std::string const& msg)
{
    if (!print_log_)
        return;

    std::stringstream s;
    if (num_jobs_ > 1)
        s << job_index_ << "> ";
    s << msg;
    message_queue_send(MessageType::log, s.str());
}

namespace detail {
template class multi_runner_base<true>;
template class multi_runner_base<false>;
}

}  // unit_test
}  // beast
