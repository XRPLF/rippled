#include <xrpl/ledger/helpers/ContractUtils.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/Log.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/ledger/helpers/NFTokenHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Rate.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STJson.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/XRPAmount.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace xrpl::contract {

struct BlobHash
{
    std::size_t
    operator()(Blob const& b) const noexcept
    {
        return std::hash<std::string_view>{}(
            std::string_view(reinterpret_cast<char const*>(b.data()), b.size()));
    }
};

int64_t
contractCreateFee(uint64_t byteCount)
{
    constexpr auto mul = static_cast<uint64_t>(createByteMultiplier);
    if (byteCount > std::numeric_limits<uint64_t>::max() / mul)
        return feeCalculationFailed;  // overflow
    uint64_t const uf = byteCount * mul;
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
        JLOG(j.trace()) << "ContractCreate/Modify: ContractCode present but "
                           "Functions missing.";
        return temARRAY_EMPTY;
    }

    auto const& functions = tx.getFieldArray(sfFunctions);

    if (functions.empty())
    {
        JLOG(j.trace()) << "ContractCreate/Modify: Functions array empty.";
        return temARRAY_EMPTY;
    }

    // Functions must not exceed n entries.
    if (functions.size() > contract::maxContractFunctions)
    {
        JLOG(j.trace()) << "ContractCreate/Modify: Functions array too large.";
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
            JLOG(j.trace()) << "Duplicate function name: " << strHex(functionName);
            return temREDUNDANT;
        }

        auto const& parameters = function.getFieldArray(sfParameters);

        // Function Parameters must not exceed n entries each.
        if (parameters.size() > contract::maxContractParams)
        {
            JLOG(j.trace()) << "ContractCreate/Modify: Function Parameters "
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
                JLOG(j.trace()) << "ContractCreate/Modify: Function Parameter "
                                   "is missing flag.";
                return temMALFORMED;
            }

            // Function Parameter must have a type.
            if (!param.isFieldPresent(sfParameterType))
            {
                JLOG(j.trace()) << "ContractCreate/Modify: Function Parameter "
                                   "is missing type.";
                return temMALFORMED;
            }

            // Function Parameter flags must be valid.
            auto const flags = param.getFieldU32(sfParameterFlag);
            if ((flags & tfContractParameterMask) != 0u)
            {
                JLOG(j.trace()) << "ContractCreate/Modify: Invalid parameter "
                                   "flag in Function.";
                return temINVALID_FLAG;
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
        JLOG(j.trace()) << "ContractCreate/Modify: InstanceParameters empty array.";
        return temARRAY_EMPTY;
    }

    // InstanceParameters must not exceed n entries.
    if (instanceParameters.size() > contract::maxContractParams)
    {
        JLOG(j.trace()) << "ContractCreate/Modify: InstanceParameters array "
                           "is too large.";
        return temARRAY_TOO_LARGE;
    }

    std::unordered_set<Blob, BlobHash> uniqueParameters;
    uniqueParameters.reserve(instanceParameters.size());
    for (auto const& param : instanceParameters)
    {
        // The inner object template requires the flag; it must be a parameter flag.
        auto const flags = param.getFieldU32(sfParameterFlag);
        if ((flags & tfContractParameterMask) != 0u)
        {
            JLOG(j.trace()) << "ContractCreate/Modify: Invalid parameter "
                               "flag in Instance Parameter.";
            return temINVALID_FLAG;
        }
    }
    return tesSUCCESS;
}

bool
validateParameterMapping(STArray const& params, STArray const& values, beast::Journal j)
{
    if (params.size() != values.size())
    {
        JLOG(j.trace()) << "ContractCreate/Modify: InstanceParameterValues size "
                           "does not match InstanceParameters size.";
        return false;
    }
    return true;
}

