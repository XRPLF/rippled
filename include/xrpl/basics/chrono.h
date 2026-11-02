#pragma once

#include <xrpl/basics/contract.h>
#include <xrpl/beast/clock/abstract_clock.h>
#include <xrpl/beast/clock/basic_seconds_clock.h>
#include <xrpl/beast/clock/manual_clock.h>

#include <date/date.h>

#include <chrono>
#include <cstdint>
#include <ratio>
#include <stdexcept>
#include <string>
#include <utility>

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
 * to_sys() and from_sys(). These follow the protocol that
 * std::chrono::clock_cast uses, but call sites call them directly:
 * libc++ (and therefore Apple Clang) does not implement clock_cast.
 */
class NetClock
{
public:
    explicit NetClock() = default;

    using rep = std::uint32_t;
    using period = std::ratio<1>;
    using duration = std::chrono::duration<rep, period>;
    using time_point = std::chrono::time_point<NetClock>;

    static bool const is_steady = false;  // NOLINT(readability-identifier-naming)

    /**
     * The epoch of network time, 2000-01-01T00:00:00Z, as a
     * std::chrono::system_clock time point.
     *
     * Its time_since_epoch() is the offset between the Unix epoch and
     * the XRPL epoch: 10957 days, or 946684800 seconds.
     */
    static constexpr std::chrono::sys_seconds kNetEpoch{
        std::chrono::sys_days{std::chrono::year{2000} / std::chrono::January / 1}};

    /**
     * Converts a network time point to the equivalent system_clock time
     * point.
     *
     * Total: every network time point has a system_clock equivalent.
     */
    [[nodiscard]] static constexpr std::chrono::sys_seconds
    to_sys(time_point tp) noexcept  // NOLINT(readability-identifier-naming)
    {
        return kNetEpoch + std::chrono::seconds{tp.time_since_epoch()};
    }

    /**
     * Converts a system_clock time point to the equivalent network time
     * point.
     *
     * @param tp The system_clock time point to convert. Any duration is
     *           accepted; sub-second precision is discarded.
     * @return The network time point for the same instant, floored to
     *         the second.
     * @throws std::domain_error if @p tp lies outside the range network
     *         time represents, since such a time has no equivalent.
     */
    template <class Duration>
    [[nodiscard]] static constexpr time_point
    from_sys(std::chrono::sys_time<Duration> tp)  // NOLINT(readability-identifier-naming)
    {
        // Network time is an unsigned 32-bit count of whole seconds since
        // the network's epoch, so the conversion is well-defined only for
        // time points on or after the epoch and no more than 2^32 - 1
        // seconds past it.
        //
        // We explicitly discard sub-second precision by flooring first, so
        // a time point a fraction of a second before the epoch is rejected
        // rather than rounded onto it.
        if (auto const since = std::chrono::floor<std::chrono::seconds>(tp) - kNetEpoch;
            std::in_range<rep>(since.count()))
            return time_point{duration{static_cast<rep>(since.count())}};

        Throw<std::domain_error>("NetClock::from_sys: time point outside network time");
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
