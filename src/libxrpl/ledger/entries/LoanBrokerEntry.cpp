#include <xrpl/ledger/entries/LoanBrokerEntry.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/TER.h>

#include <cstdint>
#include <string_view>

namespace xrpl {

template <typename ViewT>
TER
LoanBrokerEntry<ViewT>::canApplyToCover(
    Asset const& vaultAsset,
    STAmount const& amount,
    std::string_view logPrefix) const
{
    XRPL_ASSERT(this->exists(), "xrpl::LoanBrokerEntry::canApplyToCover : valid LoanBroker sle");
    XRPL_ASSERT(
        vaultAsset == amount.asset(), "xrpl::LoanBrokerEntry::canApplyToCover : valid asset");

    if (!this->readView().rules().enabled(fixCleanup3_2_0))
        return tesSUCCESS;

    if (amount == beast::kZero)
        return tecPRECISION_LOSS;

    int const coverScale = scale((*this)->at(sfCoverAvailable), vaultAsset);
    if (amount.isZeroAtScale(coverScale))
    {
        JLOG(this->journal().warn()) << logPrefix << ": amount " << amount.getFullText()
                                     << " rounds to zero at cover scale " << coverScale;
        return tecPRECISION_LOSS;
    }

    return tesSUCCESS;
}

template <typename ViewT>
void
LoanBrokerEntry<ViewT>::adjustOwnerCount(std::int32_t delta)
    requires Base::kIsWritable
{
    XRPL_ASSERT(this->exists(), "xrpl::LoanBrokerEntry::adjustOwnerCount : valid loan broker sle");
    if (!this->exists())
        return;  // LCOV_EXCL_LINE

    XRPL_ASSERT(delta != 0, "xrpl::LoanBrokerEntry::adjustOwnerCount : nonzero delta input");
    if (delta == 0)
        return;  // LCOV_EXCL_LINE

    detail::adjustOwnerCountImpl(
        this->applyView(),
        this->mutableRawSle(),
        sfOwnerCount,
        (*this)->getAccountID(sfAccount),
        delta,
        this->journal());
}

template class LoanBrokerEntry<ReadView>;
template class LoanBrokerEntry<ApplyView>;

}  // namespace xrpl
