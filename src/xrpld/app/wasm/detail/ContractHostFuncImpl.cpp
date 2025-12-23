#include <xrpld/app/misc/Transaction.h>

#include <xrpl/core/NetworkIDService.h>
#include <xrpl/ledger/helpers/ContractUtils.h>
#include <xrpl/protocol/Emitable.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/STParsedJSON.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/tx/apply.h>
#include <xrpl/tx/wasm/ContractHostFuncImpl.h>

namespace xrpl {

Expected<Bytes, HostFunctionError>
getFieldBytesFromSTData(xrpl::STData const& funcParam, std::uint32_t stTypeId)
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
ContractHostFunctionsImpl::instanceParam(std::uint32_t index, std::uint32_t stTypeId)
{
    auto j = getJournal();
    auto const& instanceParams = contractCtx.instanceParameters;

    if (instanceParams.size() <= index)
    {
        JLOG(j.trace()) << "WasmTrace[" << contractId
                        << "]: " << "instanceParam: Index out of bounds";
        return Unexpected(HostFunctionError::INDEX_OUT_OF_BOUNDS);
    }

    xrpl::STData const& instParam = instanceParams[index].value;
    return getFieldBytesFromSTData(instParam, stTypeId);
}

Expected<Bytes, HostFunctionError>
ContractHostFunctionsImpl::functionParam(std::uint32_t index, std::uint32_t stTypeId)
{
    auto j = getJournal();
    auto const& funcParams = contractCtx.functionParameters;

    if (funcParams.size() <= index)
    {
        JLOG(j.trace()) << "WasmTrace[" << contractId
                        << "]: " << "functionParam: Index out of bounds";
        return Unexpected(HostFunctionError::INDEX_OUT_OF_BOUNDS);
    }

    xrpl::STData const& funcParam = funcParams[index].value;
    return getFieldBytesFromSTData(funcParam, stTypeId);
}

inline std::optional<std::reference_wrapper<std::pair<bool, STJson> const>>
getDataCache(ContractContext& contractCtx, xrpl::AccountID const& account)
{
    auto& dataMap = contractCtx.result.dataMap;
    if (dataMap.find(account) == dataMap.end())
        return std::nullopt;

    auto const& ret = dataMap[account];
    return std::cref(ret);
}

inline std::pair<bool, STJson>
getDataOrCache(ContractContext& contractCtx, AccountID const& account)
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
            STJson data = dataSle->getFieldJson(sfContractJson);
            return {data.isObject(), data};
        }

        // Return New STJson if not found
        STJson data;
        return {true, data};
    }

    // Return the cached STJson
    auto const& cacheEntry = cacheEntryLookup->get();
    return {cacheEntry.second.isObject(), cacheEntry.second};
}

inline HostFunctionError
setDataCache(
    ContractContext& contractCtx,
    AccountID const& account,
    STJson const& data,
    beast::Journal const& j,
    bool modified = true)
{
    auto& dataMap = contractCtx.result.dataMap;
    auto& view = contractCtx.applyCtx.view();
    auto const contractId = contractCtx.result.contractKeylet.key;

    auto const sleAccount = view.read(keylet::account(account));
    if (!sleAccount)
    {
        JLOG(j.trace()) << "WasmTrace[" << contractId << "]: " << "setDataCache: Account not found";
        return HostFunctionError::INVALID_ACCOUNT;
    }

    uint32_t maxDataModifications = 1000u;

    if (modified && dataMap.modifiedCount >= maxDataModifications)
    {
        JLOG(j.trace()) << "WasmTrace[" << contractId << "]: "
                        << "setDataCache: Exceeded max data modifications";
        return HostFunctionError::INTERNAL;
    }

    if (dataMap.find(account) == dataMap.end())
    {
        auto const& fees = contractCtx.applyCtx.view().fees();
        STAmount bal = sleAccount->getFieldAmount(sfBalance);

        int64_t availableForReserves =
            bal.xrp().drops() - fees.accountReserve(sleAccount->getFieldU32(sfOwnerCount)).drops();
        int64_t increment = fees.increment.drops();
        if (increment <= 0)
            increment = 1;

        availableForReserves /= increment;

        if (availableForReserves < 1 && modified)
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId
                            << "]: " << "setDataCache: Insufficient reserve";
            return HostFunctionError::INTERNAL;
        }

        dataMap.modifiedCount++;
        dataMap[account] = {modified, data};

        // for (auto const& [acct, entry] : dataMap)
        // {
        //     JLOG(j.trace())
        //         << "Account: " << to_string(acct)
        //         << ", Modified: " << entry.first << ", Data: "
        //         << entry.second.getJson(JsonOptions::none).toStyledString();
        // }

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
    // for (auto const& [acct, entry] : dataMap)
    // {
    //     JLOG(j.trace())
    //         << "Account: " << to_string(acct) << ", Modified: " <<
    //         entry.first
    //         << ", Data: "
    //         << entry.second.getJson(JsonOptions::none).toStyledString();
    // }
    return HostFunctionError::SUCCESS;
}

