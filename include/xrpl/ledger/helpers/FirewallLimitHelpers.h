#pragma once

#include <xrpl/basics/chrono.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>

#include <cstdint>

namespace xrpl {

/**
 * The shortest period a FirewallLimit may have, in seconds.
 */
inline constexpr std::uint32_t kFirewallLimitMinPeriod = 60;

/**
 * The longest period a FirewallLimit may have, in seconds: 365 days.
 */
inline constexpr std::uint32_t kFirewallLimitMaxPeriod = 31'536'000;

/**
 * A FirewallLimit's Spent and StartDate brought to a given time.
 */
struct FirewallLimitPeriod
{
    STAmount spent;
    std::uint32_t startDate = 0;
};

/**
 * The effective Spent and StartDate of a FirewallLimit at `now`.
 *
 * When the stored period has ended, Spent is zero and StartDate advances by
 * whole periods; otherwise both are the stored values.
 */
[[nodiscard]] FirewallLimitPeriod
firewallLimitPeriod(SLE const& limit, NetClock::time_point now);

}  // namespace xrpl
