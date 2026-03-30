#include <xrpl/ledger/helpers/TokenHelpers.h>
//
#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/MPTokenHelpers.h>
#include <xrpl/ledger/helpers/RippleStateHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/Quality.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/st.h>

#include <type_traits>
#include <variant>

namespace xrpl {

// Forward declaration for function that remains in View.h/cpp
bool
isLPTokenFrozen(
    ReadView const& view,
    AccountID const& account,
    Issue const& asset,
    Issue const& asset2);

//------------------------------------------------------------------------------
//
// Freeze checking (Asset-based)
//
//------------------------------------------------------------------------------

bool
isGlobalFrozen(ReadView const& view, Asset const& asset)
{
    return std::visit(
        [&]<ValidIssueType TIss>(TIss const& issue) {
            if constexpr (std::is_same_v<TIss, Issue>)
            {
                return isGlobalFrozen(view, issue.getIssuer());
            }
            else
            {
                return isGlobalFrozen(view, issue);
            }
        },
        asset.value());
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
    return std::visit(
        [&]<ValidIssueType TIss>(TIss const& issue) {
            if constexpr (std::is_same_v<TIss, Issue>)
            {
                return isAnyFrozen(view, accounts, issue);
            }
            else
            {
                return isAnyFrozen(view, accounts, issue, depth);
            }
        },
        asset.value());
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

    if (zeroIfFrozen == fhZERO_IF_FROZEN)
    {
        if (isFrozen(view, account, currency, issuer) ||
            isDeepFrozen(view, account, currency, issuer))
        {
            return nullptr;
        }

        // when fixFrozenLPTokenTransfer is enabled, if currency is lptoken,
        // we need to check if the associated assets have been frozen
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
                    isLPTokenFrozen(
                        view,
                        account,
                        (*sleAmm)[sfAsset].get<Issue>(),
                        (*sleAmm)[sfAsset2].get<Issue>()))
                {
                    return nullptr;
                }
            }
        }
    }

    return sle;
}

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
            // Put balance in account terms.
            amount.negate();
        }
        if (includeOppositeLimit)
        {
            amount += sle->getFieldAmount(oppositeField);
        }
        amount.setIssuer(issuer);
    }
    else
    {
        amount.clear(Issue{currency, issuer});
    }

    JLOG(j.trace()) << "getTrustLineBalance:" << " account=" << to_string(account)
                    << " amount=" << amount.getFullText();

    return view.balanceHook(account, issuer, amount);
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
    STAmount amount;
    if (isXRP(currency))
    {
        return {xrpLiquid(view, account, 0, j)};
    }

    bool const returnSpendable = (includeFullBalance == shFULL_BALANCE);
    if (returnSpendable && account == issuer)
    {
        // If the account is the issuer, then their limit is effectively
        // infinite
        return STAmount{Issue{currency, issuer}, STAmount::cMaxValue, STAmount::cMaxOffset};
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
    bool const returnSpendable = (includeFullBalance == shFULL_BALANCE);

    if (returnSpendable && account == mptIssue.getIssuer())
    {
        // if the account is the issuer, and the issuance exists, their limit is
        // the issuance limit minus the outstanding value
        auto const issuance = view.read(keylet::mptIssuance(mptIssue.getMptID()));

        if (!issuance)
        {
            return STAmount{mptIssue};
        }
        return STAmount{
            mptIssue,
            issuance->at(~sfMaximumAmount).value_or(maxMPTokenAmount) -
                issuance->at(sfOutstandingAmount)};
    }

    STAmount amount;

    auto const sleMpt = view.read(keylet::mptoken(mptIssue.getMptID(), account));

    if (!sleMpt)
    {
        amount.clear(mptIssue);
    }
    else if (zeroIfFrozen == fhZERO_IF_FROZEN && isFrozen(view, account, mptIssue))
    {
        amount.clear(mptIssue);
    }
    else
    {
        amount = STAmount{mptIssue, sleMpt->getFieldU64(sfMPTAmount)};

        // Only if auth check is needed, as it needs to do an additional read
        // operation. Note featureSingleAssetVault will affect error codes.
        if (zeroIfUnauthorized == ahZERO_IF_UNAUTHORIZED &&
            view.rules().enabled(featureSingleAssetVault))
        {
            if (auto const err = requireAuth(view, mptIssue, account, AuthType::StrongAuth);
                !isTesSuccess(err))
                amount.clear(mptIssue);
        }
        else if (zeroIfUnauthorized == ahZERO_IF_UNAUTHORIZED)
        {
            auto const sleIssuance = view.read(keylet::mptIssuance(mptIssue.getMptID()));

            // if auth is enabled on the issuance and mpt is not authorized,
            // clear amount
            if (sleIssuance && sleIssuance->isFlag(lsfMPTRequireAuth) &&
                !sleMpt->isFlag(lsfMPTAuthorized))
                amount.clear(mptIssue);
        }
    }

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
    return std::visit(
        [&]<ValidIssueType TIss>(TIss const& value) {
            if constexpr (std::is_same_v<TIss, Issue>)
            {
                return accountHolds(view, account, value, zeroIfFrozen, j, includeFullBalance);
            }
            else if constexpr (std::is_same_v<TIss, MPTIssue>)
            {
                return accountHolds(
                    view, account, value, zeroIfFrozen, zeroIfUnauthorized, j, includeFullBalance);
            }
        },
        asset.value());
}