NotTEC
preflightInstanceParameterValues(STTx const& tx, beast::Journal j)
{
    if (!tx.isFieldPresent(sfInstanceParameterValues))
        return tesSUCCESS;

    auto const& instanceParameterValues = tx.getFieldArray(sfInstanceParameterValues);

    // InstanceParameters must not be empty.
    if (instanceParameterValues.empty())
    {
        JLOG(j.trace()) << "ContractCreate/Modify: InstanceParameterValues is missing.";
        return temARRAY_EMPTY;
    }

    // InstanceParameterValues must not exceed n entries.
    if (instanceParameterValues.size() > contract::maxContractParams)
    {
        JLOG(j.trace()) << "ContractCreate/Modify: InstanceParameterValues "
                           "array is too large.";
        return temARRAY_TOO_LARGE;
    }

    std::unordered_set<Blob, BlobHash> uniqueParameters;
    uniqueParameters.reserve(instanceParameterValues.size());
    for (auto const& param : instanceParameterValues)
    {
        // The inner object template requires the flag; it must be a parameter flag.
        auto const flags = param.getFieldU32(sfParameterFlag);
        if ((flags & tfContractParameterMask) != 0u)
        {
            JLOG(j.trace()) << "ContractCreate/Modify: Invalid parameter "
                               "flag in Instance Parameter.";
            return temINVALID_FLAG;
        }
    }

    // Only validate the mapping if InstanceParameters are present
    bool valid = true;
    if (tx.isFieldPresent(sfInstanceParameters))
    {
        valid = validateParameterMapping(
            tx.getFieldArray(sfInstanceParameters), tx.getFieldArray(sfInstanceParameterValues), j);
    }
    if (!valid)
    {
        JLOG(j.trace()) << "ContractCreate/Modify: InstanceParameterValues do not match "
                           "InstanceParameters.";
        return temMALFORMED;
    }

    // Validate flags in InstanceParameterValues
    if (auto const res = preflightFlagParameters(instanceParameterValues, j); !isTesSuccess(res))
    {
        JLOG(j.trace()) << "ContractCreate/Modify: InstanceParameterValues flag "
                           "validation failed: "
                        << transToken(res);
        return res;
    }

    return tesSUCCESS;
}

bool
isValidParameterFlag(std::uint32_t flags)
{
    return (flags & tfContractParameterMask) == 0;
}

NotTEC
preflightFlagParameters(STArray const& parameters, beast::Journal j)
{
    // Callers have refused any flag outside the parameter flags; a call parameter may carry
    // none.
    auto const holds = [](STObject const& param, SerializedTypeID type) {
        return param.isFieldPresent(sfParameterValue) &&
            param.getFieldData(sfParameterValue).getInnerSType() == type;
    };
    for (auto const& param : parameters)
    {
        if (!param.isFieldPresent(sfParameterFlag))
            continue;

        switch (param.getFieldU32(sfParameterFlag))
        {
            case tfSendAmount: {
                if (!holds(param, STI_AMOUNT))
                    return temMALFORMED;
                auto const& value = param.getFieldData(sfParameterValue);
                STAmount const amount = value.getFieldAmount();
                // Preflight Transfer Amount
                if (isXRP(amount))
                {
                    if (amount <= beast::kZero)
                        return temBAD_AMOUNT;
                }
                else if (amount.holds<Issue>())
                {
                    if (amount <= beast::kZero)
                        return temBAD_AMOUNT;

                    if (badCurrency() == amount.get<Issue>().currency)
                        return temBAD_CURRENCY;
                }
                // An MPT amount cannot exceed kMaxMpTokenAmount, the largest it can hold.
                else if (amount <= beast::kZero)
                {
                    return temBAD_AMOUNT;
                }
                break;
            }
            case tfSendNFToken: {
                if (!holds(param, STI_UINT256))
                    return temMALFORMED;
                break;
            }
            case tfAuthorizeToken:
                return temDISABLED;
            default:
                break;
        }
    }
    return tesSUCCESS;
}

