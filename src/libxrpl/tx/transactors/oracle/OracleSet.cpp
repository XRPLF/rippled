#include <xrpl/tx/transactors/oracle/OracleSet.h>

#include <xrpl/basics/chrono.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/InnerObjectFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/SOTemplate.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/ApplyContext.h>
#include <xrpl/tx/Transactor.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <utility>

namespace xrpl {

static inline std::pair<Currency, Currency>
tokenPairKey(STObject const& pair)
{
    return std::make_pair(
        pair.getFieldCurrency(sfBaseAsset).currency(),
        pair.getFieldCurrency(sfQuoteAsset).currency());
}

NotTEC
OracleSet::preflight(PreflightContext const& ctx)
{
    auto const& dataSeries = ctx.tx.getFieldArray(sfPriceDataSeries);
    if (dataSeries.empty())
        return temARRAY_EMPTY;
    if (dataSeries.size() > kMaxOracleDataSeries)
        return temARRAY_TOO_LARGE;

    auto isInvalidLength = [&](auto const& sField, std::size_t length) {
        return ctx.tx.isFieldPresent(sField) &&
            (ctx.tx[sField].length() == 0 || ctx.tx[sField].length() > length);
    };

    if (isInvalidLength(sfProvider, kMaxOracleProvider) || isInvalidLength(sfURI, kMaxOracleUri) ||
        isInvalidLength(sfAssetClass, kMaxOracleSymbolClass))
        return temMALFORMED;

    return tesSUCCESS;
}

TER
OracleSet::preclaim(PreclaimContext const& ctx)
{
    auto const sleSetter = ctx.view.read(keylet::account(ctx.tx.getAccountID(sfAccount)));
    if (!sleSetter)
        return terNO_ACCOUNT;  // LCOV_EXCL_LINE

    // lastUpdateTime must be within maxLastUpdateTimeDelta seconds
    // of the last closed ledger
    using namespace std::chrono;
    std::size_t const closeTime =
        duration_cast<seconds>(ctx.view.header().closeTime.time_since_epoch()).count();
    std::size_t const lastUpdateTime = ctx.tx[sfLastUpdateTime];
    if (lastUpdateTime < kEpochOffset.count())
        return tecINVALID_UPDATE_TIME;
    std::size_t const lastUpdateTimeEpoch = lastUpdateTime - kEpochOffset.count();
    if (closeTime < kMaxLastUpdateTimeDelta)
        return tecINTERNAL;  // LCOV_EXCL_LINE
    if (lastUpdateTimeEpoch < (closeTime - kMaxLastUpdateTimeDelta) ||
        lastUpdateTimeEpoch > (closeTime + kMaxLastUpdateTimeDelta))
        return tecINVALID_UPDATE_TIME;

    auto const sle =
        ctx.view.read(keylet::oracle(ctx.tx.getAccountID(sfAccount), ctx.tx[sfOracleDocumentID]));

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
        if (entry[~sfScale] > kMaxPriceScale)
            return temMALFORMED;
        if (entry.isFieldPresent(sfAssetPrice))
        {
            pairs.emplace(key);
        }
        else if (sle)
        {
            pairsDel.emplace(key);
        }
        else
        {
            return temMALFORMED;
        }
    }

    // Lambda is used to check if the value of a field, passed
    // in the transaction, is equal to the value of that field
    // in the on-ledger object.
    auto isConsistent = [&ctx, &sle](auto const& field) {
        auto const v = ctx.tx[~field];
        return !v || *v == (*sle)[field];
    };

    std::int8_t adjustReserve = 0;
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
                {
                    pairsDel.erase(key);
                }
                else
                {
                    pairs.emplace(key);
                }
            }
        }
        if (!pairsDel.empty())
            return tecTOKEN_PAIR_NOT_FOUND;

        auto const oldCount = sle->getFieldArray(sfPriceDataSeries).size() > 5 ? 2 : 1;
        auto const newCount = pairs.size() > 5 ? 2 : 1;
        adjustReserve = newCount - oldCount;
    }
    else
    {
        // create

        if (!ctx.tx.isFieldPresent(sfProvider) || !ctx.tx.isFieldPresent(sfAssetClass))
            return temMALFORMED;
        adjustReserve = pairs.size() > 5 ? 2 : 1;
    }

    if (pairs.empty())
        return tecARRAY_EMPTY;
    if (pairs.size() > kMaxOracleDataSeries)
        return tecARRAY_TOO_LARGE;

    auto const reserve =
        ctx.view.fees().accountReserve(sleSetter->getFieldU32(sfOwnerCount) + adjustReserve);
    auto const& balance = sleSetter->getFieldAmount(sfBalance);

    if (balance < reserve)
        return tecINSUFFICIENT_RESERVE;

    return tesSUCCESS;
}