STAmount
accountFunds(
    ReadView const& view,
    AccountID const& id,
    STAmount const& saDefault,
    FreezeHandling freezeHandling,
    beast::Journal j)
{
    if (!saDefault.native() && saDefault.getIssuer() == id)
        return saDefault;

    return accountHolds(
        view, id, saDefault.getCurrency(), saDefault.getIssuer(), freezeHandling, j);
}

Rate
transferRate(ReadView const& view, STAmount const& amount)
{
    return std::visit(
        [&]<ValidIssueType TIss>(TIss const& issue) {
            if constexpr (std::is_same_v<TIss, Issue>)
            {
                return transferRate(view, issue.getIssuer());
            }
            else
            {
                return transferRate(view, issue.getMptID());
            }
        },
        amount.asset().value());
}

//------------------------------------------------------------------------------
//
// Holding operations
//
//------------------------------------------------------------------------------

[[nodiscard]] TER
canAddHolding(ReadView const& view, Issue const& issue)
{
    if (issue.native())
    {
        return tesSUCCESS;  // No special checks for XRP
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
        [&]<ValidIssueType TIss>(TIss const& issue_) {
            return requireAuth(view, issue_, account, authType);
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

// Direct send w/o fees:
// - Redeeming IOUs and/or sending sender's own IOUs.
// - Create trust line if needed.
// --> bCheckIssuer : normally require issuer to be involved.
static TER
rippleCreditIOU(
    ApplyView& view,
    AccountID const& uSenderID,
    AccountID const& uReceiverID,
    STAmount const& saAmount,
    bool bCheckIssuer,
    beast::Journal j)
{
    AccountID const& issuer = saAmount.getIssuer();
    Currency const& currency = saAmount.getCurrency();

    // Make sure issuer is involved.
    XRPL_ASSERT(
        !bCheckIssuer || uSenderID == issuer || uReceiverID == issuer,
        "xrpl::rippleCreditIOU : matching issuer or don't care");
    (void)issuer;

    // Disallow sending to self.
    XRPL_ASSERT(uSenderID != uReceiverID, "xrpl::rippleCreditIOU : sender is not receiver");

    bool const bSenderHigh = uSenderID > uReceiverID;
    auto const index = keylet::line(uSenderID, uReceiverID, currency);

    XRPL_ASSERT(
        !isXRP(uSenderID) && uSenderID != noAccount(), "xrpl::rippleCreditIOU : sender is not XRP");
    XRPL_ASSERT(
        !isXRP(uReceiverID) && uReceiverID != noAccount(),
        "xrpl::rippleCreditIOU : receiver is not XRP");

    // If the line exists, modify it accordingly.
    if (auto const sleRippleState = view.peek(index))
    {
        STAmount saBalance = sleRippleState->getFieldAmount(sfBalance);

        if (bSenderHigh)
            saBalance.negate();  // Put balance in sender terms.

        view.creditHook(uSenderID, uReceiverID, saAmount, saBalance);

        STAmount const saBefore = saBalance;

        saBalance -= saAmount;

        JLOG(j.trace()) << "rippleCreditIOU: " << to_string(uSenderID) << " -> "
                        << to_string(uReceiverID) << " : before=" << saBefore.getFullText()
                        << " amount=" << saAmount.getFullText()
                        << " after=" << saBalance.getFullText();

        std::uint32_t const uFlags(sleRippleState->getFieldU32(sfFlags));
        bool bDelete = false;

        // FIXME This NEEDS to be cleaned up and simplified. It's impossible
        //       for anyone to understand.
        if (saBefore > beast::zero
            // Sender balance was positive.
            && saBalance <= beast::zero
            // Sender is zero or negative.
            && ((uFlags & (!bSenderHigh ? lsfLowReserve : lsfHighReserve)) != 0u)
            // Sender reserve is set.
            && static_cast<bool>(uFlags & (!bSenderHigh ? lsfLowNoRipple : lsfHighNoRipple)) !=
                static_cast<bool>(
                    view.read(keylet::account(uSenderID))->getFlags() & lsfDefaultRipple) &&
            ((uFlags & (!bSenderHigh ? lsfLowFreeze : lsfHighFreeze)) == 0u) &&
            !sleRippleState->getFieldAmount(!bSenderHigh ? sfLowLimit : sfHighLimit)
            // Sender trust limit is 0.
            && (sleRippleState->getFieldU32(!bSenderHigh ? sfLowQualityIn : sfHighQualityIn) == 0u)
            // Sender quality in is 0.
            &&
            (sleRippleState->getFieldU32(!bSenderHigh ? sfLowQualityOut : sfHighQualityOut) == 0u))
        // Sender quality out is 0.
        {
            // Clear the reserve of the sender, possibly delete the line!
            adjustOwnerCount(view, view.peek(keylet::account(uSenderID)), -1, j);

            // Clear reserve flag.
            sleRippleState->setFieldU32(
                sfFlags, uFlags & (!bSenderHigh ? ~lsfLowReserve : ~lsfHighReserve));

            // Balance is zero, receiver reserve is clear.
            bDelete = !saBalance  // Balance is zero.
                && ((uFlags & (bSenderHigh ? lsfLowReserve : lsfHighReserve)) == 0u);
            // Receiver reserve is clear.
        }

        if (bSenderHigh)
            saBalance.negate();

        // Want to reflect balance to zero even if we are deleting line.
        sleRippleState->setFieldAmount(sfBalance, saBalance);
        // ONLY: Adjust ripple balance.

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

    saBalance.setIssuer(noAccount());

    JLOG(j.debug()) << "rippleCreditIOU: "
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

// Send regardless of limits.
// --> saAmount: Amount/currency/issuer to deliver to receiver.
// <-- saActual: Amount actually cost.  Sender pays fees.
static TER
rippleSendIOU(
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
        "xrpl::rippleSendIOU : neither sender nor receiver is XRP");
    XRPL_ASSERT(uSenderID != uReceiverID, "xrpl::rippleSendIOU : sender is not receiver");

    if (uSenderID == issuer || uReceiverID == issuer || issuer == noAccount())
    {
        // Direct send: redeeming IOUs and/or sending own IOUs.
        auto const ter = rippleCreditIOU(view, uSenderID, uReceiverID, saAmount, false, j);
        if (!isTesSuccess(ter))
            return ter;
        saActual = saAmount;
        return tesSUCCESS;
    }

    // Sending 3rd party IOUs: transit.

    // Calculate the amount to transfer accounting
    // for any transfer fees if the fee is not waived:
    saActual = (waiveFee == WaiveTransferFee::Yes) ? saAmount
                                                   : multiply(saAmount, transferRate(view, issuer));

    JLOG(j.debug()) << "rippleSendIOU> " << to_string(uSenderID) << " - > "
                    << to_string(uReceiverID) << " : deliver=" << saAmount.getFullText()
                    << " cost=" << saActual.getFullText();

    TER terResult = rippleCreditIOU(view, issuer, uReceiverID, saAmount, true, j);

    if (tesSUCCESS == terResult)
        terResult = rippleCreditIOU(view, uSenderID, issuer, saActual, true, j);

    return terResult;
}

// Send regardless of limits.
// --> receivers: Amount/currency/issuer to deliver to receivers.
// <-- saActual: Amount actually cost to sender.  Sender pays fees.
static TER
rippleSendMultiIOU(
    ApplyView& view,
    AccountID const& senderID,
    Issue const& issue,
    MultiplePaymentDestinations const& receivers,
    STAmount& actual,
    beast::Journal j,
    WaiveTransferFee waiveFee)
{
    auto const& issuer = issue.getIssuer();

    XRPL_ASSERT(!isXRP(senderID), "xrpl::rippleSendMultiIOU : sender is not XRP");

    // These may diverge
    STAmount takeFromSender{issue};
    actual = takeFromSender;

    // Failures return immediately.
    for (auto const& r : receivers)
    {
        auto const& receiverID = r.first;
        STAmount amount{issue, r.second};

        /* If we aren't sending anything or if the sender is the same as the
         * receiver then we don't need to do anything.
         */
        if (!amount || (senderID == receiverID))
            continue;

        XRPL_ASSERT(!isXRP(receiverID), "xrpl::rippleSendMultiIOU : receiver is not XRP");

        if (senderID == issuer || receiverID == issuer || issuer == noAccount())
        {
            // Direct send: redeeming IOUs and/or sending own IOUs.
            if (auto const ter = rippleCreditIOU(view, senderID, receiverID, amount, false, j))
                return ter;
            actual += amount;
            // Do not add amount to takeFromSender, because rippleCreditIOU took
            // it.

            continue;
        }

        // Sending 3rd party IOUs: transit.

        // Calculate the amount to transfer accounting
        // for any transfer fees if the fee is not waived:
        STAmount actualSend = (waiveFee == WaiveTransferFee::Yes)
            ? amount
            : multiply(amount, transferRate(view, issuer));
        actual += actualSend;
        takeFromSender += actualSend;

        JLOG(j.debug()) << "rippleSendMultiIOU> " << to_string(senderID) << " - > "
                        << to_string(receiverID) << " : deliver=" << amount.getFullText()
                        << " cost=" << actual.getFullText();

        if (TER const terResult = rippleCreditIOU(view, issuer, receiverID, amount, true, j))
            return terResult;
    }

    if (senderID != issuer && takeFromSender)
    {
        if (TER const terResult = rippleCreditIOU(view, senderID, issuer, takeFromSender, true, j))
            return terResult;
    }

    return tesSUCCESS;
}

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
        if (saAmount < beast::zero || saAmount.holds<MPTIssue>())
        {
            return tecINTERNAL;  // LCOV_EXCL_LINE
        }
    }
    else
    {
        // LCOV_EXCL_START
        XRPL_ASSERT(
            saAmount >= beast::zero && !saAmount.holds<MPTIssue>(),
            "xrpl::accountSendIOU : minimum amount and not MPT");
        // LCOV_EXCL_STOP
    }

    /* If we aren't sending anything or if the sender is the same as the
     * receiver then we don't need to do anything.
     */
    if (!saAmount || (uSenderID == uReceiverID))
        return tesSUCCESS;

    if (!saAmount.native())
    {
        STAmount saActual;

        JLOG(j.trace()) << "accountSendIOU: " << to_string(uSenderID) << " -> "
                        << to_string(uReceiverID) << " : " << saAmount.getFullText();

        return rippleSendIOU(view, uSenderID, uReceiverID, saAmount, saActual, j, waiveFee);
    }

    /* XRP send which does not check reserve and can do pure adjustment.
     * Note that sender or receiver may be null and this not a mistake; this
     * setup is used during pathfinding and it is carefully controlled to
     * ensure that transfers are balanced.
     */
    TER terResult(tesSUCCESS);

    SLE::pointer sender =
        uSenderID != beast::zero ? view.peek(keylet::account(uSenderID)) : SLE::pointer();
    SLE::pointer receiver =
        uReceiverID != beast::zero ? view.peek(keylet::account(uReceiverID)) : SLE::pointer();

    if (auto stream = j.trace())
    {
        std::string sender_bal("-");
        std::string receiver_bal("-");

        if (sender)
            sender_bal = sender->getFieldAmount(sfBalance).getFullText();

        if (receiver)
            receiver_bal = receiver->getFieldAmount(sfBalance).getFullText();

        stream << "accountSendIOU> " << to_string(uSenderID) << " (" << sender_bal << ") -> "
               << to_string(uReceiverID) << " (" << receiver_bal
               << ") : " << saAmount.getFullText();
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
            view.creditHook(uSenderID, xrpAccount(), saAmount, sndBal);

            // Decrement XRP balance.
            sender->setFieldAmount(sfBalance, sndBal - saAmount);
            view.update(sender);
        }
    }

    if (tesSUCCESS == terResult && receiver)
    {
        // Increment XRP balance.
        auto const rcvBal = receiver->getFieldAmount(sfBalance);
        receiver->setFieldAmount(sfBalance, rcvBal + saAmount);
        view.creditHook(xrpAccount(), uReceiverID, saAmount, -rcvBal);

        view.update(receiver);
    }

    if (auto stream = j.trace())
    {
        std::string sender_bal("-");
        std::string receiver_bal("-");

        if (sender)
            sender_bal = sender->getFieldAmount(sfBalance).getFullText();

        if (receiver)
            receiver_bal = receiver->getFieldAmount(sfBalance).getFullText();

        stream << "accountSendIOU< " << to_string(uSenderID) << " (" << sender_bal << ") -> "
               << to_string(uReceiverID) << " (" << receiver_bal
               << ") : " << saAmount.getFullText();
    }

    return terResult;
}

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

        return rippleSendMultiIOU(view, senderID, issue, receivers, actual, j, waiveFee);
    }

    /* XRP send which does not check reserve and can do pure adjustment.
     * Note that sender or receiver may be null and this not a mistake; this
     * setup could be used during pathfinding and it is carefully controlled to
     * ensure that transfers are balanced.
     */

    SLE::pointer sender =
        senderID != beast::zero ? view.peek(keylet::account(senderID)) : SLE::pointer();

    if (auto stream = j.trace())
    {
        std::string sender_bal("-");

        if (sender)
            sender_bal = sender->getFieldAmount(sfBalance).getFullText();

        stream << "accountSendMultiIOU> " << to_string(senderID) << " (" << sender_bal << ") -> "
               << receivers.size() << " receivers.";
    }

    // Failures return immediately.
    STAmount takeFromSender{issue};
    for (auto const& r : receivers)
    {
        auto const& receiverID = r.first;
        STAmount amount{issue, r.second};

        if (amount < beast::zero)
        {
            return tecINTERNAL;  // LCOV_EXCL_LINE
        }

        /* If we aren't sending anything or if the sender is the same as the
         * receiver then we don't need to do anything.
         */
        if (!amount || (senderID == receiverID))
            continue;

        SLE::pointer receiver =
            receiverID != beast::zero ? view.peek(keylet::account(receiverID)) : SLE::pointer();

        if (auto stream = j.trace())
        {
            std::string receiver_bal("-");

            if (receiver)
                receiver_bal = receiver->getFieldAmount(sfBalance).getFullText();

            stream << "accountSendMultiIOU> " << to_string(senderID) << " -> "
                   << to_string(receiverID) << " (" << receiver_bal
                   << ") : " << amount.getFullText();
        }

        if (receiver)
        {
            // Increment XRP balance.
            auto const rcvBal = receiver->getFieldAmount(sfBalance);
            receiver->setFieldAmount(sfBalance, rcvBal + amount);
            view.creditHook(xrpAccount(), receiverID, amount, -rcvBal);

            view.update(receiver);

            // Take what is actually sent
            takeFromSender += amount;
        }

        if (auto stream = j.trace())
        {
            std::string receiver_bal("-");

            if (receiver)
                receiver_bal = receiver->getFieldAmount(sfBalance).getFullText();

            stream << "accountSendMultiIOU< " << to_string(senderID) << " -> "
                   << to_string(receiverID) << " (" << receiver_bal
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
        view.creditHook(senderID, xrpAccount(), takeFromSender, sndBal);

        // Decrement XRP balance.
        sender->setFieldAmount(sfBalance, sndBal - takeFromSender);
        view.update(sender);
    }

    if (auto stream = j.trace())
    {
        std::string sender_bal("-");

        if (sender)
            sender_bal = sender->getFieldAmount(sfBalance).getFullText();

        stream << "accountSendMultiIOU< " << to_string(senderID) << " (" << sender_bal << ") -> "
               << receivers.size() << " receivers.";
    }
    return tesSUCCESS;
}

