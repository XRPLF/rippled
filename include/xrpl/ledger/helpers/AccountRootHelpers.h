#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/entries/AccountRootEntry.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Rate.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>

#include <cstdint>
#include <expected>
#include <optional>
#include <vector>

namespace xrpl {

/**
 * Check if the issuer has the global freeze flag set.
 * @param issuer The account to check
 * @return true if the account has global freeze set
 */
[[nodiscard]] bool
isGlobalFrozen(ReadView const& view, AccountID const& issuer);

/**
 * Calculate liquid XRP balance for an account.
 *
 * This function may be used to calculate the amount of XRP that
 * the holder is able to freely spend. It subtracts reserve requirements.
 *
 * ownerCountAdj adjusts the owner count in case the caller calculates
 * before ledger entries are added or removed. Positive to add, negative
 * to subtract.
 *
 * @param view The ledger view to read from
 * @param id The account ID to check
 * @param ownerCountAdj Positive to add to count, negative to reduce count
 * @param j Journal for logging
 * @return The liquid XRP amount available to the account
 */
[[nodiscard]] XRPAmount
xrpLiquid(ReadView const& view, AccountID const& id, std::int32_t ownerCountAdj, beast::Journal j);

namespace detail {

/**
 * An owner count cannot be negative. If adjustment would cause a negative
 * owner count, clamp the owner count at 0. Similarly for overflow. This
 * adjustment allows the ownerCount to be adjusted up or down in multiple steps.
 * If id != std::nullopt, then do error reporting.
 *
 * Shared by AccountRootEntry::ownerCount() and the owner-count helpers.
 *
 * @param currentOwnerCount The owner count before adjustment
 * @param ownerCountAdj Positive to add to count, negative to reduce count
 * @param id The account ID to use for error reporting, or std::nullopt to
 *           skip error reporting
 * @param j Journal for logging
 * @return The adjusted owner count
 */
std::uint32_t
confineOwnerCount(
    std::uint32_t currentOwnerCount,
    std::int32_t ownerCountAdj,
    std::optional<AccountID> const& id = std::nullopt,
    beast::Journal j = beast::Journal{beast::Journal::getNullSink()});

/**
 * Returns the number of account reserves funded by this account: 1 for itself
 * (0 if sponsored by another account) plus the count of accounts it sponsors.
 * Shared by AccountRootEntry::reserve() and xrpLiquid().
 */
std::uint32_t
accountCountImpl(AccountRootEntryR const& sle, std::int32_t accountCountAdj, beast::Journal j);

}  // namespace detail

/**
 * Convenience overload that accepts AccountID instead of SLE.
 *
 * @param view The ledger view to read from
 * @param id The account ID
 * @param j Journal for logging
 * @param adj Adjustment to the owner/account count (default: 0/0). Positive to add, negative to
 * subtract.
 * @return The account reserve amount in drops
 */
[[nodiscard]] inline XRPAmount
accountReserve(ReadView const& view, AccountID const& id, beast::Journal j, Adjustment adj = {})
{
    return AccountRootEntryR(id, view, j).reserve(adj);
}

/**
 * Increase owner-count fields when the caller supplies the sponsor.
 *
 * This helper does not create a ledger object. It updates reserve accounting
 * after the caller has created/updated an object.
 * If sponsorSle is provided, this also adjusts the account's sponsored count
 * and the sponsor's sponsoring count.
 *
 * @param view The apply view for making changes
 * @param accountSle The account's ledger entry
 * @param sponsorSle The sponsor's ledger entry (if applicable)
 * @param count Amount to add to the owner count
 * @param j Journal for logging
 */
void
increaseOwnerCount(
    ApplyView& view,
    AccountRootEntryW& accountSle,
    std::optional<AccountRootEntryW>& sponsorSle,
    std::uint32_t count,
    beast::Journal j);

/**
 * Increase owner-count fields, deriving the tx reserve sponsor internally.
 *
 * Equivalent to the overload above, but resolves the sponsor via
 * getEffectiveTxReserveSponsor(ctx, accountSle) instead of taking it explicitly. Use
 * this when the sponsor is the transaction's reserve sponsor for accountSle
 * (the common create path). Deletion paths, which derive the sponsor from an
 * object's sfSponsor field, should keep using the explicit overload.
 *
 * @param ctx The apply-view context (view + tx)
 * @param accountSle The account's ledger entry
 * @param count Amount to add to the owner count
 * @param j Journal for logging
 */
void
increaseOwnerCount(
    ApplyViewContext ctx,
    AccountRootEntryW& accountSle,
    std::uint32_t count,
    beast::Journal j);

/**
 * Convenience overload that accepts AccountID instead of SLE references.
 *
 * @param view The apply view for making changes
 * @param account The account ID
 * @param sponsor The optional sponsor account ID
 * @param count Amount to add to the owner count
 * @param j Journal for logging
 */
inline void
increaseOwnerCount(
    ApplyView& view,
    AccountID const& account,
    std::optional<AccountID> const& sponsor,
    std::uint32_t count,
    beast::Journal j)
{
    AccountRootEntryW accountSle(account, view);
    std::optional<AccountRootEntryW> sponsorSle;
    if (sponsor)
        sponsorSle.emplace(*sponsor, view);
    increaseOwnerCount(view, accountSle, sponsorSle, count, j);
}

/**
 * Decrease owner-count fields when the caller supplies the sponsor.
 *
 * This helper does not delete a ledger object. It updates reserve accounting
 * after the caller has removed an owner-counted reserve, or for special
 * owner-count changes whose sponsor cannot be derived from an object's
 * sfSponsor field.
 *
 * @param view The apply view for making changes
 * @param accountSle The account's ledger entry
 * @param sponsorSle The sponsor's ledger entry (if applicable)
 * @param count Amount to remove from the owner count
 * @param j Journal for logging
 */
void
decreaseOwnerCount(
    ApplyView& view,
    AccountRootEntryW& accountSle,
    std::optional<AccountRootEntryW>& sponsorSle,
    std::uint32_t count,
    beast::Journal j);

/**
 * Convenience overload that accepts AccountID instead of SLE references.
 *
 * @param view The apply view for making changes
 * @param account The account ID
 * @param sponsor The optional sponsor account ID
 * @param count Amount to remove from the owner count
 * @param j Journal for logging
 */
inline void
decreaseOwnerCount(
    ApplyView& view,
    AccountID const& account,
    std::optional<AccountID> const& sponsor,
    std::uint32_t count,
    beast::Journal j)
{
    AccountRootEntryW accountSle(account, view);
    std::optional<AccountRootEntryW> sponsorSle;
    if (sponsor)
        sponsorSle.emplace(*sponsor, view);
    decreaseOwnerCount(view, accountSle, sponsorSle, count, j);
}

/**
 * Decrease owner-count fields for an existing ledger object.
 *
 * This helper derives the reserve sponsor from objectSle's sfSponsor field,
 * then updates the same owner-count fields as decreaseOwnerCount. Use this
 * when removing an existing object whose reserve sponsor is stored on that
 * object.
 *
 * @param view The apply view for making changes
 * @param accountSle The account's ledger entry
 * @param objectSle The object's ledger entry
 * @param count Amount to remove from the owner count
 * @param j Journal for logging
 */
void
decreaseOwnerCountForObject(
    ApplyView& view,
    AccountRootEntryW& accountSle,
    SLE::Ref objectSle,
    std::uint32_t count,
    beast::Journal j);

/**
 * Convenience overload that accepts AccountID instead of account SLE reference.
 *
 * @param view The apply view for making changes
 * @param account The account ID
 * @param objectSle The object's ledger entry
 * @param count Amount to remove from the owner count
 * @param j Journal for logging
 */
inline void
decreaseOwnerCountForObject(
    ApplyView& view,
    AccountID const& account,
    SLE::Ref objectSle,
    std::uint32_t count,
    beast::Journal j)
{
    AccountRootEntryW accountSle(account, view);
    decreaseOwnerCountForObject(view, accountSle, objectSle, count, j);
}

/**
 * Adjust a LoanBroker's owner count.
 *
 * A LoanBroker's sfOwnerCount tracks the number of outstanding loans on
 * that broker; it is not a reserve-backed owner count and is distinct
 * from the broker's pseudo-account's owner count. Loans can never carry a
 * reserve sponsor (LoanSet rejects reserve sponsorship at preflight), so
 * this never involves sponsor accounting and never invokes the
 * ownerCountHook used for ACCOUNT_ROOT reserve tracking.
 *
 * @param view The apply view for making changes
 * @param brokerSle The LoanBroker's ledger entry
 * @param delta Amount to add (positive) or remove (negative) from the count
 * @param j Journal for logging
 */
void
adjustLoanBrokerOwnerCount(
    ApplyView& view,
    SLE::Ref brokerSle,
    std::int32_t delta,
    beast::Journal j);

/**
 * Returns IOU issuer transfer fee as Rate. Rate specifies
 * the fee as fractions of 1 billion. For example, 1% transfer rate
 * is represented as 1,010,000,000.
 * @param issuer The IOU issuer
 */
[[nodiscard]] Rate
transferRate(ReadView const& view, AccountID const& issuer);

/**
 * Generate a pseudo-account address from a pseudo owner key.
 * @param pseudoOwnerKey The key to generate the address from
 * @return The generated account ID
 */
AccountID
pseudoAccountAddress(ReadView const& view, UInt256 const& pseudoOwnerKey);

/**
 * Returns the list of fields that define an ACCOUNT_ROOT as a pseudo-account
 * if set.
 *
 * The list is constructed during initialization and is const after that.
 * Pseudo-account designator fields MUST be maintained by including the
 * SField::kSmdPseudoAccount flag in the SField definition.
 */
[[nodiscard]] std::vector<SField const*> const&
getPseudoAccountFields();

/**
 * Returns true if and only if sleAcct is a pseudo-account of any kind
 * (i.e. carries at least one field flagged with SField::kSmdPseudoAccount).
 *
 * Returns false if sleAcct is:
 * - NOT a pseudo-account OR
 * - NOT a ltACCOUNT_ROOT OR
 * - null pointer
 */
[[nodiscard]] bool
isPseudoAccount(AccountRootEntryR const& sleAcct);

/**
 * Convenience overload that reads the account from the view.
 */
[[nodiscard]] inline bool
isPseudoAccount(ReadView const& view, AccountID const& accountId)
{
    return isPseudoAccount(AccountRootEntryR(accountId, view));
}

/**
 * Create pseudo-account, storing pseudoOwnerKey into ownerField.
 *
 * The list of valid ownerField is maintained in AccountRootHelpers.cpp and
 * the caller to this function must perform necessary amendment check(s)
 * before using a field. The amendment check is **not** performed in
 * createPseudoAccount.
 */
[[nodiscard]] std::expected<SLE::pointer, TER>
createPseudoAccount(ApplyView& view, UInt256 const& pseudoOwnerKey, SField const& ownerField);

/**
 * Checks the destination and tag.
 *
 * - Checks that the SLE is not null.
 * - If the SLE requires a destination tag, checks that there is a tag.
 */
[[nodiscard]] TER
checkDestinationAndTag(AccountRootEntryR const& toSle, bool hasDestinationTag);

}  // namespace xrpl
