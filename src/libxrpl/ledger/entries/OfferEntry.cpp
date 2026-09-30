#include <xrpl/ledger/entries/OfferEntry.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>  // IWYU pragma: keep
#include <xrpl/protocol/TER.h>

namespace xrpl {

template <typename ViewT>
TER
OfferEntry<ViewT>::removeFromLedger()
    requires Base::kIsWritable
{
    if (!this->exists())
        return tesSUCCESS;

    auto const offerIndex = this->key();
    auto const owner = (*this)->getAccountID(sfAccount);

    // Detect legacy directories.
    uint256 const uDirectory = (*this)->getFieldH256(sfBookDirectory);

    ApplyView& view = this->applyView();

    if (!view.dirRemove(
            keylet::ownerDir(owner), (*this)->getFieldU64(sfOwnerNode), offerIndex, false))
    {
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE
    }

    if (!view.dirRemove(
            keylet::page(uDirectory), (*this)->getFieldU64(sfBookNode), offerIndex, false))
    {
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE
    }

    if ((*this)->isFieldPresent(sfAdditionalBooks))
    {
        XRPL_ASSERT(
            (*this)->isFlag(lsfHybrid) && (*this)->isFieldPresent(sfDomainID),
            "xrpl::OfferEntry::removeFromLedger : should be a hybrid domain offer");

        auto const& additionalBookDirs = (*this)->getFieldArray(sfAdditionalBooks);

        for (auto const& bookDir : additionalBookDirs)
        {
            auto const& dirIndex = bookDir.getFieldH256(sfBookDirectory);
            auto const& dirNode = bookDir.getFieldU64(sfBookNode);

            if (!view.dirRemove(keylet::page(dirIndex), dirNode, offerIndex, false))
            {
                return tefBAD_LEDGER;  // LCOV_EXCL_LINE
            }
        }
    }

    decreaseOwnerCountForObject(view, owner, this->mutableRawSle(), 1, this->journal());

    this->erase();

    return tesSUCCESS;
}

template class OfferEntry<ReadView>;
template class OfferEntry<ApplyView>;

}  // namespace xrpl