static TER
rippleCreditMPT(
    ApplyView& view,
    AccountID const& uSenderID,
    AccountID const& uReceiverID,
    STAmount const& saAmount,
    beast::Journal j)
{
    // Do not check MPT authorization here - it must have been checked earlier
    auto const mptID = keylet::mptIssuance(saAmount.get<MPTIssue>().getMptID());
    auto const& issuer = saAmount.getIssuer();
    auto sleIssuance = view.peek(mptID);
    if (!sleIssuance)
        return tecOBJECT_NOT_FOUND;
    if (uSenderID == issuer)
    {
        (*sleIssuance)[sfOutstandingAmount] += saAmount.mpt().value();
        view.update(sleIssuance);
    }
    else
    {
        auto const mptokenID = keylet::mptoken(mptID.key, uSenderID);
        if (auto sle = view.peek(mptokenID))
        {
            auto const amt = sle->getFieldU64(sfMPTAmount);
            auto const pay = saAmount.mpt().value();
            if (amt < pay)
                return tecINSUFFICIENT_FUNDS;
            (*sle)[sfMPTAmount] = amt - pay;
            view.update(sle);
        }
        else
        {
            return tecNO_AUTH;
        }
    }

    if (uReceiverID == issuer)
    {
        auto const outstanding = sleIssuance->getFieldU64(sfOutstandingAmount);
        auto const redeem = saAmount.mpt().value();
        if (outstanding >= redeem)
        {
            sleIssuance->setFieldU64(sfOutstandingAmount, outstanding - redeem);
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
            (*sle)[sfMPTAmount] += saAmount.mpt().value();
            view.update(sle);
        }
        else
        {
            return tecNO_AUTH;
        }
    }

    return tesSUCCESS;
}

