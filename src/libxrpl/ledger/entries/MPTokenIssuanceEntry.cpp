#include <xrpl/ledger/entries/MPTokenIssuanceEntry.h>

#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/MPTokenHelpers.h>

#include <cstdint>

namespace xrpl {

template <typename ViewT>
std::int64_t
MPTokenIssuanceEntry<ViewT>::maxAmount() const
{
    return maxMPTAmount(**this);
}

template class MPTokenIssuanceEntry<ReadView>;
template class MPTokenIssuanceEntry<ApplyView>;

}  // namespace xrpl
