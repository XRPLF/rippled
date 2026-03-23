#include <xrpl/ledger/helpers/OfferHelpers.h>
//
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/st.h>

namespace xrpl {

TER
offerDelete(ApplyView& view, std::shared_ptr<SLE> const& sle, beast::Journal j)
{
    if (!sle)
        return tesSUCCESS;
    auto offerIndex = sle->key();
    auto owner = sle->getAccountID(sfAccount);

    // Detect legacy directories.
    uint256 uDirectory = sle->getFieldH256(sfBookDirectory);

    if (!view.dirRemove(keylet::ownerDir(owner), sle->getFieldU64(sfOwnerNode), offerIndex, false))
    {
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE
    }

    if (!view.dirRemove(keylet::page(uDirectory), sle->getFieldU64(sfBookNode), offerIndex, false))
    {
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE
    }

    if (sle->isFieldPresent(sfAdditionalBooks))
    {
        XRPL_ASSERT(
            sle->isFlag(lsfHybrid) && sle->isFieldPresent(sfDomainID),
            "xrpl::offerDelete : should be a hybrid domain offer");

        auto const& additionalBookDirs = sle->getFieldArray(sfAdditionalBooks);

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

    adjustOwnerCount(view, view.peek(keylet::account(owner)), -1, j);

    view.erase(sle);

    return tesSUCCESS;
}

}  // namespace xrpl
