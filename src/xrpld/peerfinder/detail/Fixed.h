#pragma once

#include <xrpld/peerfinder/detail/Tuning.h>

namespace xrpl::PeerFinder {

/** Metadata for a Fixed slot. */
class Fixed
{
public:
    explicit Fixed(clock_type& clock) : when_(clock.now())
    {
    }

    Fixed(Fixed const&) = default;

    /** Returns the time after which we should allow a connection attempt. */
    [[nodiscard]] clock_type::time_point const&
    when() const
    {
        return when_;
    }

    /** Updates metadata to reflect a failed connection. */
    void
    failure(clock_type::time_point const& now)
    {
        failures_ = std::min(failures_ + 1, Tuning::kConnectionBackoff.size() - 1);
        when_ = now + std::chrono::minutes(Tuning::kConnectionBackoff[failures_]);
    }

    /** Updates metadata to reflect a successful connection. */
    void
    success(clock_type::time_point const& now)
    {
        failures_ = 0;
        when_ = now;
    }

private:
    clock_type::time_point when_;
    std::size_t failures_{0};
};

}  // namespace xrpl::PeerFinder
