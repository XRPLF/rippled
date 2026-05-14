/** @file
 *  Unified token-dispatch layer for all non-XRP ledger token operations.
 *
 *  This file implements the asset-agnostic public API declared in
 *  `TokenHelpers.h`. Every public function accepts an `Asset`
 *  (`std::variant<Issue, MPTIssue>`) and routes — via `std::visit` or
 *  `Asset::visit` — to the appropriate type-specific leaf:
 *  `RippleStateHelpers` for IOU trust lines or `MPTokenHelpers` for
 *  `MPToken`/`MPTokenIssuance` SLEs.
 *
 *  The file is organized into four sections mirroring the header:
 *  1. **Freeze checking** — global, individual, deep-freeze queries and TER
 *     conversion.
 *  2. **Balance queries** — `accountHolds`, `accountFunds`, `transferRate`.
 *  3. **Holding lifecycle** — `canAddHolding`, `addEmptyHolding`,
 *     `removeEmptyHolding`.
 *  4. **Money transfers** — `directSendNoFee`, `accountSend`,
 *     `accountSendMulti`, `transferXRP`, and the static IOU/MPT helpers they
 *     delegate to.
 *
 *  Higher-level code (payments, offers, AMM, vault operations) calls these
 *  functions so it never has to branch on token type itself.
 */
#include <xrpl/ledger/helpers/TokenHelpers.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/MPTokenHelpers.h>
#include <xrpl/ledger/helpers/RippleStateHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Concepts.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/Rate.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/XRPAmount.h>

#include <cstdint>
#include <initializer_list>
#include <string>
#include <variant>

