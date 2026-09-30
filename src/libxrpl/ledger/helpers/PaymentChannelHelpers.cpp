#include <xrpl/ledger/helpers/PaymentChannelHelpers.h>

#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Rules.h>

#include <algorithm>
#include <cstdint>
#include <limits>

namespace xrpl {

uint32_t
saturatingAdd(Rules const& rules, uint32_t const lhs, uint32_t const rhs)
{
    if (rules.enabled(fixCleanup3_2_0))
    {
        static constexpr auto kUint32Max =
            static_cast<uint64_t>(std::numeric_limits<uint32_t>::max());
        uint64_t const saturatedResult = std::min(uint64_t{lhs} + rhs, kUint32Max);
        return static_cast<uint32_t>(saturatedResult);
    }

    return lhs + rhs;
}

}  // namespace xrpl