Expected<Bytes, HostFunctionError>
ContractHostFunctionsImpl::getDataObjectField(AccountID const& account, std::string_view const& key)
{
    auto j = getJournal();
    auto& view = contractCtx.applyCtx.view();
    AccountID const& contractAccount = contractCtx.result.contractAccount;
    try
    {
        auto const sleAccount = view.read(keylet::account(account));
        if (!sleAccount)
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId
                            << "]: " << "getDataObjectField: Account not found";
            return Unexpected(HostFunctionError::INVALID_ACCOUNT);
        }

        // first check if the requested state was previously cached this session
        auto cacheEntryLookup = getDataCache(contractCtx, account);
        if (cacheEntryLookup)
        {
            auto const& cacheEntry = cacheEntryLookup->get();
            STJson const data = cacheEntry.second;
            auto const keyValue = data.getObjectField(std::string(key));
            if (!keyValue)
            {
                JLOG(j.trace()) << "WasmTrace[" << contractId
                                << "]: " << "getDataObjectField: Invalid field";
                return Unexpected(HostFunctionError::INVALID_FIELD);
            }

            Serializer s;
            keyValue.value()->add(s);
            return Bytes{s.peekData().data(), s.peekData().data() + s.peekData().size()};
        }

        auto const dataKeylet = keylet::contractData(account, contractAccount);
        auto const dataSle = view.read(dataKeylet);
        if (!dataSle)
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId
                            << "]: " << "getDataObjectField: Data SLE not found";
            return Unexpected(HostFunctionError::INTERNAL);
        }

        STJson const data = dataSle->getFieldJson(sfContractJson);
        // it exists add it to cache and return it
        if (setDataCache(contractCtx, account, data, j, false) != HostFunctionError::SUCCESS)
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId << "]: "
                            << "getDataObjectField: Failed to set data cache";
            return Unexpected(HostFunctionError::INTERNAL);
        }

        auto const keyValue = data.getObjectField(std::string(key));
        if (!keyValue)
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId
                            << "]: " << "getDataObjectField: Invalid field";
            return Unexpected(HostFunctionError::INVALID_FIELD);
        }

        Serializer s;
        keyValue.value()->add(s);
        return Bytes{s.peekData().data(), s.peekData().data() + s.peekData().size()};
    }
    catch (std::exception const& e)
    {
        JLOG(j.trace()) << "WasmTrace[" << contractId << "]: "
                        << "getDataObjectField: Exception: " << e.what();
        return Unexpected(HostFunctionError::INTERNAL);
    }
}

