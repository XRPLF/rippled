/** @file
 *  Implements `offerDelete`, the single shared helper for atomically removing
 *  an offer from an `ApplyView`.
 *
 *  Offer deletion is a multi-step sequence: (1) remove the offer key from the
 *  owner's personal directory, (2) remove it from the order-book quality
 *  directory, (3) for hybrid Permissioned DEX offers, remove it from each
 *  additional domain-specific book directory recorded in `sfAdditionalBooks`,
 *  (4) decrement the owner's reserve count via `adjustOwnerCount`, and
 *  (5) erase the SLE.  All steps execute within the transaction-scoped
 *  `ApplyView` buffer and are rolled back if the enclosing transaction fails.
 */
#include <xrpl/ledger/helpers/OfferHelpers.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>  // IWYU pragma: keep
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>

#include <memory>

namespace xrpl {

TER
offerDelete(ApplyView& view, std::shared_ptr<SLE> const& sle, beast::Journal j)
{
    if (!sle)
        return tesSUCCESS;
    auto offerIndex = sle->key();
    auto owner = sle->getAccountID(sfAccount);

    // Detect legacy directories.
    uint256 const uDirectory = sle->getFieldH256(sfBookDirectory);

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