static bool
adjustOwnerCount(ApplyContext& ctx, int count)
{
    if (auto const sleAccount = ctx.view().peek(keylet::account(ctx.tx[sfAccount])))
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
            InnerObjectFormats::getInstance().findSOTemplateBySField(sfPriceData))
        obj.set(*elements);
}

TER
OracleSet::doApply()
{
    auto const oracleID = keylet::oracle(account_, ctx_.tx[sfOracleDocumentID]);

    auto populatePriceData = [](STObject& priceData, STObject const& entry) {
        setPriceDataInnerObjTemplate(priceData);
        priceData.setFieldCurrency(sfBaseAsset, entry.getFieldCurrency(sfBaseAsset));
        priceData.setFieldCurrency(sfQuoteAsset, entry.getFieldCurrency(sfQuoteAsset));
        priceData.setFieldU64(sfAssetPrice, entry.getFieldU64(sfAssetPrice));
        if (entry.isFieldPresent(sfScale))
            priceData.setFieldU8(sfScale, entry.getFieldU8(sfScale));
    };

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
            priceData.setFieldCurrency(sfBaseAsset, entry.getFieldCurrency(sfBaseAsset));
            priceData.setFieldCurrency(sfQuoteAsset, entry.getFieldCurrency(sfQuoteAsset));
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
                iter->second.setFieldU64(sfAssetPrice, entry.getFieldU64(sfAssetPrice));
                if (entry.isFieldPresent(sfScale))
                    iter->second.setFieldU8(sfScale, entry.getFieldU8(sfScale));
            }
            else
            {
                // add a token pair with the price
                STObject priceData{sfPriceData};
                populatePriceData(priceData, entry);
                pairs.emplace(key, std::move(priceData));
            }
        }
        STArray updatedSeries;
        for (auto const& iter : pairs)
            updatedSeries.pushBack(iter.second);
        sle->setFieldArray(sfPriceDataSeries, updatedSeries);
        if (ctx_.tx.isFieldPresent(sfURI))
            sle->setFieldVL(sfURI, ctx_.tx[sfURI]);
        sle->setFieldU32(sfLastUpdateTime, ctx_.tx[sfLastUpdateTime]);
        if (!sle->isFieldPresent(sfOracleDocumentID) &&
            ctx_.view().rules().enabled(fixIncludeKeyletFields))
        {
            (*sle)[sfOracleDocumentID] = ctx_.tx[sfOracleDocumentID];
        }

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
        if (ctx_.view().rules().enabled(fixIncludeKeyletFields))
        {
            (*sle)[sfOracleDocumentID] = ctx_.tx[sfOracleDocumentID];
        }
        sle->setFieldVL(sfProvider, ctx_.tx[sfProvider]);
        if (ctx_.tx.isFieldPresent(sfURI))
            sle->setFieldVL(sfURI, ctx_.tx[sfURI]);

        STArray series;
        if (!ctx_.view().rules().enabled(fixPriceOracleOrder))
        {
            series = ctx_.tx.getFieldArray(sfPriceDataSeries);
        }
        else
        {
            std::map<std::pair<Currency, Currency>, STObject> pairs;
            for (auto const& entry : ctx_.tx.getFieldArray(sfPriceDataSeries))
            {
                auto const key = tokenPairKey(entry);
                STObject priceData{sfPriceData};
                populatePriceData(priceData, entry);
                pairs.emplace(key, std::move(priceData));
            }
            for (auto const& iter : pairs)
                series.pushBack(iter.second);
        }

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

void
OracleSet::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
    // No transaction-specific invariants yet (future work).
}

bool
OracleSet::finalizeInvariants(STTx const&, TER, XRPAmount, ReadView const&, beast::Journal const&)
{
    // No transaction-specific invariants yet (future work).
    return true;
}

}  // namespace xrpl
