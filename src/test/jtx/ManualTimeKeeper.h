#pragma once

#include <xrpld/core/TimeKeeper.h>

#include <atomic>

namespace xrpl::test {

class ManualTimeKeeper : public TimeKeeper
{
private:
    std::atomic<time_point> now_;

public:
    ManualTimeKeeper() = default;

    [[nodiscard]] time_point
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

}  // namespace xrpl::test
