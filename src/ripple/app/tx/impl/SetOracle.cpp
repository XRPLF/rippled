//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2023 Ripple Labs Inc.

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

#include <ripple/app/tx/impl/SetOracle.h>
#include <ripple/basics/UnorderedContainers.h>
#include <ripple/ledger/Sandbox.h>
#include <ripple/ledger/View.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/InnerObjectFormats.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/protocol/digest.h>

namespace ripple {

static inline std::pair<Currency, Currency>
tokenPairKey(STObject const& pair)
{
    return std::make_pair(
        pair.getFieldCurrency(sfBaseAsset).currency(),
        pair.getFieldCurrency(sfQuoteAsset).currency());
}

NotTEC
SetOracle::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featurePriceOracle))
        return temDISABLED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    if (ctx.tx.getFlags() & tfUniversalMask)
        return temINVALID_FLAG;

    auto const& dataSeries = ctx.tx.getFieldArray(sfPriceDataSeries);
    if (dataSeries.empty())
        return temARRAY_EMPTY;
    if (dataSeries.size() > maxOracleDataSeries)
        return temARRAY_TOO_LARGE;

    auto isInvalidLength = [&](auto const& sField, std::size_t length) {
        return ctx.tx.isFieldPresent(sField) &&
            (ctx.tx[sField].length() == 0 || ctx.tx[sField].length() > length);
    };

    if (isInvalidLength(sfProvider, maxOracleProvider) ||
        isInvalidLength(sfURI, maxOracleURI) ||
        isInvalidLength(sfAssetClass, maxOracleSymbolClass))
        return temMALFORMED;

    return preflight2(ctx);
}

TER
SetOracle::preclaim(PreclaimContext const& ctx)
{
    auto const sleSetter =
        ctx.view.read(keylet::account(ctx.tx.getAccountID(sfAccount)));
    if (!sleSetter)
        return terNO_ACCOUNT;  // LCOV_EXCL_LINE

    // lastUpdateTime must be within maxLastUpdateTimeDelta seconds
    // of the last closed ledger
    using namespace std::chrono;
    std::size_t const closeTime =
        duration_cast<seconds>(ctx.view.info().closeTime.time_since_epoch())
            .count();
    std::size_t const lastUpdateTime = ctx.tx[sfLastUpdateTime];
    if (lastUpdateTime < epoch_offset.count())
        return tecINVALID_UPDATE_TIME;
    std::size_t const lastUpdateTimeEpoch =
        lastUpdateTime - epoch_offset.count();
    if (closeTime < maxLastUpdateTimeDelta)
        return tecINTERNAL;  // LCOV_EXCL_LINE
    if (lastUpdateTimeEpoch < (closeTime - maxLastUpdateTimeDelta) ||
        lastUpdateTimeEpoch > (closeTime + maxLastUpdateTimeDelta))
        return tecINVALID_UPDATE_TIME;

    auto const sle = ctx.view.read(keylet::oracle(
        ctx.tx.getAccountID(sfAccount), ctx.tx[sfOracleDocumentID]));

    // token pairs to add/update
    std::set<std::pair<Currency, Currency>> pairs;
    // token pairs to delete. if a token pair doesn't include
    // the price then this pair should be deleted from the object.
    std::set<std::pair<Currency, Currency>> pairsDel;
    for (auto const& entry : ctx.tx.getFieldArray(sfPriceDataSeries))
    {
        if (entry[sfBaseAsset] == entry[sfQuoteAsset])
            return temMALFORMED;
        auto const key = tokenPairKey(entry);
        if (pairs.contains(key) || pairsDel.contains(key))
            return temMALFORMED;
        if (entry[~sfScale] > maxPriceScale)
            return temMALFORMED;
        if (entry.isFieldPresent(sfAssetPrice))
            pairs.emplace(key);
        else if (sle)
            pairsDel.emplace(key);
        else
            return temMALFORMED;
    }

    // Lambda is used to check if the value of a field, passed
    // in the transaction, is equal to the value of that field
    // in the on-ledger object.
    auto isConsistent = [&ctx, &sle](auto const& field) {
        auto const v = ctx.tx[~field];
        return !v || *v == (*sle)[field];
    };

    std::uint32_t adjustReserve = 0;
    if (sle)
    {
        // update
        // Account is the Owner since we can get sle

        // lastUpdateTime must be more recent than the previous one
        if (ctx.tx[sfLastUpdateTime] <= (*sle)[sfLastUpdateTime])
            return tecINVALID_UPDATE_TIME;

        if (!isConsistent(sfProvider) || !isConsistent(sfAssetClass))
            return temMALFORMED;

        for (auto const& entry : sle->getFieldArray(sfPriceDataSeries))
        {
            auto const key = tokenPairKey(entry);
            if (!pairs.contains(key))
            {
                if (pairsDel.contains(key))
                    pairsDel.erase(key);
                else
                    pairs.emplace(key);
            }
        }
        if (!pairsDel.empty())
            return tecTOKEN_PAIR_NOT_FOUND;

        auto const oldCount =
            sle->getFieldArray(sfPriceDataSeries).size() > 5 ? 2 : 1;
        auto const newCount = pairs.size() > 5 ? 2 : 1;
        adjustReserve = newCount - oldCount;
    }
    else
    {
        // create

        if (!ctx.tx.isFieldPresent(sfProvider) ||
            !ctx.tx.isFieldPresent(sfAssetClass))
            return temMALFORMED;
        adjustReserve = pairs.size() > 5 ? 2 : 1;
    }

    if (pairs.empty())
        return tecARRAY_EMPTY;
    if (pairs.size() > maxOracleDataSeries)
        return tecARRAY_TOO_LARGE;

    auto const reserve = ctx.view.fees().accountReserve(
        sleSetter->getFieldU32(sfOwnerCount) + adjustReserve);
    auto const& balance = sleSetter->getFieldAmount(sfBalance);

    if (balance < reserve)
        return tecINSUFFICIENT_RESERVE;

    return tesSUCCESS;
}

