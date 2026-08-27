#pragma once

/**
 * Utilities for state and work that exist only to be recorded.
 *
 *  Each type below holds real state when telemetry is compiled in and is an
 *  empty type with no-op methods when it is not. The member is declared in
 *  both configurations, so a class's member set and public API never differ
 *  between builds -- a difference that has previously made a test mock
 *  abstract. The compiled-out forms are empty types, so such a member costs a
 *  byte of padding rather than nothing. `[[no_unique_address]]` would remove
 *  even that, but MSVC ignores the standard spelling for ABI compatibility, so
 *  it is deliberately not used. What these types buy is work not being done,
 *  not a smaller struct.
 *
 *      kEnabled ---- if constexpr ---- telemetry-only blocks
 *          |
 *          +-- Stopwatch    (a clock read nobody reads when off)
 *          +-- Counter<T>   (an atomic nobody reads when off)
 *          +-- Mirror<T>    (a value kept only to be reported)
 *
 * @note A no-op method does NOT skip evaluation of its arguments.
 *  `counter.add(expensiveCount())` still calls `expensiveCount()` when
 *  telemetry is compiled out. Pass cheap values only; put expensive work
 *  inside `if constexpr (kEnabled)`.
 *
 * @note `if constexpr (kEnabled)` still type-checks its discarded branch in
 *  non-template code, so use it only where the block names no
 *  `opentelemetry::` type. That is the normal case, because SpanGuard exists
 *  to keep those types out of call sites.
 *
 * @note Thread safety: `Counter` is safe to update from any thread.
 *  `Stopwatch` and `Mirror` are not synchronized; guard them the same way
 *  you guard the state they sit beside.
 *
 *  Usage:
 *  @code
 *      // Time a loop without a single #ifdef.
 *      telemetry::Stopwatch const timer;
 *      for (auto const& obj : objects)
 *          lookUp(obj);
 *      recordLookupMetrics(timer.elapsedUs());   // 0 when compiled out
 *  @endcode
 *
 *  @code
 *      // A counter that disappears, along with its storage, when off.
 *      class Acquirer
 *      {
 *          telemetry::Counter<> timeouts_;
 *      public:
 *          void onTimeout() { timeouts_.add(); }
 *      };
 *  @endcode
 *
 *  @code
 *      // Edge case -- an expensive value still needs a block guard,
 *      // because arguments are evaluated even when the method is a no-op.
 *      if constexpr (telemetry::kEnabled)
 *          span.setAttribute(
 *              pathfind_span::attr::sourceAccount, redactAccount(account));
 *  @endcode
 */

#include <chrono>
#include <cstdint>

// Counter names std::atomic only when telemetry is compiled in, so guarding
// the include keeps misc-include-cleaner from seeing an unused one.
#ifdef XRPL_ENABLE_TELEMETRY
#include <atomic>
#endif

namespace xrpl::telemetry {

#ifdef XRPL_ENABLE_TELEMETRY
/**
 * True when telemetry code is compiled into this build.
 */
inline constexpr bool kEnabled = true;
#else
inline constexpr bool kEnabled = false;
#endif

/**
 * A monotonic elapsed-time measurement taken only for telemetry.
 *
 *  Reads the clock on construction when telemetry is compiled in, and does
 *  nothing at all when it is not, so an untraced build performs no clock
 *  read on the measured path.
 */
class Stopwatch
{
#ifdef XRPL_ENABLE_TELEMETRY
    /**
     * When the measurement started.
     */
    std::chrono::steady_clock::time_point start_{std::chrono::steady_clock::now()};
#endif

public:
    // These read start_ when telemetry is compiled in and touch no member
    // when it is not, so clang-tidy asks for them to be static. Making them
    // static would give the two builds different signatures.
    // NOLINTBEGIN(readability-convert-member-functions-to-static)

    /**
     * Begin the measurement again from now.
     */
    void
    restart() noexcept
    {
#ifdef XRPL_ENABLE_TELEMETRY
        start_ = std::chrono::steady_clock::now();
#endif
    }

    /**
     * Microseconds since construction or the last restart(); 0 when off.
     */
    [[nodiscard]] std::chrono::microseconds
    elapsedUs() const noexcept
    {
#ifdef XRPL_ENABLE_TELEMETRY
        return std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start_);
#else
        return std::chrono::microseconds{0};
#endif
    }

