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

    auto const owner = this->sleRef()[sfOwner];

    if (!this->applyView().dirRemove(
            keylet::ownerDir(owner), this->sleRef()[sfOwnerNode], this->key(), false))
        return false;

    auto const nftokenID = this->sleRef()[sfNFTokenID];

    if (!this->applyView().dirRemove(
            this->sleRef().isFlag(lsfSellNFToken) ? keylet::nftSells(nftokenID)
                                                  : keylet::nftBuys(nftokenID),
            this->sleRef()[sfNFTokenOfferNode],
            this->key(),
            false))
        return false;

    decreaseOwnerCount(this->applyView(), owner, {}, 1, this->journal());

    this->eraseFromView();
    return true;
}

template class NFTokenOfferEntry<ReadView>;
template class NFTokenOfferEntry<ApplyView>;

}  // namespace xrpl