Expected<Bytes, HostFunctionError>
ContractHostFunctionsImpl::getDataNestedObjectField(
    AccountID const& account,
    std::string_view const& key,
    std::string_view const& nestedKey)
{
    auto j = getJournal();
    auto& view = contractCtx.applyCtx.view();
    AccountID const& contractAccount = contractCtx.result.contractAccount;
    try
    {
        auto const sleAccount = view.read(keylet::account(account));
        if (!sleAccount)
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId << "]: "
                            << "getDataNestedObjectField: Account not found";
            return Unexpected(HostFunctionError::INVALID_ACCOUNT);
        }

        // first check if the requested state was previously cached this session
        auto cacheEntryLookup = getDataCache(contractCtx, account);
        if (cacheEntryLookup)
        {
            auto const& cacheEntry = cacheEntryLookup->get();
            STJson const data = cacheEntry.second;
            auto const keyValue =
                data.getNestedObjectField(std::string(key), std::string(nestedKey));
            if (!keyValue)
            {
                JLOG(j.trace()) << "WasmTrace[" << contractId
                                << "]: " << "getDataNestedObjectField: Invalid field";
                return Unexpected(HostFunctionError::INVALID_FIELD);
            }

            Serializer s;
            keyValue.value()->add(s);
            return Bytes{s.peekData().data(), s.peekData().data() + s.peekData().size()};
        }

        auto const dataKeylet = keylet::contractData(account, contractAccount);
        auto const dataSle = view.read(dataKeylet);
        if (!dataSle)
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId << "]: "
                            << "getDataNestedObjectField: Data SLE not found";
            return Unexpected(HostFunctionError::INTERNAL);
        }

        STJson const data = dataSle->getFieldJson(sfContractJson);
        // it exists add it to cache and return it
        if (setDataCache(contractCtx, account, data, j, false) != HostFunctionError::SUCCESS)
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId << "]: "
                            << "getDataNestedObjectField: Failed to set data cache";
            return Unexpected(HostFunctionError::INTERNAL);
        }

        auto const keyValue = data.getNestedObjectField(std::string(key), std::string(nestedKey));
        if (!keyValue)
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId
                            << "]: " << "getDataNestedObjectField: Invalid field";
            return Unexpected(HostFunctionError::INVALID_FIELD);
        }

        Serializer s;
        keyValue.value()->add(s);
        return Bytes{s.peekData().data(), s.peekData().data() + s.peekData().size()};
    }
    catch (std::exception const& e)
    {
        JLOG(j.trace()) << "WasmTrace[" << contractId << "]: "
                        << "getDataNestedObjectField: Exception: " << e.what();
        return Unexpected(HostFunctionError::INTERNAL);
    }
}

Expected<int32_t, HostFunctionError>
ContractHostFunctionsImpl::setDataObjectField(
    AccountID const& account,
    std::string_view const& key,
    STJson::Value const& value)
{
    auto j = getJournal();
    try
    {
        auto [isObject, data] = getDataOrCache(contractCtx, account);
        if (!isObject)
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId << "]: "
                            << "setDataObjectField: Invalid state: not an object";
            return Unexpected(HostFunctionError::INVALID_STATE);
        }

        data.setObjectField(std::string(key), value);
        if (HostFunctionError ret = setDataCache(contractCtx, account, data, j, true);
            ret != HostFunctionError::SUCCESS)
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId << "]: "
                            << "setDataObjectField: Failed to set object field";
            return Unexpected(ret);
        }

        return static_cast<int32_t>(HostFunctionError::SUCCESS);
    }
    catch (std::exception const& e)
    {
        JLOG(j.trace()) << "WasmTrace[" << contractId << "]: "
                        << "setDataObjectField: Exception: " << e.what();
        return Unexpected(HostFunctionError::INTERNAL);
    }
}

