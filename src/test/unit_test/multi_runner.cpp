#include <test/unit_test/multi_runner.h>

#include <xrpl/beast/unit_test/amount.h>
#include <xrpl/beast/unit_test/suite_info.h>

#include <boost/container/static_vector.hpp>
#include <boost/interprocess/creation_tags.hpp>
#include <boost/interprocess/detail/os_file_functions.hpp>
#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/lexical_cast.hpp>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace xrpl {

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
SuiteResults::add(CaseResults const& r)
{
    ++cases;
    total += r.total;
    failed += r.failed;
}

//------------------------------------------------------------------------------

void
Results::add(SuiteResults const& r)
{
    ++suites;
    total += r.total;
    cases += r.cases;
    failed += r.failed;
    auto const elapsed = clock_type::now() - r.start;
    if (elapsed >= std::chrono::seconds{1})
    {
        // NOLINTNEXTLINE(modernize-use-ranges)
        auto const iter = std::lower_bound(
            top.begin(),
            top.end(),
            elapsed,
            [](run_time const& t1, typename clock_type::duration const& t2) {
                return t1.second > t2;
            });

        if (iter != top.end())
        {
            if (top.size() == kMaxTop && iter == top.end() - 1)
            {
                // avoid invalidating the iterator
                *iter = run_time{static_string{static_string::string_view_type{r.name}}, elapsed};
            }
            else
            {
                if (top.size() == kMaxTop)
                    top.resize(top.size() - 1);
                top.emplace(iter, static_string{static_string::string_view_type{r.name}}, elapsed);
            }
        }
        else if (top.size() < kMaxTop)
        {
            top.emplace_back(static_string{static_string::string_view_type{r.name}}, elapsed);
        }
    }
}

void
Results::merge(Results const& r)
{
    suites += r.suites;
    total += r.total;
    cases += r.cases;
    failed += r.failed;

    // combine the two top collections
    boost::container::static_vector<run_time, 2 * kMaxTop> topResult;
    topResult.resize(top.size() + r.top.size());
    std::ranges::merge(top, r.top, topResult.begin(), [](run_time const& t1, run_time const& t2) {
        return t1.second > t2.second;
    });

    if (topResult.size() > kMaxTop)
        topResult.resize(kMaxTop);

    top = topResult;
}

template <class S>
void
Results::print(S& s)
{
    using namespace beast::unit_test;

    if (!top.empty())
    {
        s << "Longest suite times:\n";
        for (auto const& [name, dur] : top)
            s << std::setw(8) << fmtdur(dur) << " " << name << '\n';
    }

    auto const elapsed = clock_type::now() - start;
    s << fmtdur(elapsed) << ", " << Amount{suites, "suite"} << ", " << Amount{cases, "case"} << ", "
      << Amount{total, "test"} << " total, " << Amount{failed, "failure"} << std::endl;
}

//------------------------------------------------------------------------------

template <bool IsParent>
std::size_t
MultiRunnerBase<IsParent>::Inner::checkoutJobIndex()
{
    return jobIndex++;
}

template <bool IsParent>
std::size_t
MultiRunnerBase<IsParent>::Inner::checkoutTestIndex()
{
    return testIndex++;
}

template <bool IsParent>
bool
MultiRunnerBase<IsParent>::Inner::anyFailed() const
{
    return anyFailedFlag;
}

template <bool IsParent>
void
MultiRunnerBase<IsParent>::Inner::anyFailed(bool v)
{
    anyFailedFlag = anyFailedFlag || v;
}

template <bool IsParent>
std::size_t
MultiRunnerBase<IsParent>::Inner::tests() const
{
    std::scoped_lock const l{m};
    return results.total;
}

template <bool IsParent>
std::size_t
MultiRunnerBase<IsParent>::Inner::suites() const
{
    std::scoped_lock const l{m};
    return results.suites;
}

template <bool IsParent>
void
MultiRunnerBase<IsParent>::Inner::incKeepAliveCount()
{
    ++keepAlive;
}

template <bool IsParent>
std::size_t
MultiRunnerBase<IsParent>::Inner::getKeepAliveCount()
{
    return keepAlive;
}

template <bool IsParent>
void
MultiRunnerBase<IsParent>::Inner::add(Results const& r)
{
    std::scoped_lock const l{m};
    results.merge(r);
}

template <bool IsParent>
template <class S>
void
MultiRunnerBase<IsParent>::Inner::printResults(S& s)
{
    std::scoped_lock const l{m};
    results.print(s);
}