TER
preclaimFlagParameters(
    ReadView const& view,
    AccountID const& sourceAccount,
    AccountID const& contractAccount,
    STArray const& parameters,
    beast::Journal j)
{
    // What the sends cost the caller per asset, transfer fee included. doApply makes them one
    // after another, so they are judged together.
    std::vector<STAmount> costs;
    for (auto const& param : parameters)
    {
        if (!param.isFieldPresent(sfParameterFlag))
            continue;

        switch (param.getFieldU32(sfParameterFlag))
        {
            case tfSendAmount: {
                // Preflight checked that each send holds a value of its type.
                STAmount const amount = param.getFieldData(sfParameterValue).getFieldAmount();
                bool const feeWaived = isXRP(amount) || amount.getIssuer() == sourceAccount ||
                    amount.getIssuer() == contractAccount;
                STAmount const cost =
                    feeWaived ? amount : multiply(amount, transferRate(view, amount));
                auto const sameAsset = std::ranges::find_if(
                    costs, [&](STAmount const& c) { return c.asset() == cost.asset(); });
                if (sameAsset == costs.end())
                {
                    costs.push_back(cost);
                }
                else
                {
                    *sameAsset += cost;
                }
                break;
            }
            case tfSendNFToken: {
                // Preflight checked that each send holds a value of its type.
                auto const& value = param.getFieldData(sfParameterValue);
                auto const& nftokenID = value.getFieldH256();
                // Preclaim Transfer NFT Token
                if (!nft::findToken(view, sourceAccount, nftokenID))
                {
                    JLOG(j.trace())
                        << "preclaimFlagParameters: Cannot transfer NFT token: " << nftokenID;
                    return tecNO_ENTRY;
                }
                break;
            }
            default:
                break;
        }
    }

    for (auto const& cost : costs)
    {
        if (isXRP(cost))
        {
            auto const accountSle = view.read(keylet::account(sourceAccount));
            if (!accountSle)
                return tecINTERNAL;  // LCOV_EXCL_LINE
            if (accountSle->getFieldAmount(sfBalance) < cost)
                return tecUNFUNDED;
        }
        else if (
            auto ter = canTransferFT(
                view,
                sourceAccount,
                contractAccount,
                cost,
                j,
                SendIssuerHandling::IhIgnore,
                SendEscrowHandling::EhIgnore,
                SendAuthHandling::AhBoth,
                SendFreezeHandling::FhBoth,
                SendTransferHandling::ThIgnore,
                SendBalanceHandling::BhCheck))
        {
            JLOG(j.trace()) << "preclaimFlagParameters: Cannot transfer amount: " << cost;
            return ter;
        }
    }
    return tesSUCCESS;
}

