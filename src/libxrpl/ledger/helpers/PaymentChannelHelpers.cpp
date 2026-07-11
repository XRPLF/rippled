#include <xrpl/ledger/helpers/PaymentChannelHelpers.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>

namespace xrpl {

TER
closeChannel(
    PayChannelEntry<ApplyView>& slep,
    ApplyView& view,
    uint256 const& key,
    beast::Journal j)
{
    AccountID const src = (*slep)[sfAccount];

    // Transfer any remaining balance back to the owner.
    AccountRootEntry<ApplyView> sle{src, view};
    if (!sle)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    XRPL_ASSERT(
        (*slep)[sfAmount] >= (*slep)[sfBalance], "xrpl::closeChannel : minimum channel amount");
    (*sle)[sfBalance] = (*sle)[sfBalance] + (*slep)[sfAmount] - (*slep)[sfBalance];
    sle.update();

    // Unlink the channel from the owner and destination directories, decrement
    // the owner's OwnerCount (refunding any reserve sponsor), and erase it. See
    // PayChannelEntry::ownerDirs().
    return slep.destroy();
}

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

bool
isChannelExpired(ApplyView const& view, std::optional<uint32_t> timeField)
{
    if (!timeField)
        return false;
    if (view.rules().enabled(fixCleanup3_2_0))
        return after(view.header().parentCloseTime, *timeField);
    return view.header().parentCloseTime.time_since_epoch().count() >= *timeField;
}

}  // namespace xrpl
