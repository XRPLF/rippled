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
#include <xrpld/app/misc/Transaction.h>
#include <xrpld/app/tx/apply.h>
#include <xrpld/app/wasm/ContractHostFuncImpl.h>

#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/STParsedJSON.h>
#include <xrpl/protocol/STTx.h>

namespace ripple {

Expected<Bytes, HostFunctionError>
getFieldBytesFromSTData(ripple::STData const& funcParam, std::uint32_t stTypeId)
{
    switch (stTypeId)
    {
        case STI_UINT8: {
            if (funcParam.getInnerSType() != STI_UINT8)
                return Unexpected(HostFunctionError::INVALID_PARAMS);
            uint8_t data = funcParam.getFieldU8();
            return Bytes{data};
        }
        case STI_UINT16: {
            if (funcParam.getInnerSType() != STI_UINT16)
                return Unexpected(HostFunctionError::INVALID_PARAMS);
            uint16_t data = funcParam.getFieldU16();
            return Bytes{
                static_cast<unsigned char>(data & 0xFF),
                static_cast<unsigned char>((data >> 8) & 0xFF)};
        }
        case STI_UINT32: {
            if (funcParam.getInnerSType() != STI_UINT32)
                return Unexpected(HostFunctionError::INVALID_PARAMS);
            uint32_t data = funcParam.getFieldU32();
            return Bytes{
                static_cast<unsigned char>(data & 0xFF),
                static_cast<unsigned char>((data >> 8) & 0xFF),
                static_cast<unsigned char>((data >> 16) & 0xFF),
                static_cast<unsigned char>((data >> 24) & 0xFF)};
        }
        case STI_UINT64: {
            if (funcParam.getInnerSType() != STI_UINT64)
                return Unexpected(HostFunctionError::INVALID_PARAMS);
            uint64_t data = funcParam.getFieldU64();
            return Bytes{
                static_cast<unsigned char>(data & 0xFF),
                static_cast<unsigned char>((data >> 8) & 0xFF),
                static_cast<unsigned char>((data >> 16) & 0xFF),
                static_cast<unsigned char>((data >> 24) & 0xFF),
                static_cast<unsigned char>((data >> 32) & 0xFF),
                static_cast<unsigned char>((data >> 40) & 0xFF),
                static_cast<unsigned char>((data >> 48) & 0xFF),
                static_cast<unsigned char>((data >> 56) & 0xFF)};
        }
        case STI_UINT128: {
            if (funcParam.getInnerSType() != STI_UINT128)
                return Unexpected(HostFunctionError::INVALID_PARAMS);
            uint128 data = funcParam.getFieldH128();
            return Bytes{
                reinterpret_cast<uint8_t const*>(&data),
                reinterpret_cast<uint8_t const*>(&data) + sizeof(uint128)};
        }
        case STI_UINT160: {
            if (funcParam.getInnerSType() != STI_UINT160)
                return Unexpected(HostFunctionError::INVALID_PARAMS);
            uint160 data = funcParam.getFieldH160();
            return Bytes{data.begin(), data.end()};
        }
        case STI_UINT192: {
            if (funcParam.getInnerSType() != STI_UINT192)
                return Unexpected(HostFunctionError::INVALID_PARAMS);
            uint192 data = funcParam.getFieldH192();
            return Bytes{data.begin(), data.end()};
        }
        case STI_UINT256: {
            if (funcParam.getInnerSType() != STI_UINT256)
                return Unexpected(HostFunctionError::INVALID_PARAMS);
            uint256 data = funcParam.getFieldH256();
            return Bytes{data.begin(), data.end()};
        }
        case STI_VL: {
            if (funcParam.getInnerSType() != STI_VL)
                return Unexpected(HostFunctionError::INVALID_PARAMS);
            auto data = funcParam.getFieldVL();
            return Bytes{data.begin(), data.end()};
        }
        case STI_ACCOUNT: {
            if (funcParam.getInnerSType() != STI_ACCOUNT)
                return Unexpected(HostFunctionError::INVALID_PARAMS);
            AccountID data = funcParam.getAccountID();
            return Bytes{data.data(), data.data() + data.size()};
        }
        case STI_AMOUNT: {
            if (funcParam.getInnerSType() != STI_AMOUNT)
                return Unexpected(HostFunctionError::INVALID_PARAMS);
            STAmount data = funcParam.getFieldAmount();
            Serializer s;
            data.add(s);
            auto const& serialized = s.getData();
            return Bytes{serialized.begin(), serialized.end()};
        }
        case STI_NUMBER: {
            if (funcParam.getInnerSType() != STI_NUMBER)
                return Unexpected(HostFunctionError::INVALID_PARAMS);
            STNumber data = funcParam.getFieldNumber();
            Serializer s;
            data.add(s);
            auto const& serialized = s.getData();
            return Bytes{serialized.begin(), serialized.end()};
        }
        case STI_ISSUE: {
            if (funcParam.getInnerSType() != STI_ISSUE)
                return Unexpected(HostFunctionError::INVALID_PARAMS);
            STIssue data = funcParam.getFieldIssue();
            Serializer s;
            data.add(s);
            auto const& serialized = s.getData();
            return Bytes{serialized.begin(), serialized.end()};
        }
        case STI_CURRENCY: {
            if (funcParam.getInnerSType() != STI_CURRENCY)
                return Unexpected(HostFunctionError::INVALID_PARAMS);
            STCurrency data = funcParam.getFieldCurrency();
            Serializer s;
            data.add(s);
            auto const& serialized = s.getData();
            return Bytes{serialized.begin(), serialized.end()};
        }
        case STI_PATHSET:
        case STI_VECTOR256:
        case STI_XCHAIN_BRIDGE:
        case STI_DATA:
        case STI_DATATYPE:
        case STI_JSON:
        default:
            return Unexpected(HostFunctionError::INVALID_PARAMS);
    }
    return Unexpected(HostFunctionError::INVALID_PARAMS);
}

Expected<Bytes, HostFunctionError>
ContractHostFunctionsImpl::instanceParam(
    std::uint32_t index,
    std::uint32_t stTypeId)
{
    auto const& instanceParams = contractCtx.instanceParameters;

    if (instanceParams.size() <= index)
        return Unexpected(HostFunctionError::INDEX_OUT_OF_BOUNDS);

    ripple::STData const& instParam = instanceParams[index].value;
    return getFieldBytesFromSTData(instParam, stTypeId);
}

Expected<Bytes, HostFunctionError>
ContractHostFunctionsImpl::functionParam(
    std::uint32_t index,
    std::uint32_t stTypeId)
{
    auto const& funcParams = contractCtx.functionParameters;

    if (funcParams.size() <= index)
        return Unexpected(HostFunctionError::INDEX_OUT_OF_BOUNDS);

    ripple::STData const& funcParam = funcParams[index].value;
    return getFieldBytesFromSTData(funcParam, stTypeId);
}

inline std::optional<std::reference_wrapper<std::pair<bool, STJson> const>>
getDataCache(ContractContext& contractCtx, ripple::AccountID const& account)
{
    auto& dataMap = contractCtx.result.dataMap;
    if (dataMap.find(account) == dataMap.end())
        return std::nullopt;

    auto const& ret = dataMap[account];
    return std::cref(ret);
}

HostFunctionError
setDataCache(
    ContractContext& contractCtx,
    AccountID const& account,
    STJson const& data,
    bool modified = true)
{
    auto& dataMap = contractCtx.result.dataMap;
    auto& view = contractCtx.applyCtx.view();

    auto const sleAccount = view.read(keylet::account(account));
    if (!sleAccount)
        return HostFunctionError::INVALID_ACCOUNT;

    uint32_t maxDataModifications = 1000u;

    if (modified && dataMap.modifiedCount >= maxDataModifications)
        return HostFunctionError::INTERNAL;

    if (dataMap.find(account) == dataMap.end())
    {
        auto const& fees = contractCtx.applyCtx.view().fees();
        STAmount bal = sleAccount->getFieldAmount(sfBalance);

        int64_t availableForReserves = bal.xrp().drops() -
            fees.accountReserve(sleAccount->getFieldU32(sfOwnerCount)).drops();
        int64_t increment = fees.increment.drops();
        if (increment <= 0)
            increment = 1;

        availableForReserves /= increment;

        if (availableForReserves < 1 && modified)
            return HostFunctionError::INTERNAL;

        dataMap.modifiedCount++;
        dataMap[account] = {modified, data};
        return HostFunctionError::SUCCESS;
    }

    // auto& availableForReserves = std::get<0>(dataMap[account]);
    // bool const canReserveNew = availableForReserves > 0;
    if (modified)
    {
        // if (!canReserveNew)
        //     return HostFunctionError::INSUFFICIENT_RESERVE;

        // availableForReserves--;
        dataMap.modifiedCount++;
    }

    dataMap[account] = {modified, data};
    return HostFunctionError::SUCCESS;
}

Expected<Bytes, HostFunctionError>
ContractHostFunctionsImpl::getContractDataFromKey(
    AccountID const& account,
    std::string_view const& keyName)
{
    auto& view = contractCtx.applyCtx.view();
    AccountID const& contractAccount = contractCtx.result.contractAccount;
    auto const sleAccount = view.read(keylet::account(account));
    if (!sleAccount)
        return Unexpected(HostFunctionError::INVALID_ACCOUNT);

    // first check if the requested state was previously cached this session
    auto cacheEntryLookup = getDataCache(contractCtx, account);
    if (cacheEntryLookup)
    {
        auto const& cacheEntry = cacheEntryLookup->get();
        STJson const data = cacheEntry.second;
        auto const keyValue = data.get(std::string(keyName));
        if (!keyValue)
            return Unexpected(HostFunctionError::INVALID_FIELD);

        Serializer s;
        keyValue.value()->add(s);
        return Bytes{
            s.peekData().data(), s.peekData().data() + s.peekData().size()};
    }

    auto const dataKeylet = keylet::contractData(account, contractAccount);
    auto const dataSle = view.read(dataKeylet);
    if (!dataSle)
        return Unexpected(HostFunctionError::INTERNAL);

    STJson const data = dataSle->getFieldJson(sfContractJson);
    // it exists add it to cache and return it
    if (setDataCache(contractCtx, account, data, false) !=
        HostFunctionError::SUCCESS)
        return Unexpected(HostFunctionError::INTERNAL);

    auto const keyValue = data.get(std::string(keyName));
    if (!keyValue)
        return Unexpected(HostFunctionError::INVALID_FIELD);

    Serializer s;
    keyValue.value()->add(s);
    return Bytes{
        s.peekData().data(), s.peekData().data() + s.peekData().size()};
}

Expected<Bytes, HostFunctionError>
ContractHostFunctionsImpl::getNestedContractDataFromKey(
    AccountID const& account,
    std::string_view const& nestedKeyName,
    std::string_view const& keyName)
{
    auto& view = contractCtx.applyCtx.view();
    AccountID const& contractAccount = contractCtx.result.contractAccount;
    auto const sleAccount = view.read(keylet::account(account));
    if (!sleAccount)
        return Unexpected(HostFunctionError::INVALID_ACCOUNT);

    // first check if the requested state was previously cached this session
    auto cacheEntryLookup = getDataCache(contractCtx, account);
    if (cacheEntryLookup)
    {
        auto const& cacheEntry = cacheEntryLookup->get();
        STJson const data = cacheEntry.second;
        auto const keyValue =
            data.getNested(std::string(nestedKeyName), std::string(keyName));
        if (!keyValue)
            return Unexpected(HostFunctionError::INVALID_FIELD);

        Serializer s;
        keyValue.value()->add(s);
        return Bytes{
            s.peekData().data(), s.peekData().data() + s.peekData().size()};
    }

    auto const dataKeylet = keylet::contractData(account, contractAccount);
    auto const dataSle = view.read(dataKeylet);
    if (!dataSle)
        return Unexpected(HostFunctionError::INTERNAL);

    STJson const data = dataSle->getFieldJson(sfContractJson);
    // it exists add it to cache and return it
    if (setDataCache(contractCtx, account, data, false) !=
        HostFunctionError::SUCCESS)
        return Unexpected(HostFunctionError::INTERNAL);

    auto const keyValue =
        data.getNested(std::string(nestedKeyName), std::string(keyName));
    if (!keyValue)
        return Unexpected(HostFunctionError::INVALID_FIELD);

    Serializer s;
    keyValue.value()->add(s);
    return Bytes{
        s.peekData().data(), s.peekData().data() + s.peekData().size()};
}

STJson
getContractDataOrCache(ContractContext& contractCtx, AccountID const& account)
{
    auto cacheEntryLookup = getDataCache(contractCtx, account);
    if (!cacheEntryLookup)
    {
        AccountID const& contractAccount = contractCtx.result.contractAccount;
        auto const dataKeylet = keylet::contractData(account, contractAccount);
        auto& view = contractCtx.applyCtx.view();
        auto const dataSle = view.read(dataKeylet);
        if (dataSle)
        {
            // Return the STJson from the SLE
            return dataSle->getFieldJson(sfContractJson);
        }

        // Return New STJson if not found
        STJson data;
        return data;
    }

    // Return the cached STJson
    auto const& cacheEntry = cacheEntryLookup->get();
    return cacheEntry.second;
}

Expected<int32_t, HostFunctionError>
ContractHostFunctionsImpl::setContractDataFromKey(
    AccountID const& account,
    std::string_view const& keyName,
    STJson::Value const& value)
{
    STJson data = getContractDataOrCache(contractCtx, account);
    data.set(std::string(keyName), value);
    if (HostFunctionError ret = setDataCache(contractCtx, account, data, true);
        ret != HostFunctionError::SUCCESS)
        return Unexpected(ret);

    return Unexpected(HostFunctionError::INTERNAL);
}

Expected<int32_t, HostFunctionError>
ContractHostFunctionsImpl::setNestedContractDataFromKey(
    AccountID const& account,
    std::string_view const& nestedKeyName,
    std::string_view const& keyName,
    STJson::Value const& value)
{
    STJson data = getContractDataOrCache(contractCtx, account);
    data.setNested(std::string(nestedKeyName), std::string(keyName), value);
    if (HostFunctionError ret = setDataCache(contractCtx, account, data, true);
        ret != HostFunctionError::SUCCESS)
        return Unexpected(ret);

    return Unexpected(HostFunctionError::INTERNAL);
}

Expected<int32_t, HostFunctionError>
ContractHostFunctionsImpl::buildTxn(std::uint16_t const& txType)
{
    auto jv = Json::Value(Json::objectValue);
    auto item = TxFormats::getInstance().findByType(safe_cast<TxType>(txType));
    jv[sfTransactionType] = item->getName();
    jv[sfFee] = "0";
    jv[sfFlags] = 1073741824;
    jv[sfSequence] = contractCtx.result.nextSequence;
    jv[sfAccount] = to_string(contractCtx.result.contractAccount);
    jv[sfSigningPubKey] = "";

    STParsedJSONObject parsed("txn", jv);
    contractCtx.built_txns.push_back(*parsed.object);
    contractCtx.result.nextSequence += 1;
    return contractCtx.built_txns.size() - 1;
}

Expected<int32_t, HostFunctionError>
ContractHostFunctionsImpl::addTxnField(
    std::uint32_t const& index,
    SField const& field,
    Slice const& data)
{
    auto j = getJournal();
    if (index >= contractCtx.built_txns.size())
    {
        JLOG(j.trace()) << "addTxnField: index out of bounds: " << index;
        return Unexpected(HostFunctionError::INDEX_OUT_OF_BOUNDS);
    }

    // Get the transaction STObject
    auto& obj = contractCtx.built_txns[index];

    // Ensure the transaction has a TransactionType field
    if (!obj.isFieldPresent(sfTransactionType))
    {
        JLOG(j.trace()) << "TransactionType field not present in transaction.";
        return Unexpected(HostFunctionError::FIELD_NOT_FOUND);
    }

    // Extract the numeric tx type from the STObject and convert to TxType
    auto txTypeVal = obj.getFieldU16(sfTransactionType);
    auto txFormat =
        TxFormats::getInstance().findByType(safe_cast<TxType>(txTypeVal));
    if (!txFormat)
    {
        JLOG(j.trace()) << "Invalid TransactionType: " << txTypeVal;
        return Unexpected(HostFunctionError::FIELD_NOT_FOUND);
    }

    // Check if the provided field is allowed for this transaction type
    bool found = false;
    for (auto const& e : txFormat->getSOTemplate())
    {
        if (e.sField().getName() == field.getName())
        {
            found = true;
            break;
        }
    }
    if (!found)
    {
        JLOG(j.trace()) << "Field " << field.getName()
                        << " not allowed in transaction type "
                        << txFormat->getName();
        return Unexpected(HostFunctionError::FIELD_NOT_FOUND);
    }

    obj.addFieldFromSlice(field, data);
    JLOG(j.error()) << "BUILT TXN: "
                    << obj.getJson(JsonOptions::none).toStyledString();
    return static_cast<int32_t>(HostFunctionError::SUCCESS);
}

Expected<int32_t, HostFunctionError>
ContractHostFunctionsImpl::emitBuiltTxn(std::uint32_t const& index)
{
    auto j = getJournal();
    auto& app = contractCtx.applyCtx.app;
    auto& parentTx = contractCtx.applyCtx.tx;
    auto const parentBatchId = parentTx.getTransactionID();
    try
    {
        if (index >= contractCtx.built_txns.size())
            return Unexpected(HostFunctionError::INDEX_OUT_OF_BOUNDS);

        auto stxPtr =
            std::make_shared<STTx>(std::move(contractCtx.built_txns[index]));

        std::string reason;
        auto tpTrans = std::make_shared<Transaction>(stxPtr, reason, app);
        if (tpTrans->getStatus() != NEW)
            return Unexpected(HostFunctionError::SUBMIT_TXN_FAILURE);

        OpenView wholeBatchView(batch_view, contractCtx.applyCtx.openView());
        auto applyOneTransaction = [&app, &j, &parentBatchId, &wholeBatchView](
                                       std::shared_ptr<STTx const> const& tx) {
            auto const pfresult = preflight(
                app, wholeBatchView.rules(), parentBatchId, *tx, tapBATCH, j);
            auto const ret = preclaim(pfresult, app, wholeBatchView);
            JLOG(j.error())
                << "WASM [" << parentBatchId << "]: " << tx->getTransactionID()
                << " " << transToken(ret.ter);
            return ret;
        };

        auto const result = applyOneTransaction(tpTrans->getSTransaction());
        if (isTesSuccess(result.ter))
            contractCtx.result.emittedTxns.push(tpTrans);
        return TERtoInt(result.ter);
    }
    catch (std::exception const& e)
    {
        JLOG(j.error()) << "WASM [" << parentTx.getTransactionID()
                        << "]: Exception in emitBuiltTxn: " << e.what();
        return Unexpected(HostFunctionError::INTERNAL);
    }
}

Expected<int32_t, HostFunctionError>
ContractHostFunctionsImpl::emitTxn(std::shared_ptr<STTx const> const& stxPtr)
{
    auto& app = contractCtx.applyCtx.app;
    auto& parentTx = contractCtx.applyCtx.tx;
    auto j = getJournal();

    std::string reason;
    auto tpTrans = std::make_shared<Transaction>(stxPtr, reason, app);
    if (tpTrans->getStatus() != NEW)
        return Unexpected(HostFunctionError::SUBMIT_TXN_FAILURE);

    OpenView wholeBatchView(batch_view, contractCtx.applyCtx.openView());
    auto const parentBatchId = parentTx.getTransactionID();
    auto applyOneTransaction = [&app, &j, &parentBatchId, &wholeBatchView](
                                   std::shared_ptr<STTx const> const& tx) {
        auto const pfresult = preflight(
            app, wholeBatchView.rules(), parentBatchId, *tx, tapBATCH, j);
        auto const ret = preclaim(pfresult, app, wholeBatchView);
        JLOG(j.trace()) << "WASM [" << parentBatchId
                        << "]: " << tx->getTransactionID() << " "
                        << transToken(ret.ter);
        return ret;
    };

    auto const result = applyOneTransaction(tpTrans->getSTransaction());
    if (isTesSuccess(result.ter))
        contractCtx.result.emittedTxns.push(tpTrans);
    return TERtoInt(result.ter);
}

Expected<int32_t, HostFunctionError>
ContractHostFunctionsImpl::emitEvent(
    std::string_view const& eventName,
    STJson const& eventData)
{
    // TODO: Validation
    auto& eventMap = contractCtx.result.eventMap;
    eventMap[std::string(eventName)] = eventData;
    return static_cast<int32_t>(HostFunctionError::SUCCESS);
}

}  // namespace ripple
