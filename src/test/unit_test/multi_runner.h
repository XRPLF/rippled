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

struct CaseResults
{
    std::string name;
    std::size_t total = 0;
    std::size_t failed = 0;

    explicit CaseResults(std::string name = "") : name(std::move(name))
    {
    }
};

struct SuiteResults
{
    std::string name;
    std::size_t cases = 0;
    std::size_t total = 0;
    std::size_t failed = 0;
    typename clock_type::time_point start = clock_type::now();

    explicit SuiteResults(std::string name = "") : name(std::move(name))
    {
    }

    void
    add(CaseResults const& r);
};

struct Results
{
    using static_string = boost::beast::static_string<256>;
    // results may be stored in shared memory. Use `static_string` to ensure
    // pointers from different memory spaces do not co-mingle
    using run_time = std::pair<static_string, typename clock_type::duration>;

    static constexpr auto kMaxTop = 10;

    std::size_t suites = 0;
    std::size_t cases = 0;
    std::size_t total = 0;
    std::size_t failed = 0;
    boost::container::static_vector<run_time, kMaxTop> top;
    typename clock_type::time_point start = clock_type::now();

    void
    add(SuiteResults const& r);

    void
    merge(Results const& r);

    template <class S>
    void
    print(S& s);
};

template <bool IsParent>
class MultiRunnerBase
{
    // `inner` will be created in shared memory. This is one way
    // multi_runner_parent and multi_runner_child object communicate. The other
    // way they communicate is through message queues.
    struct Inner
    {
        std::atomic<std::size_t> jobIndex{0};
        std::atomic<std::size_t> testIndex{0};
        std::atomic<bool> anyFailedFlag{false};
        // A parent process will periodically increment `keepAlive`. The child
        // processes will check if `keepAlive` is being incremented. If it is
        // not incremented for a sufficiently long time, the child will assume
        // the parent process has died.
        std::atomic<std::size_t> keepAlive{0};

        mutable boost::interprocess::interprocess_mutex m;
        detail::Results results;

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
        add(Results const& r);

        template <class S>
        void
        printResults(S& s);
    };

    static constexpr char const* kSharedMemName = "XrpldUnitTestSharedMem";
    // name of the message queue a multi_runner_child will use to communicate
    // with multi_runner_parent
    static constexpr char const* kMessageQueueName = "XrpldUnitTestMessageQueue";

    // `inner_` will be created in shared memory
    Inner* inner_;
    // shared memory to use for the `inner` member
    boost::interprocess::shared_memory_object sharedMem_;
    boost::interprocess::mapped_region region_;

protected:
    std::unique_ptr<boost::interprocess::message_queue> messageQueue_;

    enum class MessageType : std::uint8_t { TestStart, TestEnd, Log };
    void
    messageQueueSend(MessageType mt, std::string const& s);

public:
    MultiRunnerBase();
    ~MultiRunnerBase();

    std::size_t
    checkoutTestIndex();

    std::size_t
    checkoutJobIndex();

    void
    anyFailed(bool v);

    void
    add(Results const& r);

    void
    incKeepAliveCount();

    std::size_t
    getKeepAliveCount();

    template <class S>
    void
    printResults(S& s);

    [[nodiscard]] bool
    anyFailed() const;

    [[nodiscard]] std::size_t
    tests() const;

    [[nodiscard]] std::size_t
    suites() const;

    void
    addFailures(std::size_t failures);
};

}  // namespace detail

namespace test {

//------------------------------------------------------------------------------

/** Manager for children running unit tests
 */
class MultiRunnerParent : private detail::MultiRunnerBase</*IsParent*/ true>
{
private:
    // message_queue_ is used to collect log messages from the children
    std::ostream& os_;
    std::atomic<bool> continueMessageQueue_{true};
    std::thread messageQueueThread_;
    // track running suites so if a child crashes the culprit can be flagged
    std::set<std::string> runningSuites_;

public:
    MultiRunnerParent(MultiRunnerParent const&) = delete;
    MultiRunnerParent&
    operator=(MultiRunnerParent const&) = delete;

    MultiRunnerParent();
    ~MultiRunnerParent();

    [[nodiscard]] bool
    anyFailed() const;

    [[nodiscard]] std::size_t
    tests() const;

    [[nodiscard]] std::size_t
    suites() const;

    void
    addFailures(std::size_t failures);
};

//------------------------------------------------------------------------------

/** A class to run a subset of unit tests
 */
class MultiRunnerChild : public beast::unit_test::Runner,
                         private detail::MultiRunnerBase</*IsParent*/ false>
{
private:
    std::size_t jobIndex_;
    detail::Results results_;
    detail::SuiteResults suiteResults_;
    detail::CaseResults caseResults_;
    std::size_t numJobs_{0};
    bool quiet_{false};
    bool printLog_{true};

    std::atomic<bool> continueKeepAlive_{true};
    std::thread keepAliveThread_;

public:
    MultiRunnerChild(MultiRunnerChild const&) = delete;
    MultiRunnerChild&
    operator=(MultiRunnerChild const&) = delete;

    MultiRunnerChild(std::size_t numJobs, bool quiet, bool printLog);
    ~MultiRunnerChild() override;

    [[nodiscard]] std::size_t
    tests() const;

    [[nodiscard]] std::size_t
    suites() const;

    void
    addFailures(std::size_t failures);

    template <class Pred>
    bool
    runMulti(Pred pred);

private:
    void
    onSuiteBegin(beast::unit_test::SuiteInfo const& info) override;

    void
    onSuiteEnd() override;

    void
    onCaseBegin(std::string const& name) override;

    void
    onCaseEnd() override;

    void
    onPass() override;

    void
    onFail(std::string const& reason) override;

    void
    onLog(std::string const& s) override;
};

//------------------------------------------------------------------------------

template <class Pred>
bool
MultiRunnerChild::runMulti(Pred pred)
{
    auto const& suite = beast::unit_test::globalSuites();
    auto const numTests = suite.size();
    bool failed = false;

    auto getTest = [&]() -> beast::unit_test::SuiteInfo const* {
        auto const curTestIndex = checkoutTestIndex();
        if (curTestIndex >= numTests)
            return nullptr;
        auto iter = suite.begin();
        std::advance(iter, curTestIndex);
        return &*iter;
    };
    while (auto t = getTest())
    {
        if (!pred(*t))
            continue;
        try
        {
            failed = run(*t) || failed;
        }
        catch (...)
        {
            if (numJobs_ <= 1)
                throw;  // a single process can die

            // inform the parent
            std::stringstream s;
            s << jobIndex_ << ">  failed Unhandled exception in test.\n";
            messageQueueSend(MessageType::Log, s.str());
            failed = true;
        }
    }
    anyFailed(failed);
    return failed;
}

}  // namespace test
}  // namespace xrpl
