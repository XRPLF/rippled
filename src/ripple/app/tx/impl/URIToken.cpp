//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2017 Ripple Labs Inc.

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

#include <ripple/app/ledger/Ledger.h>
#include <ripple/app/tx/impl/URIToken.h>
#include <ripple/basics/Log.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/STAccount.h>
#include <ripple/protocol/TER.h>
#include <ripple/protocol/TxFlags.h>

namespace ripple {

/*
 * Mint:  URI populated only
 * Burn:  URITokenID populated, Blank but present URI
 * Buy:   URITokenID, Amount
 * Sell:  URITokenID, Amount, [Destination], flags=tfSell,
 * Clear: URITokenID only       (clear current sell offer)
 */



inline URIOperation inferOperation(STTx const& tx)
{
    uint32_t const flags = tx.getFlags();
    bool const hasDigest = tx.isFieldPresent(sfDigest);
    bool const hasURI = tx.isFieldPresent(sfURI);
    bool const blankURI = hasURI && tx.getFieldVL(sfURI).empty();
    bool const hasID  = tx.isFieldPresent(sfURITokenID);
    bool const hasAmt = tx.isFieldPresent(sfAmount);
    bool const hasDst = tx.isFieldPresent(sfDestination);
    bool const hasSellFlag = flags == tfSell;
    bool const hasBurnFlag = flags == tfBurnable;
    bool const blankFlags = flags == 0;

    uint16_t combination =
        (hasDigest      ? 0b100000000U : 0) +
        (hasURI         ? 0b010000000U : 0) +
        (blankURI       ? 0b001000000U : 0) +
        (hasID          ? 0b000100000U : 0) +
        (hasAmt         ? 0b000010000U : 0) +
        (hasDst         ? 0b000001000U : 0) +
        (hasSellFlag    ? 0b000000100U : 0) +
        (hasBurnFlag    ? 0b000000010U : 0) +
        (blankFlags     ? 0b000000001U : 0);

    switch (combination)
    {
        case 0b110000001U:
        case 0b110000010U:
        case 0b010000001U:
        case 0b010000010U:
            return URIOperation::Mint;
        case 0b011100001U:
            return URIOperation::Burn;
        case 0b000110001U:
            return URIOperation::Buy;
        case 0b000110100U:
        case 0b000111100U:
            return URIOperation::Sell;
        case 0b000100001U:
            return URIOperation::Clear;
        default:
            return URIOperation::Invalid;
    }
}

NotTEC
URIToken::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureURIToken))
        return temDISABLED;

    NotTEC const ret{preflight1(ctx)};
    if (!isTesSuccess(ret))
        return ret;

    auto const op = inferOperation(ctx.tx);
    if (op == URIOperation::Invalid)
    {
        JLOG(ctx.j.warn())
                << "Malformed transaction. Check flags/fields, "
                << "documentation for how to specify Mint/Burn/Buy/Sell operations.";
        return temMALFORMED;
    }

    JLOG(ctx.j.trace())
        << "URIToken txnid=" << ctx.tx.getTransactionID() << " inferred operation="
        <<  (op == URIOperation::Invalid ? "Invalid" :
            (op == URIOperation::Mint ? "Mint" :
            (op == URIOperation::Burn ? "Burn" :
            (op == URIOperation::Buy ? "Buy" :
            (op == URIOperation::Sell ? "Sell" :
            (op == URIOperation::Clear ? "Clear" : "Unknown")))))) << "\n";

    if (op == URIOperation::Mint && ctx.tx.getFieldVL(sfURI).size() > 256)
    {
        JLOG(ctx.j.warn())
            << "Malformed transaction. URI may not exceed 256 bytes.";
        return temMALFORMED;
    }

    if (ctx.tx.isFieldPresent(sfAmount))
    {
        STAmount const amt = ctx.tx.getFieldAmount(sfAmount);
        if (!isLegalNet(amt) || amt.signum() <= 0)
        {
            JLOG(ctx.j.warn())
                << "Malformed transaction. Negative or invalid amount specified.";
            return temBAD_AMOUNT;
        }

        if (badCurrency() == amt.getCurrency())
        {
            JLOG(ctx.j.warn())
                << "Malformed transaction. Bad currency.";
            return temBAD_CURRENCY;
        }
    }

    return preflight2(ctx);
}

