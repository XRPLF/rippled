#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/entries/SLEBase.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/UintTypes.h>

#include <cstdint>

namespace xrpl {

template <typename ViewT>
class MPTokenEntry : public SLEBase<ViewT, ltMPTOKEN>
{
public:
    using Base = SLEBase<ViewT, ltMPTOKEN>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit MPTokenEntry(
        MPTID const& issuanceID,
        AccountID const& holder,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::mptoken(issuanceID, holder), view, j)
    {
    }

    explicit MPTokenEntry(
        UInt256 const& issuanceKey,
        AccountID const& holder,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::mptoken(issuanceKey, holder), view, j)
    {
    }

    explicit MPTokenEntry(
        UInt256 const& mptokenKey,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::mptoken(mptokenKey), view, j)
    {
    }

    /**
     * Returns true if this MPToken carries the individual-lock flag
     * (lsfMPTLocked).
     *
     * @warning This checks only the raw per-holder lock bit. It does not
     * perform the transitive vault pseudo-account check. Use isFrozen() to
     * decide whether the holder may send or receive tokens.
     */
    [[nodiscard]] bool
    isIndividualFrozen() const
    {
        return (*this)->isFlag(lsfMPTLocked);
    }

    /**
     * Returns true if the issuer authorized this MPToken (lsfMPTAuthorized).
     *
     * This is only the MPToken-side part of requireAuth(). It does not check
     * whether the issuance requires authorization, the issuance's domain, or
     * the pseudo-account and vault-share rules.
     */
    [[nodiscard]] bool
    isAuthorized() const
    {
        return (*this)->isFlag(lsfMPTAuthorized);
    }

    /**
     * Returns true if @p account cannot send or receive tokens of this
     * MPToken's issuance because a freeze applies: the issuance is globally
     * locked, this MPToken is individually locked, or (for a vault share)
     * the vault pseudo-account's underlying asset is frozen.
     *
     * The issuance is read once for the global-freeze and vault checks.
     *
     * @param account The holder of this MPToken.
     * @param depth Current recursion depth for the vault-share walk.
     */
    [[nodiscard]] bool
    isFrozen(AccountID const& account, std::uint8_t depth = 0) const;

    /**
     * Creates the MPToken of @p account for @p mptIssuanceID: links it into
     * the owner directory, sets its fields and sponsor, and inserts it.
     *
     * Does not change the owner count.
     *
     * @return tesSUCCESS, or tecDIR_FULL if the owner directory is full
     */
    [[nodiscard]] static TER
    create(
        ApplyView& view,
        MPTID const& mptIssuanceID,
        AccountID const& account,
        SLE::Ref sponsorSle,
        std::uint32_t flags)
        requires Base::kIsWritable;

    /**
     * Returns true if this MPToken cannot be deleted because it still holds
     * value: a non-zero MPTAmount, a non-zero LockedAmount (once
     * fixCleanup3_1_3 is enabled), or any confidential balance field.
     */
    [[nodiscard]] bool
    hasObligations() const;

    /**
     * Moves @p amount of this MPToken's MPTAmount into its LockedAmount,
     * for an escrow. Does not change the issuance.
     *
     * @return tesSUCCESS, or tecINTERNAL on underflow or overflow
     */
    [[nodiscard]] TER
    lockEscrow(STAmount const& amount)
        requires Base::kIsWritable;

    /**
     * Removes @p grossAmount from this MPToken's LockedAmount when an escrow
     * finishes or is canceled. Removes the field when it reaches zero. Does
     * not change MPTAmount or the issuance.
     *
     * @return tesSUCCESS, or tecINTERNAL if there is no LockedAmount or it
     *         would underflow
     */
    [[nodiscard]] TER
    unlockEscrow(STAmount const& grossAmount)
        requires Base::kIsWritable;

    /**
     * Removes this MPToken of an AMM pseudo-account from @p ammAccountID's
     * owner directory and erases it. Does not change the owner count, which
     * is irrelevant for an AMM.
     *
     * @return tesSUCCESS, or tefBAD_LEDGER if the directory removal fails
     */
    [[nodiscard]] TER
    removeForAMM(AccountID const& ammAccountID)
        requires Base::kIsWritable
    {
        if (!this->applyView().dirRemove(
                keylet::ownerDir(ammAccountID), (**this)[sfOwnerNode], (*this)->key(), false))
            return tefBAD_LEDGER;  // LCOV_EXCL_LINE

        this->erase();

        return tesSUCCESS;
    }
};

using MPTokenEntryR = MPTokenEntry<ReadView>;
using MPTokenEntryW = MPTokenEntry<ApplyView>;

}  // namespace xrpl
