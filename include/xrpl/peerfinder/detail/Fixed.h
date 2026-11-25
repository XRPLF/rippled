#pragma once

#include <xrpl/peerfinder/Types.h>
#include <xrpl/peerfinder/detail/Tuning.h>

#include <algorithm>
#include <chrono>
#include <cstddef>

namespace xrpl::peer_finder {

/**
 * Metadata for a Fixed slot.
 */
class Fixed
{
public:
    explicit Fixed(ClockType& clock) : when_(clock.now())
    {
    }

    Fixed(Fixed const&) = default;

    /**
     * Returns the time after which we should allow a connection attempt.
     */
    [[nodiscard]] ClockType::time_point const&
    when() const
    {
        return when_;
    }

    /**
     * Updates metadata to reflect a failed connection.
     */
    void
    failure(ClockType::time_point const& now)
    {
        failures_ = std::min(failures_ + 1, tuning::kConnectionBackoff.size() - 1);
        when_ = now + std::chrono::minutes(tuning::kConnectionBackoff[failures_]);
    }

    /**
     * Updates metadata to reflect a successful connection.
     */
    void
    success(ClockType::time_point const& now)
    {
        failures_ = 0;
        when_ = now;
    }

private:
    ClockType::time_point when_;
    std::size_t failures_{0};
};

}  // namespace xrpl::peer_finder