TER
doApplyFlagParameters(
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
        if (!param.isFieldPresent(sfParameterFlag))
            continue;

        switch (param.getFieldU32(sfParameterFlag))
        {
            case tfSendAmount: {
                // Preflight checked that each send holds a value of its type.
                auto const& value = param.getFieldData(sfParameterValue);
                STAmount const amount = value.getFieldAmount();
                if (auto ter = accountSend(
                        view, sourceAccount, contractAccount, amount, j, {}, WaiveTransferFee::No);
                    !isTesSuccess(ter))
                {
                    JLOG(j.trace()) << "doApplyFlagParameters: Failed to send amount: " << amount;
                    return ter;
                }
                break;
            }
            case tfSendNFToken: {
                // Preflight checked that each send holds a value of its type.
                auto const& value = param.getFieldData(sfParameterValue);
                auto const& nftokenID = value.getFieldH256();
                // The contract receives the token; the caller gives it.
                if (auto ter =
                        nft::transferNFToken(view, contractAccount, sourceAccount, nftokenID, j);
                    !isTesSuccess(ter))
                {
                    JLOG(j.trace())
                        << "doApplyFlagParameters: Failed to send NFT token: " << nftokenID;
                    return ter;
                }
                break;
            }
            default:
                // Preflight refuses tfAuthorizeToken, so only the two sends reach here.
                break;
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
    ServiceRegistry& registry,
    ApplyView& view,
    AccountID const& account,
    AccountID const& contractAccount,
    STJson const& data)
{
    auto const j = registry.getJournal("View");
    auto const sleAccount = view.peek(keylet::account(account));
    if (!sleAccount)
        return tefINTERNAL;

    // if the blob is too large don't set it
    if (data.size() > maxContractDataSize)
        return temARRAY_TOO_LARGE;

    auto const dataKeylet = keylet::contractData(account, contractAccount);
    auto dataSle = view.peek(dataKeylet);

    // An object with no key, or an array with no element, is deleted.
    if (data.isObject() ? data.getMap().empty() : data.getArray().empty())
    {
        if (!dataSle)
            return tesSUCCESS;

        uint32_t const oldDataReserve =
            contractDataReserve(dataSle->getFieldJson(sfContractJson).size());

        std::uint64_t const page = (*dataSle)[sfOwnerNode];
        // Remove the page from the account directory
        if (!view.dirRemove(keylet::ownerDir(account), page, dataKeylet.key, false))
            return tefBAD_LEDGER;

        // remove the actual contract data sle
        view.erase(dataSle);

        // reduce the owner count
        decreaseOwnerCount(view, sleAccount, {}, oldDataReserve, j);
        return tesSUCCESS;
    }

    std::uint32_t const ownerCount{(*sleAccount)[sfOwnerCount]};
    bool const createNew = !dataSle;
    if (createNew)
    {
        // CREATE
        uint32_t const dataReserve = contractDataReserve(data.size());
        uint32_t const newReserve = ownerCount + dataReserve;
        XRPAmount const newReserveAmount{view.fees().accountReserve(newReserve, 1)};
        if (STAmount((*sleAccount)[sfBalance]).xrp() < newReserveAmount)
            return tecINSUFFICIENT_RESERVE;

        increaseOwnerCount(view, sleAccount, {}, dataReserve, j);
        // create an entry
        dataSle = std::make_shared<SLE>(dataKeylet);
        dataSle->setFieldJson(sfContractJson, data);
        dataSle->setAccountID(sfOwner, account);
        dataSle->setAccountID(sfContractAccount, contractAccount);

        auto const page =
            view.dirInsert(keylet::ownerDir(account), dataKeylet.key, describeOwnerDir(account));
        if (!page)
            return tecDIR_FULL;

        dataSle->setFieldU64(sfOwnerNode, *page);

        // add new data to ledger
        view.insert(dataSle);
    }
    else
    {
        // UPDATE
        uint32_t const oldDataReserve =
            contractDataReserve(dataSle->getFieldJson(sfContractJson).size());
        uint32_t const newDataReserve = contractDataReserve(data.size());
        // Move the owner count by the change in the object's reserve.
        if (newDataReserve > oldDataReserve)
        {
            uint32_t const newReserve = ownerCount - oldDataReserve + newDataReserve;
            XRPAmount const newReserveAmount{view.fees().accountReserve(newReserve, 1)};
            if (STAmount((*sleAccount)[sfBalance]).xrp() < newReserveAmount)
                return tecINSUFFICIENT_RESERVE;

            increaseOwnerCount(view, sleAccount, {}, newDataReserve - oldDataReserve, j);
        }
        else if (newDataReserve < oldDataReserve)
        {
            decreaseOwnerCount(view, sleAccount, {}, oldDataReserve - newDataReserve, j);
        }

        // update the data
        dataSle->setFieldJson(sfContractJson, data);
        view.update(dataSle);
    }
    return tesSUCCESS;
}

TER
finalizeContractData(
    ServiceRegistry& registry,
    ApplyView& view,
    AccountID const& contractAccount,
    ContractDataMap const& dataMap,
    uint256 const& txnID)
{
    auto const& j = registry.getJournal("View");
    uint16_t changeCount = 0;

    for (auto const& accEntry : dataMap)
    {
        auto const& acc = accEntry.first;
        auto const& cacheEntry = accEntry.second;
        bool const isModified = cacheEntry.first;
        auto const& jsonData = cacheEntry.second;
        if (isModified)
        {
            changeCount++;
            if (changeCount > maxDataModifications)
            {
                // overflow
                JLOG(j.trace()) << "ContractError[TX:" << txnID
                                << "]: SetContractData failed: Too many data changes";
                return tecBYTECODE_REJECTED;
            }

            TER result = setContractData(registry, view, acc, contractAccount, jsonData);
            if (!isTesSuccess(result))
            {
                JLOG(j.warn()) << "ContractError[TX:" << txnID
                               << "]: SetContractData failed: " << result << " Account: " << acc;
                return result;
            }
        }
    }
    return tesSUCCESS;
}

STArray
contractEvents(ContractEvents const& events)
{
    STArray result(sfContractEvents);
    for (auto const& [name, data] : events)
    {
        auto event = STObject::makeInnerObject(sfContractEvent);
        event.setFieldVL(sfEventName, makeSlice(name));
        event.setFieldJson(sfContractJson, data);
        result.push_back(std::move(event));
    }
    return result;
}

}  // namespace xrpl::contract