Expected<int32_t, HostFunctionError>
ContractHostFunctionsImpl::setDataNestedObjectField(
    AccountID const& account,
    std::string_view const& key,
    std::string_view const& nestedKey,
    STJson::Value const& value)
{
    auto j = getJournal();
    try
    {
        auto [isObject, data] = getDataOrCache(contractCtx, account);
        if (!isObject)
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId << "]: "
                            << "setDataNestedObjectField: Invalid state: not an object";
            return Unexpected(HostFunctionError::INVALID_STATE);
        }

        data.setNestedObjectField(std::string(key), std::string(nestedKey), value);
        if (HostFunctionError ret = setDataCache(contractCtx, account, data, j, true);
            ret != HostFunctionError::SUCCESS)
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId << "]: "
                            << "setDataNestedObjectField: Failed to set nested "
                               "object field";
            return Unexpected(ret);
        }

        return static_cast<int32_t>(HostFunctionError::SUCCESS);
    }
    catch (std::exception const& e)
    {
        JLOG(j.trace()) << "WasmTrace[" << contractId << "]: "
                        << "setDataNestedObjectField: Exception: " << e.what();
        return Unexpected(HostFunctionError::INTERNAL);
    }
}

Expected<Bytes, HostFunctionError>
ContractHostFunctionsImpl::getDataArrayElementField(
    AccountID const& account,
    size_t index,
    std::string_view const& key)
{
    auto j = getJournal();
    auto& view = contractCtx.applyCtx.view();
    AccountID const& contractAccount = contractCtx.result.contractAccount;
    try
    {
        auto const sleAccount = view.read(keylet::account(account));
        if (!sleAccount)
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId << "]: "
                            << "getDataArrayElementField: Account not found";
            return Unexpected(HostFunctionError::INVALID_ACCOUNT);
        }

        // first check if the requested state was previously cached this session
        auto cacheEntryLookup = getDataCache(contractCtx, account);
        if (cacheEntryLookup)
        {
            auto const& cacheEntry = cacheEntryLookup->get();
            STJson const data = cacheEntry.second;

            if (!data.isArray())
            {
                JLOG(j.trace()) << "WasmTrace[" << contractId << "]: "
                                << "getDataArrayElementField: Invalid state: not an array";
                return Unexpected(HostFunctionError::INVALID_STATE);
            }

            auto const fieldValue = data.getArrayElementField(index, std::string(key));
            if (!fieldValue)
            {
                JLOG(j.trace()) << "WasmTrace[" << contractId << "]: "
                                << "getDataArrayElementField: Failed to get array "
                                   "element field";
                return Unexpected(HostFunctionError::INVALID_FIELD);
            }

            Serializer s;
            fieldValue.value()->add(s);
            return Bytes{s.peekData().data(), s.peekData().data() + s.peekData().size()};
        }

        auto const dataKeylet = keylet::contractData(account, contractAccount);
        auto const dataSle = view.read(dataKeylet);
        if (!dataSle)
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId << "]: "
                            << "getDataArrayElementField: Failed to read contract data";
            return Unexpected(HostFunctionError::INTERNAL);
        }

        STJson const data = dataSle->getFieldJson(sfContractJson);

        if (!data.isArray())
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId << "]: "
                            << "getDataArrayElementField: Invalid state: not an array";
            return Unexpected(HostFunctionError::INVALID_STATE);
        }

        // it exists add it to cache and return it
        if (setDataCache(contractCtx, account, data, j, false) != HostFunctionError::SUCCESS)
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId << "]: "
                            << "setDataArrayElementField: Failed to set array "
                               "element field";
            return Unexpected(HostFunctionError::INTERNAL);
        }

        auto const fieldValue = data.getArrayElementField(index, std::string(key));
        if (!fieldValue)
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId << "]: "
                            << "getDataArrayElementField: Failed to get array "
                               "element field";
            return Unexpected(HostFunctionError::INVALID_FIELD);
        }

        Serializer s;
        fieldValue.value()->add(s);
        return Bytes{s.peekData().data(), s.peekData().data() + s.peekData().size()};
    }
    catch (std::exception const& e)
    {
        JLOG(j.trace()) << "WasmTrace[" << contractId << "]: "
                        << "getDataArrayElementField: Exception: " << e.what();
        return Unexpected(HostFunctionError::INTERNAL);
    }
}

