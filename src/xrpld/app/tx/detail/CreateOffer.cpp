//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <xrpld/app/ledger/OrderBookDB.h>
#include <xrpld/app/misc/PermissionedDEXHelpers.h>
#include <xrpld/app/paths/Flow.h>
#include <xrpld/app/tx/detail/CreateOffer.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/WrappedSink.h>
#include <xrpl/ledger/PaymentSandbox.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/st.h>

namespace ripple {
TxConsequences
CreateOffer::makeTxConsequences(PreflightContext const& ctx)
{
    auto calculateMaxXRPSpend = [](STTx const& tx) -> XRPAmount {
        auto const& amount{tx[sfTakerGets]};
        return amount.native() ? amount.xrp() : beast::zero;
    };

    return TxConsequences{ctx.tx, calculateMaxXRPSpend(ctx.tx)};
}

bool
CreateOffer::checkExtraFeatures(PreflightContext const& ctx)
{
    if (ctx.tx.isFieldPresent(sfDomainID) &&
        !ctx.rules.enabled(featurePermissionedDEX))
        return false;

    return true;
}

std::uint32_t
CreateOffer::getFlagsMask(PreflightContext const& ctx)
{
    // The tfOfferCreateMask is built assuming that PermissionedDEX is
    // enabled
    if (ctx.rules.enabled(featurePermissionedDEX))
        return tfOfferCreateMask;
    // If PermissionedDEX is not enabled, add tfHybrid to the mask,
    // indicating it is not allowed.
    return tfOfferCreateMask | tfHybrid;
}

NotTEC
CreateOffer::preflight(PreflightContext const& ctx)
{
    auto& tx = ctx.tx;
    auto& j = ctx.j;

    std::uint32_t const uTxFlags = tx.getFlags();

    if (tx.isFlag(tfHybrid) && !tx.isFieldPresent(sfDomainID))
        return temINVALID_FLAG;

    bool const bImmediateOrCancel(uTxFlags & tfImmediateOrCancel);
    bool const bFillOrKill(uTxFlags & tfFillOrKill);

    if (bImmediateOrCancel && bFillOrKill)
    {
        JLOG(j.debug()) << "Malformed transaction: both IoC and FoK set.";
        return temINVALID_FLAG;
    }

    bool const bHaveExpiration(tx.isFieldPresent(sfExpiration));

    if (bHaveExpiration && (tx.getFieldU32(sfExpiration) == 0))
    {
        JLOG(j.debug()) << "Malformed offer: bad expiration";
        return temBAD_EXPIRATION;
    }

    if (auto const cancelSequence = tx[~sfOfferSequence];
        cancelSequence && *cancelSequence == 0)
    {
        JLOG(j.debug()) << "Malformed offer: bad cancel sequence";
        return temBAD_SEQUENCE;
    }

    STAmount saTakerPays = tx[sfTakerPays];
    STAmount saTakerGets = tx[sfTakerGets];

    if (!isLegalNet(saTakerPays) || !isLegalNet(saTakerGets))
        return temBAD_AMOUNT;

    if (saTakerPays.native() && saTakerGets.native())
    {
        JLOG(j.debug()) << "Malformed offer: redundant (XRP for XRP)";
        return temBAD_OFFER;
    }
    if (saTakerPays <= beast::zero || saTakerGets <= beast::zero)
    {
        JLOG(j.debug()) << "Malformed offer: bad amount";
        return temBAD_OFFER;
    }

    auto const& uPaysIssuerID = saTakerPays.getIssuer();
    auto const& uPaysCurrency = saTakerPays.getCurrency();

    auto const& uGetsIssuerID = saTakerGets.getIssuer();
    auto const& uGetsCurrency = saTakerGets.getCurrency();

    if (uPaysCurrency == uGetsCurrency && uPaysIssuerID == uGetsIssuerID)
    {
        JLOG(j.debug()) << "Malformed offer: redundant (IOU for IOU)";
        return temREDUNDANT;
    }
    // We don't allow a non-native currency to use the currency code XRP.
    if (badCurrency() == uPaysCurrency || badCurrency() == uGetsCurrency)
    {
        JLOG(j.debug()) << "Malformed offer: bad currency";
        return temBAD_CURRENCY;
    }

    if (saTakerPays.native() != !uPaysIssuerID ||
        saTakerGets.native() != !uGetsIssuerID)
    {
        JLOG(j.debug()) << "Malformed offer: bad issuer";
        return temBAD_ISSUER;
    }

    return tesSUCCESS;
}

TER
CreateOffer::preclaim(PreclaimContext const& ctx)
{
    auto const id = ctx.tx[sfAccount];

    auto saTakerPays = ctx.tx[sfTakerPays];
    auto saTakerGets = ctx.tx[sfTakerGets];

    auto const& uPaysIssuerID = saTakerPays.getIssuer();
    auto const& uPaysCurrency = saTakerPays.getCurrency();

    auto const& uGetsIssuerID = saTakerGets.getIssuer();

    auto const cancelSequence = ctx.tx[~sfOfferSequence];

    auto const sleCreator = ctx.view.read(keylet::account(id));
    if (!sleCreator)
        return terNO_ACCOUNT;

    std::uint32_t const uAccountSequence = sleCreator->getFieldU32(sfSequence);

    auto viewJ = ctx.app.journal("View");

    if (isGlobalFrozen(ctx.view, uPaysIssuerID) ||
        isGlobalFrozen(ctx.view, uGetsIssuerID))
    {
        JLOG(ctx.j.debug()) << "Offer involves frozen asset";
        return tecFROZEN;
    }

    if (accountFunds(ctx.view, id, saTakerGets, fhZERO_IF_FROZEN, viewJ) <=
        beast::zero)
    {
        JLOG(ctx.j.debug())
            << "delay: Offers must be at least partially funded.";
        return tecUNFUNDED_OFFER;
    }

    // This can probably be simplified to make sure that you cancel sequences
    // before the transaction sequence number.
    if (cancelSequence && (uAccountSequence <= *cancelSequence))
    {
        JLOG(ctx.j.debug()) << "uAccountSequenceNext=" << uAccountSequence
                            << " uOfferSequence=" << *cancelSequence;
        return temBAD_SEQUENCE;
    }

    if (hasExpired(ctx.view, ctx.tx[~sfExpiration]))
    {
        // Note that this will get checked again in applyGuts, but it saves
        // us a call to checkAcceptAsset and possible false negative.
        //
        // The return code change is attached to featureDepositPreauth as a
        // convenience, as the change is not big enough to deserve its own
        // amendment.
        return ctx.view.rules().enabled(featureDepositPreauth)
            ? TER{tecEXPIRED}
            : TER{tesSUCCESS};
    }

    // Make sure that we are authorized to hold what the taker will pay us.
    if (!saTakerPays.native())
    {
        auto result = checkAcceptAsset(
            ctx.view,
            ctx.flags,
            id,
            ctx.j,
            Issue(uPaysCurrency, uPaysIssuerID));
        if (result != tesSUCCESS)
            return result;
    }

    // if domain is specified, make sure that domain exists and the offer create
    // is part of the domain
    if (ctx.tx.isFieldPresent(sfDomainID))
    {
        if (!permissioned_dex::accountInDomain(
                ctx.view, id, ctx.tx[sfDomainID]))
            return tecNO_PERMISSION;
    }

    return tesSUCCESS;
}

TER
CreateOffer::checkAcceptAsset(
    ReadView const& view,
    ApplyFlags const flags,
    AccountID const id,
    beast::Journal const j,
    Issue const& issue)
{
    // Only valid for custom currencies
    XRPL_ASSERT(
        !isXRP(issue.currency),
        "ripple::CreateOffer::checkAcceptAsset : input is not XRP");

    auto const issuerAccount = view.read(keylet::account(issue.account));

    if (!issuerAccount)
    {
        JLOG(j.debug())
            << "delay: can't receive IOUs from non-existent issuer: "
            << to_string(issue.account);

        return (flags & tapRETRY) ? TER{terNO_ACCOUNT} : TER{tecNO_ISSUER};
    }

    // This code is attached to the DepositPreauth amendment as a matter of
    // convenience.  The change is not significant enough to deserve its
    // own amendment.
    if (view.rules().enabled(featureDepositPreauth) && (issue.account == id))
        // An account can always accept its own issuance.
        return tesSUCCESS;

    if ((*issuerAccount)[sfFlags] & lsfRequireAuth)
    {
        auto const trustLine =
            view.read(keylet::line(id, issue.account, issue.currency));

        if (!trustLine)
        {
            return (flags & tapRETRY) ? TER{terNO_LINE} : TER{tecNO_LINE};
        }

        // Entries have a canonical representation, determined by a
        // lexicographical "greater than" comparison employing strict weak
        // ordering. Determine which entry we need to access.
        bool const canonical_gt(id > issue.account);

        bool const is_authorized(
            (*trustLine)[sfFlags] & (canonical_gt ? lsfLowAuth : lsfHighAuth));

        if (!is_authorized)
        {
            JLOG(j.debug())
                << "delay: can't receive IOUs from issuer without auth.";

            return (flags & tapRETRY) ? TER{terNO_AUTH} : TER{tecNO_AUTH};
        }
    }

    // An account can not create a trustline to itself, so no line can exist
    // to be frozen. Additionally, an issuer can always accept its own
    // issuance.
    if (issue.account == id)
    {
        return tesSUCCESS;
    }

    auto const trustLine =
        view.read(keylet::line(id, issue.account, issue.currency));

    if (!trustLine)
    {
        return tesSUCCESS;
    }

    // There's no difference which side enacted deep freeze, accepting
    // tokens shouldn't be possible.
    bool const deepFrozen =
        (*trustLine)[sfFlags] & (lsfLowDeepFreeze | lsfHighDeepFreeze);

    if (deepFrozen)
    {
        return tecFROZEN;
    }

    return tesSUCCESS;
}

std::pair<TER, Amounts>
CreateOffer::flowCross(
    PaymentSandbox& psb,
    PaymentSandbox& psbCancel,
    Amounts const& takerAmount,
    std::optional<uint256> const& domainID)
{
    try
    {
        // If the taker is unfunded before we begin crossing there's nothing
        // to do - just return an error.
        //
        // We check this in preclaim, but when selling XRP charged fees can
        // cause a user's available balance to go to 0 (by causing it to dip
        // below the reserve) so we check this case again.
        STAmount const inStartBalance =
            accountFunds(psb, account_, takerAmount.in, fhZERO_IF_FROZEN, j_);
        if (inStartBalance <= beast::zero)
        {
            // The account balance can't cover even part of the offer.
            JLOG(j_.debug()) << "Not crossing: taker is unfunded.";
            return {tecUNFUNDED_OFFER, takerAmount};
        }

        // If the gateway has a transfer rate, accommodate that.  The
        // gateway takes its cut without any special consent from the
        // offer taker.  Set sendMax to allow for the gateway's cut.
        Rate gatewayXferRate{QUALITY_ONE};
        STAmount sendMax = takerAmount.in;
        if (!sendMax.native() && (account_ != sendMax.getIssuer()))
        {
            gatewayXferRate = transferRate(psb, sendMax.getIssuer());
            if (gatewayXferRate.value != QUALITY_ONE)
            {
                sendMax = multiplyRound(
                    takerAmount.in,
                    gatewayXferRate,
                    takerAmount.in.issue(),
                    true);
            }
        }

        // Payment flow code compares quality after the transfer rate is
        // included.  Since transfer rate is incorporated compute threshold.
        Quality threshold{takerAmount.out, sendMax};

        // If we're creating a passive offer adjust the threshold so we only
        // cross offers that have a better quality than this one.
        std::uint32_t const txFlags = ctx_.tx.getFlags();
        if (txFlags & tfPassive)
            ++threshold;

        // Don't send more than our balance.
        if (sendMax > inStartBalance)
            sendMax = inStartBalance;

        // Always invoke flow() with the default path.  However if neither
        // of the takerAmount currencies are XRP then we cross through an
        // additional path with XRP as the intermediate between two books.
        // This second path we have to build ourselves.
        STPathSet paths;
        if (!takerAmount.in.native() && !takerAmount.out.native())
        {
            STPath path;
            path.emplace_back(std::nullopt, xrpCurrency(), std::nullopt);
            paths.emplace_back(std::move(path));
        }
        // Special handling for the tfSell flag.
        STAmount deliver = takerAmount.out;
        OfferCrossing offerCrossing = OfferCrossing::yes;
        if (txFlags & tfSell)
        {
            offerCrossing = OfferCrossing::sell;
            // We are selling, so we will accept *more* than the offer
            // specified.  Since we don't know how much they might offer,
            // we allow delivery of the largest possible amount.
            if (deliver.native())
                deliver = STAmount{STAmount::cMaxNative};
            else
                // We can't use the maximum possible currency here because
                // there might be a gateway transfer rate to account for.
                // Since the transfer rate cannot exceed 200%, we use 1/2
                // maxValue for our limit.
                deliver = STAmount{
                    takerAmount.out.issue(),
                    STAmount::cMaxValue / 2,
                    STAmount::cMaxOffset};
        }

        // Call the payment engine's flow() to do the actual work.
        auto const result = flow(
            psb,
            deliver,
            account_,
            account_,
            paths,
            true,                       // default path
            !(txFlags & tfFillOrKill),  // partial payment
            true,                       // owner pays transfer fee
            offerCrossing,
            threshold,
            sendMax,
            domainID,
            j_);

        // If stale offers were found remove them.
        for (auto const& toRemove : result.removableOffers)
        {
            if (auto otr = psb.peek(keylet::offer(toRemove)))
                offerDelete(psb, otr, j_);
            if (auto otr = psbCancel.peek(keylet::offer(toRemove)))
                offerDelete(psbCancel, otr, j_);
        }

        // Determine the size of the final offer after crossing.
        auto afterCross = takerAmount;  // If !tesSUCCESS offer unchanged
        if (isTesSuccess(result.result()))
        {
            STAmount const takerInBalance = accountFunds(
                psb, account_, takerAmount.in, fhZERO_IF_FROZEN, j_);

            if (takerInBalance <= beast::zero)
            {
                // If offer crossing exhausted the account's funds don't
                // create the offer.
                afterCross.in.clear();
                afterCross.out.clear();
            }
            else
            {
                STAmount const rate{
                    Quality{takerAmount.out, takerAmount.in}.rate()};

                if (txFlags & tfSell)
                {
                    // If selling then scale the new out amount based on how
                    // much we sold during crossing.  This preserves the offer
                    // Quality,

                    // Reduce the offer that is placed by the crossed amount.
                    // Note that we must ignore the portion of the
                    // actualAmountIn that may have been consumed by a
                    // gateway's transfer rate.
                    STAmount nonGatewayAmountIn = result.actualAmountIn;
                    if (gatewayXferRate.value != QUALITY_ONE)
                        nonGatewayAmountIn = divideRound(
                            result.actualAmountIn,
                            gatewayXferRate,
                            takerAmount.in.issue(),
                            true);

                    afterCross.in -= nonGatewayAmountIn;

                    // It's possible that the divRound will cause our subtract
                    // to go slightly negative.  So limit afterCross.in to zero.
                    if (afterCross.in < beast::zero)
                        // We should verify that the difference *is* small, but
                        // what is a good threshold to check?
                        afterCross.in.clear();

                    afterCross.out = divRoundStrict(
                        afterCross.in, rate, takerAmount.out.issue(), false);
                }
                else
                {
                    // If not selling, we scale the input based on the
                    // remaining output.  This too preserves the offer
                    // Quality.
                    afterCross.out -= result.actualAmountOut;
                    XRPL_ASSERT(
                        afterCross.out >= beast::zero,
                        "ripple::CreateOffer::flowCross : minimum offer");
                    if (afterCross.out < beast::zero)
                        afterCross.out.clear();
                    afterCross.in = mulRound(
                        afterCross.out, rate, takerAmount.in.issue(), true);
                }
            }
        }

        // Return how much of the offer is left.
        return {tesSUCCESS, afterCross};
    }
    catch (std::exception const& e)
    {
        JLOG(j_.error()) << "Exception during offer crossing: " << e.what();
    }
    return {tecINTERNAL, takerAmount};
}

std::string
CreateOffer::format_amount(STAmount const& amount)
{
    std::string txt = amount.getText();
    txt += "/";
    txt += to_string(amount.issue().currency);
    return txt;
}

TER
CreateOffer::applyHybrid(
    Sandbox& sb,
    std::shared_ptr<STLedgerEntry> sleOffer,
    Keylet const& offerKey,
    STAmount const& saTakerPays,
    STAmount const& saTakerGets,
    std::function<void(SLE::ref, std::optional<uint256>)> const& setDir)
{
    if (!sleOffer->isFieldPresent(sfDomainID))
        return tecINTERNAL;  // LCOV_EXCL_LINE

    // set hybrid flag
    sleOffer->setFlag(lsfHybrid);

    // if offer is hybrid, need to also place into open offer dir
    Book const book{saTakerPays.issue(), saTakerGets.issue(), std::nullopt};

    auto dir =
        keylet::quality(keylet::book(book), getRate(saTakerGets, saTakerPays));
    bool const bookExists = sb.exists(dir);

    auto const bookNode = sb.dirAppend(dir, offerKey, [&](SLE::ref sle) {
        // don't set domainID on the directory object since this directory is
        // for open book
        setDir(sle, std::nullopt);
    });

    if (!bookNode)
    {
        JLOG(j_.debug())
            << "final result: failed to add hybrid offer to open book";
        return tecDIR_FULL;  // LCOV_EXCL_LINE
    }

    STArray bookArr(sfAdditionalBooks, 1);
    auto bookInfo = STObject::makeInnerObject(sfBook);
    bookInfo.setFieldH256(sfBookDirectory, dir.key);
    bookInfo.setFieldU64(sfBookNode, *bookNode);
    bookArr.push_back(std::move(bookInfo));

    if (!bookExists)
        ctx_.app.getOrderBookDB().addOrderBook(book);

    sleOffer->setFieldArray(sfAdditionalBooks, bookArr);
    return tesSUCCESS;
}

std::pair<TER, bool>
CreateOffer::applyGuts(Sandbox& sb, Sandbox& sbCancel)
{
    using beast::zero;

    std::uint32_t const uTxFlags = ctx_.tx.getFlags();

    bool const bPassive(uTxFlags & tfPassive);
    bool const bImmediateOrCancel(uTxFlags & tfImmediateOrCancel);
    bool const bFillOrKill(uTxFlags & tfFillOrKill);
    bool const bSell(uTxFlags & tfSell);
    bool const bHybrid(uTxFlags & tfHybrid);

    auto saTakerPays = ctx_.tx[sfTakerPays];
    auto saTakerGets = ctx_.tx[sfTakerGets];
    auto const domainID = ctx_.tx[~sfDomainID];

    auto const cancelSequence = ctx_.tx[~sfOfferSequence];

    // Note that we we use the value from the sequence or ticket as the
    // offer sequence.  For more explanation see comments in SeqProxy.h.
    auto const offerSequence = ctx_.tx.getSeqValue();

    // This is the original rate of the offer, and is the rate at which
    // it will be placed, even if crossing offers change the amounts that
    // end up on the books.
    auto uRate = getRate(saTakerGets, saTakerPays);

    auto viewJ = ctx_.app.journal("View");

    TER result = tesSUCCESS;

    // Process a cancellation request that's passed along with an offer.
    if (cancelSequence)
    {
        auto const sleCancel =
            sb.peek(keylet::offer(account_, *cancelSequence));

        // It's not an error to not find the offer to cancel: it might have
        // been consumed or removed. If it is found, however, it's an error
        // to fail to delete it.
        if (sleCancel)
        {
            JLOG(j_.debug()) << "Create cancels order " << *cancelSequence;
            result = offerDelete(sb, sleCancel, viewJ);
        }
    }

    auto const expiration = ctx_.tx[~sfExpiration];

    if (hasExpired(sb, expiration))
    {
        // If the offer has expired, the transaction has successfully
        // done nothing, so short circuit from here.
        //
        // The return code change is attached to featureDepositPreauth as a
        // convenience.  The change is not big enough to deserve a fix code.
        TER const ter{
            sb.rules().enabled(featureDepositPreauth) ? TER{tecEXPIRED}
                                                      : TER{tesSUCCESS}};
        return {ter, true};
    }

    bool const bOpenLedger = sb.open();
    bool crossed = false;

    if (result == tesSUCCESS)
    {
        // If a tick size applies, round the offer to the tick size
        auto const& uPaysIssuerID = saTakerPays.getIssuer();
        auto const& uGetsIssuerID = saTakerGets.getIssuer();

        std::uint8_t uTickSize = Quality::maxTickSize;
        if (!isXRP(uPaysIssuerID))
        {
            auto const sle = sb.read(keylet::account(uPaysIssuerID));
            if (sle && sle->isFieldPresent(sfTickSize))
                uTickSize = std::min(uTickSize, (*sle)[sfTickSize]);
        }
        if (!isXRP(uGetsIssuerID))
        {
            auto const sle = sb.read(keylet::account(uGetsIssuerID));
            if (sle && sle->isFieldPresent(sfTickSize))
                uTickSize = std::min(uTickSize, (*sle)[sfTickSize]);
        }
        if (uTickSize < Quality::maxTickSize)
        {
            auto const rate =
                Quality{saTakerGets, saTakerPays}.round(uTickSize).rate();

            // We round the side that's not exact,
            // just as if the offer happened to execute
            // at a slightly better (for the placer) rate
            if (bSell)
            {
                // this is a sell, round taker pays
                saTakerPays = multiply(saTakerGets, rate, saTakerPays.issue());
            }
            else
            {
                // this is a buy, round taker gets
                saTakerGets = divide(saTakerPays, rate, saTakerGets.issue());
            }
            if (!saTakerGets || !saTakerPays)
            {
                JLOG(j_.debug()) << "Offer rounded to zero";
                return {result, true};
            }

            uRate = getRate(saTakerGets, saTakerPays);
        }

        // We reverse pays and gets because during crossing we are taking.
        Amounts const takerAmount(saTakerGets, saTakerPays);

        JLOG(j_.debug()) << "Attempting cross: "
                         << to_string(takerAmount.in.issue()) << " -> "
                         << to_string(takerAmount.out.issue());

        if (auto stream = j_.trace())
        {
            stream << "   mode: " << (bPassive ? "passive " : "")
                   << (bSell ? "sell" : "buy");
            stream << "     in: " << format_amount(takerAmount.in);
            stream << "    out: " << format_amount(takerAmount.out);
        }

        // The amount of the offer that is unfilled after crossing has been
        // performed. It may be equal to the original amount (didn't cross),
        // empty (fully crossed), or something in-between.
        Amounts place_offer;
        PaymentSandbox psbFlow{&sb};
        PaymentSandbox psbCancelFlow{&sbCancel};

        std::tie(result, place_offer) =
            flowCross(psbFlow, psbCancelFlow, takerAmount, domainID);
        psbFlow.apply(sb);
        psbCancelFlow.apply(sbCancel);

        // We expect the implementation of cross to succeed
        // or give a tec.
        XRPL_ASSERT(
            result == tesSUCCESS || isTecClaim(result),
            "ripple::CreateOffer::applyGuts : result is tesSUCCESS or "
            "tecCLAIM");

        if (auto stream = j_.trace())
        {
            stream << "Cross result: " << transToken(result);
            stream << "     in: " << format_amount(place_offer.in);
            stream << "    out: " << format_amount(place_offer.out);
        }

        if (result == tecFAILED_PROCESSING && bOpenLedger)
            result = telFAILED_PROCESSING;

        if (result != tesSUCCESS)
        {
            JLOG(j_.debug()) << "final result: " << transToken(result);
            return {result, true};
        }

        XRPL_ASSERT(
            saTakerGets.issue() == place_offer.in.issue(),
            "ripple::CreateOffer::applyGuts : taker gets issue match");
        XRPL_ASSERT(
            saTakerPays.issue() == place_offer.out.issue(),
            "ripple::CreateOffer::applyGuts : taker pays issue match");

        if (takerAmount != place_offer)
            crossed = true;

        // The offer that we need to place after offer crossing should
        // never be negative. If it is, something went very very wrong.
        if (place_offer.in < zero || place_offer.out < zero)
        {
            JLOG(j_.fatal()) << "Cross left offer negative!"
                             << "     in: " << format_amount(place_offer.in)
                             << "    out: " << format_amount(place_offer.out);
            return {tefINTERNAL, true};
        }

        if (place_offer.in == zero || place_offer.out == zero)
        {
            JLOG(j_.debug()) << "Offer fully crossed!";
            return {result, true};
        }

        // We now need to adjust the offer to reflect the amount left after
        // crossing. We reverse in and out here, since during crossing we
        // were the taker.
        saTakerPays = place_offer.out;
        saTakerGets = place_offer.in;
    }

    XRPL_ASSERT(
        saTakerPays > zero && saTakerGets > zero,
        "ripple::CreateOffer::applyGuts : taker pays and gets positive");

    if (result != tesSUCCESS)
    {
        JLOG(j_.debug()) << "final result: " << transToken(result);
        return {result, true};
    }

    if (auto stream = j_.trace())
    {
        stream << "Place" << (crossed ? " remaining " : " ") << "offer:";
        stream << "    Pays: " << saTakerPays.getFullText();
        stream << "    Gets: " << saTakerGets.getFullText();
    }

    // For 'fill or kill' offers, failure to fully cross means that the
    // entire operation should be aborted, with only fees paid.
    if (bFillOrKill)
    {
        JLOG(j_.trace()) << "Fill or Kill: offer killed";
        return {tecKILLED, false};
    }

    // For 'immediate or cancel' offers, the amount remaining doesn't get
    // placed - it gets canceled and the operation succeeds.
    if (bImmediateOrCancel)
    {
        JLOG(j_.trace()) << "Immediate or cancel: offer canceled";
        if (!crossed)
            // Any ImmediateOrCancel offer that transfers absolutely no funds
            // returns tecKILLED rather than tesSUCCESS.  Motivation for the
            // change is here: https://github.com/ripple/rippled/issues/4115
            return {tecKILLED, false};
        return {tesSUCCESS, true};
    }

    auto const sleCreator = sb.peek(keylet::account(account_));
    if (!sleCreator)
        return {tefINTERNAL, false};

    {
        XRPAmount reserve =
            sb.fees().accountReserve(sleCreator->getFieldU32(sfOwnerCount) + 1);

        if (mPriorBalance < reserve)
        {
            // If we are here, the signing account had an insufficient reserve
            // *prior* to our processing. If something actually crossed, then
            // we allow this; otherwise, we just claim a fee.
            if (!crossed)
                result = tecINSUF_RESERVE_OFFER;

            if (result != tesSUCCESS)
            {
                JLOG(j_.debug()) << "final result: " << transToken(result);
            }

            return {result, true};
        }
    }

    // We need to place the remainder of the offer into its order book.
    auto const offer_index = keylet::offer(account_, offerSequence);

    // Add offer to owner's directory.
    auto const ownerNode = sb.dirInsert(
        keylet::ownerDir(account_), offer_index, describeOwnerDir(account_));

    if (!ownerNode)
    {
        // LCOV_EXCL_START
        JLOG(j_.debug())
            << "final result: failed to add offer to owner's directory";
        return {tecDIR_FULL, true};
        // LCOV_EXCL_STOP
    }

    // Update owner count.
    adjustOwnerCount(sb, sleCreator, 1, viewJ);

    JLOG(j_.trace()) << "adding to book: " << to_string(saTakerPays.issue())
                     << " : " << to_string(saTakerGets.issue())
                     << (domainID ? (" : " + to_string(*domainID)) : "");

    Book const book{saTakerPays.issue(), saTakerGets.issue(), domainID};

    // Add offer to order book, using the original rate
    // before any crossing occured.
    //
    // Regular offer - BookDirectory points to open directory
    //
    // Domain offer (w/o hyrbid) - BookDirectory points to domain
    // directory
    //
    // Hybrid domain offer - BookDirectory points to domain directory,
    // and AdditionalBooks field stores one entry that points to the open
    // directory
    auto dir = keylet::quality(keylet::book(book), uRate);
    bool const bookExisted = static_cast<bool>(sb.peek(dir));

    auto setBookDir = [&](SLE::ref sle,
                          std::optional<uint256> const& maybeDomain) {
        sle->setFieldH160(sfTakerPaysCurrency, saTakerPays.issue().currency);
        sle->setFieldH160(sfTakerPaysIssuer, saTakerPays.issue().account);
        sle->setFieldH160(sfTakerGetsCurrency, saTakerGets.issue().currency);
        sle->setFieldH160(sfTakerGetsIssuer, saTakerGets.issue().account);
        sle->setFieldU64(sfExchangeRate, uRate);
        if (maybeDomain)
            sle->setFieldH256(sfDomainID, *maybeDomain);
    };

    auto const bookNode = sb.dirAppend(dir, offer_index, [&](SLE::ref sle) {
        // sets domainID on book directory if it's a domain offer
        setBookDir(sle, domainID);
    });

    if (!bookNode)
    {
        // LCOV_EXCL_START
        JLOG(j_.debug()) << "final result: failed to add offer to book";
        return {tecDIR_FULL, true};
        // LCOV_EXCL_STOP
    }

    auto sleOffer = std::make_shared<SLE>(offer_index);
    sleOffer->setAccountID(sfAccount, account_);
    sleOffer->setFieldU32(sfSequence, offerSequence);
    sleOffer->setFieldH256(sfBookDirectory, dir.key);
    sleOffer->setFieldAmount(sfTakerPays, saTakerPays);
    sleOffer->setFieldAmount(sfTakerGets, saTakerGets);
    sleOffer->setFieldU64(sfOwnerNode, *ownerNode);
    sleOffer->setFieldU64(sfBookNode, *bookNode);
    if (expiration)
        sleOffer->setFieldU32(sfExpiration, *expiration);
    if (bPassive)
        sleOffer->setFlag(lsfPassive);
    if (bSell)
        sleOffer->setFlag(lsfSell);
    if (domainID)
        sleOffer->setFieldH256(sfDomainID, *domainID);

    // if it's a hybrid offer, set hybrid flag, and create an open dir
    if (bHybrid)
    {
        auto const res = applyHybrid(
            sb, sleOffer, offer_index, saTakerPays, saTakerGets, setBookDir);
        if (res != tesSUCCESS)
            return {res, true};  // LCOV_EXCL_LINE
    }

    sb.insert(sleOffer);

    if (!bookExisted)
        ctx_.app.getOrderBookDB().addOrderBook(book);

    JLOG(j_.debug()) << "final result: success";

    return {tesSUCCESS, true};
}

TER
CreateOffer::doApply()
{
    // This is the ledger view that we work against. Transactions are applied
    // as we go on processing transactions.
    Sandbox sb(&ctx_.view());

    // This is a ledger with just the fees paid and any unfunded or expired
    // offers we encounter removed. It's used when handling Fill-or-Kill offers,
    // if the order isn't going to be placed, to avoid wasting the work we did.
    Sandbox sbCancel(&ctx_.view());

    auto const result = applyGuts(sb, sbCancel);
    if (result.second)
        sb.apply(ctx_.rawView());
    else
        sbCancel.apply(ctx_.rawView());
    return result.first;
}

}  // namespace ripple
