#include <xrpl/ledger/entries/DepositPreauthEntry.h>

#include <xrpl/basics/Log.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TER.h>

#include <cstdint>

namespace xrpl {

template <typename ViewT>
TER
DepositPreauthEntry<ViewT>::removeFromLedger(AccountID const& owner)
    requires Base::kIsWritable
{
    // Existence already checked in preclaim and AccountDelete
    if (!this->exists())
    {
        JLOG(this->journal().warn()) << "Selected DepositPreauth does not exist.";
        return tecNO_ENTRY;
    }

    std::uint64_t const page{(**this)[sfOwnerNode]};
    if (!this->applyView().dirRemove(keylet::ownerDir(owner), page, this->key(), false))
    {
        // LCOV_EXCL_START
        JLOG(this->journal().fatal()) << "Unable to delete DepositPreauth from owner.";
        return tefBAD_LEDGER;
        // LCOV_EXCL_STOP
    }

    // If we succeeded, update the DepositPreauth owner's reserve.
    auto const sleOwner = this->applyView().peek(keylet::account(owner));
    if (!sleOwner)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    decreaseOwnerCountForObject(
        this->applyView(), sleOwner, this->mutableRawSle(), 1, this->journal());
    // Remove DepositPreauth from ledger.
    this->erase();

    return tesSUCCESS;
}

template class DepositPreauthEntry<ReadView>;
template class DepositPreauthEntry<ApplyView>;

}  // namespace xrpl
