#include <xrpl/ledger/helpers/TokenHelpers.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/MPTokenHelpers.h>
#include <xrpl/ledger/helpers/RippleStateHelpers.h>
#include <xrpl/ledger/helpers/SponsorHelpers.h>
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
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/XRPAmount.h>

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <string>
#include <variant>

namespace xrpl {

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

TER
checkGlobalFrozen(ReadView const& view, Asset const& asset)
{
    if (isGlobalFrozen(view, asset))
        return asset.holds<MPTIssue>() ? tecLOCKED : tecFROZEN;
    return tesSUCCESS;
}

bool
isIndividualFrozen(ReadView const& view, AccountID const& account, Asset const& asset)
{
    return std::visit(
        [&](auto const& issue) { return isIndividualFrozen(view, account, issue); }, asset.value());
}

TER
checkIndividualFrozen(ReadView const& view, AccountID const& account, Asset const& asset)
{
    if (isIndividualFrozen(view, account, asset))
        return asset.holds<MPTIssue>() ? tecLOCKED : tecFROZEN;
    return tesSUCCESS;
}

bool
isFrozen(ReadView const& view, AccountID const& account, Asset const& asset, std::uint8_t depth)
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
    return std::ranges::any_of(accounts, [&](auto const& account) {
        return isFrozen(view, account, issue.currency, issue.account);
    });
}

bool
isAnyFrozen(
    ReadView const& view,
    std::initializer_list<AccountID> const& accounts,
    Asset const& asset,
    std::uint8_t depth)
{
    return asset.visit(
        [&](Issue const& issue) { return isAnyFrozen(view, accounts, issue); },
        [&](MPTIssue const& issue) { return isAnyFrozen(view, accounts, issue, depth); });
}

bool
isDeepFrozen(
    ReadView const& view,
    AccountID const& account,
    MPTIssue const& mptIssue,
    std::uint8_t depth)
{
    // Unlike IOUs, frozen / locked MPTs are not allowed to send or receive
    // funds, so checking "deep frozen" is the same as checking "frozen".
    return isFrozen(view, account, mptIssue, depth);
}

bool
isDeepFrozen(ReadView const& view, AccountID const& account, Asset const& asset, std::uint8_t depth)
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

[[nodiscard]] TER
checkWithdrawFreeze(
    ReadView const& view,
    AccountID const& pseudoAcct,
    AccountID const& submitterAcct,
    AccountID const& dstAcct,
    Asset const& asset)
{
    XRPL_ASSERT(
        isPseudoAccount(view, pseudoAcct),
        "xrpl::checkWithdrawFreeze : source is a pseudo-account");
    XRPL_ASSERT(
        !isPseudoAccount(view, submitterAcct),
        "xrpl::checkWithdrawFreeze : submitter is not a pseudo-account");
    XRPL_ASSERT(
        !isPseudoAccount(view, dstAcct),
        "xrpl::checkWithdrawFreeze : destination is not a pseudo-account");
    // The asset being withdrawn must not be issued by a pseudo-account
    XRPL_ASSERT(
        !isPseudoAccount(view, asset.getIssuer()),
        "xrpl::checkWithdrawFreeze : asset issuer cannot be a pseudo-account");

    // Funds can always be sent to the issuer
    if (dstAcct == asset.getIssuer())
        return tesSUCCESS;

    // If the asset is globally frozen, other checks are redundant
    if (auto const ret = checkGlobalFrozen(view, asset); !isTesSuccess(ret))
        return ret;

    // The transfer is from Submitter to Destination via Source (pseudo-account)
    // Both Source and Submitter must not be frozen to allow sending funds
    if (auto const ret = checkIndividualFrozen(view, pseudoAcct, asset); !isTesSuccess(ret))
        return ret;

    // Check submitter's individual freeze only when Submitter != Destination (a regular freeze
    // should not block self-withdrawal).
    if (submitterAcct != dstAcct)
    {
        if (auto const ret = checkIndividualFrozen(view, submitterAcct, asset); !isTesSuccess(ret))
            return ret;
    }

    // The destination account must not be deep frozen to receive the funds
    if (auto const ret = checkDeepFrozen(view, dstAcct, asset); !isTesSuccess(ret))
        return ret;

    if (asset.holds<MPTIssue>() &&
        isVaultPseudoAccountFrozen(view, pseudoAcct, asset.get<MPTIssue>(), 0))
    {
        // LCOV_EXCL_START
        UNREACHABLE("xrpl::checkWithdrawFreeze : pseudo-account backed object holds shares");
        return tecINTERNAL;
        // LCOV_EXCL_STOP
    }

    return tesSUCCESS;
}