Expected<Bytes, HostFunctionError>
ContractHostFunctionsImpl::getDataNestedArrayElementField(
    AccountID const& account,
    std::string_view const& key,
    size_t index,
    std::string_view const& nestedKey)
{
    auto j = getJournal();
    auto& view = contractCtx.applyCtx.view();
    AccountID const& contractAccount = contractCtx.result.contractAccount;
    try
    {
        auto const sleAccount = view.read(keylet::account(account));
        if (!sleAccount)
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId << "]: "
                            << "getDataNestedArrayElementField: Account not found";
            return Unexpected(HostFunctionError::INVALID_ACCOUNT);
        }

        // if (account != contractCtx.result.otxnAccount)
        // {
        //     JLOG(j.trace()) << "WasmTrace[" << contractId << "]: "
        //                     << "getDataNestedArrayElementField: Unauthorized
        //                     access to account data";
        //     return Unexpected(HostFunctionError::INVALID_ACCOUNT);
        // }

        // first check if the requested state was previously cached this session
        auto cacheEntryLookup = getDataCache(contractCtx, account);
        if (cacheEntryLookup)
        {
            auto const& cacheEntry = cacheEntryLookup->get();
            STJson const data = cacheEntry.second;

            if (!data.isObject())
            {
                JLOG(j.trace()) << "WasmTrace[" << contractId << "]: "
                                << "getDataNestedArrayElementField: Invalid state: "
                                   "not an object";
                return Unexpected(HostFunctionError::INVALID_STATE);
            }

            auto const fieldValue =
                data.getNestedArrayElementField(std::string(key), index, std::string(nestedKey));
            if (!fieldValue)
            {
                JLOG(j.trace()) << "WasmTrace[" << contractId << "]: "
                                << "getDataNestedArrayElementField: Failed to get "
                                   "nested array element field";
                return Unexpected(HostFunctionError::INVALID_FIELD);
            }

            Serializer s;
            fieldValue.value()->add(s);
            return Bytes{s.peekData().data(), s.peekData().data() + s.peekData().size()};
        }

        auto const dataKeylet = keylet::contractData(account, contractAccount);
        auto const dataSle = view.read(dataKeylet);
        if (!dataSle)
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId << "]: "
                            << "getDataNestedArrayElementField: Failed to read "
                               "contract data";
            return Unexpected(HostFunctionError::INTERNAL);
        }

        STJson const data = dataSle->getFieldJson(sfContractJson);

        if (!data.isObject())
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId << "]: "
                            << "getDataNestedArrayElementField: Invalid state: "
                               "not an object";
            return Unexpected(HostFunctionError::INVALID_STATE);
        }

        // it exists add it to cache and return it
        if (setDataCache(contractCtx, account, data, j, false) != HostFunctionError::SUCCESS)
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId << "]: "
                            << "setDataNestedArrayElementField: Failed to set "
                               "nested array element field";
            return Unexpected(HostFunctionError::INTERNAL);
        }

        auto const fieldValue =
            data.getNestedArrayElementField(std::string(key), index, std::string(nestedKey));
        if (!fieldValue)
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId << "]: "
                            << "getDataNestedArrayElementField: Failed to get "
                               "nested array element field";
            return Unexpected(HostFunctionError::INVALID_FIELD);
        }

        Serializer s;
        fieldValue.value()->add(s);
        return Bytes{s.peekData().data(), s.peekData().data() + s.peekData().size()};
    }
    catch (std::exception const& e)
    {
        JLOG(j.trace()) << "WasmTrace[" << contractId << "]: "
                        << "getDataNestedArrayElementField: Exception: " << e.what();
        return Unexpected(HostFunctionError::INTERNAL);
    }
}

