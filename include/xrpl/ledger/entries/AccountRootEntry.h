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
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>

#include <cstdint>
#include <expected>
#include <optional>

namespace xrpl {

struct Adjustment
{
    std::int32_t ownerCountDelta = 0;
    std::int32_t accountCountDelta = 0;
};

template <typename ViewT>
class AccountRootEntry : public SLEBase<ViewT, ltACCOUNT_ROOT>
{
public:
    using Base = SLEBase<ViewT, ltACCOUNT_ROOT>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit AccountRootEntry(
        AccountID const& id,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::account(id), view, j)
    {
    }

    /**
     * Returns the account reserve, in drops.
     *
     * Actual owner count can be adjusted by delta in ownerCountAdj
     * Actual reserve count can be adjusted by delta in accountCountAdj
     * The reserve is calculated as:
     * (ownerCount + "sponsoring object count" - "sponsored object count" + additionalOwnerCount) *
     * increment + (1 if not sponsored account + sponsoringAccountCount) * "reserve base"
     *
     * @param adj Adjustment to the owner/account count (default: 0/0). Positive to add, negative
     * to subtract.
     * @return The account reserve amount in drops
     */
    [[nodiscard]] XRPAmount
    reserve(Adjustment adj = {}) const;

    /**
     * Return number of the objects which reserve is covered by the account (so called "owner
     * count"). Actual owner count can be adjusted by delta in ownerCountAdj.
     *
     * @param ownerCountAdj Adjustment to the owner count (default: 0)
     * @return The adjusted owner count
     */
    [[nodiscard]] std::uint32_t
    ownerCount(std::int32_t ownerCountAdj = 0) const;

    /**
     * Check if this account has sufficient reserve.
     *
     * @param ctx The apply-view context (view + tx)
     * @param accBalance The account's balance
     * @param sponsorSle The sponsor's ledger entry (if applicable)
     * @param adj Adjustment to the owner/account count (default: 0/0). Positive to add, negative
     * to subtract.
     * @param insufReserveCode The transaction result code to return if the reserve is
     * insufficient (default: tecINSUFFICIENT_RESERVE).
     * @return Transaction result code
     */
    [[nodiscard]] TER
    checkReserve(
        ApplyViewContext ctx,
        XRPAmount accBalance,
        std::optional<AccountRootEntry<ReadView>> const& sponsorSle,
        Adjustment adj,
        TER insufReserveCode = tecINSUFFICIENT_RESERVE) const;

    /**
     * Check if this account has sufficient reserve, deriving the sponsor internally.
     *
     * Equivalent to the overload above, but resolves the sponsor via
     * getEffectiveTxReserveSponsor(ctx, *this) instead of taking it explicitly. Use this
     * in the common case where the sponsor is simply the transaction's reserve
     * sponsor for this account. Callers that must force the account's-own-reserve branch
     * (passing a null sponsor) or supply a different sponsor should use the
     * explicit overload above.
     *
     * @param ctx The apply-view context (view + tx)
     * @param accBalance The account's balance
     * @param adj Reserve adjustments (owner/account count deltas)
     * @return Transaction result code
     */
    [[nodiscard]] TER
    checkReserve(ApplyViewContext ctx, XRPAmount accBalance, Adjustment adj) const;

    /**
     * Returns true if and only if this entry is a pseudo-account of any kind
     * (i.e. carries at least one field flagged with SField::kSmdPseudoAccount).
     *
     * Returns false if the entry:
     * - is NOT a pseudo-account OR
     * - is NOT a ltACCOUNT_ROOT OR
     * - does not exist
     *
     * @return true if and only if this entry is a pseudo-account of any kind.
     */
    [[nodiscard]] bool
    isPseudoAccount() const;

    /**
     * Returns true if this account has the global freeze flag set.
     *
     * @return true if this account has the global freeze flag set.
     */
    [[nodiscard]] bool
    isGlobalFrozen() const
    {
        return (*this)->isFlag(lsfGlobalFreeze);
    }

