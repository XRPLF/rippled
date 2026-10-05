#include <xrpl/ledger/entries/RippleStateEntry.h>

#include <xrpl/basics/Log.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/SponsorHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TER.h>

#include <cstdint>

namespace xrpl {

template <typename ViewT>
TER
RippleStateEntry<ViewT>::removeFromLedger(AccountID const& lowAccount, AccountID const& highAccount)
    requires Base::kIsWritable
{
    auto& view = this->applyView();
    auto const j = this->journal();

    // Detect legacy dirs.
    std::uint64_t const uLowNode = (*this)->getFieldU64(sfLowNode);
    std::uint64_t const uHighNode = (*this)->getFieldU64(sfHighNode);

    JLOG(j.trace()) << "trustDelete: Deleting ripple line: low";

    if (!view.dirRemove(keylet::ownerDir(lowAccount), uLowNode, (*this)->key(), false))
    {
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE
    }

    JLOG(j.trace()) << "trustDelete: Deleting ripple line: high";

    if (!view.dirRemove(keylet::ownerDir(highAccount), uHighNode, (*this)->key(), false))
    {
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE
    }

    removeSponsorFromLedgerEntry(this->mutableRawSle(), sfHighSponsor);
    removeSponsorFromLedgerEntry(this->mutableRawSle(), sfLowSponsor);

    JLOG(j.trace()) << "trustDelete: Deleting ripple line: state";
    this->erase();

    return tesSUCCESS;
}

template class RippleStateEntry<ReadView>;
template class RippleStateEntry<ApplyView>;

}  // namespace xrpl
