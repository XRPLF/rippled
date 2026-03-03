#include <xrpl/beast/clock/basic_seconds_clock.h>
#include <xrpl/beast/unit_test.h>

namespace beast {

class basic_seconds_clock_test : public unit_test::suite
{
public:
    void
    run() override
    {
        basic_seconds_clock::now();
        pass();
    }
};

BEAST_DEFINE_TESTSUITE(basic_seconds_clock, beast, beast);

}  // namespace beast
