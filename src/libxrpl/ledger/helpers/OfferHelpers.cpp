#include <xrpl/ledger/helpers/OfferHelpers.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/protocol/Book.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STArray.h>  // IWYU pragma: keep
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>

#include <memory>
#include <optional>

namespace xrpl {

namespace {

// Reconstruct the Book this offer was placed on. The primary directory uses
// the offer's sfDomainID (if any); the open-book directory of a hybrid offer
// is the same in/out assets with no domain.
Book
primaryBookFromOffer(SLE const& sle)
{
    auto const takerPays = sle.getFieldAmount(sfTakerPays);
    auto const takerGets = sle.getFieldAmount(sfTakerGets);
    std::optional<uint256> domain;
    if (sle.isFieldPresent(sfDomainID))
        domain = sle.getFieldH256(sfDomainID);
    return Book{takerPays.asset(), takerGets.asset(), domain};
}

Book
openBookFromOffer(SLE const& sle)
{
    auto const takerPays = sle.getFieldAmount(sfTakerPays);
    auto const takerGets = sle.getFieldAmount(sfTakerGets);
    return Book{takerPays.asset(), takerGets.asset(), std::nullopt};
}

}  // namespace

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

    // Plan 8: notify the top-of-book cache that the primary book lost an
    // offer at `uDirectory`. If this was the cached top page the cache
    // invalidates; otherwise no-op. uDirectory is the first-page keylet of
    // the offer's quality bucket — i.e. exactly what the cache stores.
    view.notifyOfferDeleted(primaryBookFromOffer(*sle), uDirectory, offerIndex);

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

            // Hybrid offers also live on the open (no-domain) book — notify
            // that cache too.
            view.notifyOfferDeleted(openBookFromOffer(*sle), dirIndex, offerIndex);
        }
    }

    adjustOwnerCount(view, view.peek(keylet::account(owner)), -1, j);

    view.erase(sle);

    return tesSUCCESS;
}

}  // namespace xrpl