Expected<int32_t, HostFunctionError>
ContractHostFunctionsImpl::setDataArrayElementField(
    AccountID const& account,
    size_t index,
    std::string_view const& key,
    STJson::Value const& value)
{
    auto j = getJournal();
    auto [isObject, data] = getDataOrCache(contractCtx, account);

    try
    {
        // For array operations, we expect isObject to be false (indicating it's
        // an array) But getDataOrCache returns isObject=true for new data, so
        // we need to check the actual type
        if (isObject && data.getMap().size() > 0)
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId << "]: "
                            << "setDataArrayElementField: Invalid state: not an array";
            return Unexpected(HostFunctionError::INVALID_STATE);
        }

        // If it's a new empty object, convert it to an array
        if (isObject && data.getMap().empty())
        {
            data = STJson(STJson::Array{});
        }

        if (!data.isArray())
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId << "]: "
                            << "setDataArrayElementField: Invalid state: not an array";
            return Unexpected(HostFunctionError::INVALID_STATE);
        }

        data.setArrayElementField(index, std::string(key), value);
        if (HostFunctionError ret = setDataCache(contractCtx, account, data, j, true);
            ret != HostFunctionError::SUCCESS)
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId << "]: "
                            << "setDataArrayElementField: Failed to set array "
                               "element field";
            return Unexpected(ret);
        }

        return static_cast<int32_t>(HostFunctionError::SUCCESS);
    }
    catch (std::exception const& e)
    {
        JLOG(j.trace()) << "WasmTrace[" << contractId << "]: "
                        << "setDataArrayElementField: Exception: " << e.what();
        return Unexpected(HostFunctionError::INTERNAL);
    }
}

Expected<int32_t, HostFunctionError>
ContractHostFunctionsImpl::setDataNestedArrayElementField(
    AccountID const& account,
    std::string_view const& key,
    size_t index,
    std::string_view const& nestedKey,
    STJson::Value const& value)
{
    auto j = getJournal();
    try
    {
        auto [isObject, data] = getDataOrCache(contractCtx, account);
        if (!isObject)
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId << "]: "
                            << "setDataNestedArrayElementField: Invalid state: "
                               "not an object";
            return Unexpected(HostFunctionError::INVALID_STATE);
        }

        data.setNestedArrayElementField(std::string(key), index, std::string(nestedKey), value);
        if (HostFunctionError ret = setDataCache(contractCtx, account, data, j, true);
            ret != HostFunctionError::SUCCESS)
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId << "]: "
                            << "setDataNestedArrayElementField: Failed to set "
                               "nested array element field";
            return Unexpected(ret);
        }

        return static_cast<int32_t>(HostFunctionError::SUCCESS);
    }
    catch (std::exception const& e)
    {
        JLOG(j.trace()) << "WasmTrace[" << contractId << "]: "
                        << "setDataNestedArrayElementField: Exception: " << e.what();
        return Unexpected(HostFunctionError::INTERNAL);
    }
}

Expected<int32_t, HostFunctionError>
ContractHostFunctionsImpl::buildTxn(std::uint16_t const& txType)
{
    auto j = getJournal();
    auto& app = contractCtx.applyCtx.registry;

    if (!Emitable::getInstance().isEmitable(txType))
    {
        JLOG(j.trace()) << "Transaction type: " << txType << " is not emitable.";
        return Unexpected(HostFunctionError::SUBMIT_TXN_FAILURE);
    }

    try
    {
        auto jv = Json::Value(Json::objectValue);
        auto item = TxFormats::getInstance().findByType(safe_cast<TxType>(txType));
        jv[sfTransactionType] = item->getName();
        jv[sfFee] = "0";
        jv[sfFlags] = 1073741824;
        jv[sfSequence] = contractCtx.result.nextSequence;
        jv[sfAccount] = to_string(contractCtx.result.contractAccount);
        jv[sfSigningPubKey] = "";
        if (auto const networkID = app.get().getNetworkIDService().getNetworkID(); networkID != 0)
            jv[sfNetworkID] = networkID;

        STParsedJSONObject parsed("txn", jv);
        contractCtx.built_txns.push_back(*parsed.object);
        contractCtx.result.nextSequence += 1;
        return contractCtx.built_txns.size() - 1;
    }
    catch (std::exception const& e)
    {
        JLOG(j.trace()) << "WasmTrace[" << contractId << "]: Exception in buildTxn: " << e.what();
        return Unexpected(HostFunctionError::INTERNAL);
    }
}

