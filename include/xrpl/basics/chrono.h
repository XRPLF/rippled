#pragma once

#include <xrpl/basics/contract.h>
#include <xrpl/beast/clock/abstract_clock.h>
#include <xrpl/beast/clock/basic_seconds_clock.h>
#include <xrpl/beast/clock/manual_clock.h>

#include <date/date.h>

#include <chrono>
#include <cstdint>
#include <limits>
#include <ratio>
#include <stdexcept>
#include <string>

namespace xrpl {

/**
 * The clock in which the ledger keeps time.
 *
 * Network time is wall-clock time (UTC) with a resolution of one second
 * and an epoch of 2000-01-01T00:00:00Z, the "XRPL epoch". It is the time
 * base for every timestamp that appears in the ledger or on the wire
 * (close times, expirations, FinishAfter/CancelAfter and so on), with
 * the single exception of the price oracle's LastUpdateTime, which the
 * XLS-47 specification defines in Unix time.
 *
 * The epoch was arbitrarily chosen by Arthur Britto and David Schwartz
 * during the early development of the code. No rationale was recorded
 * for this curious and annoying, but otherwise unimportant, choice. It
 * cannot be changed: it is baked into every serialized timestamp.
 *
 * The representation is an unsigned 32-bit count of seconds, so the
 * clock covers 2000-01-01T00:00:00Z to 2136-02-07T06:28:15Z. Differences
 * of time points are unsigned, so code that subtracts them must know
 * which operand is later.
 *
 * Time points convert to and from std::chrono::system_clock through
 * to_sys() and from_sys(), which are the only way in or out of network
 * time; the epoch is not exposed. They follow the protocol that
 * std::chrono::clock_cast uses, but call sites call them directly:
 * libc++ (and therefore Apple Clang) does not implement clock_cast.
 */
class NetClock
{
public:
    using rep = std::uint32_t;
    using period = std::ratio<1>;
    using duration = std::chrono::duration<rep, period>;
    using time_point = std::chrono::time_point<NetClock>;

    static bool constexpr is_steady = false;  // NOLINT(readability-identifier-naming)

private:
    // The earliest network time that can be represented, as a
    // system_clock time: 2000-01-01T00:00:00Z.
    static constexpr std::chrono::sys_seconds kMinTime = std::chrono::sys_days(
        std::chrono::year_month_day{
            std::chrono::year(2000),
            std::chrono::January,
            std::chrono::day(1)});

    // The last time that network time can be represented, as a
    // system_clock time: 2136-02-07T06:28:15Z.
    static constexpr std::chrono::sys_seconds kMaxTime =
        kMinTime + std::chrono::seconds{std::numeric_limits<rep>::max()};

    static_assert(kMinTime.time_since_epoch().count() == 946684800);
    static_assert(kMaxTime - kMinTime == std::chrono::seconds{std::numeric_limits<rep>::max()});

public:
    explicit NetClock() = default;

    /**
     * Converts a network time point to the equivalent system_clock time
     * point.
     *
     * @param tp The network time point to convert.
     * @return The system_clock time point for the same instant. Every
     *         network time point has one.
     */
    [[nodiscard]] static constexpr std::chrono::sys_seconds
    to_sys(time_point tp) noexcept  // NOLINT(readability-identifier-naming)
    {
        return kMinTime + std::chrono::seconds{tp.time_since_epoch()};
    }

    /**
     * Converts a system_clock time point to the equivalent network time
     * point.
     *
     * @param tp The system_clock time point to convert. Its duration must
     *           be no coarser than a second; a calendar type such as
     *           sys_days is widened by the caller, as
     *           sys_seconds{sys_days{...}}. Sub-second precision is
     *           discarded.
     * @return The network time point for the same instant, floored to
     *         the second.
     * @throws std::domain_error if the floor of @p tp lies outside the
     *         range network time represents (see the class description),
     *         since such a time has no equivalent. During constant
     *         evaluation an out-of-range argument fails to compile instead.
     */
    template <class Duration>
        requires std::ratio_less_equal_v<typename Duration::period, std::ratio<1>>
    [[nodiscard]] static constexpr time_point
    from_sys(std::chrono::sys_time<Duration> tp)  // NOLINT(readability-identifier-naming)
    {
        // Floor first so the range check and subtraction operate on sys_seconds
        // and cannot overflow (Duration is never coarser than a second). This
        // rejects fractions of a second before the epoch and accepts fractions
        // after the last representable second.
        auto const secs = std::chrono::floor<std::chrono::seconds>(tp);
        if (secs < kMinTime || secs > kMaxTime)
            Throw<std::domain_error>("NetClock::from_sys: time point outside network time");
        return time_point{std::chrono::duration_cast<duration>(secs - kMinTime)};
    }
};

/**
 * Renders a system_clock time point for logs: "2026-Mar-07 09:04:05 UTC".
 *
 * Sub-second precision, if the duration has any, is printed in full.
 */
template <class Duration>
std::string
to_string(date::sys_time<Duration> tp)
{
    return date::format("%Y-%b-%d %T %Z", tp);
}

/**
 * Renders a network time point in the same form as the system_clock
 * overload. The time point is widened to system_clock::time_point first,
 * so the sub-second field is printed, as zeros, at whatever resolution
 * system_clock has on the platform: "2026-Mar-07 09:04:05.000000000 UTC"
 * with libstdc++. This has always been the output and server_info exposes
 * it (validator_list.expiration), so it is kept.
 */
inline std::string
to_string(NetClock::time_point tp)
{
    return to_string(std::chrono::system_clock::time_point{NetClock::to_sys(tp)});
}

template <class Duration>
std::string
toStringIso(date::sys_time<Duration> tp)
{
    using namespace std::chrono;
    return date::format("%FT%TZ", tp);
}

/**
 * Renders a network time point as an ISO 8601 UTC timestamp with the "Z"
 * designator: "2026-03-07T09:04:05Z".
 */
inline std::string
toStringIso(NetClock::time_point tp)
{
    return toStringIso(NetClock::to_sys(tp));
}

/**
 * A clock for measuring elapsed time.
 *
 * The epoch is unspecified.
 */
using Stopwatch = beast::AbstractClock<std::chrono::steady_clock>;

/**
 * A manual Stopwatch for unit tests.
 */
using TestStopwatch = beast::ManualClock<std::chrono::steady_clock>;

/**
 * Returns an instance of a wall clock.
 */
inline Stopwatch&
stopwatch()
{
    using Clock = beast::BasicSecondsClock;
    using Facade = Clock::Clock;
    return beast::getAbstractClock<Facade, Clock>();
}

}  // namespace xrpl
