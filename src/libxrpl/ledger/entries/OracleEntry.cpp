#include <xrpl/ledger/entries/OracleEntry.h>

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
OracleEntry<ViewT>::removeFromLedger(AccountID const& owner)
    requires Base::kIsWritable
{
    if (!this->exists())
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto& view = this->applyView();
    auto const& sle = this->mutableRawSle();

    if (!view.dirRemove(keylet::ownerDir(owner), (*sle)[sfOwnerNode], sle->key(), true))
    {
        // LCOV_EXCL_START
        JLOG(this->journal().fatal()) << "Unable to delete Oracle from owner.";
        return tefBAD_LEDGER;
        // LCOV_EXCL_STOP
    }

    auto const sleOwner = view.peek(keylet::account(owner));
    if (!sleOwner)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    std::uint32_t const count = reserveCount();
    decreaseOwnerCountForObject(view, sleOwner, sle, count, this->journal());
    this->erase();

    return tesSUCCESS;
}

template class OracleEntry<ReadView>;
template class OracleEntry<ApplyView>;

}  // namespace xrpl
