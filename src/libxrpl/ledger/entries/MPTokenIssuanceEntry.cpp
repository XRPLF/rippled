#include <xrpl/ledger/entries/MPTokenIssuanceEntry.h>

#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/MPTokenHelpers.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/Rate.h>
#include <xrpl/protocol/SField.h>

#include <cstdint>

namespace xrpl {

template <typename ViewT>
std::int64_t
MPTokenIssuanceEntry<ViewT>::maxAmount() const
{
    return maxMPTAmount(**this);
}

template <typename ViewT>
Rate
MPTokenIssuanceEntry<ViewT>::transferRate() const
{
    // fee is 0-50,000 (0-50%), rate is 1,000,000,000-2,000,000,000
    // For example, if transfer fee is 50% then 10,000 * 50,000 = 500,000
    // which represents 50% of 1,000,000,000
    if ((*this)->isFieldPresent(sfTransferFee))
    {
        auto const fee = (*this)->getFieldU16(sfTransferFee);
        XRPL_ASSERT(fee <= kMaxTransferFee, "xrpl::transferRate : fee is too large");
        return Rate{1'000'000'000u + (10'000 * fee)};
    }

    return kParityRate;
}

template class MPTokenIssuanceEntry<ReadView>;
template class MPTokenIssuanceEntry<ApplyView>;

}  // namespace xrpl
