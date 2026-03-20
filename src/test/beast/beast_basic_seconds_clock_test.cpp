#include <xrpl/beast/clock/basic_seconds_clock.h>
#include <xrpl/beast/unit_test.h>

namespace beast {

class BasicSecondsClock_test : public unit_test::suite
{
public:
    void
    run() override
    {
        BasicSecondsClock::now();
        pass();
    }
};

BEAST_DEFINE_TESTSUITE(BasicSecondsClock, beast, beast);

}  // namespace beast