TER
URIToken::preclaim(PreclaimContext const& ctx)
{
    AccountID const acc = ctx.tx.getAccountID(sfAccount);
    URIOperation op = inferOperation(ctx.tx);

    std::shared_ptr<SLE const> sleU;
    if (ctx.tx.isFieldPresent(sfURITokenID))
        sleU = ctx.view.read(Keylet {ltURI_TOKEN, ctx.tx.getFieldH256(sfURITokenID)});

    uint32_t leFlags = sleU ? sleU->getFieldU32(sfFlags) : 0;
    std::optional<AccountID> issuer;
    std::optional<AccountID> owner;
    std::optional<STAmount> saleAmount;
    std::optional<AccountID> dest;

    std::shared_ptr<SLE const> sleOwner;

    if (sleU)
    {
        if (sleU->getFieldU16(sfLedgerEntryType) != ltURI_TOKEN)
            return tecNO_ENTRY;

        owner = sleU->getAccountID(sfOwner);
        issuer = sleU->getAccountID(sfIssuer);
        if (sleU->isFieldPresent(sfAmount))
            saleAmount = sleU->getFieldAmount(sfAmount);

        if (sleU->isFieldPresent(sfDestination))
            dest = sleU->getAccountID(sfDestination);

        sleOwner = ctx.view.read(keylet::account(*owner));
        if (!sleOwner)
        {
            JLOG(ctx.j.warn())
                    << "Malformed transaction: owner of URIToken is not in the ledger.";
            return tecNO_ENTRY;
        }

    }
    else if (op != URIOperation::Mint)
        return tecNO_ENTRY;

    switch (op)
    {
        case URIOperation::Mint:
        {
            // check if this token has already been minted.
            if (ctx.view.exists(keylet::uritoken(acc, ctx.tx.getFieldVL(sfURI))))
                return tecDUPLICATE;
            
            return tesSUCCESS;
        }

        case URIOperation::Burn:
        {
            if (leFlags == tfBurnable && acc == *issuer)
            {
                // pass, the issuer can burn the URIToken if they minted it with a burn flag
            }
            else
            if (acc == *owner)
            {
                // pass, the owner can always destroy their own URI token
            }
            else
                return tecNO_PERMISSION;

            return tesSUCCESS;
        }

        case URIOperation::Buy:
        {
            // if the owner is the account then the buy operation is a clear operation
            // and we won't bother to check anything else
            if (acc == *owner)
                return tesSUCCESS;

            // check if the seller has listed it at all
            if (!saleAmount)
                return tecNO_PERMISSION;

            // check if the seller has listed it for sale to a specific account
            if (dest && *dest != acc)
                return tecNO_PERMISSION;

            // check if the buyer is paying enough
            STAmount const purchaseAmount = ctx.tx[sfAmount];

            if (purchaseAmount.issue() != saleAmount->issue())
                return tecNFTOKEN_BUY_SELL_MISMATCH;

            if (purchaseAmount < saleAmount)
                return tecINSUFFICIENT_PAYMENT;

            if (purchaseAmount.native() && saleAmount->native())
            {
                // if it's an xrp sale/purchase then no trustline needed
                if (purchaseAmount > (sleOwner->getFieldAmount(sfBalance) - ctx.tx[sfFee]))
                    return tecINSUFFICIENT_FUNDS;
            }

            // execution to here means it's an IOU sale
            // check if the buyer has the right trustline with an adequate balance

            STAmount availableFunds{accountFunds(
                ctx.view,
                acc,
                purchaseAmount,
                fhZERO_IF_FROZEN,
                ctx.j)};

            if (purchaseAmount > availableFunds)
               return tecINSUFFICIENT_FUNDS;

            return tesSUCCESS;
        }

        case URIOperation::Clear:
        {
            if (acc != *owner)
                return tecNO_PERMISSION;

            return tesSUCCESS;
        }
        case URIOperation::Sell:
        {
            if (acc != *owner)
                return tecNO_PERMISSION;

            if (!isXRP(*saleAmount))
            {
                AccountID const iouIssuer = saleAmount->getIssuer();
                if (!ctx.view.exists(keylet::account(iouIssuer)))
                        return tecNO_ISSUER;
            }
            return tesSUCCESS;
        }

        default:
        {
            JLOG(ctx.j.warn())
                << "URIToken txid=" << ctx.tx.getTransactionID() << " preclaim with URIOperation::Invalid\n";
            return tecINTERNAL;
        }
    }

}

