#include <xrpl/beast/asio/io_latency_probe.h>
#include <xrpl/beast/test/yield_to.h>
#include <xrpl/beast/unit_test.h>

#include <boost/asio/basic_waitable_timer.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>

#include <algorithm>
#include <mutex>
#include <numeric>
#include <optional>
#include <vector>

using namespace std::chrono_literals;

class io_latency_probe_test : public beast::unit_test::suite,
                              public beast::test::enable_yield_to
{
    using MyTimer =
        boost::asio::basic_waitable_timer<std::chrono::steady_clock>;

#ifdef XRPL_RUNNING_IN_CI
    /**
     * @brief attempt to measure inaccuracy of asio waitable timers
     *
     * This class is needed in some VM/CI environments where
     * timer inaccuracy impacts the io_probe tests below.
     *
     */
    template <
        class Clock,
        class MeasureClock = std::chrono::high_resolution_clock>
    struct measure_asio_timers
    {
        using duration = typename Clock::duration;
        using rep = typename MeasureClock::duration::rep;

        std::vector<duration> elapsed_times_;

        measure_asio_timers(duration interval = 100ms, size_t num_samples = 50)
        {
            using namespace std::chrono;
            boost::asio::io_context ios;
            std::optional<boost::asio::executor_work_guard<
                boost::asio::io_context::executor_type>>
                work{boost::asio::make_work_guard(ios)};
            std::thread worker{[&] { ios.run(); }};
            boost::asio::basic_waitable_timer<Clock> timer{ios};
            elapsed_times_.reserve(num_samples);
            std::mutex mtx;
            std::unique_lock<std::mutex> mainlock{mtx};
            std::condition_variable cv;
            bool done = false;
            boost::system::error_code wait_err;

            while (--num_samples)
            {
                auto const start{MeasureClock::now()};
                done = false;
                timer.expires_after(interval);
                timer.async_wait([&](boost::system::error_code const& ec) {
                    if (ec)
                        wait_err = ec;
                    auto const end{MeasureClock::now()};
                    elapsed_times_.emplace_back(end - start);
                    std::lock_guard lk{mtx};
                    done = true;
                    cv.notify_one();
                });
                cv.wait(mainlock, [&done] { return done; });
            }
            work.reset();
            worker.join();
            if (wait_err)
                boost::asio::detail::throw_error(wait_err, "wait");
        }

        template <class D>
        auto
        getMean()
        {
            double sum = {0};
            for (auto const& v : elapsed_times_)
            {
                sum += static_cast<double>(
                    std::chrono::duration_cast<D>(v).count());
            }
            return sum / elapsed_times_.size();
        }

        template <class D>
        auto
        getMax()
        {
            return std::chrono::duration_cast<D>(
                       *std::max_element(
                           elapsed_times_.begin(), elapsed_times_.end()))
                .count();
        }

        template <class D>
        auto
        getMin()
        {
            return std::chrono::duration_cast<D>(
                       *std::min_element(
                           elapsed_times_.begin(), elapsed_times_.end()))
                .count();
        }
    };
#endif

    struct test_sampler
    {
        beast::io_latency_probe<std::chrono::steady_clock> probe_;
        std::vector<std::chrono::steady_clock::duration> durations_;

        test_sampler(
            std::chrono::milliseconds interval,
            boost::asio::io_context& ios)
            : probe_(interval, ios)
        {
        }

        void
        start()
        {
            probe_.sample(std::ref(*this));
        }

        void
        start_one()
        {
            probe_.sample_one(std::ref(*this));
        }

        void
        operator()(std::chrono::steady_clock::duration const& elapsed)
        {
            durations_.push_back(elapsed);
        }
    };

    void
    testSampleOne(boost::asio::yield_context& yield)
    {
        testcase << "sample one";
        boost::system::error_code ec;
        test_sampler io_probe{100ms, get_io_context()};
        io_probe.start_one();
        MyTimer timer{get_io_context(), 1s};
        timer.async_wait(yield[ec]);
        if (!BEAST_EXPECTS(!ec, ec.message()))
            return;
        BEAST_EXPECT(io_probe.durations_.size() == 1);
        io_probe.probe_.cancel_async();
    }

    void
    testSampleOngoing(boost::asio::yield_context& yield)
    {
        testcase << "sample ongoing";
        boost::system::error_code ec;
        using namespace std::chrono;
        auto interval = 99ms;
        auto probe_duration = 1s;

        size_t expected_probe_count_max = (probe_duration / interval);
        size_t expected_probe_count_min = expected_probe_count_max;
#ifdef XRPL_RUNNING_IN_CI
        // adjust min expected based on measurements
        // if running in CI/VM environment
        measure_asio_timers<steady_clock> tt{interval};
        log << "measured mean for timers: " << tt.getMean<milliseconds>()
            << "ms\n";
        log << "measured max for timers: " << tt.getMax<milliseconds>()
            << "ms\n";
        expected_probe_count_min =
            static_cast<size_t>(
                duration_cast<milliseconds>(probe_duration).count()) /
            static_cast<size_t>(tt.getMean<milliseconds>());
#endif
        test_sampler io_probe{interval, get_io_context()};
        io_probe.start();
        MyTimer timer{get_io_context(), probe_duration};
        timer.async_wait(yield[ec]);
        if (!BEAST_EXPECTS(!ec, ec.message()))
            return;
        auto probes_seen = io_probe.durations_.size();
        BEAST_EXPECTS(
            probes_seen >= (expected_probe_count_min - 1) &&
                probes_seen <= (expected_probe_count_max + 1),
            std::string("probe count is ") + std::to_string(probes_seen));
        io_probe.probe_.cancel_async();
        // wait again in order to flush the remaining
        // probes from the work queue
        timer.expires_after(1s);
        timer.async_wait(yield[ec]);
    }

    void
    testCanceled(boost::asio::yield_context& yield)
    {
        testcase << "canceled";
        test_sampler io_probe{100ms, get_io_context()};
        io_probe.probe_.cancel_async();
        except<std::logic_error>([&io_probe]() { io_probe.start_one(); });
        except<std::logic_error>([&io_probe]() { io_probe.start(); });
    }

public:
    void
    run() override
    {
        yield_to([&](boost::asio::yield_context& yield) {
            testSampleOne(yield);
            testSampleOngoing(yield);
            testCanceled(yield);
        });
    }
};

BEAST_DEFINE_TESTSUITE(io_latency_probe, beast, beast);
