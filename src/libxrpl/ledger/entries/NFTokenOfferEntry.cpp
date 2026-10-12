#include <xrpl/ledger/entries/NFTokenOfferEntry.h>

#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>

namespace xrpl {

template <typename ViewT>
bool
NFTokenOfferEntry<ViewT>::removeFromLedger()
    requires Base::kIsWritable
{
    if (!this->exists())
        return false;

    auto const sle = this->rawSle();
    auto const owner = (*sle)[sfOwner];

    if (!this->applyView().dirRemove(
            keylet::ownerDir(owner), (*sle)[sfOwnerNode], sle->key(), false))
        return false;

    auto const nftokenID = (*sle)[sfNFTokenID];

    if (!this->applyView().dirRemove(
            sle->isFlag(lsfSellNFToken) ? keylet::nftSells(nftokenID) : keylet::nftBuys(nftokenID),
            (*sle)[sfNFTokenOfferNode],
            sle->key(),
            false))
        return false;

    decreaseOwnerCount(this->applyView(), owner, {}, 1, this->journal());

    this->erase();
    return true;
}

template class NFTokenOfferEntry<ReadView>;
template class NFTokenOfferEntry<ApplyView>;

}  // namespace xrpl
