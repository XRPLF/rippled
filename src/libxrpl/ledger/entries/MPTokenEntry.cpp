#include <xrpl/ledger/entries/MPTokenEntry.h>

#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/ledger/helpers/MPTokenHelpers.h>
#include <xrpl/ledger/helpers/SponsorHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>
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

template <typename ViewT>
TER
MPTokenEntry<ViewT>::create(
    ApplyView& view,
    MPTID const& mptIssuanceID,
    AccountID const& account,
    SLE::Ref sponsorSle,
    std::uint32_t flags)
    requires Base::kIsWritable
{
    MPTokenEntry mptoken(mptIssuanceID, account, view);

    auto const ownerNode =
        view.dirInsert(keylet::ownerDir(account), mptoken.keylet(), describeOwnerDir(account));

    if (!ownerNode)
        return tecDIR_FULL;  // LCOV_EXCL_LINE

    mptoken.newSLE();
    (*mptoken)[sfAccount] = account;
    (*mptoken)[sfMPTokenIssuanceID] = mptIssuanceID;
    (*mptoken)[sfFlags] = flags;
    (*mptoken)[sfOwnerNode] = *ownerNode;

    addSponsorToLedgerEntry(mptoken.mutableRawSle(), sponsorSle);

    mptoken.insert();

    return tesSUCCESS;
}

template <typename ViewT>
bool
MPTokenEntry<ViewT>::hasObligations() const
{
    auto const& sle = **this;

    if (sle.at(sfMPTAmount) != 0 ||
        (this->readView().rules().enabled(fixCleanup3_1_3) &&
         sle[~sfLockedAmount].value_or(0) != 0))
        return true;

    // Don't delete if the token still has confidential balances
    return sle.isFieldPresent(sfConfidentialBalanceInbox) ||
        sle.isFieldPresent(sfConfidentialBalanceSpending) ||
        sle.isFieldPresent(sfIssuerEncryptedBalance) ||
        sle.isFieldPresent(sfAuditorEncryptedBalance);
}

template class MPTokenEntry<ReadView>;
template class MPTokenEntry<ApplyView>;

}  // namespace xrpl
