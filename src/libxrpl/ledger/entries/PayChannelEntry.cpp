#include <xrpl/ledger/entries/PayChannelEntry.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/entries/AccountRootEntry.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TER.h>

namespace xrpl {

template <typename ViewT>
TER
PayChannelEntry<ViewT>::removeFromLedger()
    requires Base::kIsWritable
{
    auto& view = this->applyView();
    auto const j = this->journal();

    AccountID const src = this->sleRef()[sfAccount];

    // Remove PayChan from owner directory
    if (!view.dirRemove(keylet::ownerDir(src), this->sleRef()[sfOwnerNode], this->key(), true))
    {
        // LCOV_EXCL_START
        JLOG(j.fatal()) << "Could not remove paychan from src owner directory";
        return tefBAD_LEDGER;
        // LCOV_EXCL_STOP
    }

    // Remove PayChan from recipient's owner directory, if present.
    if (auto const page = this->sleRef()[~sfDestinationNode])
    {
        auto const dst = this->sleRef()[sfDestination];
        if (!view.dirRemove(keylet::ownerDir(dst), *page, this->key(), true))
        {
            // LCOV_EXCL_START
            JLOG(j.fatal()) << "Could not remove paychan from dst owner directory";
            return tefBAD_LEDGER;
            // LCOV_EXCL_STOP
        }
    }

    // Transfer amount back to owner, decrement owner count
    AccountRootEntryW sle(src, view, j);
    if (!sle)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    XRPL_ASSERT(
        this->sleRef()[sfAmount] >= this->sleRef()[sfBalance],
        "xrpl::PayChannelEntry::removeFromLedger : minimum channel amount");
    sle->at(sfBalance) =
        sle->getFieldAmount(sfBalance) + this->sleRef()[sfAmount] - this->sleRef()[sfBalance];
    decreaseOwnerCountForObject(view, sle.mutableRawSle(), this->mutableRawSle(), 1, j);
    sle.updateInView();

    // Remove PayChan from ledger
    this->eraseFromView();
    return tesSUCCESS;
}

template class PayChannelEntry<ReadView>;
template class PayChannelEntry<ApplyView>;

}  // namespace xrpl
