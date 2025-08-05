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

#include <xrpld/app/misc/ContractHostFuncImpl.h>
#include <xrpld/app/misc/Transaction.h>
#include <xrpld/app/tx/apply.h>

#include <xrpl/protocol/STData.h>

namespace ripple {

Expected<Bytes, HostFunctionError>
getFieldDataFromSTData(ripple::STData const& funcParam, std::uint32_t stTypeId)
{
    switch (stTypeId)
    {
        case STI_UINT8: {
            // if (write_len != 0 && write_len != 1)
            //     return INVALID_ARGUMENT;
            if (funcParam.getInnerSType() != STI_UINT8)
                return Unexpected(HostFunctionError::INVALID_PARAMS);
            uint8_t data = funcParam.getFieldU8();
            return Bytes{data};
        }
        case STI_UINT16: {
            // if (write_len != 0 && write_len != 2)
            //     return INVALID_ARGUMENT;
            if (funcParam.getInnerSType() != STI_UINT16)
                return Unexpected(HostFunctionError::INVALID_PARAMS);
            uint16_t data = funcParam.getFieldU16();
            // Serialize as two bytes in little-endian order
            return Bytes{
                static_cast<unsigned char>(data & 0xFF),
                static_cast<unsigned char>((data >> 8) & 0xFF)};
        }
        case STI_UINT32: {
            // if (write_len != 0 && write_len != 4)
            //     return INVALID_ARGUMENT;
            if (funcParam.getInnerSType() != STI_UINT32)
                return Unexpected(HostFunctionError::INVALID_PARAMS);
            uint32_t data = funcParam.getFieldU32();
            // Serialize as four bytes in little-endian order
            return Bytes{
                static_cast<unsigned char>(data & 0xFF),
                static_cast<unsigned char>((data >> 8) & 0xFF),
                static_cast<unsigned char>((data >> 16) & 0xFF),
                static_cast<unsigned char>((data >> 24) & 0xFF)};
        }
        case STI_UINT64: {
            // if (write_len != 0 && write_len != 8)
            //     return INVALID_ARGUMENT;
            if (funcParam.getInnerSType() != STI_UINT64)
                return Unexpected(HostFunctionError::INVALID_PARAMS);
            uint64_t data = funcParam.getFieldU64();
            // Serialize as eight bytes in little-endian order
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
            // if (write_len != 16)
            //     return INVALID_ARGUMENT;
            if (funcParam.getInnerSType() != STI_UINT128)
                return Unexpected(HostFunctionError::INVALID_PARAMS);
            uint128 data = funcParam.getFieldH128();
            // Convert uint128 to bytes (assuming uint128 is an array of 16
            // bytes or similar)
            return Bytes{
                reinterpret_cast<uint8_t const*>(&data),
                reinterpret_cast<uint8_t const*>(&data) + sizeof(uint128)};
        }
        case STI_UINT160: {
            // if (write_len != 20)
            //     return INVALID_ARGUMENT;
            if (funcParam.getInnerSType() != STI_UINT160)
                return Unexpected(HostFunctionError::INVALID_PARAMS);
            uint160 data = funcParam.getFieldH160();
            return Bytes{data.begin(), data.end()};
        }
        case STI_UINT192: {
            // if (write_len != 24)
            //     return INVALID_ARGUMENT;
            if (funcParam.getInnerSType() != STI_UINT192)
                return Unexpected(HostFunctionError::INVALID_PARAMS);
            uint192 data = funcParam.getFieldH192();
            return Bytes{data.begin(), data.end()};
        }
        case STI_UINT256: {
            // if (write_len != 32)
            //     return INVALID_ARGUMENT;
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
            // if (write_len != 20)
            //     return INVALID_ARGUMENT;
            if (funcParam.getInnerSType() != STI_ACCOUNT)
                return Unexpected(HostFunctionError::INVALID_PARAMS);
            AccountID data = funcParam.getAccountID();
            return Bytes{data.data(), data.data() + data.size()};
        }
        case STI_AMOUNT: {
            // if (write_len != 8 && write_len != 48)
            //     return INVALID_ARGUMENT;
            if (funcParam.getInnerSType() != STI_AMOUNT)
                return Unexpected(HostFunctionError::INVALID_PARAMS);
            STAmount data = funcParam.getFieldAmount();
            Serializer s;
            data.add(s);
            auto const& serialized = s.getData();
            // if (data.native())
            // {
            //     // if (write_len != 8)
            //     //     return Unexpected(HostFunctionError::INVALID_PARAMS);
            // }
            // else
            // {
            //     // if (write_len != 48)
            //     //     return Unexpected(HostFunctionError::INVALID_PARAMS);
            // }
            return Bytes{serialized.begin(), serialized.end()};
        }
        default:
            return Unexpected(HostFunctionError::INVALID_PARAMS);
    }
    return Unexpected(HostFunctionError::INVALID_PARAMS);
}

Expected<Bytes, HostFunctionError>
ContractHostFunctionsImpl::contractFuncParam(
    std::uint32_t index,
    std::uint32_t stTypeId)
{
    auto const& funcParams = contractCtx.funcParameters;

    if (funcParams.size() <= index)
        return Unexpected(HostFunctionError::INDEX_OUT_OF_BOUNDS);

    ripple::STData const& funcParam = funcParams[index].value;
    return getFieldDataFromSTData(funcParam, stTypeId);
}

Expected<Bytes, HostFunctionError>
ContractHostFunctionsImpl::otxnCallParam(
    std::uint32_t index,
    std::uint32_t stTypeId)
{
    auto const& callParams = contractCtx.callParameters;

    if (callParams.size() <= index)
        return Unexpected(HostFunctionError::INDEX_OUT_OF_BOUNDS);

    ripple::STData const& callParam = callParams[index].value;
    return getFieldDataFromSTData(callParam, stTypeId);
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
ContractHostFunctionsImpl::getContractData(AccountID const& account)
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
        ripple::Blob const blob = cacheEntry.second.toBlob();
        return Bytes{blob.begin(), blob.end()};
    }