[[nodiscard]] TER
checkDepositFreeze(
    ReadView const& view,
    AccountID const& srcAcct,
    AccountID const& pseudoAcct,
    Asset const& asset)
{
    XRPL_ASSERT(
        isPseudoAccount(view, pseudoAcct),
        "xrpl::checkDepositFreeze : destination is a pseudo-account");
    XRPL_ASSERT(
        !isPseudoAccount(view, srcAcct),
        "xrpl::checkDepositFreeze : source is not a pseudo-account");
    // The asset being deposited must not be issued by a pseudo-account
    XRPL_ASSERT(
        !isPseudoAccount(view, asset.getIssuer()),
        "xrpl::checkDepositFreeze : asset issuer cannot be a pseudo-account");

    if (auto const ret = checkGlobalFrozen(view, asset); !isTesSuccess(ret))
        return ret;

    if (srcAcct != asset.getIssuer())
    {
        if (auto const ret = checkIndividualFrozen(view, srcAcct, asset); !isTesSuccess(ret))
            return ret;
    }

    // Unlike regular accounts, pseudo-accounts cannot receive deposits under a regular freeze
    // because those funds cannot be later withdrawn
    if (auto const ret = checkIndividualFrozen(view, pseudoAcct, asset); !isTesSuccess(ret))
        return ret;

    if (asset.holds<MPTIssue>() &&
        isVaultPseudoAccountFrozen(view, pseudoAcct, asset.get<MPTIssue>(), 0))
    {
        // LCOV_EXCL_START
        UNREACHABLE("xrpl::checkDepositFreeze : pseudo-account backed object holds shares");
        return tecINTERNAL;
        // LCOV_EXCL_STOP
    }

    return tesSUCCESS;
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
    auto sle = view.read(keylet::trustLine(account, issuer, currency));

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
                    isLPTokenFrozen(view, account, (*sleAmm)[sfAsset], (*sleAmm)[sfAsset2]))
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
        return STAmount{Issue{currency, issuer}, STAmount::kMaxValue, STAmount::kMaxOffset};
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
        auto const issuance = view.read(keylet::mptokenIssuance(mptIssue.getMptID()));

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

    if (!sleMpt ||
        (zeroIfFrozen == FreezeHandling::ZeroIfFrozen && isFrozen(view, account, mptIssue)))
    {
        amount.clear(mptIssue);
    }
    else
    {
        amount = STAmount{mptIssue, sleMpt->getFieldU64(sfMPTAmount)};

        // Only if auth check is needed, as it needs to do an additional read
        // operation. Note featureSingleAssetVault will affect error codes.
        if (zeroIfUnauthorized == AuthHandling::ZeroIfUnauthorized &&
            (view.rules().enabled(featureSingleAssetVault) ||
             view.rules().enabled(featureConfidentialTransfer)))
        {
            if (auto const err = requireAuth(view, mptIssue, account, AuthType::StrongAuth);
                !isTesSuccess(err))
                amount.clear(mptIssue);
        }
        else if (zeroIfUnauthorized == AuthHandling::ZeroIfUnauthorized)
        {
            auto const sleIssuance = view.read(keylet::mptokenIssuance(mptIssue.getMptID()));

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
    ApplyViewContext ctx,
    AccountID const& accountID,
    XRPAmount priorBalance,
    Asset const& asset,
    beast::Journal journal)
{
    return std::visit(
        [&]<ValidIssueType TIss>(TIss const& issue) -> TER {
            return addEmptyHolding(ctx, accountID, priorBalance, issue, journal);
        },
        asset.value());
}

TER
removeEmptyHolding(
    ApplyViewContext ctx,
    AccountID const& accountID,
    Asset const& asset,
    beast::Journal journal)
{
    return std::visit(
        [&]<ValidIssueType TIss>(TIss const& issue) -> TER {
            return removeEmptyHolding(ctx, accountID, issue, journal);
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
canTransfer(
    ReadView const& view,
    Asset const& asset,
    AccountID const& from,
    AccountID const& to,
    WaiveMPTCanTransfer waive,
    std::uint8_t depth)
{
    return asset.visit(
        [&](MPTIssue const& issue) -> TER {
            return canTransfer(view, issue, from, to, waive, depth);
        },
        [&](Issue const& issue) -> TER { return canTransfer(view, issue, from, to); });
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
//
// The optional `legPolicy` argument corresponds to the NON-ISSUER PARTY on
// this specific trust line touch. For a direct payment (one party IS the
// issuer) it is the non-issuer party's leg policy; for a transit leg it
// is either the sender-leg or receiver-leg policy of the enclosing
// two-legged send. `legPolicy` reports its `balanceDelta`/`dustDelta`
// from THAT PARTY's perspective (party-positive: positive means the
// non-issuer party's holdings grew).
static TER
directSendNoFeeIOU(
    ApplyView& view,
    AccountID const& uSenderID,
    AccountID const& uReceiverID,
    STAmount const& saAmount,
    bool bCheckIssuer,
    SLE::ref sponsorSle,
    beast::Journal j,
    DustSplit::LegPolicy* legPolicy = nullptr,
    bool legIsSender = false)
{
    AccountID const& issuer = saAmount.getIssuer();
    Currency const& currency = saAmount.get<Issue>().currency;

    // Make sure issuer is involved.
    XRPL_ASSERT(
        !bCheckIssuer || uSenderID == issuer || uReceiverID == issuer,
        "xrpl::directSendNoFeeIOU : matching issuer or don't care");
    (void)issuer;

    // Disallow sending to self.
    XRPL_ASSERT(uSenderID != uReceiverID, "xrpl::directSendNoFeeIOU : sender is not receiver");

    bool const bSenderHigh = uSenderID > uReceiverID;
    auto const index = keylet::trustLine(uSenderID, uReceiverID, currency);

    XRPL_ASSERT(
        !isXRP(uSenderID) && uSenderID != noAccount(),
        "xrpl::directSendNoFeeIOU : sender is not XRP");
    XRPL_ASSERT(
        !isXRP(uReceiverID) && uReceiverID != noAccount(),
        "xrpl::directSendNoFeeIOU : receiver is not XRP");

    // sfDust is introduced by featureLendingProtocolV1_1. Every code path
    // that reads or writes sfDust enforces the same rules().enabled(...) gate.
    // In normal operation the gate is redundant — the transactor-level
    // eligibility predicates already imply the amendment is on — but a
    // hypothetical replay/testing path that ever presented a dust-aware
    // request under a pre-amendment rules() must NOT touch sfDust: the
    // field would then be neither SoeDefault(0) as expected pre-amendment
    // nor part of the ledger's canonical encoding. Assert in debug, fall
    // back to the pre-dust code path in release.
    if (legPolicy != nullptr && !view.rules().enabled(featureLendingProtocolV1_1))
    {
        XRPL_ASSERT(
            false,
            "xrpl::directSendNoFeeIOU : DustSplit::LegPolicy requires "
            "featureLendingProtocolV1_1");
        legPolicy = nullptr;
    }

    // Drain mode is defined only for the sender's leg. There is no receiver-side dust to drain.
    XRPL_ASSERT(
        legPolicy == nullptr || legPolicy->mode != DustSplit::LegPolicy::Mode::Drain || legIsSender,
        "xrpl::directSendNoFeeIOU : Drain mode is sender-leg only");

    // If the line exists, modify it accordingly.
    if (auto const sleRippleState = view.peek(index))
    {
        STAmount saBalance = sleRippleState->getFieldAmount(sfBalance);

        if (bSenderHigh)
            saBalance.negate();  // Put balance in sender terms.

        view.creditHookIOU(uSenderID, uReceiverID, saAmount, saBalance);

        STAmount const saBefore = saBalance;

        // dustLedgerAfter holds the value that must be written to sfDust once the branch below
        // decides bDelete, in the LINE'S OWN sign convention. Left unset when this call must not
        // touch sfDust at all so a line that never carries dust is byte-identical to before.
        std::optional<Number> dustLedgerAfter;

        if (legPolicy != nullptr)
        {
            // sfBalance and sfDust together are one signed extended quantity, expressed in the
            // trust line's own low/high sign convention.
            // Convert sfDust to sender terms to line up with saBefore.
            Number const lineDustLedger = Number{sleRippleState->at(sfDust)};
            Number const lineDustSender = bSenderHigh ? -lineDustLedger : lineDustLedger;

            // The rest of the math below tolerates negative dust, but under Drain we assert
            // non-negativity because folding a negative reservoir into the outgoing transfer would
            // silently INCREASE the amount sent — this must never happen in practice.
            XRPL_ASSERT(
                lineDustSender >= beast::kZero || !legIsSender ||
                    legPolicy->mode != DustSplit::LegPolicy::Mode::Drain,
                "xrpl::directSendNoFeeIOU : sender-line dust is non-negative under Drain");

            Number newBalance{0};
            Number newDust{0};

            if (legPolicy->mode == DustSplit::LegPolicy::Mode::Drain)
            {
                // Drain: fold all of sfDust into sfBalance in place, then debit `saAmount` — which
                // the caller   already inflated by `lineDustSender` so the outgoing transfer
                // reaches the receiver as `amount + D_sender`.
                // The end state on the sender's line is:
                //     sfBalance_new = (saBefore + lineDustSender) - saAmount
                //                   = saBefore - (saAmount - lineDustSender)
                //                   = saBefore - amount_originally_requested
                //     sfDust_new    = 0
                // i.e. the sender's line loses exactly the caller's originally-requested `amount`
                // in whole-quanta terms, and the sub-quantum reservoir has been forwarded.
                newBalance = (Number{saBefore} + lineDustSender) - Number{saAmount};
                newDust = Number{0};
            }
            else
            {
                // Override mode: keep sfBalance representable at
                // `overrideScale`; park any sub-quantum remainder in
                // sfDust. Truncate the SUM toward zero — never round
                // the increment separately. This is what makes
                // promotion of previously-deferred dust automatic: if
                // the combined lineDustSender + credit clears a whole
                // quantum, that quantum lands in newBalance with no
                // separate fold step.
                Number const exactBefore = Number{saBefore} + lineDustSender;
                Number const exactAfter = exactBefore - Number{saAmount};

                newBalance = roundToAsset(
                    saAmount.asset(),
                    exactAfter,
                    legPolicy->overrideScale,
                    Number::RoundingMode::TowardsZero);
                newDust = exactAfter - newBalance;

                XRPL_ASSERT(
                    newDust == beast::kZero || abs(newDust) < Number(1, legPolicy->overrideScale),
                    "xrpl::directSendNoFeeIOU : dust remainder bounded by one quantum");
                XRPL_ASSERT(
                    newDust == beast::kZero || exactAfter == beast::kZero ||
                        (newDust < beast::kZero) == (exactAfter < beast::kZero),
                    "xrpl::directSendNoFeeIOU : dust does not change sign of the extended "
                    "quantity");
            }

            Number const balanceDeltaSender = newBalance - Number{saBefore};
            Number const dustDeltaSender = newDust - lineDustSender;

            // Report the deltas from the leg's non-issuer party's
            // perspective. Sender-leg reports sender-positive; the
            // internal computation is already in sender terms, so on
            // the sender-leg pass we report the sender-space deltas as
            // is. Receiver-leg reports receiver-positive, which is the
            // sign-flip of sender-space deltas (since balance changes
            // net to -saAmount).
            if (legIsSender)
            {
                legPolicy->balanceDelta = balanceDeltaSender;
                legPolicy->dustDelta = dustDeltaSender;
            }
            else
            {
                legPolicy->balanceDelta = -balanceDeltaSender;
                legPolicy->dustDelta = -dustDeltaSender;
            }

            // newBalance is in SENDER terms (built from saBefore, which
            // was put there by the negation above); the existing
            // negate-back-to-ledger-terms code below applies to it
            // exactly as it would to the non-dust subtraction result.
            saBalance = STAmount{saAmount.asset(), newBalance};

            dustLedgerAfter = bSenderHigh ? -newDust : newDust;
        }
        else
        {
            saBalance -= saAmount;
        }

        JLOG(j.trace()) << "directSendNoFeeIOU: " << to_string(uSenderID) << " -> "
                        << to_string(uReceiverID) << " : before=" << saBefore.getFullText()
                        << " amount=" << saAmount.getFullText()
                        << " after=" << saBalance.getFullText();

        bool bDelete = false;

        auto const senderReserveFlag = bSenderHigh ? lsfHighReserve : lsfLowReserve;
        auto const senderNoRippleFlag = bSenderHigh ? lsfHighNoRipple : lsfLowNoRipple;
        auto const senderFreezeFlag = bSenderHigh ? lsfHighFreeze : lsfLowFreeze;
        auto const receiverReserveFlag = bSenderHigh ? lsfLowReserve : lsfHighReserve;

        // FIXME This NEEDS to be cleaned up and simplified. It's impossible
        //       for anyone to understand.
        if (saBefore > beast::kZero
            // Sender balance was positive.
            && saBalance <= beast::kZero
            // Sender is zero or negative.
            && sleRippleState->isFlag(senderReserveFlag)
            // Sender reserve is set.
            && sleRippleState->isFlag(senderNoRippleFlag) !=
                view.read(keylet::account(uSenderID))->isFlag(lsfDefaultRipple) &&
            !sleRippleState->isFlag(senderFreezeFlag) &&
            !sleRippleState->getFieldAmount(bSenderHigh ? sfHighLimit : sfLowLimit)
            // Sender trust limit is 0.
            && (sleRippleState->getFieldU32(bSenderHigh ? sfHighQualityIn : sfLowQualityIn) == 0u)
            // Sender quality in is 0.
            &&
            (sleRippleState->getFieldU32(bSenderHigh ? sfHighQualityOut : sfLowQualityOut) == 0u))
        // Sender quality out is 0.
        {
            // Clear the reserve of the sender, possibly delete the line!
            auto const currentSponsor = getLedgerEntryReserveSponsor(
                view, sleRippleState, !bSenderHigh ? sfLowSponsor : sfHighSponsor);
            decreaseOwnerCount(view, view.peek(keylet::account(uSenderID)), currentSponsor, 1, j);

            removeSponsorFromLedgerEntry(
                sleRippleState, !bSenderHigh ? sfLowSponsor : sfHighSponsor);

            // Clear reserve flag.
            sleRippleState->clearFlag(senderReserveFlag);

            // Balance is zero, receiver reserve is clear.
            bDelete = !saBalance  // Balance is zero.
                && !sleRippleState->isFlag(receiverReserveFlag);
            // Receiver reserve is clear.
        }

        // Trust-line-level protocol invariant: never delete a line whose
        // sfDust is non-zero — a deleted RIPPLE_STATE silently destroys
        // any value still parked there. The guard applies to the
        // dust-unaware branch too (legPolicy == nullptr), because the
        // line may carry sfDust left by an earlier dust-aware credit.
        // Only relevant when the line is actually a deletion candidate;
        // skip the field-read entirely otherwise so the hot path (the
        // overwhelming majority of calls, where bDelete is already
        // false) stays byte-identical to the base branch.
        if (bDelete)
        {
            if (dustLedgerAfter)
            {
                if (*dustLedgerAfter != beast::kZero)
                    bDelete = false;
            }
            else if (
                view.rules().enabled(featureLendingProtocolV1_1) &&
                Number{sleRippleState->at(sfDust)} != beast::kZero)
            {
                bDelete = false;
            }
        }

        if (bSenderHigh)
            saBalance.negate();

        // Want to reflect balance to zero even if we are deleting line.
        sleRippleState->setFieldAmount(sfBalance, saBalance);
        // ONLY: Adjust balance.

        if (dustLedgerAfter)
            sleRippleState->at(sfDust) = *dustLedgerAfter;

        if (bDelete)
        {
            return trustDelete(
                view,
                sleRippleState,
                bSenderHigh ? uReceiverID : uSenderID,
                bSenderHigh ? uSenderID : uReceiverID,
                j);
        }

        view.update(sleRippleState);
        return tesSUCCESS;
    }

    // A LegPolicy implies the trust line already exists — the caller is
    // supposed to arrange this (e.g. via addEmptyHolding) before any
    // dust-aware credit reaches it. Treat reaching here with a policy
    // as a caller error: assert in debug, and in release fall back to a
    // plain no-split credit so no value is silently dropped.
    XRPL_ASSERT(
        legPolicy == nullptr,
        "xrpl::directSendNoFeeIOU : dust split requires an existing trust line");
    if (legPolicy != nullptr)
    {
        // LCOV_EXCL_START
        legPolicy->balanceDelta = legIsSender ? -Number{saAmount} : Number{saAmount};
        legPolicy->dustDelta = Number{0};
        // LCOV_EXCL_STOP
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

    bool const noRipple = !sleAccount->isFlag(lsfDefaultRipple);

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
        sponsorSle,
        j);
}

// Send regardless of limits.
// --> saAmount: Amount/currency/issuer to deliver to receiver.
// <-- saActual: Amount actually cost.  Sender pays fees.
static TER
directSendNoLimitIOU(
    ApplyView& view,
    AccountID const& uSenderID,
    AccountID const& uReceiverID,
    STAmount const& saAmount,
    STAmount& saActual,
    beast::Journal j,
    SLE::ref sponsorSle,
    WaiveTransferFee waiveFee,
    DustSplit* dust = nullptr)
{
    auto const& issuer = saAmount.getIssuer();

    XRPL_ASSERT(
        !isXRP(uSenderID) && !isXRP(uReceiverID),
        "xrpl::directSendNoLimitIOU : neither sender nor receiver is XRP");
    XRPL_ASSERT(uSenderID != uReceiverID, "xrpl::directSendNoLimitIOU : sender is not receiver");

    DustSplit::LegPolicy* receiverPolicy =
        (dust != nullptr && dust->receiver.has_value()) ? &*dust->receiver : nullptr;
    DustSplit::LegPolicy* senderPolicy =
        (dust != nullptr && dust->sender.has_value()) ? &*dust->sender : nullptr;

    // Drain sender-leg: inflate the outgoing amount by the sender-line's
    // sfDust (in sender terms) so the receiver receives `amount + D_sender`.
    // The sender's line ends with `sfBalance` reduced by the caller's
    // originally-requested `amount` (whole-quanta portion) and `sfDust`
    // zeroed. Drain is meaningless when the sender IS the issuer (no
    // sender-side line exists on the issuer's side).
    STAmount saAmountEffective = saAmount;
    if (senderPolicy != nullptr && senderPolicy->mode == DustSplit::LegPolicy::Mode::Drain &&
        uSenderID != issuer && view.rules().enabled(featureLendingProtocolV1_1))
    {
        Currency const& currency = saAmount.get<Issue>().currency;
        auto const senderLine = view.peek(keylet::trustLine(uSenderID, issuer, currency));
        if (senderLine)
        {
            bool const senderIsHigh = uSenderID > issuer;
            Number const dustLineTerms = Number{senderLine->at(sfDust)};
            Number const dustSenderTerms = senderIsHigh ? -dustLineTerms : dustLineTerms;
            XRPL_ASSERT(
                dustSenderTerms >= beast::kZero,
                "xrpl::directSendNoLimitIOU : sender-line dust non-negative under Drain");
            if (dustSenderTerms != beast::kZero)
            {
                saAmountEffective = saAmount + STAmount{saAmount.asset(), dustSenderTerms};
            }
        }
    }

    if (uSenderID == issuer || uReceiverID == issuer || issuer == noAccount())
    {
        // Direct send: redeeming IOUs and/or sending own IOUs. Only one
        // trust line is touched; the corresponding non-issuer party's
        // LegPolicy applies. The issuer-side policy must be absent —
        // there is no line to keep aligned on the issuer's side.
        DustSplit::LegPolicy* legPolicy = nullptr;
        bool legIsSender = false;
        if (dust != nullptr)
        {
            if (uSenderID == issuer)
            {
                XRPL_ASSERT(
                    !dust->sender.has_value(),
                    "xrpl::directSendNoLimitIOU : issuer-side sender policy must be absent");
                if (dust->receiver.has_value())
                {
                    legPolicy = &*dust->receiver;
                    legIsSender = false;
                }
            }
            else if (uReceiverID == issuer)
            {
                XRPL_ASSERT(
                    !dust->receiver.has_value(),
                    "xrpl::directSendNoLimitIOU : issuer-side receiver policy must be absent");
                if (dust->sender.has_value())
                {
                    legPolicy = &*dust->sender;
                    legIsSender = true;
                }
            }
            else
            {
                // issuer == noAccount(): treat as a receiver-leg-only
                // credit (matches historical behaviour of the flat
                // DustSplit passed through here).
                if (dust->receiver.has_value())
                {
                    legPolicy = &*dust->receiver;
                    legIsSender = false;
                }
            }
        }

        auto const ter = directSendNoFeeIOU(
            view,
            uSenderID,
            uReceiverID,
            saAmountEffective,
            false,
            sponsorSle,
            j,
            legPolicy,
            legIsSender);
        if (!isTesSuccess(ter))
            return ter;
        saActual = saAmountEffective;
        return tesSUCCESS;
    }

    // Sending 3rd party IOUs: transit.

    // Calculate the amount to transfer accounting
    // for any transfer fees if the fee is not waived:
    saActual = (waiveFee == WaiveTransferFee::Yes)
        ? saAmountEffective
        : multiply(saAmountEffective, transferRate(view, issuer));

    JLOG(j.debug()) << "directSendNoLimitIOU> " << to_string(uSenderID) << " - > "
                    << to_string(uReceiverID) << " : deliver=" << saAmountEffective.getFullText()
                    << " cost=" << saActual.getFullText();

    // Transit path: two trust-line touches. The receiver-leg policy
    // applies to the (issuer -> receiver) credit; the sender-leg
    // policy applies to the (sender -> issuer) debit. The effective
    // `saAmountEffective` reaches both legs — inflated when Drain is
    // active — so the receiver's inflow and the sender's outflow line
    // up in extended-balance terms.
    TER terResult = directSendNoFeeIOU(
        view,
        issuer,
        uReceiverID,
        saAmountEffective,
        true,
        sponsorSle,
        j,
        receiverPolicy,
        /*legIsSender=*/false);

    if (tesSUCCESS == terResult)
    {
        terResult = directSendNoFeeIOU(
            view,
            uSenderID,
            issuer,
            saActual,
            true,
            sponsorSle,
            j,
            senderPolicy,
            /*legIsSender=*/true);
    }

    return terResult;
}

// Send regardless of limits.
// --> receivers: Amount/currency/issuer to deliver to receivers.
// <-- saActual: Amount actually cost to sender.  Sender pays fees.
//
// The optional `dust` split applies only to the sender's leg (a single
// trust line shared across all receivers). Only `dust->sender` is
// consulted; `dust->receiver` must be nullopt.
static TER
directSendNoLimitMultiIOU(
    ApplyView& view,
    AccountID const& senderID,
    Issue const& issue,
    MultiplePaymentDestinations const& receivers,
    STAmount& actual,
    beast::Journal j,
    WaiveTransferFee waiveFee,
    DustSplit* dust = nullptr)
{
    auto const& issuer = issue.getIssuer();

    XRPL_ASSERT(!isXRP(senderID), "xrpl::directSendNoLimitMultiIOU : sender is not XRP");
    XRPL_ASSERT(
        dust == nullptr || !dust->receiver.has_value(),
        "xrpl::directSendNoLimitMultiIOU : receiver-leg policy has no meaning in multi-send");

    DustSplit::LegPolicy* senderPolicy =
        (dust != nullptr && dust->sender.has_value()) ? &*dust->sender : nullptr;

    // These may diverge
    STAmount takeFromSender{issue};
    actual = takeFromSender;

    // Failures return immediately.
    for (auto const& r : receivers)
    {
        auto const& receiverID = r.first;
        STAmount const amount{issue, r.second};

        /* If we aren't sending anything or if the sender is the same as the
         * receiver then we don't need to do anything.
         */
        if (!amount || (senderID == receiverID))
            continue;

        XRPL_ASSERT(!isXRP(receiverID), "xrpl::directSendNoLimitMultiIOU : receiver is not XRP");

        if (senderID == issuer || receiverID == issuer || issuer == noAccount())
        {
            // Direct send: redeeming IOUs and/or sending own IOUs.
            // The sender-leg policy is applied only on the final bulk
            // sender-line debit below, not on any direct-to-issuer
            // intermediate credits — a sender-leg policy that touched
            // the sender's line multiple times would overwrite its own
            // out fields. In the current Vault-only use of this path
            // (loan disbursement), no recipient is the asset issuer, so
            // this branch never runs alongside a non-null senderPolicy.
            if (auto const ter = directSendNoFeeIOU(
                    view, senderID, receiverID, amount, false, {}, j, nullptr, false);
                !isTesSuccess(ter))
                return ter;
            actual += amount;
            // Do not add amount to takeFromSender, because directSendNoFeeIOU took
            // it.

            continue;
        }

        // Sending 3rd party IOUs: transit.

        // Calculate the amount to transfer accounting
        // for any transfer fees if the fee is not waived:
        STAmount const actualSend = (waiveFee == WaiveTransferFee::Yes)
            ? amount
            : multiply(amount, transferRate(view, issuer));
        actual += actualSend;
        takeFromSender += actualSend;

        JLOG(j.debug()) << "directSendNoLimitMultiIOU> " << to_string(senderID) << " - > "
                        << to_string(receiverID) << " : deliver=" << amount.getFullText()
                        << " cost=" << actual.getFullText();

        // Receiver-leg policy is not defined for multi-send (there
        // are multiple receiver lines). Pass nullptr here.
        if (TER const terResult = directSendNoFeeIOU(
                view, issuer, receiverID, amount, true, {}, j, nullptr, /*legIsSender=*/false))
            return terResult;
    }

    if (senderID != issuer && takeFromSender)
    {
        // Bulk sender-line debit for all transit legs — this is the
        // single point where the sender's line changes for this
        // multi-send, so it's the only place the sender-leg policy
        // can apply.
        if (TER const terResult = directSendNoFeeIOU(
                view,
                senderID,
                issuer,
                takeFromSender,
                true,
                {},
                j,
                senderPolicy,
                /*legIsSender=*/true))
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
    SLE::ref sponsorSle,
    WaiveTransferFee waiveFee,
    DustSplit* dust = nullptr)
{
    if (view.rules().enabled(fixAMMv1_1))
    {
        if (saAmount < beast::kZero || saAmount.holds<MPTIssue>())
        {
            return tecINTERNAL;  // LCOV_EXCL_LINE
        }
    }
    else
    {
        // LCOV_EXCL_START
        XRPL_ASSERT(
            saAmount >= beast::kZero && !saAmount.holds<MPTIssue>(),
            "xrpl::accountSendIOU : minimum amount and not MPT");
        // LCOV_EXCL_STOP
    }

    // Drain sender-leg can legitimately request a zero-amount call: the
    // caller (e.g. terminal Vault removal) may have amount==0 but still
    // want to drain any residual sfDust on the sender's line. Route
    // through in that case so directSendNoLimitIOU can peek the sender's
    // line and inflate saAmount by the reservoir. All other zero-amount
    // or self-sends short-circuit as before.
    bool const drainRequested = dust != nullptr && dust->sender.has_value() &&
        dust->sender->mode == DustSplit::LegPolicy::Mode::Drain;
    if ((!saAmount && !drainRequested) || (uSenderID == uReceiverID))
        return tesSUCCESS;

    if (!saAmount.native())
    {
        STAmount saActual;

        JLOG(j.trace()) << "accountSendIOU: " << to_string(uSenderID) << " -> "
                        << to_string(uReceiverID) << " : " << saAmount.getFullText();

        return directSendNoLimitIOU(
            view, uSenderID, uReceiverID, saAmount, saActual, j, sponsorSle, waiveFee, dust);
    }

    XRPL_ASSERT(dust == nullptr, "xrpl::accountSendIOU : dust split is IOU-only");

    /* XRP send which does not check reserve and can do pure adjustment.
     * Note that sender or receiver may be null and this not a mistake; this
     * setup is used during pathfinding and it is carefully controlled to
     * ensure that transfers are balanced.
     */
    TER terResult(tesSUCCESS);

    SLE::pointer const sender =
        uSenderID != beast::kZero ? view.peek(keylet::account(uSenderID)) : SLE::pointer();
    SLE::pointer const receiver =
        uReceiverID != beast::kZero ? view.peek(keylet::account(uReceiverID)) : SLE::pointer();

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

static TER
accountSendMultiIOU(
    ApplyView& view,
    AccountID const& senderID,
    Issue const& issue,
    MultiplePaymentDestinations const& receivers,
    beast::Journal j,
    WaiveTransferFee waiveFee,
    DustSplit* dust = nullptr)
{
    XRPL_ASSERT_PARTS(
        receivers.size() > 1, "xrpl::accountSendMultiIOU", "multiple recipients provided");

    if (!issue.native())
    {
        STAmount actual;
        JLOG(j.trace()) << "accountSendMultiIOU: " << to_string(senderID) << " sending "
                        << receivers.size() << " IOUs";

        return directSendNoLimitMultiIOU(
            view, senderID, issue, receivers, actual, j, waiveFee, dust);
    }

    XRPL_ASSERT(dust == nullptr, "xrpl::accountSendMultiIOU : dust split is IOU-only");

    /* XRP send which does not check reserve and can do pure adjustment.
     * Note that sender or receiver may be null and this not a mistake; this
     * setup could be used during pathfinding and it is carefully controlled to
     * ensure that transfers are balanced.
     */

    SLE::pointer const sender =
        senderID != beast::kZero ? view.peek(keylet::account(senderID)) : SLE::pointer();

    if (auto stream = j.trace())
    {
        std::string senderBal("-");

        if (sender)
            senderBal = sender->getFieldAmount(sfBalance).getFullText();

        stream << "accountSendMultiIOU> " << to_string(senderID) << " (" << senderBal << ") -> "
               << receivers.size() << " receivers.";
    }

    // Failures return immediately.
    STAmount takeFromSender{issue};
    for (auto const& r : receivers)
    {
        auto const& receiverID = r.first;
        STAmount const amount{issue, r.second};

        if (amount < beast::kZero)
        {
            return tecINTERNAL;  // LCOV_EXCL_LINE
        }

        /* If we aren't sending anything or if the sender is the same as the
         * receiver then we don't need to do anything.
         */
        if (!amount || (senderID == receiverID))
            continue;

        SLE::pointer const receiver =
            receiverID != beast::kZero ? view.peek(keylet::account(receiverID)) : SLE::pointer();

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
            // Increment XRP balance.
            auto const rcvBal = receiver->getFieldAmount(sfBalance);
            receiver->setFieldAmount(sfBalance, rcvBal + amount);
            view.creditHookIOU(xrpAccount(), receiverID, amount, -rcvBal);

            view.update(receiver);

            // Take what is actually sent
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

        // Decrement XRP balance.
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

static TER
directSendNoFeeMPT(
    ApplyView& view,
    AccountID const& uSenderID,
    AccountID const& uReceiverID,
    STAmount const& saAmount,
    beast::Journal j)
{
    // Do not check MPT authorization here - it must have been checked earlier
    auto const mptID = keylet::mptokenIssuance(saAmount.get<MPTIssue>().getMptID());
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
            if (view.rules().enabled(featureMPTokensV2))
            {
                if ((*sle)[sfMPTAmount] > (std::numeric_limits<std::uint64_t>::max() - amt))
                {
                    return tecINTERNAL;  // LCOV_EXCL_LINE
                }
            }
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

    // Safe to get MPT since directSendNoLimitMPT is only called by accountSendMPT
    auto const& issuer = saAmount.getIssuer();

    auto const sle = view.read(keylet::mptokenIssuance(saAmount.get<MPTIssue>().getMptID()));
    if (!sle)
        return tecOBJECT_NOT_FOUND;

    if (uSenderID == issuer || uReceiverID == issuer)
    {
        // if sender is issuer, check that the new OutstandingAmount will not
        // exceed MaximumAmount
        if (uSenderID == issuer)
        {
            auto const sendAmount = saAmount.mpt().value();
            auto const maxAmount = maxMPTAmount(*sle);
            auto const outstanding = sle->getFieldU64(sfOutstandingAmount);
            auto const mptokensV2 = view.rules().enabled(featureMPTokensV2);
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

    // Sending 3rd party MPTs: transit.
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

    auto const sle = view.read(keylet::mptokenIssuance(mptIssue.getMptID()));
    if (!sle)
        return tecOBJECT_NOT_FOUND;

    // For the issuer-as-sender case, track the running total to validate
    // against MaximumAmount. The read-only SLE (view.read) is not updated
    // by directSendNoFeeMPT, so a per-iteration SLE read would be stale.
    // Use uint64_t, not STAmount, to keep MaximumAmount comparisons in exact
    // integer arithmetic. STAmount implicitly converts to Number, whose
    // small-scale mantissa (~16 digits) can lose precision for values near
    // kMaxMpTokenAmount (19 digits).
    std::uint64_t totalSendAmount{0};
    std::uint64_t const maximumAmount = sle->at(~sfMaximumAmount).value_or(kMaxMpTokenAmount);
    std::uint64_t const outstandingAmount = sle->getFieldU64(sfOutstandingAmount);

    // actual accumulates the total cost to the sender (includes transfer
    // fees for third-party transit sends). takeFromSender accumulates only
    // the transit portion that is debited to the issuer in bulk after the
    // loop. They diverge when there are transfer fees.
    STAmount takeFromSender{mptIssue};
    actual = takeFromSender;

    for (auto const& [receiverID, amt] : receivers)
    {
        STAmount const amount{mptIssue, amt};

        if (amount < beast::kZero)
            return tecINTERNAL;  // LCOV_EXCL_LINE

        if (!amount || senderID == receiverID)
            continue;

        if (senderID == issuer || receiverID == issuer)
        {
            if (senderID == issuer)
            {
                XRPL_ASSERT_PARTS(
                    takeFromSender == beast::kZero,
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
                        // This send alone exceeds the max cap
                        sendAmount > maximumAmount ||
                        // The aggregate of all sends exceeds the max cap
                        totalSendAmount > maximumAmount - sendAmount ||
                        // Outstanding + aggregate exceeds the max cap
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

            // Direct send: redeeming MPTs and/or sending own MPTs.
            if (auto const ter = directSendNoFeeMPT(view, senderID, receiverID, amount, j);
                !isTesSuccess(ter))
                return ter;
            actual += amount;
            // Do not add amount to takeFromSender, because directSendNoFeeMPT took it.

            continue;
        }

        // Sending 3rd party MPTs: transit.
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
        saAmount >= beast::kZero && saAmount.holds<MPTIssue>(),
        "xrpl::accountSendMPT : minimum amount and MPT");

    /* If we aren't sending anything or if the sender is the same as the
     * receiver then we don't need to do anything.
     */
    if (!saAmount || (uSenderID == uReceiverID))
        return tesSUCCESS;

    STAmount saActual{saAmount.asset()};

    return directSendNoLimitMPT(
        view, uSenderID, uReceiverID, saAmount, saActual, j, waiveFee, allowOverflow);
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
            return directSendNoFeeIOU(
                view,
                uSenderID,
                uReceiverID,
                saAmount,
                bCheckIssuer,
                {},
                j,
                /*legPolicy=*/nullptr,
                /*legIsSender=*/false);
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
    SLE::ref sponsorSle,
    WaiveTransferFee waiveFee,
    AllowMPTOverflow allowOverflow,
    DustSplit* dust)
{
    return saAmount.asset().visit(
        [&](Issue const&) {
            return accountSendIOU(
                view, uSenderID, uReceiverID, saAmount, j, sponsorSle, waiveFee, dust);
        },
        [&](MPTIssue const&) {
            XRPL_ASSERT(dust == nullptr, "xrpl::accountSend : dust split is IOU-only");
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
    WaiveTransferFee waiveFee,
    DustSplit* dust)
{
    XRPL_ASSERT_PARTS(
        receivers.size() > 1, "xrpl::accountSendMulti", "multiple recipients provided");
    return asset.visit(
        [&](Issue const& issue) {
            return accountSendMultiIOU(view, senderID, issue, receivers, j, waiveFee, dust);
        },
        [&](MPTIssue const& issue) {
            XRPL_ASSERT(dust == nullptr, "xrpl::accountSendMulti : dust split is IOU-only");
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
    XRPL_ASSERT(from != beast::kZero, "xrpl::transferXRP : nonzero from account");
    XRPL_ASSERT(to != beast::kZero, "xrpl::transferXRP : nonzero to account");
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