template <bool IsParent>
MultiRunnerBase<IsParent>::MultiRunnerBase()
{
    try
    {
        if (IsParent)
        {
            // cleanup any leftover state for any previous failed runs
            boost::interprocess::shared_memory_object::remove(kSharedMemName);
            boost::interprocess::message_queue::remove(kMessageQueueName);
        }

        sharedMem_ = boost::interprocess::shared_memory_object{
            std::conditional_t<
                IsParent,
                boost::interprocess::create_only_t,
                boost::interprocess::open_only_t>{},
            kSharedMemName,
            boost::interprocess::read_write};

        if (IsParent)
        {
            sharedMem_.truncate(sizeof(Inner));
            messageQueue_ = std::make_unique<boost::interprocess::message_queue>(
                boost::interprocess::create_only,
                kMessageQueueName,
                /*max messages*/ 16,
                /*max message size*/ 1 << 20);
        }
        else
        {
            messageQueue_ = std::make_unique<boost::interprocess::message_queue>(
                boost::interprocess::open_only, kMessageQueueName);
        }

        region_ = boost::interprocess::mapped_region{sharedMem_, boost::interprocess::read_write};
        if (IsParent)
        {
            inner_ = new (region_.get_address()) Inner{};
        }
        else
        {
            inner_ = reinterpret_cast<Inner*>(region_.get_address());
        }
    }
    catch (...)
    {
        if (IsParent)
        {
            boost::interprocess::shared_memory_object::remove(kSharedMemName);
            boost::interprocess::message_queue::remove(kMessageQueueName);
        }
        throw;
    }
}

template <bool IsParent>
MultiRunnerBase<IsParent>::~MultiRunnerBase()
{
    if (IsParent)
    {
        inner_->~Inner();
        boost::interprocess::shared_memory_object::remove(kSharedMemName);
        boost::interprocess::message_queue::remove(kMessageQueueName);
    }
}

template <bool IsParent>
std::size_t
MultiRunnerBase<IsParent>::checkoutTestIndex()
{
    return inner_->checkoutTestIndex();
}

template <bool IsParent>
std::size_t
MultiRunnerBase<IsParent>::checkoutJobIndex()
{
    return inner_->checkoutJobIndex();
}

template <bool IsParent>
bool
MultiRunnerBase<IsParent>::anyFailed() const
{
    return inner_->anyFailed();
}

template <bool IsParent>
void
MultiRunnerBase<IsParent>::anyFailed(bool v)
{
    return inner_->anyFailed(v);
}

template <bool IsParent>
void
MultiRunnerBase<IsParent>::add(Results const& r)
{
    inner_->add(r);
}

template <bool IsParent>
void
MultiRunnerBase<IsParent>::incKeepAliveCount()
{
    inner_->incKeepAliveCount();
}

template <bool IsParent>
std::size_t
MultiRunnerBase<IsParent>::getKeepAliveCount()
{
    return inner_->getKeepAliveCount();
}

template <bool IsParent>
template <class S>
void
MultiRunnerBase<IsParent>::printResults(S& s)
{
    inner_->printResults(s);
}

template <bool IsParent>
void
MultiRunnerBase<IsParent>::messageQueueSend(MessageType mt, std::string const& s)
{
    // must use a mutex since the two "sends" must happen in order
    std::scoped_lock const l{inner_->m};
    messageQueue_->send(&mt, sizeof(mt), /*priority*/ 0);
    messageQueue_->send(s.c_str(), s.size(), /*priority*/ 0);
}

template <bool IsParent>
std::size_t
MultiRunnerBase<IsParent>::tests() const
{
    return inner_->tests();
}

template <bool IsParent>
std::size_t
MultiRunnerBase<IsParent>::suites() const
{
    return inner_->suites();
}

template <bool IsParent>
void
MultiRunnerBase<IsParent>::addFailures(std::size_t failures)
{
    Results results;
    results.failed += failures;
    add(results);
    anyFailed(failures != 0);
}

}  // namespace detail

