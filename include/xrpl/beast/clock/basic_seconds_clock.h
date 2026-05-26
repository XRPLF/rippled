#pragma once

#include <chrono>

namespace beast {

/** A clock whose minimum resolution is one second.

    The purpose of this class is to optimize the performance of the now()
    member function call. It uses a dedicated thread that wakes up at least
    once per second to sample the requested trivial clock.

    @tparam Clock A type meeting these requirements:
        http://en.cppreference.com/w/cpp/concept/Clock
*/
class BasicSecondsClock
{
public:
    using Clock = std::chrono::steady_clock;

    explicit BasicSecondsClock() = default;

    using rep = typename Clock::rep;
    using period = typename Clock::period;
    using duration = typename Clock::duration;
    using time_point = typename Clock::time_point;

    static bool const is_steady =  // NOLINT(readability-identifier-naming)
        Clock::is_steady;

    static time_point
    now();
};

}  // namespace beast
