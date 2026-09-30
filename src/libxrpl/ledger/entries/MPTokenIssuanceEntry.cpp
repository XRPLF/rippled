#include <xrpl/ledger/entries/MPTokenIssuanceEntry.h>

#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/MPTokenHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/Rate.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>

#include <cstdint>

namespace xrpl {

template <typename ViewT>
std::int64_t
MPTokenIssuanceEntry<ViewT>::maxAmount() const
{
    return maxMPTAmount(**this);
}

template <typename ViewT>
Rate
MPTokenIssuanceEntry<ViewT>::transferRate() const
{
    // fee is 0-50,000 (0-50%), rate is 1,000,000,000-2,000,000,000
    // For example, if transfer fee is 50% then 10,000 * 50,000 = 500,000
    // which represents 50% of 1,000,000,000
    if ((*this)->isFieldPresent(sfTransferFee))
    {
        auto const fee = (*this)->getFieldU16(sfTransferFee);
        XRPL_ASSERT(fee <= kMaxTransferFee, "xrpl::transferRate : fee is too large");
        return Rate{1'000'000'000u + (10'000 * fee)};
    }

    return kParityRate;
}

template <typename ViewT>
STAmount
MPTokenIssuanceEntry<ViewT>::issuerFundsToSelfIssue() const
{
    MPTIssue const issue{(**this)[sfSequence], (**this)[sfIssuer]};
    auto const available = availableAmount();
    return this->readView().balanceHookSelfIssueMPT(issue, available);
}

template <typename ViewT>
void
MPTokenIssuanceEntry<ViewT>::issuerSelfDebitHook(std::uint64_t amount)
    requires Base::kIsWritable
{
    MPTIssue const issue{(**this)[sfSequence], (**this)[sfIssuer]};
    auto const available = availableAmount();
    this->applyView().issuerSelfDebitHookMPT(issue, amount, available);
}

template <typename ViewT>
bool
MPTokenIssuanceEntry<ViewT>::isVaultPseudoAccountFrozen(
    AccountID const& account,
    std::uint8_t depth) const
{
    auto const& view = this->readView();

    if (!view.rules().enabled(featureSingleAssetVault))
        return false;

    if (depth >= kMaxAssetCheckDepth)
    {
        // LCOV_EXCL_START
        UNREACHABLE(
            "xrpl::View::checkVaultPseudoAccountFrozenPreconditions : reached asset check depth");
        return true;
        // LCOV_EXCL_STOP
    }

    if (!this->exists())
        return false;  // zero MPToken won't block deletion of MPTokenIssuance

    auto const issuer = (*this)->getAccountID(sfIssuer);

    // Post-fixCleanup3_2_0: vault shares carry sfReferenceHolding pointing
    // to the vault pseudo's MPToken or RippleState for the underlying.
    // Read it to derive the underlying asset and recurse, skipping the
    // issuer-account-then-vault chain. Pre-amendment shares (no field)
    // fall back to the chain lookup below.
    if ((*this)->isFieldPresent(sfReferenceHolding))
    {
        auto const sleHolding =
            view.read(keylet::unchecked((*this)->getFieldH256(sfReferenceHolding)));
        if (!sleHolding)
        {
            // LCOV_EXCL_START
            UNREACHABLE("xrpl::isVaultPseudoAccountFrozen : dangling sfReferenceHolding");
            return false;
            // LCOV_EXCL_STOP
        }
        return isAnyFrozen(view, {issuer, account}, assetOfHolding(*this, *sleHolding), depth + 1);
    }

    auto const mptIssuer = view.read(keylet::account(issuer));
    if (mptIssuer == nullptr)
    {
        // LCOV_EXCL_START
        UNREACHABLE("xrpl::isVaultPseudoAccountFrozen : null MPToken issuer");
        return false;
        // LCOV_EXCL_STOP
    }

    if (!mptIssuer->isFieldPresent(sfVaultID))
        return false;  // not a Vault pseudo-account, common case

    auto const vault = view.read(keylet::vault(mptIssuer->getFieldH256(sfVaultID)));
    if (vault == nullptr)
    {  // LCOV_EXCL_START
        UNREACHABLE("xrpl::isVaultPseudoAccountFrozen : null vault");
        return false;
        // LCOV_EXCL_STOP
    }

    return isAnyFrozen(view, {issuer, account}, vault->at(sfAsset), depth + 1);
}

template <typename ViewT>
bool
MPTokenIssuanceEntry<ViewT>::isSoleShareholder(AccountID const& account) const
{
    std::uint64_t const outstanding = (*this)->at(sfOutstandingAmount);
    if (outstanding == 0)
        return false;

    auto const shareMPTID =
        makeMptID((*this)->getFieldU32(sfSequence), (*this)->getAccountID(sfIssuer));
    auto const sleToken = this->readView().read(keylet::mptoken(shareMPTID, account));
    if (!sleToken)
        return false;  // LCOV_EXCL_LINE

    return sleToken->getFieldU64(sfMPTAmount) == outstanding;
}

template class MPTokenIssuanceEntry<ReadView>;
template class MPTokenIssuanceEntry<ApplyView>;

}  // namespace xrpl
