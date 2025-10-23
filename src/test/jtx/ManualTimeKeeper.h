#ifndef XRPL_TEST_MANUALTIMEKEEPER_H_INCLUDED
#define XRPL_TEST_MANUALTIMEKEEPER_H_INCLUDED

#include <xrpld/core/TimeKeeper.h>

#include <atomic>

namespace ripple {
namespace test {

class ManualTimeKeeper : public TimeKeeper
{
private:
    std::atomic<time_point> now_{};

public:
    ManualTimeKeeper() = default;

    time_point
    now() const override
    {
        return now_.load();
    }

    void
    set(time_point now)
    {
        now_.store(now);
    }
};

}  // namespace test
}  // namespace ripple

#endif
