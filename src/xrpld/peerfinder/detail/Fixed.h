#pragma once

#include <xrpld/peerfinder/detail/Tuning.h>

namespace xrpl::PeerFinder {

/** Metadata for a Fixed slot. */
class Fixed
{
public:
    explicit Fixed(clock_type& clock) : m_when(clock.now())
    {
    }

    Fixed(Fixed const&) = default;

    /** Returns the time after which we should allow a connection attempt. */
    clock_type::time_point const&
    when() const
    {
        return m_when;
    }

    /** Updates metadata to reflect a failed connection. */
    void
    failure(clock_type::time_point const& now)
    {
        m_failures = std::min(m_failures + 1, Tuning::connectionBackoff.size() - 1);
        m_when = now + std::chrono::minutes(Tuning::connectionBackoff[m_failures]);
    }

    /** Updates metadata to reflect a successful connection. */
    void
    success(clock_type::time_point const& now)
    {
        m_failures = 0;
        m_when = now;
    }

private:
    clock_type::time_point m_when;
    std::size_t m_failures{0};
};

}  // namespace xrpl::PeerFinder
