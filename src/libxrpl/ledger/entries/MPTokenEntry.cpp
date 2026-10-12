#include <xrpl/ledger/entries/MPTokenEntry.h>

#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/MPTokenHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/UintTypes.h>

#include <cstdint>

namespace xrpl {

template <typename ViewT>
bool
MPTokenEntry<ViewT>::isFrozen(AccountID const& account, std::uint8_t depth) const
{
    auto const& view = this->readView();
    auto const& sle = **this;

    XRPL_ASSERT(sle[sfAccount] == account, "xrpl::MPTokenEntry::isFrozen : valid MPToken holder");

    MPTID const mptID = sle[sfMPTokenIssuanceID];
    auto const issuanceSle = view.read(keylet::mptokenIssuance(mptID));

    if ((issuanceSle && isGlobalFrozen(*issuanceSle)) || sle.isFlag(lsfMPTLocked))
        return true;

    if (issuanceSle)
        return isVaultPseudoAccountFrozen(view, account, *issuanceSle, depth);

    return isVaultPseudoAccountFrozen(view, account, MPTIssue{mptID}, depth);
}

template class MPTokenEntry<ReadView>;
template class MPTokenEntry<ApplyView>;

}  // namespace xrpl