    /**
     * Checks the destination and tag.
     *
     * - Checks that the entry exists.
     * - If the entry requires a destination tag, checks that there is a tag.
     *
     * @param hasDestinationTag Whether the transaction supplies a destination tag
     * @return tecNO_DST if the entry does not exist; tecDST_TAG_NEEDED if the
     *         entry requires a destination tag and hasDestinationTag is false;
     *         tesSUCCESS otherwise
     */
    [[nodiscard]] TER
    checkDestinationAndTag(bool hasDestinationTag) const
    {
        if (!this->exists())
            return tecNO_DST;

        // The tag is basically account-specific information we don't
        // understand, but we can require someone to fill it in.
        if ((*this)->isFlag(lsfRequireDestTag) && !hasDestinationTag)
            return tecDST_TAG_NEEDED;  // Cannot send without a tag

        return tesSUCCESS;
    }

    /**
     * Increase owner-count fields when the caller supplies the sponsor.
     *
     * This helper does not create a ledger object. It updates reserve accounting
     * after the caller has created/updated an object.
     * If sponsorSle is provided, this also adjusts the account's sponsored count
     * and the sponsor's sponsoring count.
     *
     * @param sponsorSle The sponsor's ledger entry (if applicable)
     * @param count Amount to add to the owner count
     */
    void
    increaseOwnerCount(std::optional<AccountRootEntry<ApplyView>>& sponsorSle, std::uint32_t count)
        requires Base::kIsWritable;

    /**
     * Increase owner-count fields, deriving the tx reserve sponsor internally.
     *
     * Equivalent to the overload above, but resolves the sponsor via
     * getEffectiveTxReserveSponsor(ctx, *this) instead of taking it explicitly. Use
     * this when the sponsor is the transaction's reserve sponsor for this account
     * (the common create path). Deletion paths, which derive the sponsor from an
     * object's sfSponsor field, should keep using the explicit overload.
     *
     * @param ctx The apply-view context (view + tx)
     * @param count Amount to add to the owner count
     */
    void
    increaseOwnerCount(ApplyViewContext ctx, std::uint32_t count)
        requires Base::kIsWritable;

    /**
     * Decrease owner-count fields when the caller supplies the sponsor.
     *
     * This helper does not delete a ledger object. It updates reserve accounting
     * after the caller has removed an owner-counted reserve, or for special
     * owner-count changes whose sponsor cannot be derived from an object's
     * sfSponsor field.
     *
     * @param sponsorSle The sponsor's ledger entry (if applicable)
     * @param count Amount to remove from the owner count
     */
    void
    decreaseOwnerCount(std::optional<AccountRootEntry<ApplyView>>& sponsorSle, std::uint32_t count)
        requires Base::kIsWritable;

    /**
     * Create pseudo-account, storing pseudoOwnerKey into ownerField.
     *
     * The list of valid ownerField is maintained in AccountRootHelpers.cpp and
     * the caller to this function must perform necessary amendment check(s)
     * before using a field. The amendment check is **not** performed in
     * createPseudoAccount.
     *
     * @param view The ledger view to create the pseudo-account in
     * @param pseudoOwnerKey The key of the object that owns the pseudo-account,
     *                       stored into ownerField
     * @param ownerField The field linking the pseudo-account to its owner
     * @return The newly created pseudo-account, or tecDUPLICATE if an account
     *         already exists at the derived pseudo-account address
     */
    [[nodiscard]] static std::expected<AccountRootEntry<ApplyView>, TER>
    createPseudoAccount(ApplyView& view, UInt256 const& pseudoOwnerKey, SField const& ownerField)
        requires Base::kIsWritable;

private:
    void
    adjustOwnerCountSigned(
        std::optional<AccountRootEntry<ApplyView>>& sponsorSle,
        std::int32_t adjustment)
        requires Base::kIsWritable;
};

using AccountRootEntryR = AccountRootEntry<ReadView>;
using AccountRootEntryW = AccountRootEntry<ApplyView>;

}  // namespace xrpl
