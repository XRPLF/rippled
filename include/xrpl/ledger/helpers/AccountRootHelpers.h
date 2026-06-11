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

/** Adjust the owner count up or down. */
void
adjustOwnerCount(ApplyView& view, SLE::ref sle, std::int32_t amount, beast::Journal j);

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
