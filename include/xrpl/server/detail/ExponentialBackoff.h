#pragma once

#include <algorithm>
#include <chrono>

namespace xrpl {

/**
 * @brief Exponential backoff delay manager with configurable limits.
 *
 * Manages delay values that double on each increase (capped at maximum)
 * and can be reset to initial value. Used for throttling accept() calls
 * when file descriptor pressure is detected.
 */
class ExponentialBackoff
{
public:
    using duration_type = std::chrono::milliseconds;

    static constexpr duration_type DEFAULT_INITIAL_DELAY{50};
    static constexpr duration_type DEFAULT_MAX_DELAY{2000};

    /**
     * @brief Construct with custom or default delay parameters.
     *
     * @param initial Initial delay value (default: 50ms)
     * @param maximum Maximum delay cap (default: 2000ms)
     */
    explicit ExponentialBackoff(
        duration_type initial = DEFAULT_INITIAL_DELAY,
        duration_type maximum = DEFAULT_MAX_DELAY)
        : initial_(initial), maximum_(maximum), current_(initial)
    {
    }

    /**
     * @brief Get current delay value.
     */
    [[nodiscard]] duration_type
    current() const noexcept
    {
        return current_;
    }

    /**
     * @brief Double the current delay, capped at maximum.
     *
     * @return The new current delay value after increase.
     */
    duration_type
    increase() noexcept
    {
        current_ = std::min(current_ * 2, maximum_);
        return current_;
    }

    /**
     * @brief Reset delay to initial value.
     *
     * @return The initial delay value.
     */
    duration_type
    reset() noexcept
    {
        current_ = initial_;
        return current_;
    }

    /**
     * @brief Get initial delay value.
     */
    [[nodiscard]] duration_type
    initial() const noexcept
    {
        return initial_;
    }

    /**
     * @brief Get maximum delay value.
     */
    [[nodiscard]] duration_type
    maximum() const noexcept
    {
        return maximum_;
    }

private:
    duration_type const initial_;
    duration_type const maximum_;
    duration_type current_;
};

}  // namespace xrpl