Expected<int32_t, HostFunctionError>
ContractHostFunctionsImpl::addTxnField(
    std::uint32_t const& index,
    SField const& field,
    Slice const& data)
{
    auto j = getJournal();
    try
    {
        if (index >= contractCtx.built_txns.size())
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId << "]: "
                            << "addTxnField: index out of bounds: " << index;
            return Unexpected(HostFunctionError::INDEX_OUT_OF_BOUNDS);
        }

        // Get the transaction STObject
        auto& obj = contractCtx.built_txns[index];

        // Ensure the transaction has a TransactionType field
        if (!obj.isFieldPresent(sfTransactionType))
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId << "]: "
                            << "addTxnField: TransactionType field not present "
                               "in transaction.";
            return Unexpected(HostFunctionError::FIELD_NOT_FOUND);
        }

        // Extract the numeric tx type from the STObject and convert to TxType
        auto txTypeVal = obj.getFieldU16(sfTransactionType);
        auto txFormat = TxFormats::getInstance().findByType(safe_cast<TxType>(txTypeVal));
        if (!txFormat)
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId << "]: "
                            << "addTxnField: Invalid TransactionType: " << txTypeVal;
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
            JLOG(j.trace()) << "WasmTrace[" << contractId << "]: " << "addTxnField: Field "
                            << field.getName() << " not allowed in transaction type "
                            << txFormat->getName();
            return Unexpected(HostFunctionError::FIELD_NOT_FOUND);
        }

        obj.addFieldFromSlice(field, data);
        JLOG(j.trace()) << "WasmTrace[" << contractId << "]: " << "addTxnField: TXN: "
                        << obj.getJson(JsonOptions::none).toStyledString();
        return static_cast<int32_t>(HostFunctionError::SUCCESS);
    }
    catch (std::exception const& e)
    {
        JLOG(j.trace()) << "WasmTrace[" << contractId
                        << "]: Exception in addTxnField: " << e.what();
        return Unexpected(HostFunctionError::INTERNAL);
    }
}

Expected<int32_t, HostFunctionError>
ContractHostFunctionsImpl::emitBuiltTxn(std::uint32_t const& index)
{
    auto j = getJournal();
    auto& app = contractCtx.applyCtx.registry;
    auto& parentTx = contractCtx.applyCtx.tx;
    auto const parentBatchId = parentTx.getTransactionID();
    try
    {
        if (index >= contractCtx.built_txns.size())
        {
            JLOG(j.trace()) << "WasmTrace[" << parentBatchId
                            << "]: " << "emitBuiltTxn: index out of bounds: " << index;
            return Unexpected(HostFunctionError::INDEX_OUT_OF_BOUNDS);
        }

        // Ensure tfInnerBatchTxn is always set, even if the contract
        // overwrote sfFlags via addTxnField.
        contractCtx.built_txns[index].setFlag(tfInnerBatchTxn);
        auto stxPtr = std::make_shared<STTx>(std::move(contractCtx.built_txns[index]));

        std::string reason;
        auto tpTrans = std::make_shared<Transaction>(stxPtr, reason, app.get().getApp());
        if (tpTrans->getStatus() != NEW)
        {
            JLOG(j.trace()) << "WasmTrace[" << parentBatchId << "]: "
                            << "emitBuiltTxn: Failed to decode transaction: " << reason;
            return Unexpected(HostFunctionError::SUBMIT_TXN_FAILURE);
        }

        OpenView wholeBatchView(batch_view, contractCtx.applyCtx.openView());
        auto applyOneTransaction =
            [&app, &j, &parentBatchId, &wholeBatchView](std::shared_ptr<STTx const> const& tx) {
                auto const pfResult =
                    preflight(app, wholeBatchView.rules(), parentBatchId, *tx, tapBATCH, j);
                auto const ret = preclaim(pfResult, app, wholeBatchView);
                JLOG(j.trace()) << "WasmTrace[" << parentBatchId << "]: " << tx->getTransactionID()
                                << " " << transToken(ret.ter);
                return ret;
            };

        auto const stx = tpTrans->getSTransaction();
        auto const result = applyOneTransaction(stx);
        if (isTesSuccess(result.ter))
            contractCtx.result.emittedTxns.push(stx);
        return TERtoInt(result.ter);
    }
    catch (std::exception const& e)
    {
        JLOG(j.trace()) << "WasmTrace[" << parentBatchId
                        << "]: Exception in emitBuiltTxn: " << e.what();
        return Unexpected(HostFunctionError::INTERNAL);
    }
}