namespace xrpl {

// Forward declaration for function that remains in View.h/cpp
bool
isLPTokenFrozen(
    ReadView const& view,
    AccountID const& account,
    Asset const& asset,
    Asset const& asset2);

//------------------------------------------------------------------------------
//
// Freeze checking (Asset-based)
//
//------------------------------------------------------------------------------

bool
isGlobalFrozen(ReadView const& view, Asset const& asset)
{
    return asset.visit(
        [&](Issue const& issue) { return isGlobalFrozen(view, issue.getIssuer()); },
        [&](MPTIssue const& issue) { return isGlobalFrozen(view, issue); });
}

bool
isIndividualFrozen(ReadView const& view, AccountID const& account, Asset const& asset)
{
    return std::visit(
        [&](auto const& issue) { return isIndividualFrozen(view, account, issue); }, asset.value());
}

bool
isFrozen(ReadView const& view, AccountID const& account, Asset const& asset, int depth)
{
    return std::visit(
        [&](auto const& issue) { return isFrozen(view, account, issue, depth); }, asset.value());
}

TER
checkFrozen(ReadView const& view, AccountID const& account, Issue const& issue)
{
    return isFrozen(view, account, issue) ? (TER)tecFROZEN : (TER)tesSUCCESS;
}

TER
checkFrozen(ReadView const& view, AccountID const& account, MPTIssue const& mptIssue)
{
    return isFrozen(view, account, mptIssue) ? (TER)tecLOCKED : (TER)tesSUCCESS;
}

TER
checkFrozen(ReadView const& view, AccountID const& account, Asset const& asset)
{
    return std::visit(
        [&](auto const& issue) { return checkFrozen(view, account, issue); }, asset.value());
}

bool
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

bool
isAnyFrozen(
    ReadView const& view,
    std::initializer_list<AccountID> const& accounts,
    Asset const& asset,
    int depth)
{
    return asset.visit(
        [&](Issue const& issue) { return isAnyFrozen(view, accounts, issue); },
        [&](MPTIssue const& issue) { return isAnyFrozen(view, accounts, issue, depth); });
}

bool
isDeepFrozen(ReadView const& view, AccountID const& account, MPTIssue const& mptIssue, int depth)
{
    // Unlike IOUs, frozen / locked MPTs are not allowed to send or receive
    // funds, so checking "deep frozen" is the same as checking "frozen".
    return isFrozen(view, account, mptIssue, depth);
}

bool
isDeepFrozen(ReadView const& view, AccountID const& account, Asset const& asset, int depth)
{
    return std::visit(
        [&](auto const& issue) { return isDeepFrozen(view, account, issue, depth); },
        asset.value());
}

TER
checkDeepFrozen(ReadView const& view, AccountID const& account, MPTIssue const& mptIssue)
{
    return isDeepFrozen(view, account, mptIssue) ? (TER)tecLOCKED : (TER)tesSUCCESS;
}

TER
checkDeepFrozen(ReadView const& view, AccountID const& account, Asset const& asset)
{
    return std::visit(
        [&](auto const& issue) { return checkDeepFrozen(view, account, issue); }, asset.value());
}

//------------------------------------------------------------------------------
//
// Account balance functions
//
//------------------------------------------------------------------------------

/** Look up an IOU trust line and return it only if the account may use it.
 *
 *  Returns `nullptr` (treat as zero balance) in any of these cases:
 *  - The trust line SLE does not exist.
 *  - `zeroIfFrozen == ZeroIfFrozen` and the line is individually or
 *    deep-frozen.
 *  - `zeroIfFrozen == ZeroIfFrozen`, `fixFrozenLPTokenTransfer` is enabled,
 *    the `issuer` is an AMM account (`sfAMMID` present on their
 *    `AccountRoot`), and either the AMM SLE is missing or one of the AMM's
 *    pool assets is frozen via `isLPTokenFrozen`. This check is a retrofit:
 *    earlier ledger versions allowed LP-token transfers even when the
 *    underlying pool assets were frozen.
 *
 *  @param view          Read-only ledger view.
 *  @param account       The account whose trust-line balance is needed.
 *  @param currency      The IOU currency.
 *  @param issuer        The IOU issuer (low or high side resolved by keylet).
 *  @param zeroIfFrozen  Whether to suppress the SLE when the line is frozen.
 *  @param j             Journal for trace logging.
 *  @return The trust-line SLE if usable, `nullptr` otherwise.
 */
static SLE::const_pointer
getLineIfUsable(
    ReadView const& view,
    AccountID const& account,
    Currency const& currency,
    AccountID const& issuer,
    FreezeHandling zeroIfFrozen,
    beast::Journal j)
{
    auto sle = view.read(keylet::line(account, issuer, currency));

    if (!sle)
    {
        return nullptr;
    }

    if (zeroIfFrozen == FreezeHandling::ZeroIfFrozen)
    {
        if (isFrozen(view, account, currency, issuer) ||
            isDeepFrozen(view, account, currency, issuer))
        {
            return nullptr;
        }

        // fixFrozenLPTokenTransfer: if the issuer is an AMM account, also
        // verify that neither of the AMM's underlying pool assets is frozen.
        if (view.rules().enabled(fixFrozenLPTokenTransfer))
        {
            auto const sleIssuer = view.read(keylet::account(issuer));
            if (!sleIssuer)
            {
                return nullptr;  // LCOV_EXCL_LINE
            }
            if (sleIssuer->isFieldPresent(sfAMMID))
            {
                auto const sleAmm = view.read(keylet::amm((*sleIssuer)[sfAMMID]));

                if (!sleAmm ||
                    isLPTokenFrozen(view, account, (*sleAmm)[sfAsset], (*sleAmm)[sfAsset2]))
                {
                    return nullptr;
                }
            }
        }
    }

    return sle;
}

/** Compute an account-centric IOU trust-line balance from a raw SLE.
 *
 *  Converts the ledger-internal `sfBalance` — which is always stored from the
 *  low-account's perspective — into the caller's account-centric view.
 *  If `includeOppositeLimit` is true, adds the *peer's* credit limit so the
 *  result represents the full spendable amount (the account can draw down the
 *  peer's limit). Returns zero cleared to the correct `Issue` when @p sle is
 *  null.
 *
 *  The result is passed through `view.balanceHookIOU` so that
 *  `PaymentSandbox` can intercept it for deferred-credit accounting.
 *
 *  @param view                 Read-only ledger view (for `balanceHookIOU`).
 *  @param sle                  Trust-line SLE, or null for a non-existent line.
 *  @param account              The account whose perspective is taken.
 *  @param currency             The IOU currency (used when @p sle is null).
 *  @param issuer               The IOU issuer.
 *  @param includeOppositeLimit Whether to add the peer's credit limit
 *      (`shFULL_BALANCE` semantics).
 *  @param j                    Journal for trace logging.
 *  @return Balance from @p account's perspective, possibly with borrowed
 *      credit added. May be negative if the account owes the issuer.
 */
static STAmount
getTrustLineBalance(
    ReadView const& view,
    SLE::const_ref sle,
    AccountID const& account,
    Currency const& currency,
    AccountID const& issuer,
    bool includeOppositeLimit,
    beast::Journal j)
{
    STAmount amount;
    if (sle)
    {
        amount = sle->getFieldAmount(sfBalance);
        bool const accountHigh = account > issuer;
        auto const& oppositeField = accountHigh ? sfLowLimit : sfHighLimit;
        if (accountHigh)
        {
            // Trust-line orientation: sfBalance is stored low-account-side;
            // negate to convert to the high-account (sender) perspective.
            amount.negate();
        }
        if (includeOppositeLimit)
        {
            amount += sle->getFieldAmount(oppositeField);
        }
        amount.get<Issue>().account = issuer;
    }
    else
    {
        amount.clear(Issue{currency, issuer});
    }

    JLOG(j.trace()) << "getTrustLineBalance:" << " account=" << to_string(account)
                    << " amount=" << amount.getFullText();

    return view.balanceHookIOU(account, issuer, amount);
}

STAmount
accountHolds(
    ReadView const& view,
    AccountID const& account,
    Currency const& currency,
    AccountID const& issuer,
    FreezeHandling zeroIfFrozen,
    beast::Journal j,
    SpendableHandling includeFullBalance)
{
    STAmount const amount;
    if (isXRP(currency))
    {
        return {xrpLiquid(view, account, 0, j)};
    }

    bool const returnSpendable = (includeFullBalance == SpendableHandling::FullBalance);
    if (returnSpendable && account == issuer)
    {
        // If the account is the issuer, then their limit is effectively
        // infinite
        return STAmount{Issue{currency, issuer}, STAmount::kMAX_VALUE, STAmount::kMAX_OFFSET};
    }

    // IOU: Return balance on trust line modulo freeze
    SLE::const_pointer const sle =
        getLineIfUsable(view, account, currency, issuer, zeroIfFrozen, j);

    return getTrustLineBalance(view, sle, account, currency, issuer, returnSpendable, j);
}

STAmount
accountHolds(
    ReadView const& view,
    AccountID const& account,
    Issue const& issue,
    FreezeHandling zeroIfFrozen,
    beast::Journal j,
    SpendableHandling includeFullBalance)
{
    return accountHolds(
        view, account, issue.currency, issue.account, zeroIfFrozen, j, includeFullBalance);
}

STAmount
accountHolds(
    ReadView const& view,
    AccountID const& account,
    MPTIssue const& mptIssue,
    FreezeHandling zeroIfFrozen,
    AuthHandling zeroIfUnauthorized,
    beast::Journal j,
    SpendableHandling includeFullBalance)
{
    bool const returnSpendable = (includeFullBalance == SpendableHandling::FullBalance);
    STAmount amount{mptIssue};
    auto const& issuer = mptIssue.getIssuer();
    bool const mptokensV2 = view.rules().enabled(featureMPTokensV2);

    if (returnSpendable && account == mptIssue.getIssuer())
    {
        // if the account is the issuer, and the issuance exists, their limit is
        // the issuance limit minus the outstanding value
        auto const issuance = view.read(keylet::mptIssuance(mptIssue.getMptID()));

        if (!issuance)
        {
            return amount;
        }
        auto const available = availableMPTAmount(*issuance);
        if (!mptokensV2)
            return STAmount{mptIssue, available};
        return view.balanceHookMPT(issuer, mptIssue, available);
    }

    auto const sleMpt = view.read(keylet::mptoken(mptIssue.getMptID(), account));

    if (!sleMpt)
    {
        amount.clear(mptIssue);
    }
    else if (zeroIfFrozen == FreezeHandling::ZeroIfFrozen && isFrozen(view, account, mptIssue))
    {
        amount.clear(mptIssue);
    }
    else
    {
        amount = STAmount{mptIssue, sleMpt->getFieldU64(sfMPTAmount)};

        // Only if auth check is needed, as it needs to do an additional read
        // operation. Note featureSingleAssetVault will affect error codes.
        if (zeroIfUnauthorized == AuthHandling::ZeroIfUnauthorized &&
            view.rules().enabled(featureSingleAssetVault))
        {
            if (auto const err = requireAuth(view, mptIssue, account, AuthType::StrongAuth);
                !isTesSuccess(err))
                amount.clear(mptIssue);
        }
        else if (zeroIfUnauthorized == AuthHandling::ZeroIfUnauthorized)
        {
            auto const sleIssuance = view.read(keylet::mptIssuance(mptIssue.getMptID()));

            // if auth is enabled on the issuance and mpt is not authorized,
            // clear amount
            if (sleIssuance && sleIssuance->isFlag(lsfMPTRequireAuth) &&
                !sleMpt->isFlag(lsfMPTAuthorized))
                amount.clear(mptIssue);
        }
    }

    if (view.rules().enabled(featureMPTokensV2))
        return view.balanceHookMPT(account, mptIssue, amount.mpt().value());
    return amount;
}

[[nodiscard]] STAmount
accountHolds(
    ReadView const& view,
    AccountID const& account,
    Asset const& asset,
    FreezeHandling zeroIfFrozen,
    AuthHandling zeroIfUnauthorized,
    beast::Journal j,
    SpendableHandling includeFullBalance)
{
    return asset.visit(
        [&](Issue const& issue) {
            return accountHolds(view, account, issue, zeroIfFrozen, j, includeFullBalance);
        },
        [&](MPTIssue const& issue) {
            return accountHolds(
                view, account, issue, zeroIfFrozen, zeroIfUnauthorized, j, includeFullBalance);
        });
}

STAmount
accountFunds(
    ReadView const& view,
    AccountID const& id,
    STAmount const& saDefault,
    FreezeHandling freezeHandling,
    beast::Journal j)
{
    XRPL_ASSERT(saDefault.holds<Issue>(), "xrpl::accountFunds: saDefault holds Issue");

    if (!saDefault.native() && saDefault.getIssuer() == id)
        return saDefault;

    return accountHolds(
        view, id, saDefault.get<Issue>().currency, saDefault.getIssuer(), freezeHandling, j);
}

STAmount
accountFunds(
    ReadView const& view,
    AccountID const& id,
    STAmount const& saDefault,
    FreezeHandling freezeHandling,
    AuthHandling authHandling,
    beast::Journal j)
{
    return saDefault.asset().visit(
        [&](Issue const&) { return accountFunds(view, id, saDefault, freezeHandling, j); },
        [&](MPTIssue const&) {
            return accountHolds(
                view,
                id,
                saDefault.asset(),
                freezeHandling,
                authHandling,
                j,
                SpendableHandling::FullBalance);
        });
}

Rate
transferRate(ReadView const& view, STAmount const& amount)
{
    return amount.asset().visit(
        [&](Issue const& issue) { return transferRate(view, issue.getIssuer()); },
        [&](MPTIssue const& issue) { return transferRate(view, issue.getMptID()); });
}

//------------------------------------------------------------------------------
//
// Holding operations
//
//------------------------------------------------------------------------------

/** Check whether a new IOU trust line can be created for @p issue.
 *
 *  XRP always returns `tesSUCCESS`. For IOU, the issuer's `AccountRoot` must
 *  exist and have `lsfDefaultRipple` set; without it, a new trust line would
 *  be stuck in a `noRipple` state that prevents payments from routing through
 *  it.
 *
 *  @param view   Read-only ledger view.
 *  @param issue  The IOU to check (XRP passes through unconditionally).
 *  @return `tesSUCCESS`, `terNO_ACCOUNT` if the issuer SLE is missing, or
 *      `terNO_RIPPLE` if the issuer has not enabled `lsfDefaultRipple`.
 */
[[nodiscard]] TER
canAddHolding(ReadView const& view, Issue const& issue)
{
    if (issue.native())
    {
        return tesSUCCESS;
    }

    auto const issuer = view.read(keylet::account(issue.getIssuer()));
    if (!issuer)
    {
        return terNO_ACCOUNT;
    }
    if (!issuer->isFlag(lsfDefaultRipple))
    {
        return terNO_RIPPLE;
    }

    return tesSUCCESS;
}

[[nodiscard]] TER
canAddHolding(ReadView const& view, Asset const& asset)
{
    return std::visit(
        [&]<ValidIssueType TIss>(TIss const& issue) -> TER { return canAddHolding(view, issue); },
        asset.value());
}

TER
addEmptyHolding(
    ApplyView& view,
    AccountID const& accountID,
    XRPAmount priorBalance,
    Asset const& asset,
    beast::Journal journal)
{
    return std::visit(
        [&]<ValidIssueType TIss>(TIss const& issue) -> TER {
            return addEmptyHolding(view, accountID, priorBalance, issue, journal);
        },
        asset.value());
}

TER
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

//------------------------------------------------------------------------------
//
// Authorization and transfer checks
//
//------------------------------------------------------------------------------

TER
requireAuth(ReadView const& view, Asset const& asset, AccountID const& account, AuthType authType)
{
    return std::visit(
        [&]<ValidIssueType TIss>(TIss const& issue) {
            return requireAuth(view, issue, account, authType);
        },
        asset.value());
}

TER
canTransfer(ReadView const& view, Asset const& asset, AccountID const& from, AccountID const& to)
{
    return std::visit(
        [&]<ValidIssueType TIss>(TIss const& issue) -> TER {
            return canTransfer(view, issue, from, to);
        },
        asset.value());
}

//------------------------------------------------------------------------------
//
// Money Transfers
//
//------------------------------------------------------------------------------

/** Adjust an IOU trust-line balance directly, bypassing limits and fees.
 *
 *  Handles two cases:
 *  - **Line exists**: adjusts `sfBalance` in the sender's direction. If after
 *    the adjustment the sender's balance crosses zero from positive to
 *    non-positive and the sender's side meets seven conditions (reserve set,
 *    no-ripple flag disagrees with `lsfDefaultRipple`, no freeze, zero trust
 *    limit, zero quality in/out), the function releases the sender's ledger
 *    reserve and clears the reserve flag. If the balance is then zero *and*
 *    the receiver's reserve flag is also clear, the trust line is deleted via
 *    `trustDelete`.
 *  - **Line does not exist**: creates a new trust line via `trustCreate` with
 *    the receiver's `noRipple` flag mirrored from their `lsfDefaultRipple`
 *    setting. This implicit creation only applies to direct (issuer-involved)
 *    sends.
 *
 *  @note The seven-condition delete path is acknowledged as complex
 *      ("FIXME…NEEDS to be cleaned up") in the source; it must not be changed
 *      without careful replay testing.
 *
 *  @param view          Mutable ledger view.
 *  @param uSenderID     Sending account; must not be XRP or `noAccount()`.
 *  @param uReceiverID   Receiving account; must not be XRP or `noAccount()`;
 *      must differ from @p uSenderID.
 *  @param saAmount      Amount to transfer; must carry an IOU asset.
 *  @param bCheckIssuer  If `true`, asserts that the issuer equals sender or
 *      receiver (disabled for recursive calls from `directSendNoLimitIOU`).
 *  @param j             Journal for trace/debug logging.
 *  @return `tesSUCCESS`, or a `tec`/`tef` from `trustCreate`/`trustDelete`.
 */
static TER
directSendNoFeeIOU(
    ApplyView& view,
    AccountID const& uSenderID,
    AccountID const& uReceiverID,
    STAmount const& saAmount,
    bool bCheckIssuer,
    beast::Journal j)
{
    AccountID const& issuer = saAmount.getIssuer();
    Currency const& currency = saAmount.get<Issue>().currency;

    XRPL_ASSERT(
        !bCheckIssuer || uSenderID == issuer || uReceiverID == issuer,
        "xrpl::directSendNoFeeIOU : matching issuer or don't care");
    (void)issuer;

    XRPL_ASSERT(uSenderID != uReceiverID, "xrpl::directSendNoFeeIOU : sender is not receiver");

    bool const bSenderHigh = uSenderID > uReceiverID;
    auto const index = keylet::line(uSenderID, uReceiverID, currency);

    XRPL_ASSERT(
        !isXRP(uSenderID) && uSenderID != noAccount(),
        "xrpl::directSendNoFeeIOU : sender is not XRP");
    XRPL_ASSERT(
        !isXRP(uReceiverID) && uReceiverID != noAccount(),
        "xrpl::directSendNoFeeIOU : receiver is not XRP");

    if (auto const sleRippleState = view.peek(index))
    {
        STAmount saBalance = sleRippleState->getFieldAmount(sfBalance);

        if (bSenderHigh)
            saBalance.negate();  // Convert ledger-stored low-side balance to sender perspective.

        view.creditHookIOU(uSenderID, uReceiverID, saAmount, saBalance);

        STAmount const saBefore = saBalance;

        saBalance -= saAmount;

        JLOG(j.trace()) << "directSendNoFeeIOU: " << to_string(uSenderID) << " -> "
                        << to_string(uReceiverID) << " : before=" << saBefore.getFullText()
                        << " amount=" << saAmount.getFullText()
                        << " after=" << saBalance.getFullText();

        std::uint32_t const uFlags(sleRippleState->getFieldU32(sfFlags));
        bool bDelete = false;

        // FIXME This NEEDS to be cleaned up and simplified. It's impossible
        //       for anyone to understand.
        if (saBefore > beast::kZERO
            && saBalance <= beast::kZERO
            && ((uFlags & (!bSenderHigh ? lsfLowReserve : lsfHighReserve)) != 0u)
            && static_cast<bool>(uFlags & (!bSenderHigh ? lsfLowNoRipple : lsfHighNoRipple)) !=
                static_cast<bool>(
                    view.read(keylet::account(uSenderID))->getFlags() & lsfDefaultRipple) &&
            ((uFlags & (!bSenderHigh ? lsfLowFreeze : lsfHighFreeze)) == 0u) &&
            !sleRippleState->getFieldAmount(!bSenderHigh ? sfLowLimit : sfHighLimit)
            && (sleRippleState->getFieldU32(!bSenderHigh ? sfLowQualityIn : sfHighQualityIn) == 0u)
            &&
            (sleRippleState->getFieldU32(!bSenderHigh ? sfLowQualityOut : sfHighQualityOut) == 0u))
        {
            // All seven conditions met: release the sender's reserve.
            adjustOwnerCount(view, view.peek(keylet::account(uSenderID)), -1, j);

            sleRippleState->setFieldU32(
                sfFlags, uFlags & (!bSenderHigh ? ~lsfLowReserve : ~lsfHighReserve));

            // If balance is now zero and receiver holds no reserve either,
            // the line is eligible for deletion.
            bDelete = !saBalance
                && ((uFlags & (bSenderHigh ? lsfLowReserve : lsfHighReserve)) == 0u);
        }

        if (bSenderHigh)
            saBalance.negate();

        // Persist the new balance even when we are about to delete the line,
        // so the object is in a consistent state for trustDelete.
        sleRippleState->setFieldAmount(sfBalance, saBalance);

        if (bDelete)
        {
            return trustDelete(
                view,
                sleRippleState,
                bSenderHigh ? uReceiverID : uSenderID,
                !bSenderHigh ? uReceiverID : uSenderID,
                j);
        }

        view.update(sleRippleState);
        return tesSUCCESS;
    }

    STAmount const saReceiverLimit(Issue{currency, uReceiverID});
    STAmount saBalance{saAmount};

    saBalance.get<Issue>().account = noAccount();

    JLOG(j.debug()) << "directSendNoFeeIOU: "
                       "create line: "
                    << to_string(uSenderID) << " -> " << to_string(uReceiverID) << " : "
                    << saAmount.getFullText();

    auto const sleAccount = view.peek(keylet::account(uReceiverID));
    if (!sleAccount)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    bool const noRipple = (sleAccount->getFlags() & lsfDefaultRipple) == 0;

    return trustCreate(
        view,
        bSenderHigh,
        uSenderID,
        uReceiverID,
        index.key,
        sleAccount,
        false,
        noRipple,
        false,
        false,
        saBalance,
        saReceiverLimit,
        0,
        0,
        j);
}

/** Send an IOU amount to a receiver, applying transfer fees for third-party sends.
 *
 *  Handles two cases:
 *  - **Direct send** (sender or receiver is the issuer, or issuer is
 *    `noAccount()`): delegates to `directSendNoFeeIOU` with no fee;
 *    sets @p saActual equal to @p saAmount.
 *  - **Third-party transit** (sender, receiver, and issuer all distinct):
 *    computes the gross cost as `saAmount × transferRate(issuer)` (or
 *    `saAmount` when `WaiveTransferFee::Yes`), then executes two sequential
 *    `directSendNoFeeIOU` calls — issuer→receiver for the delivery amount,
 *    then sender→issuer for the gross amount including the fee.
 *
 *  @param view        Mutable ledger view.
 *  @param uSenderID   Sending account; must not be XRP.
 *  @param uReceiverID Receiving account; must not be XRP; must differ from
 *      @p uSenderID.
 *  @param saAmount    Amount to deliver to @p uReceiverID.
 *  @param saActual    Out-parameter: amount actually debited from sender
 *      (delivery amount + fee for third-party sends).
 *  @param j           Journal for debug logging.
 *  @param waiveFee    Whether to skip the transfer fee.
 *  @return `tesSUCCESS`, or the first `tec`/`tef` from an inner send.
 */
static TER
directSendNoLimitIOU(
    ApplyView& view,
    AccountID const& uSenderID,
    AccountID const& uReceiverID,
    STAmount const& saAmount,
    STAmount& saActual,
    beast::Journal j,
    WaiveTransferFee waiveFee)
{
    auto const& issuer = saAmount.getIssuer();

    XRPL_ASSERT(
        !isXRP(uSenderID) && !isXRP(uReceiverID),
        "xrpl::directSendNoLimitIOU : neither sender nor receiver is XRP");
    XRPL_ASSERT(uSenderID != uReceiverID, "xrpl::directSendNoLimitIOU : sender is not receiver");

    if (uSenderID == issuer || uReceiverID == issuer || issuer == noAccount())
    {
        // Direct send: redeeming IOUs and/or sending own IOUs.
        auto const ter = directSendNoFeeIOU(view, uSenderID, uReceiverID, saAmount, false, j);
        if (!isTesSuccess(ter))
            return ter;
        saActual = saAmount;
        return tesSUCCESS;
    }

    // Third-party transit: sender pays delivery amount + transfer fee.
    saActual = (waiveFee == WaiveTransferFee::Yes) ? saAmount
                                                   : multiply(saAmount, transferRate(view, issuer));

    JLOG(j.debug()) << "directSendNoLimitIOU> " << to_string(uSenderID) << " - > "
                    << to_string(uReceiverID) << " : deliver=" << saAmount.getFullText()
                    << " cost=" << saActual.getFullText();

    TER terResult = directSendNoFeeIOU(view, issuer, uReceiverID, saAmount, true, j);

    if (tesSUCCESS == terResult)
        terResult = directSendNoFeeIOU(view, uSenderID, issuer, saActual, true, j);

    return terResult;
}

/** Send an IOU from one sender to multiple receivers in a single operation.
 *
 *  Iterates @p receivers. For each destination:
 *  - **Direct send** (sender or receiver is the issuer): calls
 *    `directSendNoFeeIOU` and immediately adjusts @p actual. The issuer
 *    debit has already happened inside `directSendNoFeeIOU`, so the amount
 *    is *not* added to `takeFromSender`.
 *  - **Third-party transit**: applies the transfer fee (or uses the raw amount
 *    when `WaiveTransferFee::Yes`), delivers issuer→receiver via
 *    `directSendNoFeeIOU`, and accumulates the gross cost in `takeFromSender`.
 *
 *  After the loop, a single `directSendNoFeeIOU(sender→issuer, takeFromSender)`
 *  call debits the sender for all third-party fees at once.
 *
 *  @param view        Mutable ledger view.
 *  @param senderID    The sending account; must not be XRP.
 *  @param issue       The IOU (currency + issuer) being sent.
 *  @param receivers   List of (AccountID, Number) destination pairs.
 *  @param actual      Out-parameter: total gross cost to the sender across all
 *      destinations, including all transfer fees.
 *  @param j           Journal for debug logging.
 *  @param waiveFee    Whether to skip transfer fees.
 *  @return `tesSUCCESS`, or the first `tec`/`tef` from an inner send.
 */
static TER
directSendNoLimitMultiIOU(
    ApplyView& view,
    AccountID const& senderID,
    Issue const& issue,
    MultiplePaymentDestinations const& receivers,
    STAmount& actual,
    beast::Journal j,
    WaiveTransferFee waiveFee)
{
    auto const& issuer = issue.getIssuer();

    XRPL_ASSERT(!isXRP(senderID), "xrpl::directSendNoLimitMultiIOU : sender is not XRP");

    // takeFromSender accumulates the transit-fee portion debited from sender
    // to issuer after the loop. actual accumulates the full cost including
    // direct sends. They diverge only when there are transfer fees.
    STAmount takeFromSender{issue};
    actual = takeFromSender;

    for (auto const& r : receivers)
    {
        auto const& receiverID = r.first;
        STAmount const amount{issue, r.second};

        if (!amount || (senderID == receiverID))
            continue;

        XRPL_ASSERT(!isXRP(receiverID), "xrpl::directSendNoLimitMultiIOU : receiver is not XRP");

        if (senderID == issuer || receiverID == issuer || issuer == noAccount())
        {
            // Direct send: redeeming IOUs and/or sending own IOUs.
            // directSendNoFeeIOU handles the issuer debit internally; do
            // not add to takeFromSender.
            if (auto const ter = directSendNoFeeIOU(view, senderID, receiverID, amount, false, j))
                return ter;
            actual += amount;
            continue;
        }

        // Third-party transit: accumulate gross cost for bulk sender debit.
        STAmount const actualSend = (waiveFee == WaiveTransferFee::Yes)
            ? amount
            : multiply(amount, transferRate(view, issuer));
        actual += actualSend;
        takeFromSender += actualSend;

        JLOG(j.debug()) << "directSendNoLimitMultiIOU> " << to_string(senderID) << " - > "
                        << to_string(receiverID) << " : deliver=" << amount.getFullText()
                        << " cost=" << actual.getFullText();

        if (TER const terResult = directSendNoFeeIOU(view, issuer, receiverID, amount, true, j))
            return terResult;
    }

    if (senderID != issuer && takeFromSender)
    {
        if (TER const terResult =
                directSendNoFeeIOU(view, senderID, issuer, takeFromSender, true, j))
            return terResult;
    }

    return tesSUCCESS;
}

/** Send an IOU or XRP amount from sender to receiver, the IOU path.
 *
 *  Dispatches on amount type:
 *  - **IOU**: delegates to `directSendNoLimitIOU`, which handles direct and
 *    third-party sends including transfer fees.
 *  - **XRP** (native): adjusts `sfBalance` directly on sender and receiver
 *    `AccountRoot` SLEs. Either account may be `beast::kZERO` (null SLE), a
 *    setup used during pathfinding where transfers are guaranteed balanced by
 *    the caller.
 *
 *  Under `fixAMMv1_1`, a negative or MPT amount returns `tecINTERNAL` rather
 *  than asserting, to ensure the error surfaces in closed-ledger replay.
 *
 *  @note For XRP, null sender/receiver are permitted by design; the caller is
 *      responsible for ensuring balanced books.
 *
 *  @param view        Mutable ledger view.
 *  @param uSenderID   Sending account (`beast::kZERO` allowed for XRP).
 *  @param uReceiverID Receiving account (`beast::kZERO` allowed for XRP).
 *  @param saAmount    Non-negative amount; must not be MPT.
 *  @param j           Journal for trace logging.
 *  @param waiveFee    Whether to skip the transfer fee (IOU only).
 *  @return `tesSUCCESS`, `telFAILED_PROCESSING`/`tecFAILED_PROCESSING` on
 *      insufficient XRP balance, or a `tec`/`tef` from `directSendNoLimitIOU`.
 */
static TER
accountSendIOU(
    ApplyView& view,
    AccountID const& uSenderID,
    AccountID const& uReceiverID,
    STAmount const& saAmount,
    beast::Journal j,
    WaiveTransferFee waiveFee)
{
    if (view.rules().enabled(fixAMMv1_1))
    {
        if (saAmount < beast::kZERO || saAmount.holds<MPTIssue>())
        {
            return tecINTERNAL;  // LCOV_EXCL_LINE
        }
    }
    else
    {
        // LCOV_EXCL_START
        XRPL_ASSERT(
            saAmount >= beast::kZERO && !saAmount.holds<MPTIssue>(),
            "xrpl::accountSendIOU : minimum amount and not MPT");
        // LCOV_EXCL_STOP
    }

    if (!saAmount || (uSenderID == uReceiverID))
        return tesSUCCESS;

    if (!saAmount.native())
    {
        STAmount saActual;

        JLOG(j.trace()) << "accountSendIOU: " << to_string(uSenderID) << " -> "
                        << to_string(uReceiverID) << " : " << saAmount.getFullText();

        return directSendNoLimitIOU(view, uSenderID, uReceiverID, saAmount, saActual, j, waiveFee);
    }

    /* XRP send which does not check reserve and can do pure adjustment.
     * Note that sender or receiver may be null and this not a mistake; this
     * setup is used during pathfinding and it is carefully controlled to
     * ensure that transfers are balanced.
     */
    TER terResult(tesSUCCESS);

    SLE::pointer const sender =
        uSenderID != beast::kZERO ? view.peek(keylet::account(uSenderID)) : SLE::pointer();
    SLE::pointer const receiver =
        uReceiverID != beast::kZERO ? view.peek(keylet::account(uReceiverID)) : SLE::pointer();

    if (auto stream = j.trace())
    {
        std::string senderBal("-");
        std::string receiverBal("-");

        if (sender)
            senderBal = sender->getFieldAmount(sfBalance).getFullText();

        if (receiver)
            receiverBal = receiver->getFieldAmount(sfBalance).getFullText();

        stream << "accountSendIOU> " << to_string(uSenderID) << " (" << senderBal << ") -> "
               << to_string(uReceiverID) << " (" << receiverBal << ") : " << saAmount.getFullText();
    }

    if (sender)
    {
        if (sender->getFieldAmount(sfBalance) < saAmount)
        {
            // VFALCO Its laborious to have to mutate the
            //        TER based on params everywhere
            // LCOV_EXCL_START
            terResult = view.open() ? TER{telFAILED_PROCESSING} : TER{tecFAILED_PROCESSING};
            // LCOV_EXCL_STOP
        }
        else
        {
            auto const sndBal = sender->getFieldAmount(sfBalance);
            view.creditHookIOU(uSenderID, xrpAccount(), saAmount, sndBal);
            sender->setFieldAmount(sfBalance, sndBal - saAmount);
            view.update(sender);
        }
    }

    if (tesSUCCESS == terResult && receiver)
    {
        auto const rcvBal = receiver->getFieldAmount(sfBalance);
        receiver->setFieldAmount(sfBalance, rcvBal + saAmount);
        view.creditHookIOU(xrpAccount(), uReceiverID, saAmount, -rcvBal);

        view.update(receiver);
    }

    if (auto stream = j.trace())
    {
        std::string senderBal("-");
        std::string receiverBal("-");

        if (sender)
            senderBal = sender->getFieldAmount(sfBalance).getFullText();

        if (receiver)
            receiverBal = receiver->getFieldAmount(sfBalance).getFullText();

        stream << "accountSendIOU< " << to_string(uSenderID) << " (" << senderBal << ") -> "
               << to_string(uReceiverID) << " (" << receiverBal << ") : " << saAmount.getFullText();
    }

    return terResult;
}

/** Send an IOU or XRP amount to multiple receivers atomically, the IOU path.
 *
 *  Dispatches on amount type:
 *  - **IOU**: delegates to `directSendNoLimitMultiIOU`, which batches
 *    transfer fees into a single sender→issuer debit after delivering to
 *    all receivers.
 *  - **XRP** (native): credits each receiver's `sfBalance` in turn, then
 *    debits the accumulated total from the sender in a single step after the
 *    loop. Null sender/receiver SLEs (`beast::kZERO`) are permitted for the
 *    same pathfinding reason as `accountSendIOU`.
 *
 *  @note `receivers.size()` must be > 1 (asserted).
 *
 *  @param view        Mutable ledger view.
 *  @param senderID    The sending account.
 *  @param issue       The IOU or XRP issue being sent.
 *  @param receivers   List of (AccountID, Number) destination pairs. Negative
 *      amounts return `tecINTERNAL`.
 *  @param j           Journal for trace logging.
 *  @param waiveFee    Whether to skip transfer fees (IOU only).
 *  @return `tesSUCCESS`, `tecFAILED_PROCESSING` on insufficient XRP balance,
 *      or a `tec`/`tef` from `directSendNoLimitMultiIOU`.
 */
static TER
accountSendMultiIOU(
    ApplyView& view,
    AccountID const& senderID,
    Issue const& issue,
    MultiplePaymentDestinations const& receivers,
    beast::Journal j,
    WaiveTransferFee waiveFee)
{
    XRPL_ASSERT_PARTS(
        receivers.size() > 1, "xrpl::accountSendMultiIOU", "multiple recipients provided");

    if (!issue.native())
    {
        STAmount actual;
        JLOG(j.trace()) << "accountSendMultiIOU: " << to_string(senderID) << " sending "
                        << receivers.size() << " IOUs";

        return directSendNoLimitMultiIOU(view, senderID, issue, receivers, actual, j, waiveFee);
    }

    /* XRP send which does not check reserve and can do pure adjustment.
     * Note that sender or receiver may be null and this not a mistake; this
     * setup could be used during pathfinding and it is carefully controlled to
     * ensure that transfers are balanced.
     */

    SLE::pointer const sender =
        senderID != beast::kZERO ? view.peek(keylet::account(senderID)) : SLE::pointer();

    if (auto stream = j.trace())
    {
        std::string senderBal("-");

        if (sender)
            senderBal = sender->getFieldAmount(sfBalance).getFullText();

        stream << "accountSendMultiIOU> " << to_string(senderID) << " (" << senderBal << ") -> "
               << receivers.size() << " receivers.";
    }

    // Credit receivers first; accumulate total to debit sender after loop.
    STAmount takeFromSender{issue};
    for (auto const& r : receivers)
    {
        auto const& receiverID = r.first;
        STAmount const amount{issue, r.second};

        if (amount < beast::kZERO)
        {
            return tecINTERNAL;  // LCOV_EXCL_LINE
        }

        if (!amount || (senderID == receiverID))
            continue;

        SLE::pointer const receiver =
            receiverID != beast::kZERO ? view.peek(keylet::account(receiverID)) : SLE::pointer();

        if (auto stream = j.trace())
        {
            std::string receiverBal("-");

            if (receiver)
                receiverBal = receiver->getFieldAmount(sfBalance).getFullText();

            stream << "accountSendMultiIOU> " << to_string(senderID) << " -> "
                   << to_string(receiverID) << " (" << receiverBal
                   << ") : " << amount.getFullText();
        }

        if (receiver)
        {
            auto const rcvBal = receiver->getFieldAmount(sfBalance);
            receiver->setFieldAmount(sfBalance, rcvBal + amount);
            view.creditHookIOU(xrpAccount(), receiverID, amount, -rcvBal);

            view.update(receiver);

            takeFromSender += amount;
        }

        if (auto stream = j.trace())
        {
            std::string receiverBal("-");

            if (receiver)
                receiverBal = receiver->getFieldAmount(sfBalance).getFullText();

            stream << "accountSendMultiIOU< " << to_string(senderID) << " -> "
                   << to_string(receiverID) << " (" << receiverBal
                   << ") : " << amount.getFullText();
        }
    }

    if (sender)
    {
        if (sender->getFieldAmount(sfBalance) < takeFromSender)
        {
            return TER{tecFAILED_PROCESSING};
        }
        auto const sndBal = sender->getFieldAmount(sfBalance);
        view.creditHookIOU(senderID, xrpAccount(), takeFromSender, sndBal);
        sender->setFieldAmount(sfBalance, sndBal - takeFromSender);
        view.update(sender);
    }

    if (auto stream = j.trace())
    {
        std::string senderBal("-");

        if (sender)
            senderBal = sender->getFieldAmount(sfBalance).getFullText();

        stream << "accountSendMultiIOU< " << to_string(senderID) << " (" << senderBal << ") -> "
               << receivers.size() << " receivers.";
    }
    return tesSUCCESS;
}

/** Adjust MPT balances directly without applying fees or limit checks.
 *
 *  Modifies `sfMPTAmount` on sender/receiver `MPToken` SLEs and
 *  `sfOutstandingAmount` on the `MPTokenIssuance` SLE:
 *  - **Sender == issuer**: increments `sfOutstandingAmount` (new tokens
 *    enter circulation). Under `featureMPTokensV2`, also checks
 *    `isMPTOverflow` with `AllowMPTOverflow::Yes`; returns `tecPATH_DRY`
 *    on overflow.
 *  - **Sender != issuer**: decrements `sfMPTAmount` on the sender's
 *    `MPToken` SLE. Returns `tecNO_AUTH` if no SLE exists (holder is not
 *    authorized), `tecINSUFFICIENT_FUNDS` if balance is too low.
 *  - **Receiver == issuer**: decrements `sfOutstandingAmount` (tokens
 *    redeemed). Returns `tecINTERNAL` if `outstanding < amount` (should
 *    never happen in a valid ledger).
 *  - **Receiver != issuer**: increments `sfMPTAmount` on the receiver's
 *    `MPToken` SLE. Returns `tecNO_AUTH` if no SLE exists.
 *
 *  Authorization must have been verified by the caller before this function
 *  is invoked.
 *
 *  @param view        Mutable ledger view.
 *  @param uSenderID   Sending account.
 *  @param uReceiverID Receiving account; must differ from @p uSenderID.
 *  @param saAmount    MPT amount to transfer.
 *  @param j           Journal (unused; reserved for future tracing).
 *  @return `tesSUCCESS`, `tecOBJECT_NOT_FOUND`, `tecPATH_DRY`,
 *      `tecINSUFFICIENT_FUNDS`, `tecNO_AUTH`, or `tecINTERNAL`.
 */
static TER
directSendNoFeeMPT(
    ApplyView& view,
    AccountID const& uSenderID,
    AccountID const& uReceiverID,
    STAmount const& saAmount,
    beast::Journal j)
{
    // Authorization must have been checked by the caller.
    auto const mptID = keylet::mptIssuance(saAmount.get<MPTIssue>().getMptID());
    auto const& issuer = saAmount.getIssuer();
    auto sleIssuance = view.peek(mptID);
    if (!sleIssuance)
        return tecOBJECT_NOT_FOUND;

    auto const maxAmount = maxMPTAmount(*sleIssuance);
    auto const outstanding = sleIssuance->getFieldU64(sfOutstandingAmount);
    auto const available = availableMPTAmount(*sleIssuance);
    auto const amt = saAmount.mpt().value();

    if (uSenderID == issuer)
    {
        if (view.rules().enabled(featureMPTokensV2))
        {
            if (isMPTOverflow(amt, outstanding, maxAmount, AllowMPTOverflow::Yes))
                return tecPATH_DRY;
        }
        (*sleIssuance)[sfOutstandingAmount] += amt;
        view.update(sleIssuance);
    }
    else
    {
        auto const mptokenID = keylet::mptoken(mptID.key, uSenderID);
        if (auto sle = view.peek(mptokenID))
        {
            auto const senderBalance = sle->getFieldU64(sfMPTAmount);
            if (senderBalance < amt)
                return tecINSUFFICIENT_FUNDS;
            view.creditHookMPT(uSenderID, uReceiverID, saAmount, (*sle)[sfMPTAmount], available);
            (*sle)[sfMPTAmount] = senderBalance - amt;
            view.update(sle);
        }
        else
        {
            return tecNO_AUTH;
        }
    }

    if (uReceiverID == issuer)
    {
        if (outstanding >= amt)
        {
            sleIssuance->setFieldU64(sfOutstandingAmount, outstanding - amt);
            view.update(sleIssuance);
        }
        else
        {
            return tecINTERNAL;  // LCOV_EXCL_LINE
        }
    }
    else
    {
        auto const mptokenID = keylet::mptoken(mptID.key, uReceiverID);
        if (auto sle = view.peek(mptokenID))
        {
            view.creditHookMPT(uSenderID, uReceiverID, saAmount, (*sle)[sfMPTAmount], available);
            (*sle)[sfMPTAmount] += amt;
            view.update(sle);
        }
        else
        {
            return tecNO_AUTH;
        }
    }

    return tesSUCCESS;
}

/** Send an MPT amount to a receiver, applying transfer fees for third-party sends.
 *
 *  Mirrors `directSendNoLimitIOU` for the MPT token model:
 *  - **Direct send** (sender or receiver is the issuer): validates that
 *    `OutstandingAmount + sendAmount <= MaximumAmount` for issuer-as-sender
 *    (gated by `AllowMPTOverflow` and `featureMPTokensV2`), then delegates to
 *    `directSendNoFeeMPT`. Sets @p saActual equal to @p saAmount.
 *  - **Third-party transit**: computes gross cost as
 *    `saAmount × transferRate(mptID)` (or `saAmount` when
 *    `WaiveTransferFee::Yes`), then executes two `directSendNoFeeMPT` calls:
 *    issuer→receiver for delivery, sender→issuer for gross cost.
 *
 *  The `allowOverflow` flag is only meaningful when `featureMPTokensV2` is
 *  active; without it the flag is forced to `No`.
 *
 *  @param view          Mutable ledger view.
 *  @param uSenderID     Sending account; must differ from @p uReceiverID.
 *  @param uReceiverID   Receiving account.
 *  @param saAmount      MPT amount to deliver to @p uReceiverID.
 *  @param saActual      Out-parameter: gross cost to the sender.
 *  @param j             Journal for debug logging.
 *  @param waiveFee      Whether to skip the transfer fee.
 *  @param allowOverflow Whether MPT outstanding may transiently exceed
 *      maximum (only honored under `featureMPTokensV2`).
 *  @return `tesSUCCESS`, `tecOBJECT_NOT_FOUND`, `tecPATH_DRY`, or a
 *      `tec`/`tef` from `directSendNoFeeMPT`.
 */
static TER
directSendNoLimitMPT(
    ApplyView& view,
    AccountID const& uSenderID,
    AccountID const& uReceiverID,
    STAmount const& saAmount,
    STAmount& saActual,
    beast::Journal j,
    WaiveTransferFee waiveFee,
    AllowMPTOverflow allowOverflow)
{
    XRPL_ASSERT(uSenderID != uReceiverID, "xrpl::directSendNoLimitMPT : sender is not receiver");

    // Only called by accountSendMPT, which guarantees saAmount holds MPTIssue.
    auto const& issuer = saAmount.getIssuer();

    auto const sle = view.read(keylet::mptIssuance(saAmount.get<MPTIssue>().getMptID()));
    if (!sle)
        return tecOBJECT_NOT_FOUND;

    if (uSenderID == issuer || uReceiverID == issuer)
    {
        if (uSenderID == issuer)
        {
            auto const sendAmount = saAmount.mpt().value();
            auto const maxAmount = maxMPTAmount(*sle);
            auto const outstanding = sle->getFieldU64(sfOutstandingAmount);
            auto const mptokensV2 = view.rules().enabled(featureMPTokensV2);
            // AllowMPTOverflow::Yes is only effective under featureMPTokensV2.
            allowOverflow = (allowOverflow == AllowMPTOverflow::Yes && mptokensV2)
                ? AllowMPTOverflow::Yes
                : AllowMPTOverflow::No;
            if (isMPTOverflow(sendAmount, outstanding, maxAmount, allowOverflow))
                return tecPATH_DRY;
        }

        // Direct send: redeeming MPTs and/or sending own MPTs.
        auto const ter = directSendNoFeeMPT(view, uSenderID, uReceiverID, saAmount, j);
        if (!isTesSuccess(ter))
            return ter;
        saActual = saAmount;
        return tesSUCCESS;
    }

    // Third-party transit: sender pays delivery amount + transfer fee.
    saActual = (waiveFee == WaiveTransferFee::Yes)
        ? saAmount
        : multiply(saAmount, transferRate(view, saAmount.get<MPTIssue>().getMptID()));

    JLOG(j.debug()) << "directSendNoLimitMPT> " << to_string(uSenderID) << " - > "
                    << to_string(uReceiverID) << " : deliver=" << saAmount.getFullText()
                    << " cost=" << saActual.getFullText();

    if (auto const terResult = directSendNoFeeMPT(view, issuer, uReceiverID, saAmount, j);
        !isTesSuccess(terResult))
        return terResult;

    return directSendNoFeeMPT(view, uSenderID, issuer, saActual, j);
}

/** Send an MPT amount from one sender to multiple receivers in a single operation.
 *
 *  Mirrors `directSendNoLimitMultiIOU` for the MPT model. For each receiver:
 *  - **Direct send** (sender or receiver is the issuer): validates
 *    `MaximumAmount` for issuer-as-sender and calls `directSendNoFeeMPT`.
 *    The MPT issuance SLE is mutated inside `directSendNoFeeMPT`; do not
 *    add the amount to `takeFromSender`.
 *  - **Third-party transit**: accumulates the gross cost (with or without
 *    fee) into `takeFromSender`; delivers via issuer→receiver
 *    `directSendNoFeeMPT`. A single sender→issuer call follows the loop.
 *
 *  **`MaximumAmount` enforcement for issuer-as-sender** differs by amendment:
 *  - Post-`fixCleanup3_1_3`: accumulates a `uint64_t totalSendAmount`
 *    across all iterations and performs a three-part overflow-safe check
 *    (`sendAmount > max || total > max - send || outstanding > max - send -
 *    total`). The subtraction order is critical — each condition guards the
 *    next against unsigned underflow; do not reorder.
 *  - Pre-`fixCleanup3_1_3`: per-iteration check against the stale
 *    `view.read()` snapshot. Retained for ledger replay compatibility.
 *
 *  `uint64_t` arithmetic (not `STAmount`/`Number`) is used to avoid 16-digit
 *  mantissa precision loss when values approach `kMAX_MP_TOKEN_AMOUNT`
 *  (19 digits).
 *
 *  @param view        Mutable ledger view.
 *  @param senderID    The sending account.
 *  @param mptIssue    The MPT issuance being sent.
 *  @param receivers   List of (AccountID, Number) destination pairs.
 *  @param actual      Out-parameter: total gross cost to the sender.
 *  @param j           Journal for debug logging.
 *  @param waiveFee    Whether to skip transfer fees.
 *  @return `tesSUCCESS`, `tecOBJECT_NOT_FOUND`, `tecPATH_DRY`,
 *      `tecINTERNAL`, or a `tec`/`tef` from `directSendNoFeeMPT`.
 */
static TER
directSendNoLimitMultiMPT(
    ApplyView& view,
    AccountID const& senderID,
    MPTIssue const& mptIssue,
    MultiplePaymentDestinations const& receivers,
    STAmount& actual,
    beast::Journal j,
    WaiveTransferFee waiveFee)
{
    auto const& issuer = mptIssue.getIssuer();

    auto const sle = view.read(keylet::mptIssuance(mptIssue.getMptID()));
    if (!sle)
        return tecOBJECT_NOT_FOUND;

    // Snapshot MaximumAmount and OutstandingAmount once. view.read() returns a
    // const SLE that is NOT updated by directSendNoFeeMPT (which uses peek()),
    // so per-iteration re-reads would be stale. totalSendAmount tracks the
    // running aggregate for the post-fixCleanup3_1_3 overflow check.
    // Use uint64_t — not STAmount/Number — to keep comparisons exact at
    // 19-digit magnitudes near kMAX_MP_TOKEN_AMOUNT.
    std::uint64_t totalSendAmount{0};
    std::uint64_t const maximumAmount = sle->at(~sfMaximumAmount).value_or(kMAX_MP_TOKEN_AMOUNT);
    std::uint64_t const outstandingAmount = sle->getFieldU64(sfOutstandingAmount);

    // actual: total cost to the sender (delivery + fees).
    // takeFromSender: transit-fee portion to debit sender→issuer after loop.
    // They diverge only when there are transfer fees on third-party sends.
    STAmount takeFromSender{mptIssue};
    actual = takeFromSender;

    for (auto const& [receiverID, amt] : receivers)
    {
        STAmount const amount{mptIssue, amt};

        if (amount < beast::kZERO)
            return tecINTERNAL;  // LCOV_EXCL_LINE

        if (!amount || senderID == receiverID)
            continue;

        if (senderID == issuer || receiverID == issuer)
        {
            if (senderID == issuer)
            {
                XRPL_ASSERT_PARTS(
                    takeFromSender == beast::kZERO,
                    "xrpl::directSendNoLimitMultiMPT",
                    "sender == issuer, takeFromSender == zero");

                std::uint64_t const sendAmount = amount.mpt().value();

                if (view.rules().enabled(fixCleanup3_1_3))
                {
                    // Post-fixCleanup3_1_3: aggregate MaximumAmount
                    // check. WARNING: the order of conditions is
                    // critical — each guards the subtraction in the
                    // next against unsigned underflow. Do not reorder.
                    bool const exceedsMaximumAmount =
                        sendAmount > maximumAmount ||
                        totalSendAmount > maximumAmount - sendAmount ||
                        outstandingAmount > maximumAmount - sendAmount - totalSendAmount;

                    if (exceedsMaximumAmount)
                        return tecPATH_DRY;
                    totalSendAmount += sendAmount;
                }
                else
                {
                    // Pre-fixCleanup3_1_3: per-iteration MaximumAmount
                    // check. Reads sfOutstandingAmount from a stale
                    // view.read() snapshot — incorrect for multi-destination
                    // sends but retained for ledger replay compatibility.
                    if (sendAmount > maximumAmount ||
                        outstandingAmount > maximumAmount - sendAmount)
                        return tecPATH_DRY;
                }
            }

            // Direct send: directSendNoFeeMPT handles issuer debit
            // internally; do not add to takeFromSender.
            if (auto const ter = directSendNoFeeMPT(view, senderID, receiverID, amount, j);
                !isTesSuccess(ter))
                return ter;
            actual += amount;
            continue;
        }

        // Third-party transit: accumulate gross cost for bulk sender debit.
        STAmount const actualSend = (waiveFee == WaiveTransferFee::Yes)
            ? amount
            : multiply(amount, transferRate(view, amount.get<MPTIssue>().getMptID()));
        actual += actualSend;
        takeFromSender += actualSend;

        JLOG(j.debug()) << "directSendNoLimitMultiMPT> " << to_string(senderID) << " - > "
                        << to_string(receiverID) << " : deliver=" << amount.getFullText()
                        << " cost=" << actualSend.getFullText();

        if (auto const ter = directSendNoFeeMPT(view, issuer, receiverID, amount, j);
            !isTesSuccess(ter))
            return ter;
    }
    if (senderID != issuer && takeFromSender)
    {
        if (auto const ter = directSendNoFeeMPT(view, senderID, issuer, takeFromSender, j);
            !isTesSuccess(ter))
            return ter;
    }

    return tesSUCCESS;
}

/** Send an MPT amount from sender to receiver, the MPT path of `accountSend`.
 *
 *  Validates that @p saAmount is non-negative and holds `MPTIssue`, then
 *  delegates to `directSendNoLimitMPT`. No-ops when the amount is zero or
 *  sender equals receiver.
 *
 *  @param view          Mutable ledger view.
 *  @param uSenderID     Sending account.
 *  @param uReceiverID   Receiving account.
 *  @param saAmount      Non-negative MPT amount to send.
 *  @param j             Journal for logging.
 *  @param waiveFee      Whether to skip the transfer fee.
 *  @param allowOverflow Whether MPT outstanding may transiently exceed
 *      maximum during payment-engine routing.
 *  @return `tesSUCCESS`, or a `tec`/`tef` from `directSendNoLimitMPT`.
 */
static TER
accountSendMPT(
    ApplyView& view,
    AccountID const& uSenderID,
    AccountID const& uReceiverID,
    STAmount const& saAmount,
    beast::Journal j,
    WaiveTransferFee waiveFee,
    AllowMPTOverflow allowOverflow)
{
    XRPL_ASSERT(
        saAmount >= beast::kZERO && saAmount.holds<MPTIssue>(),
        "xrpl::accountSendMPT : minimum amount and MPT");

    if (!saAmount || (uSenderID == uReceiverID))
        return tesSUCCESS;

    STAmount saActual{saAmount.asset()};

    return directSendNoLimitMPT(
        view, uSenderID, uReceiverID, saAmount, saActual, j, waiveFee, allowOverflow);
}

/** Send an MPT amount to multiple receivers atomically, the MPT path of
 *  `accountSendMulti`.
 *
 *  Thin wrapper that discards the `actual` out-parameter and delegates to
 *  `directSendNoLimitMultiMPT`.
 *
 *  @param view        Mutable ledger view.
 *  @param senderID    The sending account.
 *  @param mptIssue    The MPT issuance being sent.
 *  @param receivers   List of (AccountID, Number) destination pairs.
 *  @param j           Journal for logging.
 *  @param waiveFee    Whether to skip transfer fees.
 *  @return `tesSUCCESS` or the first error from `directSendNoLimitMultiMPT`.
 */
static TER
accountSendMultiMPT(
    ApplyView& view,
    AccountID const& senderID,
    MPTIssue const& mptIssue,
    MultiplePaymentDestinations const& receivers,
    beast::Journal j,
    WaiveTransferFee waiveFee)
{
    STAmount actual;

    return directSendNoLimitMultiMPT(view, senderID, mptIssue, receivers, actual, j, waiveFee);
}

//------------------------------------------------------------------------------
//
// Public Dispatcher Functions
//
//------------------------------------------------------------------------------

TER
directSendNoFee(
    ApplyView& view,
    AccountID const& uSenderID,
    AccountID const& uReceiverID,
    STAmount const& saAmount,
    bool bCheckIssuer,
    beast::Journal j)
{
    return saAmount.asset().visit(
        [&](Issue const&) {
            return directSendNoFeeIOU(view, uSenderID, uReceiverID, saAmount, bCheckIssuer, j);
        },
        [&](MPTIssue const&) {
            XRPL_ASSERT(!bCheckIssuer, "xrpl::directSendNoFee : not checking issuer");
            return directSendNoFeeMPT(view, uSenderID, uReceiverID, saAmount, j);
        });
}

TER
accountSend(
    ApplyView& view,
    AccountID const& uSenderID,
    AccountID const& uReceiverID,
    STAmount const& saAmount,
    beast::Journal j,
    WaiveTransferFee waiveFee,
    AllowMPTOverflow allowOverflow)
{
    return saAmount.asset().visit(
        [&](Issue const&) {
            return accountSendIOU(view, uSenderID, uReceiverID, saAmount, j, waiveFee);
        },
        [&](MPTIssue const&) {
            return accountSendMPT(
                view, uSenderID, uReceiverID, saAmount, j, waiveFee, allowOverflow);
        });
}

TER
accountSendMulti(
    ApplyView& view,
    AccountID const& senderID,
    Asset const& asset,
    MultiplePaymentDestinations const& receivers,
    beast::Journal j,
    WaiveTransferFee waiveFee)
{
    XRPL_ASSERT_PARTS(
        receivers.size() > 1, "xrpl::accountSendMulti", "multiple recipients provided");
    return asset.visit(
        [&](Issue const& issue) {
            return accountSendMultiIOU(view, senderID, issue, receivers, j, waiveFee);
        },
        [&](MPTIssue const& issue) {
            return accountSendMultiMPT(view, senderID, issue, receivers, j, waiveFee);
        });
}

TER
transferXRP(
    ApplyView& view,
    AccountID const& from,
    AccountID const& to,
    STAmount const& amount,
    beast::Journal j)
{
    XRPL_ASSERT(from != beast::kZERO, "xrpl::transferXRP : nonzero from account");
    XRPL_ASSERT(to != beast::kZERO, "xrpl::transferXRP : nonzero to account");
    XRPL_ASSERT(from != to, "xrpl::transferXRP : sender is not receiver");
    XRPL_ASSERT(amount.native(), "xrpl::transferXRP : amount is XRP");

    SLE::pointer const sender = view.peek(keylet::account(from));
    SLE::pointer const receiver = view.peek(keylet::account(to));
    if (!sender || !receiver)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    JLOG(j.trace()) << "transferXRP: " << to_string(from) << " -> " << to_string(to)
                    << ") : " << amount.getFullText();

    if (sender->getFieldAmount(sfBalance) < amount)
    {
        // VFALCO Its unfortunate we have to keep
        //        mutating these TER everywhere
        // FIXME: this logic should be moved to callers maybe?
        // LCOV_EXCL_START
        return view.open() ? TER{telFAILED_PROCESSING} : TER{tecFAILED_PROCESSING};
        // LCOV_EXCL_STOP
    }

    // Decrement XRP balance.
    sender->setFieldAmount(sfBalance, sender->getFieldAmount(sfBalance) - amount);
    view.update(sender);

    receiver->setFieldAmount(sfBalance, receiver->getFieldAmount(sfBalance) + amount);
    view.update(receiver);

    return tesSUCCESS;
}

}  // namespace xrpl
