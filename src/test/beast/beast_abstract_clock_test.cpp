// MODULES: ../impl/chrono_io.cpp

#include <xrpl/beast/clock/abstract_clock.h>
#include <xrpl/beast/clock/manual_clock.h>
#include <xrpl/beast/unit_test/suite.h>

#include <chrono>
#include <ostream>
#include <string>
#include <thread>

namespace beast {

class abstract_clock_test : public unit_test::Suite
{
public:
    template <class Clock>
    void
    test(std::string name, AbstractClock<Clock>& c)
    {
        testcase(name);

        auto const t1(c.now());
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        auto const t2(c.now());

        log << "t1= " << t1.time_since_epoch().count() << ", t2= " << t2.time_since_epoch().count()
            << ", elapsed= " << (t2 - t1).count() << std::endl;

        pass();
    }

    void
    testManual()
    {
        testcase("manual");

        using clock_type = ManualClock<std::chrono::steady_clock>;
        clock_type c;

        auto c1 = c.now().time_since_epoch();
        c.set(clock_type::time_point(std::chrono::seconds(1)));
        auto c2 = c.now().time_since_epoch();
        c.set(clock_type::time_point(std::chrono::seconds(2)));
        auto c3 = c.now().time_since_epoch();

        log << "[" << c1.count() << "," << c2.count() << "," << c3.count() << "]" << std::endl;

        pass();
    }

    void
    run() override
    {
        test("steady_clock", getAbstractClock<std::chrono::steady_clock>());
        test("system_clock", getAbstractClock<std::chrono::system_clock>());
        test("high_resolution_clock", getAbstractClock<std::chrono::high_resolution_clock>());

        testManual();
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL(abstract_clock, beast, beast);

}  // namespace beast
