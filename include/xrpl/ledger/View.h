#ifndef XRPL_LEDGER_VIEW_H_INCLUDED
#define XRPL_LEDGER_VIEW_H_INCLUDED

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/Rate.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/TER.h>

#include <functional>
#include <initializer_list>
#include <map>
#include <utility>

namespace ripple {

enum class WaiveTransferFee : bool { No = false, Yes };
enum class SkipEntry : bool { No = false, Yes };

//------------------------------------------------------------------------------
//
// Observers
//
//------------------------------------------------------------------------------

/** Determines whether the given expiration time has passed.

    In the XRP Ledger, expiration times are defined as the number of whole
    seconds after the "Ripple Epoch" which, for historical reasons, is set
    to January 1, 2000 (00:00 UTC).

    This is like the way the Unix epoch works, except the Ripple Epoch is
    precisely 946,684,800 seconds after the Unix Epoch.

    See https://xrpl.org/basic-data-types.html#specifying-time

    Expiration is defined in terms of the close time of the parent ledger,
    because we definitively know the time that it closed (since consensus
    agrees on time) but we do not know the closing time of the ledger that
    is under construction.

    @param view The ledger whose parent time is used as the clock.
    @param exp The optional expiration time we want to check.

    @returns `true` if `exp` is in the past; `false` otherwise.
 */
[[nodiscard]] bool
hasExpired(ReadView const& view, std::optional<std::uint32_t> const& exp);

/** Controls the treatment of frozen account balances */
enum FreezeHandling { fhIGNORE_FREEZE, fhZERO_IF_FROZEN };

/** Controls the treatment of unauthorized MPT balances */
enum AuthHandling { ahIGNORE_AUTH, ahZERO_IF_UNAUTHORIZED };

[[nodiscard]] bool
isGlobalFrozen(ReadView const& view, AccountID const& issuer);

[[nodiscard]] bool
isGlobalFrozen(ReadView const& view, MPTIssue const& mptIssue);

[[nodiscard]] bool
isGlobalFrozen(ReadView const& view, Asset const& asset);

// Note, depth parameter is used to limit the recursion depth
[[nodiscard]] bool
isVaultPseudoAccountFrozen(
    ReadView const& view,
    AccountID const& account,
    MPTIssue const& mptShare,
    int depth);

[[nodiscard]] bool
isIndividualFrozen(
    ReadView const& view,
    AccountID const& account,
    Currency const& currency,
    AccountID const& issuer);

[[nodiscard]] inline bool
isIndividualFrozen(
    ReadView const& view,
    AccountID const& account,
    Issue const& issue)
{
    return isIndividualFrozen(view, account, issue.currency, issue.account);
}

[[nodiscard]] bool
isIndividualFrozen(
    ReadView const& view,
    AccountID const& account,
    MPTIssue const& mptIssue);

[[nodiscard]] inline bool
isIndividualFrozen(
    ReadView const& view,
    AccountID const& account,
    Asset const& asset)
{
    return std::visit(
        [&](auto const& issue) {
            return isIndividualFrozen(view, account, issue);
        },
        asset.value());
}

[[nodiscard]] bool
isFrozen(
    ReadView const& view,
    AccountID const& account,
    Currency const& currency,
    AccountID const& issuer);

[[nodiscard]] inline bool
isFrozen(
    ReadView const& view,
    AccountID const& account,
    Issue const& issue,
    int = 0 /*ignored*/)
{
    return isFrozen(view, account, issue.currency, issue.account);
}

[[nodiscard]] bool
isFrozen(
    ReadView const& view,
    AccountID const& account,
    MPTIssue const& mptIssue,
    int depth = 0);

/**
 *   isFrozen check is recursive for MPT shares in a vault, descending to
 *   assets in the vault, up to maxAssetCheckDepth recursion depth. This is
 *   purely defensive, as we currently do not allow such vaults to be created.
 */
[[nodiscard]] inline bool
isFrozen(
    ReadView const& view,
    AccountID const& account,
    Asset const& asset,
    int depth = 0)
{
    return std::visit(
        [&](auto const& issue) {
            return isFrozen(view, account, issue, depth);
        },
        asset.value());
}

[[nodiscard]] inline TER
checkFrozen(ReadView const& view, AccountID const& account, Issue const& issue)
{
    return isFrozen(view, account, issue) ? (TER)tecFROZEN : (TER)tesSUCCESS;
}

[[nodiscard]] inline TER
checkFrozen(
    ReadView const& view,
    AccountID const& account,
    MPTIssue const& mptIssue)
{
    return isFrozen(view, account, mptIssue) ? (TER)tecLOCKED : (TER)tesSUCCESS;
}

[[nodiscard]] inline TER
checkFrozen(ReadView const& view, AccountID const& account, Asset const& asset)
{
    return std::visit(
        [&](auto const& issue) { return checkFrozen(view, account, issue); },
        asset.value());
}

[[nodiscard]] bool
isAnyFrozen(
    ReadView const& view,
    std::initializer_list<AccountID> const& accounts,
    MPTIssue const& mptIssue,
    int depth = 0);

[[nodiscard]] inline bool
isAnyFrozen(
    ReadView const& view,
    std::initializer_list<AccountID> const& accounts,
    Issue const& issue)
{
    for (auto const& account : accounts)
    {
        if (isFrozen(view, account, issue.currency, issue.account))
            return true;
    }
    return false;
}

[[nodiscard]] inline bool
isAnyFrozen(
    ReadView const& view,
    std::initializer_list<AccountID> const& accounts,
    Asset const& asset,
    int depth = 0)
{
    return std::visit(
        [&]<ValidIssueType TIss>(TIss const& issue) {
            if constexpr (std::is_same_v<TIss, Issue>)
                return isAnyFrozen(view, accounts, issue);
            else
                return isAnyFrozen(view, accounts, issue, depth);
        },
        asset.value());
}

[[nodiscard]] bool
isDeepFrozen(
    ReadView const& view,
    AccountID const& account,
    Currency const& currency,
    AccountID const& issuer);

[[nodiscard]] inline bool
isDeepFrozen(
    ReadView const& view,
    AccountID const& account,
    Issue const& issue,
    int = 0 /*ignored*/)
{
    return isDeepFrozen(view, account, issue.currency, issue.account);
}

[[nodiscard]] inline bool
isDeepFrozen(
    ReadView const& view,
    AccountID const& account,
    MPTIssue const& mptIssue,
    int depth = 0)
{
    // Unlike IOUs, frozen / locked MPTs are not allowed to send or receive
    // funds, so checking "deep frozen" is the same as checking "frozen".
    return isFrozen(view, account, mptIssue, depth);
}

/**
 *   isFrozen check is recursive for MPT shares in a vault, descending to
 *   assets in the vault, up to maxAssetCheckDepth recursion depth. This is
 *   purely defensive, as we currently do not allow such vaults to be created.
 */
[[nodiscard]] inline bool
isDeepFrozen(
    ReadView const& view,
    AccountID const& account,
    Asset const& asset,
    int depth = 0)
{
    return std::visit(
        [&](auto const& issue) {
            return isDeepFrozen(view, account, issue, depth);
        },
        asset.value());
}

[[nodiscard]] inline TER
checkDeepFrozen(
    ReadView const& view,
    AccountID const& account,
    Issue const& issue)
{
    return isDeepFrozen(view, account, issue) ? (TER)tecFROZEN
                                              : (TER)tesSUCCESS;
}

[[nodiscard]] inline TER
checkDeepFrozen(
    ReadView const& view,
    AccountID const& account,
    MPTIssue const& mptIssue)
{
    return isDeepFrozen(view, account, mptIssue) ? (TER)tecLOCKED
                                                 : (TER)tesSUCCESS;
}

[[nodiscard]] inline TER
checkDeepFrozen(
    ReadView const& view,
    AccountID const& account,
    Asset const& asset)
{
    return std::visit(
        [&](auto const& issue) {
            return checkDeepFrozen(view, account, issue);
        },
        asset.value());
}

[[nodiscard]] bool
isLPTokenFrozen(
    ReadView const& view,
    AccountID const& account,
    Issue const& asset,
    Issue const& asset2);

// Returns the amount an account can spend without going into debt.
//
// <-- saAmount: amount of currency held by account. May be negative.
[[nodiscard]] STAmount
accountHolds(
    ReadView const& view,
    AccountID const& account,
    Currency const& currency,
    AccountID const& issuer,
    FreezeHandling zeroIfFrozen,
    beast::Journal j);

[[nodiscard]] STAmount
accountHolds(
    ReadView const& view,
    AccountID const& account,
    Issue const& issue,
    FreezeHandling zeroIfFrozen,
    beast::Journal j);

[[nodiscard]] STAmount
accountHolds(
    ReadView const& view,
    AccountID const& account,
    MPTIssue const& mptIssue,
    FreezeHandling zeroIfFrozen,
    AuthHandling zeroIfUnauthorized,
    beast::Journal j);

[[nodiscard]] STAmount
accountHolds(
    ReadView const& view,
    AccountID const& account,
    Asset const& asset,
    FreezeHandling zeroIfFrozen,
    AuthHandling zeroIfUnauthorized,
    beast::Journal j);

// Returns the amount an account can spend total.
//
// These functions use accountHolds, but unlike accountHolds:
// * The account can go into debt.
// * If the account is the asset issuer the only limit is defined by the asset /
//   issuance.
//
// <-- saAmount: amount of currency held by account. May be negative.
[[nodiscard]] STAmount
accountSpendable(
    ReadView const& view,
    AccountID const& account,
    Currency const& currency,
    AccountID const& issuer,
    FreezeHandling zeroIfFrozen,
    beast::Journal j);

[[nodiscard]] STAmount
accountSpendable(
    ReadView const& view,
    AccountID const& account,
    Issue const& issue,
    FreezeHandling zeroIfFrozen,
    beast::Journal j);

[[nodiscard]] STAmount
accountSpendable(
    ReadView const& view,
    AccountID const& account,
    MPTIssue const& mptIssue,
    FreezeHandling zeroIfFrozen,
    AuthHandling zeroIfUnauthorized,
    beast::Journal j);

[[nodiscard]] STAmount
accountSpendable(
    ReadView const& view,
    AccountID const& account,
    Asset const& asset,
    FreezeHandling zeroIfFrozen,
    AuthHandling zeroIfUnauthorized,
    beast::Journal j);

// Returns the amount an account can spend of the currency type saDefault, or
// returns saDefault if this account is the issuer of the currency in
// question. Should be used in favor of accountHolds when questioning how much
// an account can spend while also allowing currency issuers to spend
// unlimited amounts of their own currency (since they can always issue more).
[[nodiscard]] STAmount
accountFunds(
    ReadView const& view,
    AccountID const& id,
    STAmount const& saDefault,
    FreezeHandling freezeHandling,
    beast::Journal j);

// Return the account's liquid (not reserved) XRP.  Generally prefer
// calling accountHolds() over this interface.  However, this interface
// allows the caller to temporarily adjust the owner count should that be
// necessary.
//
// @param ownerCountAdj positive to add to count, negative to reduce count.
[[nodiscard]] XRPAmount
xrpLiquid(
    ReadView const& view,
    AccountID const& id,
    std::int32_t ownerCountAdj,
    beast::Journal j);

/** Iterate all items in the given directory. */
void
forEachItem(
    ReadView const& view,
    Keylet const& root,
    std::function<void(std::shared_ptr<SLE const> const&)> const& f);

/** Iterate all items after an item in the given directory.
    @param after The key of the item to start after
    @param hint The directory page containing `after`
    @param limit The maximum number of items to return
    @return `false` if the iteration failed
*/
bool
forEachItemAfter(
    ReadView const& view,
    Keylet const& root,
    uint256 const& after,
    std::uint64_t const hint,
    unsigned int limit,
    std::function<bool(std::shared_ptr<SLE const> const&)> const& f);

/** Iterate all items in an account's owner directory. */
inline void
forEachItem(
    ReadView const& view,
    AccountID const& id,
    std::function<void(std::shared_ptr<SLE const> const&)> const& f)
{
    return forEachItem(view, keylet::ownerDir(id), f);
}

/** Iterate all items after an item in an owner directory.
    @param after The key of the item to start after
    @param hint The directory page containing `after`
    @param limit The maximum number of items to return
    @return `false` if the iteration failed
*/
inline bool
forEachItemAfter(
    ReadView const& view,
    AccountID const& id,
    uint256 const& after,
    std::uint64_t const hint,
    unsigned int limit,
    std::function<bool(std::shared_ptr<SLE const> const&)> const& f)
{
    return forEachItemAfter(view, keylet::ownerDir(id), after, hint, limit, f);
}

/** Returns IOU issuer transfer fee as Rate. Rate specifies
 * the fee as fractions of 1 billion. For example, 1% transfer rate
 * is represented as 1,010,000,000.
 * @param issuer The IOU issuer
 */
[[nodiscard]] Rate
transferRate(ReadView const& view, AccountID const& issuer);

/** Returns MPT transfer fee as Rate. Rate specifies
 * the fee as fractions of 1 billion. For example, 1% transfer rate
 * is represented as 1,010,000,000.
 * @param issuanceID MPTokenIssuanceID of MPTTokenIssuance object
 */
[[nodiscard]] Rate
transferRate(ReadView const& view, MPTID const& issuanceID);

/** Returns the transfer fee as Rate based on the type of token
 * @param view The ledger view
 * @param amount The amount to transfer
 */
[[nodiscard]] Rate
transferRate(ReadView const& view, STAmount const& amount);

/** Returns `true` if the directory is empty
    @param key The key of the directory
*/
[[nodiscard]] bool
dirIsEmpty(ReadView const& view, Keylet const& k);

// Return the list of enabled amendments
[[nodiscard]] std::set<uint256>
getEnabledAmendments(ReadView const& view);

// Return a map of amendments that have achieved majority
using majorityAmendments_t = std::map<uint256, NetClock::time_point>;
[[nodiscard]] majorityAmendments_t
getMajorityAmendments(ReadView const& view);

/** Return the hash of a ledger by sequence.
    The hash is retrieved by looking up the "skip list"
    in the passed ledger. As the skip list is limited
    in size, if the requested ledger sequence number is
    out of the range of ledgers represented in the skip
    list, then std::nullopt is returned.
    @return The hash of the ledger with the
            given sequence number or std::nullopt.
*/
[[nodiscard]] std::optional<uint256>
hashOfSeq(ReadView const& ledger, LedgerIndex seq, beast::Journal journal);

/** Find a ledger index from which we could easily get the requested ledger

    The index that we return should meet two requirements:
        1) It must be the index of a ledger that has the hash of the ledger
            we are looking for. This means that its sequence must be equal to
            greater than the sequence that we want but not more than 256 greater
            since each ledger contains the hashes of the 256 previous ledgers.

        2) Its hash must be easy for us to find. This means it must be 0 mod 256
            because every such ledger is permanently enshrined in a LedgerHashes
            page which we can easily retrieve via the skip list.
*/
inline LedgerIndex
getCandidateLedger(LedgerIndex requested)
{
    return (requested + 255) & (~255);
}

/** Return false if the test ledger is provably incompatible
    with the valid ledger, that is, they could not possibly
    both be valid. Use the first form if you have both ledgers,
    use the second form if you have not acquired the valid ledger yet
*/
[[nodiscard]] bool
areCompatible(
    ReadView const& validLedger,
    ReadView const& testLedger,
    beast::Journal::Stream& s,
    char const* reason);

[[nodiscard]] bool
areCompatible(
    uint256 const& validHash,
    LedgerIndex validIndex,
    ReadView const& testLedger,
    beast::Journal::Stream& s,
    char const* reason);

//------------------------------------------------------------------------------
//
// Modifiers
//
//------------------------------------------------------------------------------

/** Adjust the owner count up or down. */
void
adjustOwnerCount(
    ApplyView& view,
    std::shared_ptr<SLE> const& sle,
    std::int32_t amount,
    beast::Journal j);

/** @{ */
/** Returns the first entry in the directory, advancing the index

    @deprecated These are legacy function that are considered deprecated
                and will soon be replaced with an iterator-based model
                that is easier to use. You should not use them in new code.

    @param view The view against which to operate
    @param root The root (i.e. first page) of the directory to iterate
    @param page The current page
    @param index The index inside the current page
    @param entry The entry at the current index

    @return true if the directory isn't empty; false otherwise
 */
bool
cdirFirst(
    ReadView const& view,
    uint256 const& root,
    std::shared_ptr<SLE const>& page,
    unsigned int& index,
    uint256& entry);

bool
dirFirst(
    ApplyView& view,
    uint256 const& root,
    std::shared_ptr<SLE>& page,
    unsigned int& index,
    uint256& entry);
/** @} */

/** @{ */
/** Returns the next entry in the directory, advancing the index

    @deprecated These are legacy function that are considered deprecated
                and will soon be replaced with an iterator-based model
                that is easier to use. You should not use them in new code.

    @param view The view against which to operate
    @param root The root (i.e. first page) of the directory to iterate
    @param page The current page
    @param index The index inside the current page
    @param entry The entry at the current index

    @return true if the directory isn't empty; false otherwise
 */
bool
cdirNext(
    ReadView const& view,
    uint256 const& root,
    std::shared_ptr<SLE const>& page,
    unsigned int& index,
    uint256& entry);

bool
dirNext(
    ApplyView& view,
    uint256 const& root,
    std::shared_ptr<SLE>& page,
    unsigned int& index,
    uint256& entry);
/** @} */

[[nodiscard]] std::function<void(SLE::ref)>
describeOwnerDir(AccountID const& account);

[[nodiscard]] TER
dirLink(
    ApplyView& view,
    AccountID const& owner,
    std::shared_ptr<SLE>& object,
    SF_UINT64 const& node = sfOwnerNode);

AccountID
pseudoAccountAddress(ReadView const& view, uint256 const& pseudoOwnerKey);

/**
 *
 * Create pseudo-account, storing pseudoOwnerKey into ownerField.
 *
 * The list of valid ownerField is maintained in View.cpp and the caller to
 * this function must perform necessary amendment check(s) before using a
 * field. The amendment check is **not** performed in createPseudoAccount.
 */
[[nodiscard]] Expected<std::shared_ptr<SLE>, TER>
createPseudoAccount(
    ApplyView& view,
    uint256 const& pseudoOwnerKey,
    SField const& ownerField);

// Returns true iff sleAcct is a pseudo-account or specific
// pseudo-accounts in pseudoFieldFilter.
//
// Returns false if sleAcct is
// * NOT a pseudo-account OR
// * NOT a ltACCOUNT_ROOT OR
// * null pointer
[[nodiscard]] bool
isPseudoAccount(
    std::shared_ptr<SLE const> sleAcct,
    std::set<SField const*> const& pseudoFieldFilter = {});

// Returns the list of fields that define an ACCOUNT_ROOT as a pseudo-account if
// set
// Pseudo-account designator fields MUST be maintained by including the
// SField::sMD_PseudoAccount flag in the SField definition. (Don't forget to
// "| SField::sMD_Default"!) The fields do NOT need to be amendment-gated,
// since a non-active amendment will not set any field, by definition.
// Specific properties of a pseudo-account are NOT checked here, that's what
// InvariantCheck is for.
[[nodiscard]] std::vector<SField const*> const&
getPseudoAccountFields();

[[nodiscard]] inline bool
isPseudoAccount(
    ReadView const& view,
    AccountID const& accountId,
    std::set<SField const*> const& pseudoFieldFilter = {})
{
    return isPseudoAccount(
        view.read(keylet::account(accountId)), pseudoFieldFilter);
}

[[nodiscard]] TER
canAddHolding(ReadView const& view, Asset const& asset);

/** Validates that the destination SLE and tag are valid

   - Checks that the SLE is not null.
   - If the SLE requires a destination tag, checks that there is a tag.
*/
[[nodiscard]] TER
checkDestinationAndTag(SLE::const_ref toSle, bool hasDestinationTag);

/** Checks that can withdraw funds from an object to itself or a destination.
 *
 * The receiver may be either the submitting account (sfAccount) or a different
 * destination account (sfDestination).
 *
 *    - Checks that the receiver account exists.
 *    - If the receiver requires a destination tag, check that one exists, even
 *      if withdrawing to self.
 *    - If withdrawing to self, succeed.
 *    - If not, checks if the receiver requires deposit authorization, and if
 *      the sender has it.
 */
[[nodiscard]] TER
canWithdraw(
    AccountID const& from,
    ReadView const& view,
    AccountID const& to,
    SLE::const_ref toSle,
    bool hasDestinationTag);

/** Checks that can withdraw funds from an object to itself or a destination.
 *
 * The receiver may be either the submitting account (sfAccount) or a different
 * destination account (sfDestination).
 *
 *    - Checks that the receiver account exists.
 *    - If the receiver requires a destination tag, check that one exists, even
 *      if withdrawing to self.
 *    - If withdrawing to self, succeed.
 *    - If not, checks if the receiver requires deposit authorization, and if
 *      the sender has it.
 */
[[nodiscard]] TER
canWithdraw(
    AccountID const& from,
    ReadView const& view,
    AccountID const& to,
    bool hasDestinationTag);

/** Checks that can withdraw funds from an object to itself or a destination.
 *
 * The receiver may be either the submitting account (sfAccount) or a different
 * destination account (sfDestination).
 *
 *    - Checks that the receiver account exists.
 *    - If the receiver requires a destination tag, check that one exists, even
 *      if withdrawing to self.
 *    - If withdrawing to self, succeed.
 *    - If not, checks if the receiver requires deposit authorization, and if
 *      the sender has it.
 */
[[nodiscard]] TER
canWithdraw(ReadView const& view, STTx const& tx);

[[nodiscard]] TER
doWithdraw(
    ApplyView& view,
    STTx const& tx,
    AccountID const& senderAcct,
    AccountID const& dstAcct,
    AccountID const& sourceAcct,
    XRPAmount priorBalance,
    STAmount const& amount,
    beast::Journal j);

/// Any transactors that call addEmptyHolding() in doApply must call
/// canAddHolding() in preflight with the same View and Asset
[[nodiscard]] TER
addEmptyHolding(
    ApplyView& view,
    AccountID const& accountID,
    XRPAmount priorBalance,
    Issue const& issue,
    beast::Journal journal);

[[nodiscard]] TER
addEmptyHolding(
    ApplyView& view,
    AccountID const& accountID,
    XRPAmount priorBalance,
    MPTIssue const& mptIssue,
    beast::Journal journal);

[[nodiscard]] inline TER
addEmptyHolding(
    ApplyView& view,
    AccountID const& accountID,
    XRPAmount priorBalance,
    Asset const& asset,
    beast::Journal journal)
{
    return std::visit(
        [&]<ValidIssueType TIss>(TIss const& issue) -> TER {
            return addEmptyHolding(
                view, accountID, priorBalance, issue, journal);
        },
        asset.value());
}

[[nodiscard]] TER
authorizeMPToken(
    ApplyView& view,
    XRPAmount const& priorBalance,
    MPTID const& mptIssuanceID,
    AccountID const& account,
    beast::Journal journal,
    std::uint32_t flags = 0,
    std::optional<AccountID> holderID = std::nullopt);

// VFALCO NOTE Both STAmount parameters should just
//             be "Amount", a unit-less number.
//
/** Create a trust line

    This can set an initial balance.
*/
[[nodiscard]] TER
trustCreate(
    ApplyView& view,
    bool const bSrcHigh,
    AccountID const& uSrcAccountID,
    AccountID const& uDstAccountID,
    uint256 const& uIndex,      // --> ripple state entry
    SLE::ref sleAccount,        // --> the account being set.
    bool const bAuth,           // --> authorize account.
    bool const bNoRipple,       // --> others cannot ripple through
    bool const bFreeze,         // --> funds cannot leave
    bool bDeepFreeze,           // --> can neither receive nor send funds
    STAmount const& saBalance,  // --> balance of account being set.
                                // Issuer should be noAccount()
    STAmount const& saLimit,    // --> limit for account being set.
                                // Issuer should be the account being set.
    std::uint32_t uSrcQualityIn,
    std::uint32_t uSrcQualityOut,
    beast::Journal j);

[[nodiscard]] TER
removeEmptyHolding(
    ApplyView& view,
    AccountID const& accountID,
    Issue const& issue,
    beast::Journal journal);

[[nodiscard]] TER
removeEmptyHolding(
    ApplyView& view,
    AccountID const& accountID,
    MPTIssue const& mptIssue,
    beast::Journal journal);

[[nodiscard]] inline TER
removeEmptyHolding(
    ApplyView& view,
    AccountID const& accountID,
    Asset const& asset,
    beast::Journal journal)
{
    return std::visit(
        [&]<ValidIssueType TIss>(TIss const& issue) -> TER {
            return removeEmptyHolding(view, accountID, issue, journal);
        },
        asset.value());
}

[[nodiscard]] TER
trustDelete(
    ApplyView& view,
    std::shared_ptr<SLE> const& sleRippleState,
    AccountID const& uLowAccountID,
    AccountID const& uHighAccountID,
    beast::Journal j);

/** Delete an offer.

    Requirements:
        The passed `sle` be obtained from a prior
        call to view.peek()
*/
// [[nodiscard]] // nodiscard commented out so Flow, BookTip and others compile.
TER
offerDelete(ApplyView& view, std::shared_ptr<SLE> const& sle, beast::Journal j);

//------------------------------------------------------------------------------

//
// Money Transfers
//

// Direct send w/o fees:
// - Redeeming IOUs and/or sending sender's own IOUs.
// - Create trust line of needed.
// --> bCheckIssuer : normally require issuer to be involved.
// [[nodiscard]] // nodiscard commented out so DirectStep.cpp compiles.

/** Calls static rippleCreditIOU if saAmount represents Issue.
 * Calls static rippleCreditMPT if saAmount represents MPTIssue.
 */
TER
rippleCredit(
    ApplyView& view,
    AccountID const& uSenderID,
    AccountID const& uReceiverID,
    STAmount const& saAmount,
    bool bCheckIssuer,
    beast::Journal j);

TER
rippleLockEscrowMPT(
    ApplyView& view,
    AccountID const& uGrantorID,
    STAmount const& saAmount,
    beast::Journal j);

TER
rippleUnlockEscrowMPT(
    ApplyView& view,
    AccountID const& uGrantorID,
    AccountID const& uGranteeID,
    STAmount const& netAmount,
    STAmount const& grossAmount,
    beast::Journal j);

/** Calls static accountSendIOU if saAmount represents Issue.
 * Calls static accountSendMPT if saAmount represents MPTIssue.
 */
[[nodiscard]] TER
accountSend(
    ApplyView& view,
    AccountID const& from,
    AccountID const& to,
    STAmount const& saAmount,
    beast::Journal j,
    WaiveTransferFee waiveFee = WaiveTransferFee::No);

using MultiplePaymentDestinations = std::vector<std::pair<AccountID, Number>>;
/** Like accountSend, except one account is sending multiple payments (with the
 *  same asset!) simultaneously
 *
 * Calls static accountSendMultiIOU if saAmount represents Issue.
 * Calls static accountSendMultiMPT if saAmount represents MPTIssue.
 */
[[nodiscard]] TER
accountSendMulti(
    ApplyView& view,
    AccountID const& senderID,
    Asset const& asset,
    MultiplePaymentDestinations const& receivers,
    beast::Journal j,
    WaiveTransferFee waiveFee = WaiveTransferFee::No);

[[nodiscard]] TER
issueIOU(
    ApplyView& view,
    AccountID const& account,
    STAmount const& amount,
    Issue const& issue,
    beast::Journal j);

[[nodiscard]] TER
redeemIOU(
    ApplyView& view,
    AccountID const& account,
    STAmount const& amount,
    Issue const& issue,
    beast::Journal j);

[[nodiscard]] TER
transferXRP(
    ApplyView& view,
    AccountID const& from,
    AccountID const& to,
    STAmount const& amount,
    beast::Journal j);

/* Check if MPToken (for MPT) or trust line (for IOU) exists:
 * - StrongAuth - before checking if authorization is required
 * - WeakAuth
 *    for MPT - after checking lsfMPTRequireAuth flag
 *    for IOU - do not check if trust line exists
 * - Legacy
 *    for MPT - before checking lsfMPTRequireAuth flag i.e. same as StrongAuth
 *    for IOU - do not check if trust line exists i.e. same as WeakAuth
 */
enum class AuthType { StrongAuth, WeakAuth, Legacy };

/** Check if the account lacks required authorization.
 *
 * Return tecNO_AUTH or tecNO_LINE if it does
 * and tesSUCCESS otherwise.
 *
 * If StrongAuth then return tecNO_LINE if the RippleState doesn't exist. Return
 * tecNO_AUTH if lsfRequireAuth is set on the issuer's AccountRoot, and the
 * RippleState does exist, and the RippleState is not authorized.
 *
 * If WeakAuth then return tecNO_AUTH if lsfRequireAuth is set, and the
 * RippleState exists, and is not authorized. Return tecNO_LINE if
 * lsfRequireAuth is set and the RippleState doesn't exist. Consequently, if
 * WeakAuth and lsfRequireAuth is *not* set, this function will return
 * tesSUCCESS even if RippleState does *not* exist.
 *
 * The default "Legacy" auth type is equivalent to WeakAuth.
 */
[[nodiscard]] TER
requireAuth(
    ReadView const& view,
    Issue const& issue,
    AccountID const& account,
    AuthType authType = AuthType::Legacy);

/** Check if the account lacks required authorization.
 *
 * This will also check for expired credentials. If it is called directly
 * from preclaim, the user should convert result tecEXPIRED to tesSUCCESS and
 * proceed to also check permissions with enforceMPTokenAuthorization inside
 * doApply. This will ensure that any expired credentials are deleted.
 *
 * requireAuth check is recursive for MPT shares in a vault, descending to
 * assets in the vault, up to maxAssetCheckDepth recursion depth. This is
 * purely defensive, as we currently do not allow such vaults to be created.
 *
 * If StrongAuth then return tecNO_AUTH if MPToken doesn't exist or
 * lsfMPTRequireAuth is set and MPToken is not authorized. Vault and LoanBroker
 * pseudo-accounts are implicitly authorized.
 *
 * If WeakAuth then return tecNO_AUTH if lsfMPTRequireAuth is set and MPToken
 * doesn't exist or is not authorized (explicitly or via credentials, if
 * DomainID is set in MPTokenIssuance). Consequently, if WeakAuth and
 * lsfMPTRequireAuth is *not* set, this function will return true even if
 * MPToken does *not* exist.
 *
 * The default "Legacy" auth type is equivalent to StrongAuth.
 */
[[nodiscard]] TER
requireAuth(
    ReadView const& view,
    MPTIssue const& mptIssue,
    AccountID const& account,
    AuthType authType = AuthType::Legacy,
    int depth = 0);

[[nodiscard]] TER inline requireAuth(
    ReadView const& view,
    Asset const& asset,
    AccountID const& account,
    AuthType authType = AuthType::Legacy)
{
    return std::visit(
        [&]<ValidIssueType TIss>(TIss const& issue_) {
            return requireAuth(view, issue_, account, authType);
        },
        asset.value());
}

/** Enforce account has MPToken to match its authorization.
 *
 *   Called from doApply - it will check for expired (and delete if found any)
 *   credentials matching DomainID set in MPTokenIssuance. Must be called if
 *   requireAuth(...MPTIssue...) returned tesSUCCESS or tecEXPIRED in preclaim,
 *   which implies that preclaim should replace `tecEXPIRED` with `tesSUCCESS`
 *   in order for the transactor to proceed to doApply.
 *
 *   This function will create MPToken (if needed) on the basis of any
 *   non-expired credentials and will delete any expired credentials, indirectly
 *   via verifyValidDomain, as per DomainID (if set in MPTokenIssuance).
 *
 *   The caller does NOT need to ensure that DomainID is actually set - this
 *   function handles gracefully both cases when DomainID is set and when not.
 *
 *   The caller does NOT need to look for existing MPToken to match
 *   mptIssue/account - this function checks lsfMPTAuthorized of an existing
 *   MPToken iff DomainID is not set.
 *
 *   Do not use for accounts which hold implied permission e.g. object owners or
 *   if MPTokenIssuance does not require authorization. In both cases use
 *   MPTokenAuthorize::authorize if MPToken does not yet exist.
 */
[[nodiscard]] TER
enforceMPTokenAuthorization(
    ApplyView& view,
    MPTID const& mptIssuanceID,
    AccountID const& account,
    XRPAmount const& priorBalance,
    beast::Journal j);

/** Check if the destination account is allowed
 *  to receive MPT. Return tecNO_AUTH if it doesn't
 *  and tesSUCCESS otherwise.
 */
[[nodiscard]] TER
canTransfer(
    ReadView const& view,
    MPTIssue const& mptIssue,
    AccountID const& from,
    AccountID const& to);

[[nodiscard]] TER
canTransfer(
    ReadView const& view,
    Issue const& issue,
    AccountID const& from,
    AccountID const& to);

[[nodiscard]] TER inline canTransfer(
    ReadView const& view,
    Asset const& asset,
    AccountID const& from,
    AccountID const& to)
{
    return std::visit(
        [&]<ValidIssueType TIss>(TIss const& issue) -> TER {
            return canTransfer(view, issue, from, to);
        },
        asset.value());
}

/** Deleter function prototype. Returns the status of the entry deletion
 * (if should not be skipped) and if the entry should be skipped. The status
 * is always tesSUCCESS if the entry should be skipped.
 */
using EntryDeleter = std::function<std::pair<TER, SkipEntry>(
    LedgerEntryType,
    uint256 const&,
    std::shared_ptr<SLE>&)>;
/** Cleanup owner directory entries on account delete.
 * Used for a regular and AMM accounts deletion. The caller
 * has to provide the deleter function, which handles details of
 * specific account-owned object deletion.
 * @return tecINCOMPLETE indicates maxNodesToDelete
 * are deleted and there remains more nodes to delete.
 */
[[nodiscard]] TER
cleanupOnAccountDelete(
    ApplyView& view,
    Keylet const& ownerDirKeylet,
    EntryDeleter const& deleter,
    beast::Journal j,
    std::optional<std::uint16_t> maxNodesToDelete = std::nullopt);

/** Delete trustline to AMM. The passed `sle` must be obtained from a prior
 * call to view.peek(). Fail if neither side of the trustline is AMM or
 * if ammAccountID is seated and is not one of the trustline's side.
 */
[[nodiscard]] TER
deleteAMMTrustLine(
    ApplyView& view,
    std::shared_ptr<SLE> sleState,
    std::optional<AccountID> const& ammAccountID,
    beast::Journal j);

// From the perspective of a vault, return the number of shares to give the
// depositor when they deposit a fixed amount of assets. Since shares are MPT
// this number is integral and always truncated in this calculation.
[[nodiscard]] std::optional<STAmount>
assetsToSharesDeposit(
    std::shared_ptr<SLE const> const& vault,
    std::shared_ptr<SLE const> const& issuance,
    STAmount const& assets);

// From the perspective of a vault, return the number of assets to take from
// depositor when they receive a fixed amount of shares. Note, since shares are
// MPT, they are always an integral number.
[[nodiscard]] std::optional<STAmount>
sharesToAssetsDeposit(
    std::shared_ptr<SLE const> const& vault,
    std::shared_ptr<SLE const> const& issuance,
    STAmount const& shares);

enum class TruncateShares : bool { no = false, yes = true };

// From the perspective of a vault, return the number of shares to demand from
// the depositor when they ask to withdraw a fixed amount of assets. Since
// shares are MPT this number is integral, and it will be rounded to nearest
// unless explicitly requested to be truncated instead.
[[nodiscard]] std::optional<STAmount>
assetsToSharesWithdraw(
    std::shared_ptr<SLE const> const& vault,
    std::shared_ptr<SLE const> const& issuance,
    STAmount const& assets,
    TruncateShares truncate = TruncateShares::no);

// From the perspective of a vault, return the number of assets to give the
// depositor when they redeem a fixed amount of shares. Note, since shares are
// MPT, they are always an integral number.
[[nodiscard]] std::optional<STAmount>
sharesToAssetsWithdraw(
    std::shared_ptr<SLE const> const& vault,
    std::shared_ptr<SLE const> const& issuance,
    STAmount const& shares);

/** Has the specified time passed?

    @param now  the current time
    @param mark the cutoff point
    @return true if \a now refers to a time strictly after \a mark, else false.
*/
bool
after(NetClock::time_point now, std::uint32_t mark);

}  // namespace ripple

#endif
