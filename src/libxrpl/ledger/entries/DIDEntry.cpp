#include <xrpl/ledger/entries/DIDEntry.h>

#include <xrpl/basics/Log.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TER.h>

namespace xrpl {

template <typename ViewT>
TER
DIDEntry<ViewT>::addToLedger(AccountID const& owner)
    requires Base::kIsWritable
{
    auto& view = this->applyView();
    auto const j = this->journal();

    auto const sleAccount = view.peek(keylet::account(owner));
    if (!sleAccount)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    // Check reserve availability for new object creation
    {
        auto const balance = STAmount((*sleAccount)[sfBalance]).xrp();
        auto const reserve = accountReserve(view, sleAccount, j, {.ownerCountDelta = 1});

        if (balance < reserve)
            return tecINSUFFICIENT_RESERVE;
    }

    // Add ledger object to ledger
    this->insert();

    // Add ledger object to owner's page
    {
        auto page = view.dirInsert(keylet::ownerDir(owner), this->key(), describeOwnerDir(owner));
        if (!page)
            return tecDIR_FULL;  // LCOV_EXCL_LINE
        (**this)[sfOwnerNode] = *page;
    }
    increaseOwnerCount(view, sleAccount, {}, 1, j);
    view.update(sleAccount);

    return tesSUCCESS;
}

template <typename ViewT>
TER
DIDEntry<ViewT>::removeFromLedger(AccountID const& owner)
    requires Base::kIsWritable
{
    auto& view = this->applyView();
    auto const j = this->journal();

    // Remove object from owner directory
    if (!view.dirRemove(keylet::ownerDir(owner), (**this)[sfOwnerNode], this->key(), true))
    {
        // LCOV_EXCL_START
        JLOG(j.fatal()) << "Unable to delete DID from owner.";
        return tefBAD_LEDGER;
        // LCOV_EXCL_STOP
    }

    auto const sleOwner = view.peek(keylet::account(owner));
    if (!sleOwner)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    decreaseOwnerCountForObject(view, sleOwner, this->mutableRawSle(), 1, j);

    // Remove object from ledger
    this->erase();
    return tesSUCCESS;
}

template class DIDEntry<ReadView>;
template class DIDEntry<ApplyView>;

}  // namespace xrpl
