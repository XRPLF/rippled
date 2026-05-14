/**
 * @file Payment.cpp
 * @brief Implementation of the `ttPAYMENT` transactor.
 *
 * A single transaction type covers three structurally distinct execution paths:
 * direct XRP-to-XRP transfers, direct MPToken (MPT) transfers (pre-`featureMPTokensV2`),
 * and cross-currency / path-based payments routed through `path::RippleCalc`.
 * The branching lives entirely in `doApply()`; the serialized transaction format
 * is the same for all three cases.
 *
 * See `Payment.h` for the full class documentation and per-method contracts.
 */
#include <xrpl/tx/transactors/payment/Payment.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/PaymentSandbox.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/CredentialHelpers.h>
#include <xrpl/ledger/helpers/DelegateHelpers.h>
#include <xrpl/ledger/helpers/MPTokenHelpers.h>
#include <xrpl/ledger/helpers/PermissionedDEXHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Permissions.h>
#include <xrpl/protocol/Quality.h>
#include <xrpl/protocol/Rate.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STPathSet.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/tx/Transactor.h>
#include <xrpl/tx/applySteps.h>
#include <xrpl/tx/paths/RippleCalc.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_set>

namespace xrpl {

TxConsequences
Payment::makeTxConsequences(PreflightContext const& ctx)
{
    auto calculateMaxXRPSpend = [](STTx const& tx) -> XRPAmount {
        STAmount const maxAmount = tx.isFieldPresent(sfSendMax) ? tx[sfSendMax] : tx[sfAmount];

        // If there's no sfSendMax in XRP, and the sfAmount isn't
        // in XRP, then the transaction does not spend XRP.
        return maxAmount.native() ? maxAmount.xrp() : beast::kZERO;
    };

    return TxConsequences{ctx.tx, calculateMaxXRPSpend(ctx.tx)};
}

/**
 * Computes the maximum amount the sender is willing to spend.
 *
 * When `sfSendMax` is present it is returned as-is. When it is absent
 * and the destination asset is an XRP or MPT, the destination amount
 * itself is the ceiling. For IOU destinations the amount is re-expressed
 * with the *sender's* account as the issuer, because IOU trust lines are
 * scoped to the issuer: the "same" currency from two different issuers is
 * not fungible and must be tracked separately.
 *
 * @param account   The sending account, used to substitute the IOU issuer.
 * @param dstAmount The requested destination amount (`sfAmount`).
 * @param sendMax   The optional `sfSendMax` field from the transaction.
 * @return The effective maximum source amount.
 */
STAmount
getMaxSourceAmount(
    AccountID const& account,
    STAmount const& dstAmount,
    std::optional<STAmount> const& sendMax)
{
    if (sendMax)
    {
        return *sendMax;
    }
    return dstAmount.asset().visit(
        [&](MPTIssue const& issue) { return dstAmount; },
        [&](Issue const& issue) {
            if (issue.native())
                return dstAmount;
            return STAmount(
                Issue{issue.currency, account},
                dstAmount.mantissa(),
                dstAmount.exponent(),
                dstAmount < beast::kZERO);
        });
}

bool
Payment::checkExtraFeatures(PreflightContext const& ctx)
{
    if (ctx.tx.isFieldPresent(sfCredentialIDs) && !ctx.rules.enabled(featureCredentials))
        return false;
    if (ctx.tx.isFieldPresent(sfDomainID) && !ctx.rules.enabled(featurePermissionedDEX))
        return false;

    return true;
}

std::uint32_t
Payment::getFlagsMask(PreflightContext const& ctx)
{
    auto& tx = ctx.tx;

    STAmount const dstAmount(tx.getFieldAmount(sfAmount));
    bool const isDstMPT = dstAmount.holds<MPTIssue>();
    bool const mpTokensV2 = ctx.rules.enabled(featureMPTokensV2);

    constexpr std::uint32_t kTF_MPT_PAYMENT_MASK_V1 = ~(tfUniversal | tfPartialPayment);
    std::uint32_t const paymentMask =
        (isDstMPT && !mpTokensV2) ? kTF_MPT_PAYMENT_MASK_V1 : tfPaymentMask;

    return paymentMask;
}

NotTEC
Payment::preflight(PreflightContext const& ctx)
{
    auto& tx = ctx.tx;
    auto& j = ctx.j;

    STAmount const dstAmount(tx.getFieldAmount(sfAmount));
    bool const isDstMPT = dstAmount.holds<MPTIssue>();
    bool const mpTokensV2 = ctx.rules.enabled(featureMPTokensV2);

    if (!ctx.rules.enabled(featureMPTokensV1) && isDstMPT)
        return temDISABLED;

    std::uint32_t const txFlags = tx.getFlags();

    if (!mpTokensV2 && isDstMPT && ctx.tx.isFieldPresent(sfPaths))
        return temMALFORMED;

    bool const partialPaymentAllowed = (txFlags & tfPartialPayment) != 0u;
    bool const limitQuality = (txFlags & tfLimitQuality) != 0u;
    bool const defaultPathsAllowed = (txFlags & tfNoRippleDirect) == 0u;
    bool const hasPaths = tx.isFieldPresent(sfPaths);
    bool const hasMax = tx.isFieldPresent(sfSendMax);

    auto const deliverMin = tx[~sfDeliverMin];

    auto const account = tx.getAccountID(sfAccount);
    STAmount const maxSourceAmount = getMaxSourceAmount(account, dstAmount, tx[~sfSendMax]);

    if (!mpTokensV2 &&
        ((isDstMPT && dstAmount.asset() != maxSourceAmount.asset()) ||
         (!isDstMPT && maxSourceAmount.holds<MPTIssue>())))
    {
        JLOG(j.trace()) << "Malformed transaction: inconsistent issues: " << dstAmount.getFullText()
                        << " " << maxSourceAmount.getFullText() << " "
                        << deliverMin.value_or(STAmount{}).getFullText();
        return temMALFORMED;
    }

    auto const& srcAsset = maxSourceAmount.asset();
    auto const& dstAsset = dstAmount.asset();

    bool const xrpDirect = srcAsset.native() && dstAsset.native();

    if (!isLegalNet(dstAmount) || !isLegalNet(maxSourceAmount))
        return temBAD_AMOUNT;

    auto const dstAccountID = tx.getAccountID(sfDestination);

    if (!dstAccountID)
    {
        JLOG(j.trace()) << "Malformed transaction: "
                        << "Payment destination account not specified.";
        return temDST_NEEDED;
    }
    if (hasMax && maxSourceAmount <= beast::kZERO)
    {
        JLOG(j.trace()) << "Malformed transaction: bad max amount: "
                        << maxSourceAmount.getFullText();
        return temBAD_AMOUNT;
    }
    if (dstAmount <= beast::kZERO)
    {
        JLOG(j.trace()) << "Malformed transaction: bad dst amount: " << dstAmount.getFullText();
        return temBAD_AMOUNT;
    }
    auto bad = [&](auto const& asset) {
        if (ctx.rules.enabled(featureMPTokensV2))
            return badAsset() == asset;
        return badCurrency() == asset;
    };
    if (bad(srcAsset) || bad(dstAsset))
    {
        JLOG(j.trace()) << "Malformed transaction: Bad currency.";
        return temBAD_CURRENCY;
    }
    if (account == dstAccountID && equalTokens(srcAsset, dstAsset) && !hasPaths)
    {
        // A self-payment with no paths is always redundant. With paths the
        // sender may be attempting an arbitrage cycle, which is permitted.
        JLOG(j.trace()) << "Malformed transaction: "
                        << "Redundant payment from " << to_string(account)
                        << " to self without path for " << to_string(dstAsset);
        return temREDUNDANT;
    }
    if (xrpDirect && hasMax)
    {
        JLOG(j.trace()) << "Malformed transaction: "
                        << "SendMax specified for XRP to XRP.";
        return temBAD_SEND_XRP_MAX;
    }
    if ((xrpDirect || (!mpTokensV2 && isDstMPT)) && hasPaths)
    {
        JLOG(j.trace()) << "Malformed transaction: "
                        << "Paths specified for XRP to XRP or MPT to MPT.";
        return temBAD_SEND_XRP_PATHS;
    }
    if (xrpDirect && partialPaymentAllowed)
    {
        JLOG(j.trace()) << "Malformed transaction: "
                        << "Partial payment specified for XRP to XRP.";
        return temBAD_SEND_XRP_PARTIAL;
    }
    if ((xrpDirect || (!mpTokensV2 && isDstMPT)) && limitQuality)
    {
        JLOG(j.trace()) << "Malformed transaction: "
                        << "Limit quality specified for XRP to XRP or MPT to MPT.";
        return temBAD_SEND_XRP_LIMIT;
    }
    if ((xrpDirect || (!mpTokensV2 && isDstMPT)) && !defaultPathsAllowed)
    {
        JLOG(j.trace()) << "Malformed transaction: "
                        << "No ripple direct specified for XRP to XRP or MPT to MPT.";
        return temBAD_SEND_XRP_NO_DIRECT;
    }

    if (deliverMin)
    {
        if (!partialPaymentAllowed)
        {
            JLOG(j.trace()) << "Malformed transaction: Partial payment not "
                               "specified for "
                            << jss::DeliverMin.cStr() << ".";
            return temBAD_AMOUNT;
        }

        auto const dMin = *deliverMin;
        if (!isLegalNet(dMin) || dMin <= beast::kZERO)
        {
            JLOG(j.trace()) << "Malformed transaction: Invalid " << jss::DeliverMin.cStr()
                            << " amount. " << dMin.getFullText();
            return temBAD_AMOUNT;
        }
        if (dMin.asset() != dstAmount.asset())
        {
            JLOG(j.trace()) << "Malformed transaction: Dst issue differs "
                               "from "
                            << jss::DeliverMin.cStr() << ". " << dMin.getFullText();
            return temBAD_AMOUNT;
        }
        if (dMin > dstAmount)
        {
            JLOG(j.trace()) << "Malformed transaction: Dst amount less than "
                            << jss::DeliverMin.cStr() << ". " << dMin.getFullText();
            return temBAD_AMOUNT;
        }
    }

    if (auto const err = credentials::checkFields(ctx.tx, ctx.j); !isTesSuccess(err))
        return err;

    return tesSUCCESS;
}

NotTEC
Payment::checkPermission(ReadView const& view, STTx const& tx)
{
    auto const delegate = tx[~sfDelegate];
    if (!delegate)
        return tesSUCCESS;

    auto const delegateKey = keylet::delegate(tx[sfAccount], *delegate);
    auto const sle = view.read(delegateKey);

    if (!sle)
        return terNO_DELEGATE_PERMISSION;

    if (isTesSuccess(checkTxPermission(sle, tx)))
        return tesSUCCESS;

    std::unordered_set<GranularPermissionType> granularPermissions;
    loadGranularPermission(sle, ttPAYMENT, granularPermissions);

    auto const& dstAmount = tx.getFieldAmount(sfAmount);
    auto const& amountAsset = dstAmount.asset();

    // Granular permissions are only valid for direct payments.
    if ((tx.isFieldPresent(sfSendMax) && tx[sfSendMax].asset() != amountAsset) ||
        tx.isFieldPresent(sfPaths))
        return terNO_DELEGATE_PERMISSION;

    // PaymentMint and PaymentBurn apply to both IOU and MPT direct payments.
    if (granularPermissions.contains(PaymentMint) && !isXRP(amountAsset) &&
        amountAsset.getIssuer() == tx[sfAccount])
        return tesSUCCESS;

    if (granularPermissions.contains(PaymentBurn) && !isXRP(amountAsset) &&
        amountAsset.getIssuer() == tx[sfDestination])
        return tesSUCCESS;

    return terNO_DELEGATE_PERMISSION;
}

TER
Payment::preclaim(PreclaimContext const& ctx)
{
    std::uint32_t const txFlags = ctx.tx.getFlags();
    bool const partialPaymentAllowed = (txFlags & tfPartialPayment) != 0u;
    auto const hasPaths = ctx.tx.isFieldPresent(sfPaths);
    auto const sendMax = ctx.tx[~sfSendMax];

    AccountID const dstAccountID(ctx.tx[sfDestination]);
    STAmount const dstAmount(ctx.tx[sfAmount]);

    auto const k = keylet::account(dstAccountID);
    auto const sleDst = ctx.view.read(k);

    if (!sleDst)
    {
        if (!dstAmount.native())
        {
            JLOG(ctx.j.trace()) << "Delay transaction: Destination account does not exist.";

            // tec (not tem) because another transaction could create the
            // account first, after which this one would succeed.
            return tecNO_DST;
        }
        if (ctx.view.open() && partialPaymentAllowed)
        {
            // Partial payments cannot create accounts; use tel (not tec) to
            // make retry cheaper by dropping the transaction early.
            JLOG(ctx.j.trace()) << "Delay transaction: Partial payment not "
                                   "allowed to create account.";

            return telNO_DST_PARTIAL;
        }
        if (dstAmount < STAmount(ctx.view.fees().reserve))
        {
            // Reserve is not load-scaled; dstAmount must meet the base
            // account reserve to fund the new account root.
            JLOG(ctx.j.trace()) << "Delay transaction: Destination account does not exist. "
                                << "Insufficent payment to create account.";

            // TODO: de-dupe
            return tecNO_DST_INSUF_XRP;
        }
    }
    else if (
        ((sleDst->getFlags() & lsfRequireDestTag) != 0u) &&
        !ctx.tx.isFieldPresent(sfDestinationTag))
    {
        // The destination tag is opaque to the protocol but lets the
        // destination owner require senders to provide one (e.g. for
        // exchange sub-account routing). The check is skipped for
        // newly-formed accounts because lsfRequireDestTag can't be set yet.
        JLOG(ctx.j.trace()) << "Malformed transaction: DestinationTag required.";

        return tecDST_TAG_NEEDED;
    }

    if ((hasPaths || sendMax || !dstAmount.native()) && ctx.view.open())
    {
        STPathSet const& paths = ctx.tx.getFieldPathSet(sfPaths);

        if (paths.size() > kMAX_PATH_SIZE || std::ranges::any_of(paths, [](STPath const& path) {
                return path.size() > kMAX_PATH_LENGTH;
            }))
        {
            return telBAD_PATH_COUNT;
        }
    }

    if (auto const err = credentials::valid(ctx.tx, ctx.view, ctx.tx[sfAccount], ctx.j);
        !isTesSuccess(err))
        return err;

    if (ctx.tx.isFieldPresent(sfDomainID))
    {
        if (!permissioned_dex::accountInDomain(ctx.view, ctx.tx[sfAccount], ctx.tx[sfDomainID]))
            return tecNO_PERMISSION;

        if (!permissioned_dex::accountInDomain(ctx.view, ctx.tx[sfDestination], ctx.tx[sfDomainID]))
            return tecNO_PERMISSION;
    }

    return tesSUCCESS;
}

TER
Payment::doApply()
{
    auto const deliverMin = ctx_.tx[~sfDeliverMin];

    std::uint32_t const txFlags = ctx_.tx.getFlags();
    bool const partialPaymentAllowed = (txFlags & tfPartialPayment) != 0u;
    bool const limitQuality = (txFlags & tfLimitQuality) != 0u;
    bool const defaultPathsAllowed = (txFlags & tfNoRippleDirect) == 0u;
    auto const hasPaths = ctx_.tx.isFieldPresent(sfPaths);
    auto const sendMax = ctx_.tx[~sfSendMax];

    AccountID const dstAccountID(ctx_.tx.getAccountID(sfDestination));
    STAmount const dstAmount(ctx_.tx.getFieldAmount(sfAmount));
    bool const isDstMPT = dstAmount.holds<MPTIssue>();
    STAmount const maxSourceAmount = getMaxSourceAmount(account_, dstAmount, sendMax);

    JLOG(j_.trace()) << "maxSourceAmount=" << maxSourceAmount.getFullText()
                     << " dstAmount=" << dstAmount.getFullText();

    auto const k = keylet::account(dstAccountID);
    SLE::pointer sleDst = view().peek(k);

    if (!sleDst)
    {
        sleDst = std::make_shared<SLE>(k);
        sleDst->setAccountID(sfAccount, dstAccountID);
        sleDst->setFieldU32(sfSequence, view().seq());
        sleDst->setFieldAmount(sfBalance, XRPAmount(beast::kZERO));

        view().insert(sleDst);
    }
    else
    {
        // Mark the destination as modified so the engine tracks it; the source
        // is always modified because a fee is always deducted.
        view().update(sleDst);
    }

    bool const mpTokensV2 = view().rules().enabled(featureMPTokensV2);

    // Direct MPT payment is handled by payment engine if MPTokensV2 is enabled
    bool const ripple = (hasPaths || sendMax || !dstAmount.native()) && (!isDstMPT || mpTokensV2);

    if (ripple)
    {
        // An account that requires deposit authorization has two ways to
        // receive an IOU payment:
        //  1. Account == Destination, or
        //  2. Account is deposit-preauthorized by the destination.
        if (auto err = verifyDepositPreauth(
                ctx_.tx, ctx_.view(), account_, dstAccountID, sleDst, ctx_.journal);
            !isTesSuccess(err))
            return err;

        path::RippleCalc::Input rcInput;
        rcInput.partialPaymentAllowed = partialPaymentAllowed;
        rcInput.defaultPathsAllowed = defaultPathsAllowed;
        rcInput.limitQuality = limitQuality;
        rcInput.isLedgerOpen = view().open();

        path::RippleCalc::Output rc;
        {
            PaymentSandbox pv(&view());
            JLOG(j_.debug()) << "Entering RippleCalc in payment: " << ctx_.tx.getTransactionID();
            rc = path::RippleCalc::rippleCalculate(
                pv,
                maxSourceAmount,
                dstAmount,
                dstAccountID,
                account_,
                ctx_.tx.getFieldPathSet(sfPaths),
                ctx_.tx[~sfDomainID],
                ctx_.registry,
                &rcInput);
            pv.apply(ctx_.rawView());
        }

        // TODO: is this right?  If the amount is the correct amount, was
        // the delivered amount previously set?
        if (isTesSuccess(rc.result()) && rc.actualAmountOut != dstAmount)
        {
            if (deliverMin && rc.actualAmountOut < *deliverMin)
            {
                rc.setResult(tecPATH_PARTIAL);
            }
            else
            {
                ctx_.deliver(rc.actualAmountOut);
            }
        }

        auto terResult = rc.result();

        // Promote ter* retry codes to tecPATH_DRY so a fee is charged.
        // Running the path engine has non-trivial cost; charging a fee
        // discourages users from submitting poorly-constructed path specs.
        if (isTerRetry(terResult))
            terResult = tecPATH_DRY;
        return terResult;
    }
    if (isDstMPT)
    {
        JLOG(j_.trace()) << " dstAmount=" << dstAmount.getFullText();
        auto const& mptIssue = dstAmount.get<MPTIssue>();

        if (auto const ter = requireAuth(view(), mptIssue, account_); !isTesSuccess(ter))
            return ter;

        if (auto const ter = requireAuth(view(), mptIssue, dstAccountID); !isTesSuccess(ter))
            return ter;

        if (auto const ter = canTransfer(view(), mptIssue, account_, dstAccountID);
            !isTesSuccess(ter))
            return ter;

        if (auto err = verifyDepositPreauth(
                ctx_.tx, ctx_.view(), account_, dstAccountID, sleDst, ctx_.journal);
            !isTesSuccess(err))
            return err;

        auto const& issuer = mptIssue.getIssuer();

        Rate rate{QUALITY_ONE};
        if (account_ != issuer && dstAccountID != issuer)
        {
            // Freeze checks apply only between holders; issuers can always
            // send to holders and holders can always return to issuers even
            // when the issuance is globally or individually locked.
            if (isAnyFrozen(view(), {account_, dstAccountID}, mptIssue))
                return tecLOCKED;

            rate = transferRate(view(), mptIssue.getMptID());
        }

        STAmount amountDeliver = dstAmount;
        // No rounding here — rounding semantics will change once MPT is
        // integrated into the DEX path engine.
        STAmount requiredMaxSourceAmount = multiply(dstAmount, rate);

        if (partialPaymentAllowed && requiredMaxSourceAmount > maxSourceAmount)
        {
            requiredMaxSourceAmount = maxSourceAmount;
            // No rounding — same note as above.
            amountDeliver = divide(maxSourceAmount, rate);
        }

        if (requiredMaxSourceAmount > maxSourceAmount ||
            (deliverMin && amountDeliver < *deliverMin))
            return tecPATH_PARTIAL;

        PaymentSandbox pv(&view());
        auto res = accountSend(pv, account_, dstAccountID, amountDeliver, ctx_.journal);
        if (isTesSuccess(res))
        {
            pv.apply(ctx_.rawView());

            // Record actual delivered amount for the DeliveredAmount metadata
            // field when it differs from sfAmount (partial pay or transfer fee).
            // Gated on fixMPTDeliveredAmount, mirroring the IOU payment pattern.
            if (view().rules().enabled(fixMPTDeliveredAmount) && amountDeliver != dstAmount)
                ctx_.deliver(amountDeliver);
        }
        else if (res == tecINSUFFICIENT_FUNDS || res == tecPATH_DRY)
        {
            res = tecPATH_PARTIAL;
        }

        return res;
    }

    XRPL_ASSERT(dstAmount.native(), "xrpl::Payment::doApply : amount is XRP");

    // Direct XRP payment.

    auto const sleSrc = view().peek(keylet::account(account_));
    if (!sleSrc)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto const ownerCount = sleSrc->getFieldU32(sfOwnerCount);
    auto const reserve = view().fees().accountReserve(ownerCount);

    // In a delegated payment the fee is charged to the delegate, not to
    // account_. When account_ IS the fee payer it must also cover the fee
    // amount, so minRequiredFunds is max(reserve, fee) rather than just reserve.
    bool const accountIsPayer = (ctx_.tx.getFeePayer() == account_);
    auto const minRequiredFunds =
        accountIsPayer ? std::max(reserve, ctx_.tx.getFieldAmount(sfFee).xrp()) : reserve;

    if (preFeeBalance_ < dstAmount.xrp() + minRequiredFunds)
    {
        // Vote no. However the transaction might succeed, if applied in
        // a different order.
        JLOG(j_.trace()) << "Delay transaction: Insufficient funds: " << to_string(preFeeBalance_)
                         << " / " << to_string(dstAmount.xrp() + minRequiredFunds) << " ("
                         << to_string(reserve) << ")";

        return tecUNFUNDED_PAYMENT;
    }

    // Pseudo-accounts cannot receive payments, other than these native to
    // their underlying ledger object - implemented in their respective
    // transaction types. Note, this is not amendment-gated because all writes
    // to pseudo-account discriminator fields **are** amendment gated, hence the
    // behaviour of this check will always match the active amendments.
    if (isPseudoAccount(sleDst))
        return tecNO_PERMISSION;

    // An account with lsfDepositAuth set has three ways to receive XRP:
    //  1. Account == Destination, or
    //  2. Account is deposit-preauthorized by the destination, or
    //  3. Both the destination's current balance AND the payment amount
    //     are ≤ the base reserve (Rule 3 / small-balance bypass).
    //
    // Rule 3 prevents an account from becoming permanently wedged: if an
    // account sets lsfDepositAuth and then spends all its XRP it would be
    // unable to pay the fee required to unset the flag. The base reserve
    // is the bound because it is small, seldom changes, and is always
    // enough to fund the account-management transaction needed to recover.
    XRPAmount const dstReserve{view().fees().reserve};

    if (dstAmount > dstReserve || sleDst->getFieldAmount(sfBalance) > dstReserve)
    {
        if (auto err = verifyDepositPreauth(
                ctx_.tx, ctx_.view(), account_, dstAccountID, sleDst, ctx_.journal);
            !isTesSuccess(err))
            return err;
    }

    sleSrc->setFieldAmount(sfBalance, sleSrc->getFieldAmount(sfBalance) - dstAmount);
    sleDst->setFieldAmount(sfBalance, sleDst->getFieldAmount(sfBalance) + dstAmount);

    // Clear lsfPasswordSpent if set — legacy flag from the original
    // password-based account creation flow; receiving XRP re-enables it.
    if ((sleDst->getFlags() & lsfPasswordSpent) != 0u)
        sleDst->clearFlag(lsfPasswordSpent);

    return tesSUCCESS;
}

void
Payment::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
    // No transaction-specific invariants yet (future work).
}

bool
Payment::finalizeInvariants(STTx const&, TER, XRPAmount, ReadView const&, beast::Journal const&)
{
    // No transaction-specific invariants yet (future work).
    return true;
}

}  // namespace xrpl