    auto const dataKeylet = keylet::contractData(account, contractAccount);
    auto const dataSle = view.read(dataKeylet);
    if (!dataSle)
        return Unexpected(HostFunctionError::INTERNAL);

    auto const data = dataSle->getFieldJson(sfContractJson);
    // it exists add it to cache and return it
    if (setDataCache(contractCtx, account, data, false) !=
        HostFunctionError::SUCCESS)
        return Unexpected(HostFunctionError::INTERNAL);

    ripple::Blob const dataBlob = data.toBlob();
    return Bytes{dataBlob.begin(), dataBlob.end()};
}

Expected<int32_t, HostFunctionError>
ContractHostFunctionsImpl::setContractData(
    AccountID const& account,
    STJson const& data)
{
    uint32_t maxSize = 1024U;
    if (data.toBlob().size() > maxSize)
        return Unexpected(HostFunctionError::DATA_FIELD_TOO_LARGE);

    if (HostFunctionError ret = setDataCache(contractCtx, account, data, true);
        ret != HostFunctionError::SUCCESS)
        return Unexpected(ret);

    return Unexpected(HostFunctionError::INTERNAL);
}

Expected<int32_t, HostFunctionError>
ContractHostFunctionsImpl::emitTxn(std::shared_ptr<STTx const> const& stxPtr)
{
    auto& app = contractCtx.applyCtx.app;
    auto& tx = contractCtx.applyCtx.tx;
    auto j = getJournal();

    std::string reason;
    auto tpTrans = std::make_shared<Transaction>(stxPtr, reason, app);
    if (tpTrans->getStatus() != NEW)
        return Unexpected(HostFunctionError::SUBMIT_TXN_FAILURE);

    // NOTE: SmartContract Txn Ordering
    // contractCtx.applyCtx.apply(tesSUCCESS);
    OpenView wholeBatchView(batch_view, contractCtx.applyCtx.openView());
    auto const parentTxId = tx.getTransactionID();
    auto applyOneTransaction = [&app, &j, &parentTxId, &wholeBatchView](
                                   std::shared_ptr<STTx const> const& tx) {
        OpenView perTxBatchView(batch_view, wholeBatchView);

        auto const ret = ripple::apply(
            app, perTxBatchView, parentTxId, *tx, tapGENERATED, j);
        JLOG(j.error()) << "WASM [" << parentTxId
                        << "]: " << tx->getTransactionID() << " "
                        << (ret.applied ? "applied" : "failure") << ": "
                        << transToken(ret.ter);

        if (ret.applied && (isTesSuccess(ret.ter) || isTecClaim(ret.ter)))
            perTxBatchView.apply(wholeBatchView);

        return ret;
    };

    auto const result = applyOneTransaction(tpTrans->getSTransaction());
    if (result.applied)
        wholeBatchView.apply(contractCtx.applyCtx.openView());
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
