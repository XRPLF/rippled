#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/entries/SLEBase.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Rate.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/UintTypes.h>

#include <cstdint>

namespace xrpl {

template <typename ViewT>
class MPTokenIssuanceEntry : public SLEBase<ViewT, ltMPTOKEN_ISSUANCE>
{
public:
    using Base = SLEBase<ViewT, ltMPTOKEN_ISSUANCE>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit MPTokenIssuanceEntry(
        std::uint32_t seq,
        AccountID const& issuer,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::mptokenIssuance(makeMptID(seq, issuer)), view, j)
    {
    }

    explicit MPTokenIssuanceEntry(
        MPTID const& issuanceID,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::mptokenIssuance(issuanceID), view, j)
    {
    }

    explicit MPTokenIssuanceEntry(
        UInt256 const& issuanceKey,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::mptokenIssuance(issuanceKey), view, j)
    {
    }

    /**
     * Returns true if the issuance is locked (lsfMPTLocked).
     *
     * @return true if lsfMPTLocked is set, false otherwise.
     */
    [[nodiscard]] bool
    isGlobalFrozen() const
    {
        return (*this)->isFlag(lsfMPTLocked);
    }

    /**
     * Returns the issuance's MaximumAmount, or the protocol maximum if the
     * field is absent. Never exceeds 2**63-1.
     *
     * @return the maximum amount that may be outstanding for this issuance.
     */
    [[nodiscard]] std::int64_t
    maxAmount() const;

    /**
     * Returns maxAmount() minus OutstandingAmount.
     *
     * OutstandingAmount may overflow, so the result might be negative, but it
     * is always <= |MaximumAmount - OutstandingAmount|.
     *
     * @return the amount of this issuance still available to be issued.
     */
    [[nodiscard]] std::int64_t
    availableAmount() const
    {
        auto const max = maxAmount();
        auto const outstanding = (**this)[sfOutstandingAmount];
        return max - outstanding;
    }

    /**
     * Returns the MPT transfer fee as a Rate, in fractions of 1 billion
     * (a 1% fee is 1,010,000,000). Returns parity if TransferFee is absent.
     *
     * @return the transfer fee rate for this issuance.
     */
    [[nodiscard]] Rate
    transferRate() const;

    /**
     * Returns tesSUCCESS if a holding of this issuance may be added:
     * tecOBJECT_NOT_FOUND if the issuance does not exist, tecNO_AUTH if
     * lsfMPTCanTransfer is not set.
     *
     * @return tesSUCCESS, tecOBJECT_NOT_FOUND, or tecNO_AUTH.
     */
    [[nodiscard]] TER
    canAddHolding() const
    {
        if (!this->exists())
        {
            return tecOBJECT_NOT_FOUND;
        }
        if (!(*this)->isFlag(lsfMPTCanTransfer))
        {
            return tecNO_AUTH;
        }

        return tesSUCCESS;
    }

    /**
     * Returns the funds the issuer can still self-issue through an
     * issuer-owned MPT sell offer: availableAmount(), less amounts already
     * self-sold, as tracked by the view's balanceHookSelfIssueMPT().
     *
     * @return the amount the issuer may still self-issue.
     */
    [[nodiscard]] STAmount
    issuerFundsToSelfIssue() const;

    /**
     * Records @p amount of this MPT sold by the issuer through an
     * issuer-owned sell offer, via the view's issuerSelfDebitHookMPT().
     * See ApplyView::issuerSelfDebitHookMPT().
     *
     * @param amount the amount of this MPT the issuer sold.
     */
    void
    issuerSelfDebitHook(std::uint64_t amount)
        requires Base::kIsWritable;

    /**
     * Returns true if @p account is frozen through the vault behind this
     * issuance: if these are vault shares, checks whether the issuer (the
     * vault pseudo-account) or @p account is frozen for the vault's
     * underlying asset, recursing up to kMaxAssetCheckDepth. Returns false
     * if featureSingleAssetVault is disabled or the issuance does not exist.
     *
     * @param account the account to check for freeze.
     * @param depth Current recursion depth; callers outside the freeze
     *              checks should pass 0.
     * @return true if @p account is frozen through the vault, false otherwise.
     */
    [[nodiscard]] bool
    isVaultPseudoAccountFrozen(AccountID const& account, std::uint8_t depth) const;

    /**
     * Returns true iff @p account holds all of this vault share issuance's
     * outstanding shares, i.e. is the sole remaining shareholder. Returns
     * false if the account holds no shares or fewer than the total
     * outstanding.
     */
    [[nodiscard]] bool
    isSoleShareholder(AccountID const& account) const;
};

using MPTokenIssuanceEntryR = MPTokenIssuanceEntry<ReadView>;
using MPTokenIssuanceEntryW = MPTokenIssuanceEntry<ApplyView>;

}  // namespace xrpl