static TER
rippleSendMPT(
    ApplyView& view,
    AccountID const& uSenderID,
    AccountID const& uReceiverID,
    STAmount const& saAmount,
    STAmount& saActual,
    beast::Journal j,
    WaiveTransferFee waiveFee)
{
    XRPL_ASSERT(uSenderID != uReceiverID, "xrpl::rippleSendMPT : sender is not receiver");

    // Safe to get MPT since rippleSendMPT is only called by accountSendMPT
    auto const& issuer = saAmount.getIssuer();

    auto const sle = view.read(keylet::mptIssuance(saAmount.get<MPTIssue>().getMptID()));
    if (!sle)
        return tecOBJECT_NOT_FOUND;

    if (uSenderID == issuer || uReceiverID == issuer)
    {
        // if sender is issuer, check that the new OutstandingAmount will not
        // exceed MaximumAmount
        if (uSenderID == issuer)
        {
            auto const sendAmount = saAmount.mpt().value();
            auto const maximumAmount = sle->at(~sfMaximumAmount).value_or(maxMPTokenAmount);
            if (sendAmount > maximumAmount ||
                sle->getFieldU64(sfOutstandingAmount) > maximumAmount - sendAmount)
                return tecPATH_DRY;
        }

        // Direct send: redeeming MPTs and/or sending own MPTs.
        auto const ter = rippleCreditMPT(view, uSenderID, uReceiverID, saAmount, j);
        if (!isTesSuccess(ter))
            return ter;
        saActual = saAmount;
        return tesSUCCESS;
    }

    // Sending 3rd party MPTs: transit.
    saActual = (waiveFee == WaiveTransferFee::Yes)
        ? saAmount
        : multiply(saAmount, transferRate(view, saAmount.get<MPTIssue>().getMptID()));

    JLOG(j.debug()) << "rippleSendMPT> " << to_string(uSenderID) << " - > "
                    << to_string(uReceiverID) << " : deliver=" << saAmount.getFullText()
                    << " cost=" << saActual.getFullText();

    if (auto const terResult = rippleCreditMPT(view, issuer, uReceiverID, saAmount, j);
        !isTesSuccess(terResult))
        return terResult;

    return rippleCreditMPT(view, uSenderID, issuer, saActual, j);
}