    // NOLINTEND(readability-convert-member-functions-to-static)
};

/**
 * A monotonically increasing count kept only to be reported.
 *
 * @tparam T  The counter's value type; defaults to std::uint64_t.
 */
template <class T = std::uint64_t>
class Counter
{
#ifdef XRPL_ENABLE_TELEMETRY
    /**
     * The running count. Relaxed: only ever read for reporting.
     */
    std::atomic<T> value_{0};
#endif

public:
    /**
     * Default-constructed at zero.
     */
    Counter() = default;

    // Copying and moving are deleted so that a class holding a Counter has the
    // same copy semantics in both builds. std::atomic deletes all four
    // implicitly when telemetry is compiled in; without these declarations the
    // compiled-out Counter would be an empty, freely copyable type, and its
    // owner would silently become copyable in that build only.
    Counter(Counter const&) = delete;
    Counter&
    operator=(Counter const&) = delete;
    Counter(Counter&&) = delete;
    Counter&
    operator=(Counter&&) = delete;

    // These read value_ when telemetry is compiled in and touch no member
    // when it is not, so clang-tidy asks for them to be static. Making them
    // static would give the two builds different signatures.
    // NOLINTBEGIN(readability-convert-member-functions-to-static)

    /**
     * Add to the count. A no-op, with no storage, when off.
     *
     * @param n  How much to add; defaults to 1.
     */
    void
    add(T const n = 1) noexcept
    {
#ifdef XRPL_ENABLE_TELEMETRY
        value_.fetch_add(n, std::memory_order_relaxed);
#else
        (void)n;
#endif
    }

    /**
     * The current count.
     *
     * @return The accumulated count, or T{} when telemetry is compiled out.
     */
    [[nodiscard]] T
    load() const noexcept
    {
#ifdef XRPL_ENABLE_TELEMETRY
        return value_.load(std::memory_order_relaxed);
#else
        return T{};
#endif
    }

    // NOLINTEND(readability-convert-member-functions-to-static)
};

/**
 * A value kept only so that a change in it can be reported.
 *
 *  The common shape is compare-then-store: read whether the incoming value
 *  differs from the last one, store it, and report an event only on a
 *  change. `changedTo()` does all three in one call and answers false when
 *  telemetry is compiled out, so the reporting branch is never taken.
 *
 * @tparam T  The mirrored value's type. Must be equality-comparable and
 *  default-constructible.
 *
 * @note Holds a plain T, not an atomic, and is therefore not synchronized.
 *  Guard it exactly the way you guard the state it sits beside. A value that
 *  is read from another thread -- a gauge callback, for instance -- must stay
 *  an atomic of its own; Mirror is not a substitute for one.
 */
template <class T>
class Mirror
{
#ifdef XRPL_ENABLE_TELEMETRY
    /**
     * The last value stored.
     */
    T value_{};
#endif

public:
    // These read value_ when telemetry is compiled in and touch no member
    // when it is not, so clang-tidy asks for them to be static. Making them
    // static would give the two builds different signatures.
    // NOLINTBEGIN(readability-convert-member-functions-to-static)

    /**
     * Store a value. A no-op, with no storage, when off.
     *
     * @param value  The value to remember.
     */
    void
    store(T const& value) noexcept
    {
#ifdef XRPL_ENABLE_TELEMETRY
        value_ = value;
#else
        (void)value;
#endif
    }

    /**
     * Store a value and say whether it differed from the previous one.
     *
     * @param value  The value to compare against the stored one and then store.
     *
     * @return True only when telemetry is compiled in AND the value changed.
     *  False when compiled out, so a caller's reporting branch never runs.
     */
    [[nodiscard]] bool
    changedTo(T const& value) noexcept
    {
#ifdef XRPL_ENABLE_TELEMETRY
        bool const changed = value_ != value;
        value_ = value;
        return changed;
#else
        (void)value;
        return false;
#endif
    }

    /**
     * The last value stored.
     *
     * @return The stored value, or T{} when telemetry is compiled out.
     */
    [[nodiscard]] T
    load() const noexcept
    {
#ifdef XRPL_ENABLE_TELEMETRY
        return value_;
#else
        return T{};
#endif
    }

    // NOLINTEND(readability-convert-member-functions-to-static)
};

}  // namespace xrpl::telemetry
