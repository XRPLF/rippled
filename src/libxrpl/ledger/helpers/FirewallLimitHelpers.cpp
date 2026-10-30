#include <xrpl/ledger/helpers/FirewallLimitHelpers.h>

#include <xrpl/basics/chrono.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>

#include <cstdint>

namespace xrpl {

FirewallLimitPeriod
firewallLimitPeriod(SLE const& limit, NetClock::time_point now)
{
    STAmount const spent = limit.getFieldAmount(sfSpent);
    std::uint64_t const start = limit.getFieldU32(sfStartDate);
    std::uint64_t const period = limit.getFieldU32(sfPeriod);
    std::uint64_t const at = now.time_since_epoch().count();

    if (period == 0 || at < start + period)
        return {.spent = spent, .startDate = static_cast<std::uint32_t>(start)};

    // A new period starts a whole number of periods after the stored one.
    return {
        .spent = STAmount{spent.asset()},
        .startDate = static_cast<std::uint32_t>(start + (period * ((at - start) / period))),
    };
}

}  // namespace xrpl