static TER
rippleSendMultiMPT(
    ApplyView& view,
    AccountID const& senderID,
    MPTIssue const& mptIssue,
    MultiplePaymentDestinations const& receivers,
    STAmount& actual,
    beast::Journal j,
    WaiveTransferFee waiveFee)
{
    // Safe to get MPT since rippleSendMultiMPT is only called by
    // accountSendMultiMPT
    auto const& issuer = mptIssue.getIssuer();

    auto const sle = view.read(keylet::mptIssuance(mptIssue.getMptID()));
    if (!sle)
        return tecOBJECT_NOT_FOUND;

    // These may diverge
    STAmount takeFromSender{mptIssue};
    actual = takeFromSender;

    for (auto const& r : receivers)
    {
        auto const& receiverID = r.first;
        STAmount amount{mptIssue, r.second};

        if (amount < beast::zero)
        {
            return tecINTERNAL;  // LCOV_EXCL_LINE
        }

        /* If we aren't sending anything or if the sender is the same as the
         * receiver then we don't need to do anything.
         */
        if (!amount || (senderID == receiverID))
            continue;

        if (senderID == issuer || receiverID == issuer)
        {
            // if sender is issuer, check that the new OutstandingAmount will
            // not exceed MaximumAmount
            if (senderID == issuer)
            {
                XRPL_ASSERT_PARTS(
                    takeFromSender == beast::zero,
                    "xrpl::rippleSendMultiMPT",
                    "sender == issuer, takeFromSender == zero");
                auto const sendAmount = amount.mpt().value();
                auto const maximumAmount = sle->at(~sfMaximumAmount).value_or(maxMPTokenAmount);
                if (sendAmount > maximumAmount ||
                    sle->getFieldU64(sfOutstandingAmount) > maximumAmount - sendAmount)
                    return tecPATH_DRY;
            }

            // Direct send: redeeming MPTs and/or sending own MPTs.
            if (auto const ter = rippleCreditMPT(view, senderID, receiverID, amount, j))
                return ter;
            actual += amount;
            // Do not add amount to takeFromSender, because rippleCreditMPT took
            // it

            continue;
        }

        // Sending 3rd party MPTs: transit.
        STAmount actualSend = (waiveFee == WaiveTransferFee::Yes)
            ? amount
            : multiply(amount, transferRate(view, amount.get<MPTIssue>().getMptID()));
        actual += actualSend;
        takeFromSender += actualSend;

        JLOG(j.debug()) << "rippleSendMultiMPT> " << to_string(senderID) << " - > "
                        << to_string(receiverID) << " : deliver=" << amount.getFullText()
                        << " cost=" << actualSend.getFullText();

        if (auto const terResult = rippleCreditMPT(view, issuer, receiverID, amount, j))
            return terResult;
    }
    if (senderID != issuer && takeFromSender)
    {
        if (TER const terResult = rippleCreditMPT(view, senderID, issuer, takeFromSender, j))
            return terResult;
    }

    return tesSUCCESS;
}

