#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/entries/SLEBase.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>

#include <cstdint>
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