namespace test {

//------------------------------------------------------------------------------

MultiRunnerParent::MultiRunnerParent() : os_(std::cout)
{
    messageQueueThread_ = std::thread([this] {
        std::vector<char> buf(1 << 20);
        while (this->continueMessageQueue_ || this->messageQueue_->get_num_msg())
        {
            // let children know the parent is still alive
            this->incKeepAliveCount();
            if (!this->messageQueue_->get_num_msg())
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
                std::size_t recvdSize = 0;
                unsigned int priority = 0;
                this->messageQueue_->receive(buf.data(), buf.size(), recvdSize, priority);
                if (!recvdSize)
                    continue;
                assert(recvdSize == 1);
                MessageType const mt{*reinterpret_cast<MessageType*>(buf.data())};

                this->messageQueue_->receive(buf.data(), buf.size(), recvdSize, priority);
                if (recvdSize)
                {
                    std::string s{buf.data(), recvdSize};
                    switch (mt)
                    {
                        case MessageType::Log:
                            this->os_ << s;
                            this->os_.flush();
                            break;
                        case MessageType::TestStart:
                            runningSuites_.insert(std::move(s));
                            break;
                        case MessageType::TestEnd:
                            runningSuites_.erase(s);
                            break;
                        default:
                            assert(0);  // unknown message type
                    }
                }
            }
            catch (std::exception const& e)
            {
                std::cerr << "Error: " << e.what() << " reading unit test message queue.\n";
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

MultiRunnerParent::~MultiRunnerParent()
{
    using namespace beast::unit_test;

    continueMessageQueue_ = false;
    messageQueueThread_.join();

    addFailures(runningSuites_.size());

    printResults(os_);

    for (auto const& s : runningSuites_)
    {
        os_ << "\nSuite: " << s << " failed to complete. The child process may have crashed.\n";
    }
}

bool
MultiRunnerParent::anyFailed() const
{
    return MultiRunnerBase<true>::anyFailed();
}

std::size_t
MultiRunnerParent::tests() const
{
    return MultiRunnerBase<true>::tests();
}

std::size_t
MultiRunnerParent::suites() const
{
    return MultiRunnerBase<true>::suites();
}

void
MultiRunnerParent::addFailures(std::size_t failures)
{
    MultiRunnerBase<true>::addFailures(failures);
}

//------------------------------------------------------------------------------

MultiRunnerChild::MultiRunnerChild(std::size_t numJobs, bool quiet, bool printLog)
    : jobIndex_{checkoutJobIndex()}, numJobs_{numJobs}, quiet_{quiet}, printLog_{!quiet || printLog}
{
    if (numJobs_ > 1)
    {
        keepAliveThread_ = std::thread([this] {
            std::size_t lastCount = getKeepAliveCount();
            while (this->continueKeepAlive_)
            {
                // Use a small sleep time so in the normal case the child
                // process may shutdown quickly. However, to protect against
                // false alarms, use a longer sleep time later on.
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                auto curCount = this->getKeepAliveCount();
                if (curCount == lastCount)
                {
                    // longer sleep time to protect against false alarms
                    std::this_thread::sleep_for(std::chrono::seconds(2));
                    curCount = this->getKeepAliveCount();
                    if (curCount == lastCount)
                    {
                        // assume parent process is no longer alive
                        std::cerr << "multi_runner_child " << jobIndex_
                                  << ": Assuming parent died, exiting.\n";
                        std::exit(EXIT_FAILURE);
                    }
                }
                lastCount = curCount;
            }
        });
    }
}

MultiRunnerChild::~MultiRunnerChild()
{
    if (numJobs_ > 1)
    {
        continueKeepAlive_ = false;
        keepAliveThread_.join();
    }

    add(results_);
}

std::size_t
MultiRunnerChild::tests() const
{
    return results_.total;
}

std::size_t
MultiRunnerChild::suites() const
{
    return results_.suites;
}

void
MultiRunnerChild::addFailures(std::size_t failures)
{
    results_.failed += failures;
    anyFailed(failures != 0);
}

void
MultiRunnerChild::onSuiteBegin(beast::unit_test::SuiteInfo const& info)
{
    suiteResults_ = detail::SuiteResults{info.fullName()};
    messageQueueSend(MessageType::TestStart, suiteResults_.name);
}

void
MultiRunnerChild::onSuiteEnd()
{
    if (printLog_ || suiteResults_.failed > 0)
    {
        std::stringstream s;
        if (numJobs_ > 1)
            s << jobIndex_ << "> ";
        s << (suiteResults_.failed > 0 ? "failed: " : "") << suiteResults_.name << " had "
          << suiteResults_.failed << " failures." << std::endl;
        messageQueueSend(MessageType::Log, s.str());
    }
    results_.add(suiteResults_);
    messageQueueSend(MessageType::TestEnd, suiteResults_.name);
}

void
MultiRunnerChild::onCaseBegin(std::string const& name)
{
    caseResults_ = detail::CaseResults(name);

    if (quiet_)
        return;

    std::stringstream s;
    if (numJobs_ > 1)
        s << jobIndex_ << "> ";
    s << suiteResults_.name << (caseResults_.name.empty() ? "" : (" " + caseResults_.name)) << '\n';
    messageQueueSend(MessageType::Log, s.str());
}

void
MultiRunnerChild::onCaseEnd()
{
    suiteResults_.add(caseResults_);
}

void
MultiRunnerChild::onPass()
{
    ++caseResults_.total;
}

void
MultiRunnerChild::onFail(std::string const& reason)
{
    ++caseResults_.failed;
    ++caseResults_.total;
    std::stringstream s;
    if (numJobs_ > 1)
        s << jobIndex_ << "> ";
    s << "#" << caseResults_.total << " failed" << (reason.empty() ? "" : ": ") << reason << '\n';
    messageQueueSend(MessageType::Log, s.str());
}

void
MultiRunnerChild::onLog(std::string const& msg)
{
    if (!printLog_)
        return;

    std::stringstream s;
    if (numJobs_ > 1)
        s << jobIndex_ << "> ";
    s << msg;
    messageQueueSend(MessageType::Log, s.str());
}

}  // namespace test

namespace detail {
template class MultiRunnerBase<true>;
template class MultiRunnerBase<false>;
}  // namespace detail

}  // namespace xrpl