static TER
accountSendMPT(
    ApplyView& view,
    AccountID const& uSenderID,
    AccountID const& uReceiverID,
    STAmount const& saAmount,
    beast::Journal j,
    WaiveTransferFee waiveFee)
{
    XRPL_ASSERT(
        saAmount >= beast::zero && saAmount.holds<MPTIssue>(),
        "xrpl::accountSendMPT : minimum amount and MPT");

    /* If we aren't sending anything or if the sender is the same as the
     * receiver then we don't need to do anything.
     */
    if (!saAmount || (uSenderID == uReceiverID))
        return tesSUCCESS;

    STAmount saActual{saAmount.asset()};

    return rippleSendMPT(view, uSenderID, uReceiverID, saAmount, saActual, j, waiveFee);
}

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

    return rippleSendMultiMPT(view, senderID, mptIssue, receivers, actual, j, waiveFee);
}

//------------------------------------------------------------------------------
//
// Public Dispatcher Functions
//
//------------------------------------------------------------------------------

TER
rippleCredit(
    ApplyView& view,
    AccountID const& uSenderID,
    AccountID const& uReceiverID,
    STAmount const& saAmount,
    bool bCheckIssuer,
    beast::Journal j)
{
    return std::visit(
        [&]<ValidIssueType TIss>(TIss const& issue) {
            if constexpr (std::is_same_v<TIss, Issue>)
            {
                return rippleCreditIOU(view, uSenderID, uReceiverID, saAmount, bCheckIssuer, j);
            }
            else
            {
                XRPL_ASSERT(!bCheckIssuer, "xrpl::rippleCredit : not checking issuer");
                return rippleCreditMPT(view, uSenderID, uReceiverID, saAmount, j);
            }
        },
        saAmount.asset().value());
}