TER
URIToken::doApply()
{
    auto j = ctx_.app.journal("View"); 
    URIOperation op = inferOperation(ctx_.tx);

    auto const sle = view().peek(keylet::account(account_));
    if (!sle)
        return tefINTERNAL;

    if (op == URIOperation::Mint || op == URIOperation::Buy)
    {
        STAmount const reserve{view().fees().accountReserve(sle->getFieldU32(sfOwnerCount) + 1)};

        if (mPriorBalance - ctx_.tx.getFieldAmount(sfFee).xrp() < reserve)
            return tecINSUFFICIENT_RESERVE;
    }

    std::shared_ptr<SLE> sleU;
    std::optional<AccountID> issuer;
    std::optional<AccountID> owner;
    std::optional<STAmount> saleAmount;
    std::optional<AccountID> dest;
    std::optional<Keylet> kl;
    std::shared_ptr<SLE> sleOwner;

    if (op != URIOperation::Mint)
    {
        kl = Keylet {ltURI_TOKEN, ctx_.tx.getFieldH256(sfURITokenID)};
        sleU  = view().peek(*kl);

        if (!sleU)
            return tecNO_ENTRY;
        
        if (sleU->getFieldU16(sfLedgerEntryType) != ltURI_TOKEN)
            return tecNO_ENTRY;

        owner = (*sleU)[sfOwner];
        issuer = (*sleU)[sfIssuer];
        saleAmount = (*sleU)[~sfAmount];
        dest = (*sleU)[~sfDestination];

        if (*owner == account_)
            sleOwner = sle;
        else
            sleOwner = view().peek(keylet::account(*owner));

        if (!sleOwner)
        {
            JLOG(j.warn())
                    << "Malformed transaction: owner of URIToken is not in the ledger.";
            return tecNO_ENTRY;
        }
    }

    switch (op)
    {
        case URIOperation::Mint:
        {

            kl = keylet::uritoken(account_, ctx_.tx.getFieldVL(sfURI));
            if (view().exists(*kl))
                return tecDUPLICATE;

            sleU = std::make_shared<SLE>(*kl);
            sleU->setAccountID(sfOwner, account_);
            sleU->setAccountID(sfIssuer, account_);
            sleU->setFieldVL(sfURI, ctx_.tx.getFieldVL(sfURI));

            if (ctx_.tx.isFieldPresent(sfDigest))
                sleU->setFieldH256(sfDigest, ctx_.tx.getFieldH256(sfDigest));

            auto const page = view().dirInsert(
                keylet::ownerDir(account_),
                *kl,
                describeOwnerDir(account_));

            JLOG(j_.trace())
                << "Adding URIToken to owner directory "
                << to_string(kl->key) << ": "
                << (page ? "success" : "failure");

            if (!page)
                return tecDIR_FULL;

            sleU->setFieldU64(sfOwnerNode, *page);
            view().insert(sleU);

            adjustOwnerCount(view(), sle, 1, j);
            return tesSUCCESS;
        }
        
        case URIOperation::Clear:
        {
            sleU->makeFieldAbsent(sfAmount);
            if (sleU->isFieldPresent(sfDestination))
                sleU->makeFieldAbsent(sfDestination);
            view().update(sleU);
            return tesSUCCESS;
        }

        case URIOperation::Buy:
        {
            if (account_ == *owner)
            {
                // this is a clear operation
                sleU->makeFieldAbsent(sfAmount);
                if (sleU->isFieldPresent(sfDestination))
                    sleU->makeFieldAbsent(sfDestination);
                view().update(sleU);
                return tesSUCCESS;
            }

            STAmount const purchaseAmount = ctx_.tx.getFieldAmount(sfAmount);

            bool const sellerLow = purchaseAmount.getIssuer() > *owner;
            bool const buyerLow = purchaseAmount.getIssuer() > account_;

            // check if the seller has listed it at all
            if (!saleAmount)
                return tecNO_PERMISSION;

            // check if the seller has listed it for sale to a specific account
            if (dest && *dest != account_)
                return tecNO_PERMISSION;

            if (purchaseAmount.issue() != saleAmount->issue())
                return tecNFTOKEN_BUY_SELL_MISMATCH;

            std::optional<STAmount> initBuyerBal;
            std::optional<STAmount> initSellerBal;
            std::optional<STAmount> finBuyerBal;
            std::optional<STAmount> finSellerBal;
            std::optional<STAmount> dstAmt;
            std::optional<Keylet> tlSeller;
            std::shared_ptr<SLE> sleDstLine;
            std::shared_ptr<SLE> sleSrcLine;

            // if it's an xrp sale/purchase then no trustline needed
            if (purchaseAmount.native())
            {
                if (purchaseAmount < saleAmount)
                    return tecINSUFFICIENT_PAYMENT;

                if (purchaseAmount > ((*sleOwner)[sfBalance] - ctx_.tx[sfFee]))
                    return tecINSUFFICIENT_FUNDS;

                dstAmt = purchaseAmount;

                initSellerBal = (*sleOwner)[sfBalance];
                initBuyerBal = (*sle)[sfBalance];

                finSellerBal = *initSellerBal + purchaseAmount;
                finBuyerBal = *initBuyerBal - purchaseAmount;
            }
            else
            { 
                // IOU sale

                STAmount availableFunds{accountFunds(
                    view(),
                    account_,
                    purchaseAmount,
                    fhZERO_IF_FROZEN,
                    j)};

                if (purchaseAmount > availableFunds)
                    return tecINSUFFICIENT_FUNDS;


                // check if the seller has a line
                tlSeller =
                    keylet::line(*owner, purchaseAmount.getIssuer(), purchaseAmount.getCurrency());
                Keylet tlBuyer =
                    keylet::line(account_, purchaseAmount.getIssuer(), purchaseAmount.getCurrency());

                sleDstLine = view().peek(*tlSeller);
                sleSrcLine = view().peek(tlBuyer);

                if (!sleDstLine)
                {
                    // they do not, so we can create one if they have sufficient reserve

                    if (std::uint32_t const ownerCount = {sleOwner->at(sfOwnerCount)};
                        (*sleOwner)[sfBalance] < view().fees().accountReserve(ownerCount + 1))
                    {
                        JLOG(j_.trace()) << "Trust line does not exist. "
                                            "Insufficent reserve to create line.";

                        return tecNO_LINE_INSUF_RESERVE;
                    }
                }

                // remove from buyer
                initBuyerBal = buyerLow ? (*sleSrcLine)[sfBalance] : -(*sleSrcLine)[sfBalance];
                finBuyerBal = *initBuyerBal - purchaseAmount;
                
                // compute amount to deliver
                static Rate const parityRate(QUALITY_ONE);
                auto xferRate = transferRate(view(), saleAmount->getIssuer());
                dstAmt = 
                    xferRate == parityRate 
                    ? purchaseAmount
                    : multiplyRound(purchaseAmount, xferRate, purchaseAmount.issue(), true);

                if (!sellerLow)
                    dstAmt->negate();

                initSellerBal = !sleDstLine 
                    ? purchaseAmount.zeroed() 
                    : (sellerLow ? (*sleDstLine)[sfBalance] : -(*sleDstLine)[sfBalance]);

                finSellerBal = *initSellerBal + *dstAmt;
                    
            }

            // sanity check balance mutations (xrp or iou, both are checked the same way now)
            if (*finSellerBal < *initSellerBal)
            {
                JLOG(j.warn())
                    << "URIToken txid=" << ctx_.tx.getTransactionID() << " "
                    << "finSellerBal < initSellerBal";
                return tecINTERNAL;
            }

            if (*finBuyerBal > *initBuyerBal)
            {
                JLOG(j.warn())
                    << "URIToken txid=" << ctx_.tx.getTransactionID() << " "
                    << "finBuyerBal > initBuyerBal";
                return tecINTERNAL;
            }

            if (*finBuyerBal < beast::zero)
            {
                JLOG(j.warn())
                    << "URIToken txid=" << ctx_.tx.getTransactionID() << " "
                    << "finBuyerBal < 0";
                return tecINTERNAL;
            }

            if (*finSellerBal < beast::zero)
            {
                JLOG(j.warn())
                    << "URIToken txid=" << ctx_.tx.getTransactionID() << " "
                    << "finSellerBal < 0";
                return tecINTERNAL;
            }

            // to this point no ledger changes have been made
            // make them in a sensible order such that failure doesn't require cleanup

            // add to new owner's directory first, this can fail if they have too many objects
            auto const newPage = view().dirInsert(
                keylet::ownerDir(account_),
                *kl,
                describeOwnerDir(account_));

            JLOG(j_.trace())
                << "Adding URIToken to owner directory "
                << to_string(kl->key) << ": "
                << (newPage ? "success" : "failure");

            if (!newPage)
            {
                // nothing has happened at all and there is nothing to clean up
                // we can just leave with DIR_FULL
                return tecDIR_FULL;
            }
            
            // Next create destination trustline where applicable. This could fail for a variety of reasons.
            // If it does fail we need to remove the dir entry we just added to the buyer before we leave.
            bool lineCreated = false;
            if (!isXRP(purchaseAmount) && !sleDstLine)
            {
                // clang-format off
                if (TER const ter = trustCreate(
                        view(),                         // payment sandbox
                        sellerLow,                      // is dest low?
                        *issuer,                        // source
                        *owner,                         // destination
                        tlSeller->key,                  // ledger index
                        sleOwner,                      // Account to add to
                        false,                          // authorize account
                        (sleOwner->getFlags() & lsfDefaultRipple) == 0,
                        false,                          // freeze trust line
                        *dstAmt,                        // initial balance zero
                        Issue(purchaseAmount.getCurrency(), *owner),      // limit of zero
                        0,                              // quality in
                        0,                              // quality out
                        j);                             // journal
                    !isTesSuccess(ter))
                {
                    // remove the newly inserted directory entry before we leave
                    //
                    if (!view().dirRemove(keylet::ownerDir(account_), *newPage, kl->key, true))
                    {
                        JLOG(j.fatal())
                            << "Could not remove URIToken from owner directory";

                        return tefBAD_LEDGER;
                    }

                    // leave
                    return ter;
                }
                // clang-format on
           
                // add their trustline to their ownercount 
                lineCreated = true;
            }
            
            // execution to here means we added the URIToken to the buyer's directory
            // and we definitely have a way to send the funds to the seller.

            // remove from current owner directory
            if (!view().dirRemove(keylet::ownerDir(*owner), sleU->getFieldU64(sfOwnerNode), kl->key, true))
            {
                JLOG(j.fatal())
                    << "Could not remove URIToken from owner directory";

                // remove the newly inserted directory entry before we leave
                if (!view().dirRemove(keylet::ownerDir(account_), *newPage, kl->key, true))
                {
                    JLOG(j.fatal())
                        << "Could not remove URIToken from owner directory (2)";
                }

                // clean up any trustline we might have made
                if (lineCreated)
                {
                    auto line = view().peek(*tlSeller);
                    if (line)
                        view().erase(line);
                }

                return tefBAD_LEDGER;
            }

            // above is all the things that could fail. we now have swapped the ownership as far as the ownerdirs
            // are concerned, and we have a place to pay to and from.

            // if a trustline was created then the ownercount stays the same on the seller +1 TL -1 URIToken
            if (!lineCreated)
                adjustOwnerCount(view(), sleOwner, -1, j);

            // the buyer gets a new object
            adjustOwnerCount(view(), sle, 1, j);

            // clean the offer off the object
            sleU->makeFieldAbsent(sfAmount);
            if (sleU->isFieldPresent(sfDestination))
                sleU->makeFieldAbsent(sfDestination);

            // set the new owner of the object
            sleU->setAccountID(sfOwner, account_);

            // tell the ledger where to find it
            sleU->setFieldU64(sfOwnerNode, *newPage);


            // update the buyer's balance
            if (isXRP(purchaseAmount))
            {
                // the sale is for xrp, so set the balance
                sle->setFieldAmount(sfBalance, *finBuyerBal);
            }
            else if (sleSrcLine)
            {
                // update the buyer's line to reflect the reduction of the purchase price
                sleSrcLine->setFieldAmount(sfBalance, buyerLow ? *finBuyerBal : -(*finBuyerBal));
            }
            else
                return tecINTERNAL;

            // update the seller's balance
            if (isXRP(purchaseAmount))
            {
                // the sale is for xrp, so set the balance
                sleOwner->setFieldAmount(sfBalance, *finSellerBal);
            }
            else if (sleDstLine)
            {
                // the line already existed on the seller side so update it
                sleDstLine->setFieldAmount(sfBalance, sellerLow ? *finSellerBal : -(*finSellerBal));
            }
            else if (lineCreated)
            {
                // pass, the TL already has this balance set on it at creation
            }
            else
                return tecINTERNAL;


            if (sleSrcLine)
                view().update(sleSrcLine);
            if (sleDstLine)
                view().update(sleDstLine);

            view().update(sleU);
            view().update(sleOwner);

            return tesSUCCESS;
        }
        case URIOperation::Burn:
        {
            if (sleU->getAccountID(sfOwner) == account_)
            {
                // pass, owner may always delete own object
            }
            else if (sleU->getAccountID(sfIssuer) == account_ && (sleU->getFlags() & tfBurnable))
            {
                // pass, issuer may burn if the tfBurnable flag was set during minting
            }
            else
                return tecNO_PERMISSION;

            // execution to here means there is permission to burn

            auto const page = (*sleU)[sfOwnerNode];
            if (!view().dirRemove(keylet::ownerDir(*owner), page, kl->key, true))
            {
                JLOG(j.fatal())
                    << "Could not remove URIToken from owner directory";
                return tefBAD_LEDGER;
            }

            view().erase(sleU);
            adjustOwnerCount(view(), sle, -1, j);
            return tesSUCCESS;
        }

        case URIOperation::Sell:
        {
            if (account_ != *owner)
                return tecNO_PERMISSION;

            auto const txDest = ctx_.tx[~sfDestination];

            // update destination where applicable
            if (txDest)
                sleU->setAccountID(sfDestination, *txDest);
            else if (dest)
                sleU->makeFieldAbsent(sfDestination);

            sleU->setFieldAmount(sfAmount, ctx_.tx[sfAmount]);

            view().update(sleU);
            std::cout << "sleU on sell: " << (*sleU) << "\n";
            return tesSUCCESS;
        }

        default:
            return tecINTERNAL;

    }
}

}  // namespace ripple