static bool
adjustOwnerCount(ApplyContext& ctx, int count)
{
    if (auto const sleAccount =
            ctx.view().peek(keylet::account(ctx.tx[sfAccount])))
    {
        adjustOwnerCount(ctx.view(), sleAccount, count, ctx.journal);
        return true;
    }

    return false;  // LCOV_EXCL_LINE
}

static void
setPriceDataInnerObjTemplate(STObject& obj)
{
    if (SOTemplate const* elements =
            InnerObjectFormats::getInstance().findSOTemplateBySField(
                sfPriceData))
        obj.set(*elements);
}

TER
SetOracle::doApply()
{
    auto const oracleID = keylet::oracle(account_, ctx_.tx[sfOracleDocumentID]);

    if (auto sle = ctx_.view().peek(oracleID))
    {
        // update
        // the token pair that doesn't have their price updated will not
        // include neither price nor scale in the updated PriceDataSeries

        std::map<std::pair<Currency, Currency>, STObject> pairs;
        // collect current token pairs
        for (auto const& entry : sle->getFieldArray(sfPriceDataSeries))
        {
            STObject priceData{sfPriceData};
            setPriceDataInnerObjTemplate(priceData);
            priceData.setFieldCurrency(
                sfBaseAsset, entry.getFieldCurrency(sfBaseAsset));
            priceData.setFieldCurrency(
                sfQuoteAsset, entry.getFieldCurrency(sfQuoteAsset));
            pairs.emplace(tokenPairKey(entry), std::move(priceData));
        }
        auto const oldCount = pairs.size() > 5 ? 2 : 1;
        // update/add/delete pairs
        for (auto const& entry : ctx_.tx.getFieldArray(sfPriceDataSeries))
        {
            auto const key = tokenPairKey(entry);
            if (!entry.isFieldPresent(sfAssetPrice))
            {
                // delete token pair
                pairs.erase(key);
            }
            else if (auto iter = pairs.find(key); iter != pairs.end())
            {
                // update the price
                iter->second.setFieldU64(
                    sfAssetPrice, entry.getFieldU64(sfAssetPrice));
                if (entry.isFieldPresent(sfScale))
                    iter->second.setFieldU8(sfScale, entry.getFieldU8(sfScale));
            }
            else
            {
                // add a token pair with the price
                STObject priceData{sfPriceData};
                setPriceDataInnerObjTemplate(priceData);
                priceData.setFieldCurrency(
                    sfBaseAsset, entry.getFieldCurrency(sfBaseAsset));
                priceData.setFieldCurrency(
                    sfQuoteAsset, entry.getFieldCurrency(sfQuoteAsset));
                priceData.setFieldU64(
                    sfAssetPrice, entry.getFieldU64(sfAssetPrice));
                if (entry.isFieldPresent(sfScale))
                    priceData.setFieldU8(sfScale, entry.getFieldU8(sfScale));
                pairs.emplace(key, std::move(priceData));
            }
        }
        STArray updatedSeries;
        for (auto const& iter : pairs)
            updatedSeries.push_back(std::move(iter.second));
        sle->setFieldArray(sfPriceDataSeries, updatedSeries);
        if (ctx_.tx.isFieldPresent(sfURI))
            sle->setFieldVL(sfURI, ctx_.tx[sfURI]);
        sle->setFieldU32(sfLastUpdateTime, ctx_.tx[sfLastUpdateTime]);

        auto const newCount = pairs.size() > 5 ? 2 : 1;
        auto const adjust = newCount - oldCount;
        if (adjust != 0 && !adjustOwnerCount(ctx_, adjust))
            return tefINTERNAL;  // LCOV_EXCL_LINE

        ctx_.view().update(sle);
    }
    else
    {
        // create

        sle = std::make_shared<SLE>(oracleID);
        sle->setAccountID(sfOwner, ctx_.tx.getAccountID(sfAccount));
        sle->setFieldVL(sfProvider, ctx_.tx[sfProvider]);
        if (ctx_.tx.isFieldPresent(sfURI))
            sle->setFieldVL(sfURI, ctx_.tx[sfURI]);
        auto const& series = ctx_.tx.getFieldArray(sfPriceDataSeries);
        sle->setFieldArray(sfPriceDataSeries, series);
        sle->setFieldVL(sfAssetClass, ctx_.tx[sfAssetClass]);
        sle->setFieldU32(sfLastUpdateTime, ctx_.tx[sfLastUpdateTime]);

        auto page = ctx_.view().dirInsert(
            keylet::ownerDir(account_), sle->key(), describeOwnerDir(account_));
        if (!page)
            return tecDIR_FULL;  // LCOV_EXCL_LINE

        (*sle)[sfOwnerNode] = *page;

        auto const count = series.size() > 5 ? 2 : 1;
        if (!adjustOwnerCount(ctx_, count))
            return tefINTERNAL;  // LCOV_EXCL_LINE

        ctx_.view().insert(sle);
    }

    return tesSUCCESS;
}

}  // namespace ripple
