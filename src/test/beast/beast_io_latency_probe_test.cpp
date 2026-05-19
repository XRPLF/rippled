#include <xrpl/beast/asio/io_latency_probe.h>
#include <xrpl/beast/test/yield_to.h>
#include <xrpl/beast/unit_test/suite.h>

#include <boost/asio/basic_waitable_timer.hpp>
#include <boost/asio/executor_work_guard.hpp>  // IWYU pragma: keep
#include <boost/asio/io_context.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/system/detail/error_code.hpp>

#include <chrono>
#include <condition_variable>  // IWYU pragma: keep
#include <cstddef>
#include <functional>
#include <mutex>     // IWYU pragma: keep
#include <optional>  // IWYU pragma: keep
#include <stdexcept>
#include <string>
#include <thread>  // IWYU pragma: keep
#include <vector>

using namespace std::chrono_literals;

class io_latency_probe_test : public beast::unit_test::Suite, public beast::test::EnableYieldTo
{
    using MyTimer = boost::asio::basic_waitable_timer<std::chrono::steady_clock>;

#ifdef XRPL_RUNNING_IN_CI
    /**
     * @brief attempt to measure inaccuracy of asio waitable timers
     *
     * This class is needed in some VM/CI environments where
     * timer inaccuracy impacts the io_probe tests below.
     *
     */
    template <class Clock, class MeasureClock = std::chrono::high_resolution_clock>
    struct MeasureAsioTimers
    {
        using duration = typename Clock::duration;
        using rep = typename MeasureClock::duration::rep;

        std::vector<duration> elapsedTimes;

        MeasureAsioTimers(duration interval = 100ms, size_t numSamples = 50)
        {
            using namespace std::chrono;
            boost::asio::io_context ios;
            std::optional<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>>
                work{boost::asio::make_work_guard(ios)};
            std::thread worker{[&] { ios.run(); }};
            boost::asio::basic_waitable_timer<Clock> timer{ios};
            elapsedTimes.reserve(numSamples);
            std::mutex mtx;
            std::unique_lock<std::mutex> mainlock{mtx};
            std::condition_variable cv;
            bool done = false;
            boost::system::error_code waitErr;

            while (--numSamples > 0u)
            {
                auto const start{MeasureClock::now()};
                done = false;
                timer.expires_after(interval);
                timer.async_wait([&](boost::system::error_code const& ec) {
                    if (ec)
                        waitErr = ec;
                    auto const end{MeasureClock::now()};
                    elapsedTimes.emplace_back(end - start);
                    std::scoped_lock const lk{mtx};
                    done = true;
                    cv.notify_one();
                });
                cv.wait(mainlock, [&done] { return done; });
            }
            work.reset();
            worker.join();
            if (waitErr)
                boost::asio::detail::throw_error(waitErr, "wait");
        }

        template <class D>
        auto
        getMean()
        {
            double sum = {0};
            for (auto const& v : elapsedTimes)
            {
                sum += static_cast<double>(std::chrono::duration_cast<D>(v).count());
            }
            return sum / elapsedTimes.size();
        }

        template <class D>
        auto
        getMax()
        {
            return std::chrono::duration_cast<D>(
                       *std::max_element(elapsedTimes.begin(), elapsedTimes.end()))
                .count();
        }

        template <class D>
        auto
        getMin()
        {
            return std::chrono::duration_cast<D>(
                       *std::min_element(elapsedTimes.begin(), elapsedTimes.end()))
                .count();
        }
    };
#endif

    struct TestSampler
    {
        beast::IOLatencyProbe<std::chrono::steady_clock> probe;
        std::vector<std::chrono::steady_clock::duration> durations;

        TestSampler(std::chrono::milliseconds interval, boost::asio::io_context& ios)
            : probe(interval, ios)
        {
        }

        void
        start()
        {
            probe.sample(std::ref(*this));
        }

        void
        startOne()
        {
            probe.sampleOne(std::ref(*this));
        }

        void
        operator()(std::chrono::steady_clock::duration const& elapsed)
        {
            durations.push_back(elapsed);
        }
    };

    void
    testSampleOne(boost::asio::yield_context& yield)
    {
        testcase << "sample one";
        boost::system::error_code ec;
        TestSampler ioProbe{100ms, getIoContext()};
        ioProbe.startOne();
        MyTimer timer{getIoContext(), 1s};
        timer.async_wait(yield[ec]);
        if (!BEAST_EXPECTS(!ec, ec.message()))
            return;
        BEAST_EXPECT(ioProbe.durations.size() == 1);
        ioProbe.probe.cancelAsync();
    }

    void
    testSampleOngoing(boost::asio::yield_context& yield)
    {
        testcase << "sample ongoing";
        boost::system::error_code ec;
        using namespace std::chrono;
        auto interval = 99ms;
        auto probeDuration = 1s;

        size_t const expectedProbeCountMax = (probeDuration / interval);
        // NOLINTNEXTLINE(misc-const-correctness)
        size_t expectedProbeCountMin = expectedProbeCountMax;
#ifdef XRPL_RUNNING_IN_CI
        // adjust min expected based on measurements
        // if running in CI/VM environment
        MeasureAsioTimers<steady_clock> tt{interval};
        log << "measured mean for timers: " << tt.getMean<milliseconds>() << "ms\n";
        log << "measured max for timers: " << tt.getMax<milliseconds>() << "ms\n";
        expectedProbeCountMin =
            static_cast<size_t>(duration_cast<milliseconds>(probeDuration).count()) /
            static_cast<size_t>(tt.getMean<milliseconds>());
#endif
        TestSampler ioProbe{interval, getIoContext()};
        ioProbe.start();
        MyTimer timer{getIoContext(), probeDuration};
        timer.async_wait(yield[ec]);
        if (!BEAST_EXPECTS(!ec, ec.message()))
            return;
        auto probesSeen = ioProbe.durations.size();
        BEAST_EXPECTS(
            probesSeen >= (expectedProbeCountMin - 1) && probesSeen <= (expectedProbeCountMax + 1),
            std::string("probe count is ") + std::to_string(probesSeen));
        ioProbe.probe.cancelAsync();
        // wait again in order to flush the remaining
        // probes from the work queue
        timer.expires_after(1s);
        timer.async_wait(yield[ec]);
    }

    void
    testCanceled(boost::asio::yield_context& yield)
    {
        testcase << "canceled";
        TestSampler ioProbe{100ms, getIoContext()};
        ioProbe.probe.cancelAsync();
        except<std::logic_error>([&ioProbe]() { ioProbe.startOne(); });
        except<std::logic_error>([&ioProbe]() { ioProbe.start(); });
    }

public:
    void
    run() override
    {
        yieldTo([&](boost::asio::yield_context& yield) {
            testSampleOne(yield);
            testSampleOngoing(yield);
            testCanceled(yield);
        });
    }
};

BEAST_DEFINE_TESTSUITE(io_latency_probe, beast, beast);