Expected<int32_t, HostFunctionError>
ContractHostFunctionsImpl::emitTxn(std::shared_ptr<STTx const> const& stxPtr)
{
    auto& app = contractCtx.applyCtx.registry;
    auto& parentTx = contractCtx.applyCtx.tx;
    auto j = getJournal();

    try
    {
        // Ensure tfInnerBatchTxn is always set on emitted transactions.
        // Since STTx is const, create a mutable copy if the flag is missing.
        std::shared_ptr<STTx const> txPtr = stxPtr;
        if (!stxPtr->isFlag(tfInnerBatchTxn))
        {
            STObject obj(static_cast<STObject const&>(*stxPtr));
            obj.setFlag(tfInnerBatchTxn);
            txPtr = std::make_shared<STTx const>(std::move(obj));
        }

        std::string reason;
        auto tpTrans = std::make_shared<Transaction>(txPtr, reason, app.get().getApp());
        if (tpTrans->getStatus() != NEW)
            return Unexpected(HostFunctionError::SUBMIT_TXN_FAILURE);

        OpenView wholeBatchView(batch_view, contractCtx.applyCtx.openView());
        auto const parentBatchId = parentTx.getTransactionID();
        auto applyOneTransaction =
            [&app, &j, &parentBatchId, &wholeBatchView](std::shared_ptr<STTx const> const& tx) {
                auto const pfResult =
                    preflight(app, wholeBatchView.rules(), parentBatchId, *tx, tapBATCH, j);
                auto const ret = preclaim(pfResult, app, wholeBatchView);
                JLOG(j.trace()) << "WasmTrace[" << parentBatchId << "]: " << tx->getTransactionID()
                                << " " << transToken(ret.ter);
                return ret;
            };

        auto const stx = tpTrans->getSTransaction();
        auto const result = applyOneTransaction(stx);
        if (isTesSuccess(result.ter))
            contractCtx.result.emittedTxns.push(stx);
        return TERtoInt(result.ter);
    }
    catch (std::exception const& e)
    {
        JLOG(j.trace()) << "WasmTrace[" << parentTx.getTransactionID()
                        << "]: Exception in emitTxn: " << e.what();
        return Unexpected(HostFunctionError::INTERNAL);
    }
}

Expected<int32_t, HostFunctionError>
ContractHostFunctionsImpl::emitEvent(std::string_view const& eventName, STJson const& eventData)
{
    auto j = getJournal();

    try
    {
        // TODO: Validation
        auto& eventMap = contractCtx.result.eventMap;
        eventMap[std::string(eventName)] = eventData;
        return static_cast<int32_t>(HostFunctionError::SUCCESS);
    }
    catch (std::exception const& e)
    {
        JLOG(j.trace()) << "WasmTrace[" << contractId << "]: Exception in emitEvent: " << e.what();
        return Unexpected(HostFunctionError::INTERNAL);
    }
}

}  // namespace xrpl
