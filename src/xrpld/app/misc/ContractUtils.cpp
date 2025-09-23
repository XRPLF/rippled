//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2025 Ripple Labs Inc.

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

#include <xrpld/app/misc/ContractUtils.h>
#include <xrpld/app/misc/NetworkOPs.h>
#include <xrpld/app/tx/detail/NFTokenUtils.h>

#include <xrpl/ledger/View.h>

namespace ripple {
namespace contract {

struct BlobHash
{
    std::size_t
    operator()(Blob const& b) const noexcept
    {
        if (b.empty())
            return 0;
        return std::hash<std::string_view>{}(std::string_view(
            reinterpret_cast<char const*>(b.data()), b.size()));
    }
};

int64_t
contractCreateFee(uint64_t byteCount)
{
    constexpr uint64_t mul = static_cast<uint64_t>(createByteMultiplier);
    if (byteCount > std::numeric_limits<uint64_t>::max() / mul)
        return feeCalculationFailed;  // overflow
    uint64_t uf = byteCount * mul;
    if (uf > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
        return feeCalculationFailed;
    return static_cast<int64_t>(uf);
}

NotTEC
preflightFunctions(STTx const& tx, beast::Journal j)
{
    // Functions must be present if ContractCode is present.
    if (!tx.isFieldPresent(sfContractCode))
        return tesSUCCESS;

    if (!tx.isFieldPresent(sfFunctions))
    {
        JLOG(j.error()) << "ContractCreate/Modify: ContractCode present but "
                           "Functions missing.";
        return temARRAY_EMPTY;
    }

    auto const& functions = tx.getFieldArray(sfFunctions);

    if (functions.empty())
    {
        JLOG(j.error()) << "ContractCreate/Modify: Functions array empty.";
        return temARRAY_EMPTY;
    }

    // Functions must not exceed n entries.
    if (functions.size() > contract::maxContractFunctions)
    {
        JLOG(j.error()) << "ContractCreate/Modify: Functions array too large.";
        return temARRAY_TOO_LARGE;
    }

    std::unordered_set<Blob, BlobHash> uniqueFunctions;
    uniqueFunctions.reserve(functions.size());
    for (auto const& function : functions)
    {
        // Functions must be unique by name.
        auto const& functionName = function.getFieldVL(sfFunctionName);
        if (!uniqueFunctions.insert(functionName).second)
        {
            JLOG(j.error())
                << "Duplicate function name: " << strHex(functionName);
            return temREDUNDANT;
        }

        auto const& parameters = function.getFieldArray(sfParameters);

        // Function Parameters must not exceed n entries each.
        if (parameters.size() > contract::maxContractParams)
        {
            JLOG(j.error()) << "ContractCreate/Modify: Function Parameters "
                               "array is too large.";
            return temARRAY_TOO_LARGE;
        }

        std::unordered_set<Blob, BlobHash> uniqueParameters;
        uniqueParameters.reserve(parameters.size());

        for (auto const& param : parameters)
        {
            // Function Parameter must have a flag.
            if (!param.isFieldPresent(sfParameterFlag))
            {
                JLOG(j.error()) << "ContractCreate/Modify: Function Parameter "
                                   "is missing flag.";
                return temMALFORMED;
            }

            // Function Parameter must have a name.
            if (!param.isFieldPresent(sfParameterName))
            {
                JLOG(j.error()) << "ContractCreate/Modify: Function Parameter "
                                   "is missing name.";
                return temMALFORMED;
            }

            // Function Parameter must have a type.
            if (!param.isFieldPresent(sfParameterType))
            {
                JLOG(j.error()) << "ContractCreate/Modify: Function Parameter "
                                   "is missing type.";
                return temMALFORMED;
            }

            // Function Parameter flags must be valid.
            auto const flags = param.getFieldU32(sfParameterFlag);
            if (flags & tfContractParameterMask)
            {
                JLOG(j.error()) << "ContractCreate/Modify: Invalid parameter "
                                   "flag in Function.";
                return temINVALID_FLAG;
            }

            // Function Parameter name must be unique.
            auto const& paramName = param.getFieldVL(sfParameterName);
            if (!uniqueParameters.insert(paramName).second)
            {
                JLOG(j.error()) << "ContractCreate/Modify: Duplicate Function "
                                   "Parameter name: "
                                << strHex(paramName);
                return temREDUNDANT;
            }
        }
    }
    return tesSUCCESS;
}

NotTEC
preflightInstanceParameters(STTx const& tx, beast::Journal j)
{
    if (!tx.isFieldPresent(sfInstanceParameters))
        return tesSUCCESS;

    auto const& instanceParameters = tx.getFieldArray(sfInstanceParameters);

    // InstanceParameters must not be empty.
    if (instanceParameters.empty())
    {
        JLOG(j.error())
            << "ContractCreate/Modify: InstanceParameters empty array.";
        return temARRAY_EMPTY;
    }

    // InstanceParameters must not exceed n entries.
    if (instanceParameters.size() > contract::maxContractParams)
    {
        JLOG(j.error()) << "ContractCreate/Modify: InstanceParameters array "
                           "is too large.";
        return temARRAY_TOO_LARGE;
    }

    std::unordered_set<Blob, BlobHash> uniqueParameters;
    uniqueParameters.reserve(instanceParameters.size());
    for (auto const& param : instanceParameters)
    {
        // Instance Parameter must have a flag.
        if (!param.isFieldPresent(sfParameterFlag))
        {
            JLOG(j.error())
                << "ContractCreate/Modify: Instance Parameter is missing flag.";
            return temMALFORMED;
        }

        // Instance Parameter must have a name.
        if (!param.isFieldPresent(sfParameterName))
        {
            JLOG(j.error())
                << "ContractCreate/Modify: Instance Parameter is missing name.";
            return temMALFORMED;
        }

        // Instance Parameter must have a type.
        if (!param.isFieldPresent(sfParameterType))
        {
            JLOG(j.error())
                << "ContractCreate/Modify: Instance Parameter is missing type.";
            return temMALFORMED;
        }

        // Instance Parameter flags must be valid.
        auto const flags = param.getFieldU32(sfParameterFlag);
        if (flags & tfContractParameterMask)
        {
            JLOG(j.error()) << "ContractCreate/Modify: Invalid parameter "
                               "flag in Instance Parameter.";
            return temINVALID_FLAG;
        }

        // Instance Parameter name must be unique.
        auto const& paramName = param.getFieldVL(sfParameterName);
        if (!uniqueParameters.insert(paramName).second)
        {
            JLOG(j.error())
                << "ContractCreate/Modify: Duplicate parameter name: "
                << strHex(paramName);
            return temREDUNDANT;
        }
    }
    return tesSUCCESS;
}

bool
validateParameterMapping(
    STArray const& params,
    STArray const& values,
    beast::Journal j)
{
    if (params.size() != values.size())
    {
        JLOG(j.error())
            << "ContractCreate/Modify: InstanceParameterValues size "
               "does not match InstanceParameters size.";
        return false;
    }

    std::unordered_set<Blob, BlobHash> valueNames;
    for (auto const& val : values)
    {
        if (val.isFieldPresent(sfParameterName))
            valueNames.insert(val.getFieldVL(sfParameterName));
    }

    for (auto const& param : params)
    {
        if (!param.isFieldPresent(sfParameterName))
            continue;

        // auto const& paramName = param.getFieldVL(sfParameterName);
        // if (valueNames.find(paramName) == valueNames.end())
        // {
        //     JLOG(j.error()) << "ContractCreate/Modify: Missing "
        //                        "InstanceParameterValue for parameter: "
        //                     << strHex(paramName);
        //     return false;
        // }
    }
    return true;
}

NotTEC
preflightInstanceParameterValues(STTx const& tx, beast::Journal j)
{
    if (!tx.isFieldPresent(sfInstanceParameterValues))
        return tesSUCCESS;

    auto const& instanceParameterValues =
        tx.getFieldArray(sfInstanceParameterValues);

    // InstanceParameters must not be empty.
    if (instanceParameterValues.empty())
    {
        JLOG(j.error())
            << "ContractCreate/Modify: InstanceParameterValues is missing.";
        return temARRAY_EMPTY;
    }

    // InstanceParameterValues must not exceed n entries.
    if (instanceParameterValues.size() > contract::maxContractParams)
    {
        JLOG(j.error()) << "ContractCreate/Modify: InstanceParameterValues "
                           "array is too large.";
        return temARRAY_TOO_LARGE;
    }

    std::unordered_set<Blob, BlobHash> uniqueParameters;
    uniqueParameters.reserve(instanceParameterValues.size());
    for (auto const& param : instanceParameterValues)
    {
        // Instance Parameter must have a flag.
        if (!param.isFieldPresent(sfParameterFlag))
        {
            JLOG(j.error())
                << "ContractCreate/Modify: Instance Parameter is missing flag.";
            return temMALFORMED;
        }

        // Instance Parameter must have a value.
        if (!param.isFieldPresent(sfParameterValue))
        {
            JLOG(j.error()) << "ContractCreate/Modify: Instance Parameter is "
                               "missing value.";
            return temMALFORMED;
        }

        // Instance Parameter flags must be valid.
        auto const flags = param.getFieldU32(sfParameterFlag);
        if (flags & tfContractParameterMask)
        {
            JLOG(j.error()) << "ContractCreate/Modify: Invalid parameter "
                               "flag in Instance Parameter.";
            return temINVALID_FLAG;
        }

        // Instance Parameter name must be unique.
        // auto const& paramName = param.getFieldVL(sfParameterName);
        // if (!uniqueParameters.insert(paramName).second)
        // {
        //     JLOG(j.error())
        //         << "ContractCreate/Modify: Duplicate parameter name: "
        //         << strHex(paramName);
        //     return temREDUNDANT;
        // }
    }

    // Only validate the mapping if InstanceParameters are present
    bool valid = true;
    if (tx.isFieldPresent(sfInstanceParameters))
        valid = validateParameterMapping(
            tx.getFieldArray(sfInstanceParameters),
            tx.getFieldArray(sfInstanceParameterValues),
            j);
    if (!valid)
    {
        JLOG(j.error())
            << "ContractCreate/Modify: InstanceParameterValues do not match "
               "InstanceParameters.";
        return temMALFORMED;
    }

    return tesSUCCESS;
}

bool
isValidParameterFlag(std::uint32_t flags)
{
    return (flags & tfContractParameterMask) == 0;
}

TER
handleFlagParameters(
    ApplyView& view,
    STTx const& tx,
    AccountID const& sourceAccount,
    AccountID const& contractAccount,
    STArray const& parameters,
    XRPAmount const& priorBalance,
    beast::Journal j)
{
    for (auto const& param : parameters)
    {
        if (!param.isFieldPresent(sfParameterFlag) ||
            !isValidParameterFlag(param.getFieldU32(sfParameterFlag)))
            continue;  // Skip invalid flags

        switch (param.getFieldU32(sfParameterFlag))
        {
            case tfSendAmount: {
                if (!param.isFieldPresent(sfParameterValue))
                    return tecINTERNAL;

                auto const& value = param.getFieldData(sfParameterValue);
                STAmount amount = value.getFieldAmount();
                if (auto ter = accountSend(
                        view,
                        sourceAccount,
                        contractAccount,
                        amount,
                        j,
                        WaiveTransferFee::No);
                    !isTesSuccess(ter))
                {
                    JLOG(j.error())
                        << "handleFlagParameters: Failed to send amount: "
                        << amount;
                    return ter;
                }
                break;
            }
            case tfSendNFToken: {
                if (!param.isFieldPresent(sfParameterValue))
                    return tecINTERNAL;
                auto const& value = param.getFieldData(sfParameterValue);
                auto const& nftokenID = value.getFieldH256();
                if (auto ter = nft::transferNFToken(
                        view, sourceAccount, contractAccount, nftokenID);
                    !isTesSuccess(ter))
                {
                    JLOG(j.error())
                        << "handleFlagParameters: Failed to send NFT token: "
                        << nftokenID;
                    return ter;
                }
                break;
            }
            case tfAuthorizeToken: {
                if (!param.isFieldPresent(sfParameterValue))
                    return tecINTERNAL;
                // Handle tfAuthorizeToken if needed
                auto const& value = param.getFieldData(sfParameterValue);
                STAmount limit = value.getFieldAmount();
                Asset asset = Asset{limit.issue()};
                if (auto ter = canAddHolding(view, asset); !isTesSuccess(ter))
                {
                    JLOG(j.error()) << "handleFlagParameters: Cannot add "
                                       "holding for asset: "
                                    << to_string(asset);
                    return ter;
                }
                // Set the issuer to the contract account for the holding
                limit.setIssuer(contractAccount);
                if (auto ter = addEmptyHolding(
                        view, contractAccount, priorBalance, asset, limit, j);
                    !isTesSuccess(ter))
                {
                    JLOG(j.error()) << "handleFlagParameters: Failed to add "
                                       "holding for asset: "
                                    << to_string(asset);
                    return ter;
                }
                break;
            }
        }
    }
    return tesSUCCESS;
}

uint32_t
contractDataReserve(uint32_t size)
{
    // Divide by dataByteMultiplier and round up to the nearest whole number
    return (size + dataByteMultiplier - 1U) / dataByteMultiplier;
}

TER
setContractData(
    ApplyContext& applyCtx,
    AccountID const& account,
    AccountID const& contractAccount,
    STJson const& data)
{
    auto& view = applyCtx.view();
    auto j = applyCtx.app.journal("View");
    auto const sleAccount = view.peek(keylet::account(account));
    if (!sleAccount)
        return tefINTERNAL;

    // if the blob is too large don't set it
    if (data.size() > maxContractDataSize)
        return temARRAY_TOO_LARGE;

    auto dataKeylet = keylet::contractData(account, contractAccount);
    auto dataSle = view.peek(dataKeylet);

    // DELETE
    if (data.size() == 0)
    {
        if (!dataSle)
            return tesSUCCESS;

        uint32_t oldDataReserve =
            contractDataReserve(dataSle->getFieldJson(sfContractJson).size());

        std::uint64_t const page = (*dataSle)[sfOwnerNode];
        // Remove the page from the account directory
        if (!view.dirRemove(
                keylet::ownerDir(account), page, dataKeylet.key, false))
            return tefBAD_LEDGER;

        // remove the actual contract data sle
        view.erase(dataSle);

        // reduce the owner count
        adjustOwnerCount(view, sleAccount, -oldDataReserve, j);
        return tesSUCCESS;
    }

    std::uint32_t ownerCount{(*sleAccount)[sfOwnerCount]};
    bool createNew = !dataSle;
    if (createNew)
    {
        // CREATE
        uint32_t dataReserve = contractDataReserve(data.size());
        uint32_t newReserve = ownerCount + dataReserve;
        XRPAmount const newReserveAmount{
            view.fees().accountReserve(newReserve)};
        if (STAmount((*sleAccount)[sfBalance]).xrp() < newReserveAmount)
            return tecINSUFFICIENT_RESERVE;

        adjustOwnerCount(view, sleAccount, dataReserve, j);
        // create an entry
        dataSle = std::make_shared<SLE>(dataKeylet);
        dataSle->setFieldJson(sfContractJson, data);
        dataSle->setAccountID(sfOwner, account);
        dataSle->setAccountID(sfContractAccount, contractAccount);

        auto const page = view.dirInsert(
            keylet::ownerDir(account),
            dataKeylet.key,
            describeOwnerDir(account));
        if (!page)
            return tecDIR_FULL;

        dataSle->setFieldU64(sfOwnerNode, *page);

        // add new data to ledger
        view.insert(dataSle);
    }
    else
    {
        // UPDATE
        uint32_t oldDataReserve =
            contractDataReserve(dataSle->getFieldJson(sfContractJson).size());
        uint32_t newDataReserve = contractDataReserve(data.size());
        if (newDataReserve != oldDataReserve)
        {
            // if the reserve changes, we need to adjust the owner count
            uint32_t newReserve = ownerCount - oldDataReserve + newDataReserve;
            XRPAmount const newReserveAmount{
                view.fees().accountReserve(newReserve)};
            if (STAmount((*sleAccount)[sfBalance]).xrp() < newReserveAmount)
                return tecINSUFFICIENT_RESERVE;

            adjustOwnerCount(view, sleAccount, newReserve, j);
        }

        // update the data
        dataSle->setFieldJson(sfContractJson, data);
        view.update(dataSle);
    }
    return tesSUCCESS;
}

TER
finalizeContractData(
    ApplyContext& applyCtx,
    AccountID const& contractAccount,
    ContractDataMap const& dataMap,
    ContractEventMap const& eventMap,
    uint256 const& txnID)
{
    auto const& j = applyCtx.app.journal("View");
    uint16_t changeCount = 0;

    for (auto const& [name, data] : eventMap)
        applyCtx.app.getOPs().pubContractEvent(name, data);

    for (auto const& accEntry : dataMap)
    {
        auto const& acc = accEntry.first;
        auto const& cacheEntry = accEntry.second;
        bool is_modified = cacheEntry.first;
        auto const& jsonData = cacheEntry.second;
        if (is_modified)
        {
            changeCount++;
            if (changeCount > maxDataModifications)
            {
                // overflow
                JLOG(j.error())
                    << "ContractError[TX:" << txnID
                    << "]: SetContractData failed: Too many data changes";
                return tecWASM_REJECTED;
            }

            TER result =
                setContractData(applyCtx, acc, contractAccount, jsonData);
            if (!isTesSuccess(result))
            {
                JLOG(j.warn()) << "ContractError[TX:" << txnID
                               << "]: SetContractData failed: " << result
                               << " Account: " << acc;
                return result;
            }
        }
    }
    return tesSUCCESS;
}

}  // namespace contract
}  // namespace ripple
