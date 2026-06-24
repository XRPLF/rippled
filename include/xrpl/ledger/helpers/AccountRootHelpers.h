#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Rate.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>

#include <expected>
#include <set>
#include <vector>

namespace xrpl {

/** Check if the issuer has the global freeze flag set.
    @param issuer The account to check
    @return true if the account has global freeze set
*/
[[nodiscard]] bool
isGlobalFrozen(ReadView const& view, AccountID const& issuer);

// Calculate liquid XRP balance for an account.
// This function may be used to calculate the amount of XRP that
// the holder is able to freely spend. It subtracts reserve requirements.
//
// ownerCountAdj adjusts the owner count in case the caller calculates
// before ledger entries are added or removed. Positive to add, negative
// to subtract.
//
// @param ownerCountAdj positive to add to count, negative to reduce count.
[[nodiscard]] XRPAmount
xrpLiquid(ReadView const& view, AccountID const& id, std::int32_t ownerCountAdj, beast::Journal j);

/*  Calculates the number of objects that the account is responsible for reserve-wise
    sfOwnerCount - sfSponsoredOwnerCount + sfSponsoringOwnerCount
*/
inline std::uint32_t
objectOwnerCount(SLE::const_pointer accountSle, std::int32_t delta = 0)
{
    XRPL_ASSERT(
        accountSle && accountSle->getType() == ltACCOUNT_ROOT,
        "xrpl::objectOwnerCount : valid account sle");
    return accountSle->at(sfOwnerCount) - accountSle->at(sfSponsoredOwnerCount) +
        accountSle->at(sfSponsoringOwnerCount) + delta;
}

/*  Calculates the number of accounts that the account is responsible for reserve-wise
    (accountIsSponsored ? 0 : 1) + sfSponsoringAccountCount
*/
inline std::uint32_t
accountOwnerCount(SLE::const_pointer accountSle, std::int32_t delta = 0)
{
    XRPL_ASSERT(
        accountSle && accountSle->getType() == ltACCOUNT_ROOT,
        "xrpl::accountOwnerCount : valid account sle");
    return (accountSle->isFieldPresent(sfSponsor) ? 0 : 1) +
        accountSle->getFieldU32(sfSponsoringAccountCount) + delta;
}

/*  Calculates the total reserve, in XRP, that the account is currently responsible for
 */
inline XRPAmount
totalAccountReserve(
    ReadView const& view,
    SLE::const_pointer accountSle,
    std::int32_t objectDelta = 0,
    std::int32_t accountDelta = 0)
{
    auto const& fees = view.fees();
    return (fees.reserve * accountOwnerCount(accountSle, accountDelta)) +
        (fees.increment * objectOwnerCount(accountSle, objectDelta));
}

[[nodiscard]] inline XRPAmount
totalAccountReserve(
    ReadView const& view,
    AccountID const& id,
    std::int32_t ownerCountDelta = 0,
    std::int32_t accountCountDelta = 0)
{
    return totalAccountReserve(
        view, view.read(keylet::account(id)), ownerCountDelta, accountCountDelta);
}

[[nodiscard]] TER
checkInsufficientReserve(
    ReadView const& view,
    STTx const& tx,
    SLE::const_ref accSle,
    STAmount const& accBalance,
    SLE::const_ref sponsorSle,
    std::int32_t ownerCountDelta,
    std::int32_t reserveCountDelta = 0,
    beast::Journal j = beast::Journal{beast::Journal::getNullSink()});

/** Adjust the owner count up or down. */
void
adjustOwnerCount(
    ApplyView& view,
    SLE::pointer accountSle,
    SLE::pointer sponsorSle,
    std::int32_t amount,
    beast::Journal j = beast::Journal{beast::Journal::getNullSink()});

inline void
adjustOwnerCount(
    ApplyView& view,
    AccountID const& account,
    std::optional<AccountID> const& sponsor,
    std::int32_t amount,
    beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
{
    adjustOwnerCount(
        view,
        view.peek(keylet::account(account)),
        sponsor ? view.peek(keylet::account(*sponsor)) : SLE::pointer(),
        amount,
        j);
}

void
adjustOwnerCountObj(
    ApplyView& view,
    SLE::pointer accountSle,
    SLE::pointer objectSle,
    std::int32_t amount,
    beast::Journal j = beast::Journal{beast::Journal::getNullSink()});

inline void
adjustOwnerCountObj(
    ApplyView& view,
    AccountID const& account,
    SLE::pointer objectSle,
    std::int32_t amount,
    beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
{
    SLE::pointer accountSle = view.peek(keylet::account(account));
    adjustOwnerCountObj(view, accountSle, objectSle, amount, j);
}

/** Returns IOU issuer transfer fee as Rate. Rate specifies
 * the fee as fractions of 1 billion. For example, 1% transfer rate
 * is represented as 1,010,000,000.
 * @param issuer The IOU issuer
 */
[[nodiscard]] Rate
transferRate(ReadView const& view, AccountID const& issuer);

/** Generate a pseudo-account address from a pseudo owner key.
    @param pseudoOwnerKey The key to generate the address from
    @return The generated account ID
*/
AccountID
pseudoAccountAddress(ReadView const& view, uint256 const& pseudoOwnerKey);

/** Returns the list of fields that define an ACCOUNT_ROOT as a pseudo-account
    if set.

    The list is constructed during initialization and is const after that.
    Pseudo-account designator fields MUST be maintained by including the
    SField::sMD_PseudoAccount flag in the SField definition.
*/
[[nodiscard]] std::vector<SField const*> const&
getPseudoAccountFields();

/** Returns true if and only if sleAcct is a pseudo-account or specific
    pseudo-accounts in pseudoFieldFilter.

    Returns false if sleAcct is:
    - NOT a pseudo-account OR
    - NOT a ltACCOUNT_ROOT OR
    - null pointer
*/
[[nodiscard]] bool
isPseudoAccount(SLE::const_pointer sleAcct, std::set<SField const*> const& pseudoFieldFilter = {});

/** Convenience overload that reads the account from the view. */
[[nodiscard]] inline bool
isPseudoAccount(
    ReadView const& view,
    AccountID const& accountId,
    std::set<SField const*> const& pseudoFieldFilter = {})
{
    return isPseudoAccount(view.read(keylet::account(accountId)), pseudoFieldFilter);
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
createPseudoAccount(ApplyView& view, uint256 const& pseudoOwnerKey, SField const& ownerField);

/** Checks the destination and tag.

   - Checks that the SLE is not null.
   - If the SLE requires a destination tag, checks that there is a tag.
*/
[[nodiscard]] TER
checkDestinationAndTag(SLE::const_ref toSle, bool hasDestinationTag);

}  // namespace xrpl