TER
accountSend(
    ApplyView& view,
    AccountID const& uSenderID,
    AccountID const& uReceiverID,
    STAmount const& saAmount,
    beast::Journal j,
    WaiveTransferFee waiveFee)
{
    return std::visit(
        [&]<ValidIssueType TIss>(TIss const& issue) {
            if constexpr (std::is_same_v<TIss, Issue>)
            {
                return accountSendIOU(view, uSenderID, uReceiverID, saAmount, j, waiveFee);
            }
            else
            {
                return accountSendMPT(view, uSenderID, uReceiverID, saAmount, j, waiveFee);
            }
        },
        saAmount.asset().value());
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
    return std::visit(
        [&]<ValidIssueType TIss>(TIss const& issue) {
            if constexpr (std::is_same_v<TIss, Issue>)
            {
                return accountSendMultiIOU(view, senderID, issue, receivers, j, waiveFee);
            }
            else
            {
                return accountSendMultiMPT(view, senderID, issue, receivers, j, waiveFee);
            }
        },
        asset.value());
}

TER
transferXRP(
    ApplyView& view,
    AccountID const& from,
    AccountID const& to,
    STAmount const& amount,
    beast::Journal j)
{
    XRPL_ASSERT(from != beast::zero, "xrpl::transferXRP : nonzero from account");
    XRPL_ASSERT(to != beast::zero, "xrpl::transferXRP : nonzero to account");
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
