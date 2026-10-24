#include <xrpl/tx/wasm/ContractHostFuncImpl.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/core/NetworkIDService.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/ledger/helpers/ContractUtils.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Emitable.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STCurrency.h>
#include <xrpl/protocol/STData.h>
#include <xrpl/protocol/STIssue.h>
#include <xrpl/protocol/STJson.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/STParsedJSON.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/tx/apply.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace xrpl {

std::expected<Bytes, HostFunctionError>
getFieldBytesFromSTData(xrpl::STData const& funcParam, std::uint32_t stTypeId)
{
    switch (stTypeId)
    {
        case STI_UINT8: {
            if (funcParam.getInnerSType() != STI_UINT8)
                return std::unexpected(HostFunctionError::InvalidParams);
            uint8_t const data = funcParam.getFieldU8();
            return Bytes{data};
        }
        case STI_UINT16: {
            if (funcParam.getInnerSType() != STI_UINT16)
                return std::unexpected(HostFunctionError::InvalidParams);
            uint16_t const data = funcParam.getFieldU16();
            return Bytes{
                static_cast<unsigned char>(data & 0xFF),
                static_cast<unsigned char>((data >> 8) & 0xFF),
            };
        }
        case STI_UINT32: {
            if (funcParam.getInnerSType() != STI_UINT32)
                return std::unexpected(HostFunctionError::InvalidParams);
            uint32_t const data = funcParam.getFieldU32();
            return Bytes{
                static_cast<unsigned char>(data & 0xFF),
                static_cast<unsigned char>((data >> 8) & 0xFF),
                static_cast<unsigned char>((data >> 16) & 0xFF),
                static_cast<unsigned char>((data >> 24) & 0xFF),
            };
        }
        case STI_UINT64: {
            if (funcParam.getInnerSType() != STI_UINT64)
                return std::unexpected(HostFunctionError::InvalidParams);
            uint64_t const data = funcParam.getFieldU64();
            return Bytes{
                static_cast<unsigned char>(data & 0xFF),
                static_cast<unsigned char>((data >> 8) & 0xFF),
                static_cast<unsigned char>((data >> 16) & 0xFF),
                static_cast<unsigned char>((data >> 24) & 0xFF),
                static_cast<unsigned char>((data >> 32) & 0xFF),
                static_cast<unsigned char>((data >> 40) & 0xFF),
                static_cast<unsigned char>((data >> 48) & 0xFF),
                static_cast<unsigned char>((data >> 56) & 0xFF),
            };
        }
        case STI_UINT128: {
            if (funcParam.getInnerSType() != STI_UINT128)
                return std::unexpected(HostFunctionError::InvalidParams);
            uint128 data = funcParam.getFieldH128();
            return Bytes{
                reinterpret_cast<uint8_t const*>(&data),
                reinterpret_cast<uint8_t const*>(&data) + sizeof(uint128)};
        }
        case STI_UINT160: {
            if (funcParam.getInnerSType() != STI_UINT160)
                return std::unexpected(HostFunctionError::InvalidParams);
            uint160 data = funcParam.getFieldH160();
            return Bytes{data.begin(), data.end()};
        }
        case STI_UINT192: {
            if (funcParam.getInnerSType() != STI_UINT192)
                return std::unexpected(HostFunctionError::InvalidParams);
            uint192 data = funcParam.getFieldH192();
            return Bytes{data.begin(), data.end()};
        }
        case STI_UINT256: {
            if (funcParam.getInnerSType() != STI_UINT256)
                return std::unexpected(HostFunctionError::InvalidParams);
            uint256 data = funcParam.getFieldH256();
            return Bytes{data.begin(), data.end()};
        }
        case STI_VL: {
            if (funcParam.getInnerSType() != STI_VL)
                return std::unexpected(HostFunctionError::InvalidParams);
            auto data = funcParam.getFieldVL();
            return Bytes{data.begin(), data.end()};
        }
        case STI_ACCOUNT: {
            if (funcParam.getInnerSType() != STI_ACCOUNT)
                return std::unexpected(HostFunctionError::InvalidParams);
            AccountID data = funcParam.getAccountID();
            return Bytes{data.data(), data.data() + data.size()};
        }
        case STI_AMOUNT: {
            if (funcParam.getInnerSType() != STI_AMOUNT)
                return std::unexpected(HostFunctionError::InvalidParams);
            STAmount const data = funcParam.getFieldAmount();
            Serializer s;
            data.add(s);
            auto const& serialized = s.getData();
            return Bytes{serialized.begin(), serialized.end()};
        }
        case STI_NUMBER: {
            if (funcParam.getInnerSType() != STI_NUMBER)
                return std::unexpected(HostFunctionError::InvalidParams);
            STNumber const data = funcParam.getFieldNumber();
            Serializer s;
            data.add(s);
            auto const& serialized = s.getData();
            return Bytes{serialized.begin(), serialized.end()};
        }
        case STI_ISSUE: {
            if (funcParam.getInnerSType() != STI_ISSUE)
                return std::unexpected(HostFunctionError::InvalidParams);
            STIssue const data = funcParam.getFieldIssue();
            Serializer s;
            data.add(s);
            auto const& serialized = s.getData();
            return Bytes{serialized.begin(), serialized.end()};
        }
        case STI_CURRENCY: {
            if (funcParam.getInnerSType() != STI_CURRENCY)
                return std::unexpected(HostFunctionError::InvalidParams);
            STCurrency const data = funcParam.getFieldCurrency();
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
            return std::unexpected(HostFunctionError::InvalidParams);
    }
    return std::unexpected(HostFunctionError::InvalidParams);
}

std::expected<Bytes, HostFunctionError>
ContractHostFunctionsImpl::instanceParam(std::uint32_t index, std::uint32_t stTypeId)
{
    auto const j = getJournal();
    auto const& instanceParams = contractCtx_.instanceParameters;

    if (instanceParams.size() <= index)
    {
        JLOG(j.trace()) << "WasmTrace[" << contractId_
                        << "]: " << "instanceParam: Index out of bounds";
        return std::unexpected(HostFunctionError::IndexOutOfBounds);
    }

    xrpl::STData const& instParam = instanceParams[index].value;
    return getFieldBytesFromSTData(instParam, stTypeId);
}

std::expected<Bytes, HostFunctionError>
ContractHostFunctionsImpl::functionParam(std::uint32_t index, std::uint32_t stTypeId)
{
    auto const j = getJournal();
    auto const& funcParams = contractCtx_.functionParameters;

    if (funcParams.size() <= index)
    {
        JLOG(j.trace()) << "WasmTrace[" << contractId_
                        << "]: " << "functionParam: Index out of bounds";
        return std::unexpected(HostFunctionError::IndexOutOfBounds);
    }

    xrpl::STData const& funcParam = funcParams[index].value;
    return getFieldBytesFromSTData(funcParam, stTypeId);
}

inline std::optional<std::reference_wrapper<std::pair<bool, STJson> const>>
getDataCache(ContractContext& contractCtx, xrpl::AccountID const& account)
{
    auto& dataMap = contractCtx.result.dataMap;
    if (!dataMap.contains(account))
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
        auto const& view = contractCtx.applyCtx.view();
        auto const dataSle = view.read(dataKeylet);
        if (dataSle)
        {
            // Return the STJson from the SLE
            STJson const data = dataSle->getFieldJson(sfContractJson);
            return {data.isObject(), data};
        }

        // Return New STJson if not found
        STJson const data;
        return {true, data};
    }

    // Return the cached STJson
    auto const& cacheEntry = cacheEntryLookup->get();
    return {cacheEntry.second.isObject(), cacheEntry.second};
}

inline std::expected<void, HostFunctionError>
setDataCache(
    ContractContext& contractCtx,
    AccountID const& account,
    STJson const& data,
    beast::Journal const& j,
    bool modified = true)
{
    auto& dataMap = contractCtx.result.dataMap;
    auto const& view = contractCtx.applyCtx.view();
    auto const contractId = contractCtx.result.contractKeylet.key;

    auto const sleAccount = view.read(keylet::account(account));
    if (!sleAccount)
    {
        JLOG(j.trace()) << "WasmTrace[" << contractId << "]: " << "setDataCache: Account not found";
        return std::unexpected(HostFunctionError::InvalidAccount);
    }

    uint32_t const maxDataModifications = 1000u;

    if (modified && dataMap.modifiedCount >= maxDataModifications)
    {
        JLOG(j.trace()) << "WasmTrace[" << contractId << "]: "
                        << "setDataCache: Exceeded max data modifications";
        return std::unexpected(HostFunctionError::InvalidState);
    }

    if (!dataMap.contains(account))
    {
        auto const& fees = contractCtx.applyCtx.view().fees();
        STAmount const bal = sleAccount->getFieldAmount(sfBalance);

        int64_t availableForReserves = bal.xrp().drops() -
            fees.accountReserve(sleAccount->getFieldU32(sfOwnerCount), 1).drops();
        int64_t increment = fees.increment.drops();
        if (increment <= 0)
            increment = 1;

        availableForReserves /= increment;

        if (availableForReserves < 1 && modified)
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId
                            << "]: " << "setDataCache: Insufficient reserve";
            return std::unexpected(HostFunctionError::InvalidState);
        }

        dataMap.modifiedCount++;
        dataMap[account] = {modified, data};

        // for (auto const& [acct, entry] : dataMap)
        // {
        //     JLOG(j.trace())
        //         << "Account: " << to_string(acct)
        //         << ", Modified: " << entry.first << ", Data: "
        //         << entry.second.getJson(JsonOptions::Values::None).toStyledString();
        // }

        return {};
    }

    // auto& availableForReserves = std::get<0>(dataMap[account]);
    // bool const canReserveNew = availableForReserves > 0;
    if (modified)
    {
        // if (!canReserveNew)
        //     return std::unexpected(HostFunctionError::InsufficientReserve);

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
    //         << entry.second.getJson(JsonOptions::Values::None).toStyledString();
    // }
    return {};
}

std::expected<Bytes, HostFunctionError>
ContractHostFunctionsImpl::getDataObjectField(AccountID const& account, std::string_view const& key)
{
    auto const j = getJournal();
    auto const& view = contractCtx_.applyCtx.view();
    AccountID const& contractAccount = contractCtx_.result.contractAccount;
    try
    {
        auto const sleAccount = view.read(keylet::account(account));
        if (!sleAccount)
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId_
                            << "]: " << "getDataObjectField: Account not found";
            return std::unexpected(HostFunctionError::InvalidAccount);
        }

        // first check if the requested state was previously cached this session
        auto cacheEntryLookup = getDataCache(contractCtx_, account);
        if (cacheEntryLookup)
        {
            auto const& cacheEntry = cacheEntryLookup->get();
            STJson const data = cacheEntry.second;
            auto const keyValue = data.getObjectField(std::string(key));
            if (!keyValue)
            {
                JLOG(j.trace()) << "WasmTrace[" << contractId_
                                << "]: " << "getDataObjectField: Invalid field";
                return std::unexpected(HostFunctionError::InvalidField);
            }

            Serializer s;
            keyValue.value()->add(s);
            return Bytes{s.peekData().data(), s.peekData().data() + s.peekData().size()};
        }

        auto const dataKeylet = keylet::contractData(account, contractAccount);
        auto const dataSle = view.read(dataKeylet);
        if (!dataSle)
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId_
                            << "]: " << "getDataObjectField: Data SLE not found";
            return std::unexpected(HostFunctionError::LedgerObjNotFound);
        }

        STJson const data = dataSle->getFieldJson(sfContractJson);
        // it exists add it to cache and return it
        if (auto const cacheResult = setDataCache(contractCtx_, account, data, j, false);
            !cacheResult)
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId_ << "]: "
                            << "getDataObjectField: Failed to set data cache";
            return std::unexpected(cacheResult.error());
        }

        auto const keyValue = data.getObjectField(std::string(key));
        if (!keyValue)
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId_
                            << "]: " << "getDataObjectField: Invalid field";
            return std::unexpected(HostFunctionError::InvalidField);
        }

        Serializer s;
        keyValue.value()->add(s);
        return Bytes{s.peekData().data(), s.peekData().data() + s.peekData().size()};
    }
    catch (std::exception const& e)
    {
        JLOG(j.trace()) << "WasmTrace[" << contractId_ << "]: "
                        << "getDataObjectField: Exception: " << e.what();
        return std::unexpected(HostFunctionError::InternalFatal);
    }
}

std::expected<Bytes, HostFunctionError>
ContractHostFunctionsImpl::getDataNestedObjectField(
    AccountID const& account,
    std::string_view const& key,
    std::string_view const& nestedKey)
{
    auto const j = getJournal();
    auto const& view = contractCtx_.applyCtx.view();
    AccountID const& contractAccount = contractCtx_.result.contractAccount;
    try
    {
        auto const sleAccount = view.read(keylet::account(account));
        if (!sleAccount)
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId_ << "]: "
                            << "getDataNestedObjectField: Account not found";
            return std::unexpected(HostFunctionError::InvalidAccount);
        }

        // first check if the requested state was previously cached this session
        auto cacheEntryLookup = getDataCache(contractCtx_, account);
        if (cacheEntryLookup)
        {
            auto const& cacheEntry = cacheEntryLookup->get();
            STJson const data = cacheEntry.second;
            auto const keyValue =
                data.getNestedObjectField(std::string(key), std::string(nestedKey));
            if (!keyValue)
            {
                JLOG(j.trace()) << "WasmTrace[" << contractId_
                                << "]: " << "getDataNestedObjectField: Invalid field";
                return std::unexpected(HostFunctionError::InvalidField);
            }

            Serializer s;
            keyValue.value()->add(s);
            return Bytes{s.peekData().data(), s.peekData().data() + s.peekData().size()};
        }

        auto const dataKeylet = keylet::contractData(account, contractAccount);
        auto const dataSle = view.read(dataKeylet);
        if (!dataSle)
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId_ << "]: "
                            << "getDataNestedObjectField: Data SLE not found";
            return std::unexpected(HostFunctionError::LedgerObjNotFound);
        }

        STJson const data = dataSle->getFieldJson(sfContractJson);
        // it exists add it to cache and return it
        if (auto const cacheResult = setDataCache(contractCtx_, account, data, j, false);
            !cacheResult)
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId_ << "]: "
                            << "getDataNestedObjectField: Failed to set data cache";
            return std::unexpected(cacheResult.error());
        }

        auto const keyValue = data.getNestedObjectField(std::string(key), std::string(nestedKey));
        if (!keyValue)
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId_
                            << "]: " << "getDataNestedObjectField: Invalid field";
            return std::unexpected(HostFunctionError::InvalidField);
        }

        Serializer s;
        keyValue.value()->add(s);
        return Bytes{s.peekData().data(), s.peekData().data() + s.peekData().size()};
    }
    catch (std::exception const& e)
    {
        JLOG(j.trace()) << "WasmTrace[" << contractId_ << "]: "
                        << "getDataNestedObjectField: Exception: " << e.what();
        return std::unexpected(HostFunctionError::InternalFatal);
    }
}

std::expected<int32_t, HostFunctionError>
ContractHostFunctionsImpl::setDataObjectField(
    AccountID const& account,
    std::string_view const& key,
    STJson::Value const& value)
{
    auto const j = getJournal();
    try
    {
        auto [isObject, data] = getDataOrCache(contractCtx_, account);
        if (!isObject)
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId_ << "]: "
                            << "setDataObjectField: Invalid state: not an object";
            return std::unexpected(HostFunctionError::InvalidState);
        }

        data.setObjectField(std::string(key), value);
        if (auto const ret = setDataCache(contractCtx_, account, data, j, true); !ret)
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId_ << "]: "
                            << "setDataObjectField: Failed to set object field";
            return std::unexpected(ret.error());
        }

        return 0;
    }
    catch (std::exception const& e)
    {
        JLOG(j.trace()) << "WasmTrace[" << contractId_ << "]: "
                        << "setDataObjectField: Exception: " << e.what();
        return std::unexpected(HostFunctionError::InternalFatal);
    }
}

std::expected<int32_t, HostFunctionError>
ContractHostFunctionsImpl::setDataNestedObjectField(
    AccountID const& account,
    std::string_view const& key,
    std::string_view const& nestedKey,
    STJson::Value const& value)
{
    auto const j = getJournal();
    try
    {
        auto [isObject, data] = getDataOrCache(contractCtx_, account);
        if (!isObject)
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId_ << "]: "
                            << "setDataNestedObjectField: Invalid state: not an object";
            return std::unexpected(HostFunctionError::InvalidState);
        }

        data.setNestedObjectField(std::string(key), std::string(nestedKey), value);
        if (auto const ret = setDataCache(contractCtx_, account, data, j, true); !ret)
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId_ << "]: "
                            << "setDataNestedObjectField: Failed to set nested "
                               "object field";
            return std::unexpected(ret.error());
        }

        return 0;
    }
    catch (std::exception const& e)
    {
        JLOG(j.trace()) << "WasmTrace[" << contractId_ << "]: "
                        << "setDataNestedObjectField: Exception: " << e.what();
        return std::unexpected(HostFunctionError::InternalFatal);
    }
}

std::expected<Bytes, HostFunctionError>
ContractHostFunctionsImpl::getDataArrayElementField(
    AccountID const& account,
    size_t index,
    std::string_view const& key)
{
    auto const j = getJournal();
    auto const& view = contractCtx_.applyCtx.view();
    AccountID const& contractAccount = contractCtx_.result.contractAccount;
    try
    {
        auto const sleAccount = view.read(keylet::account(account));
        if (!sleAccount)
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId_ << "]: "
                            << "getDataArrayElementField: Account not found";
            return std::unexpected(HostFunctionError::InvalidAccount);
        }

        // first check if the requested state was previously cached this session
        auto cacheEntryLookup = getDataCache(contractCtx_, account);
        if (cacheEntryLookup)
        {
            auto const& cacheEntry = cacheEntryLookup->get();
            STJson const data = cacheEntry.second;

            if (!data.isArray())
            {
                JLOG(j.trace()) << "WasmTrace[" << contractId_ << "]: "
                                << "getDataArrayElementField: Invalid state: not an array";
                return std::unexpected(HostFunctionError::InvalidState);
            }

            auto const fieldValue = data.getArrayElementField(index, std::string(key));
            if (!fieldValue)
            {
                JLOG(j.trace()) << "WasmTrace[" << contractId_ << "]: "
                                << "getDataArrayElementField: Failed to get array "
                                   "element field";
                return std::unexpected(HostFunctionError::InvalidField);
            }

            Serializer s;
            fieldValue.value()->add(s);
            return Bytes{s.peekData().data(), s.peekData().data() + s.peekData().size()};
        }

        auto const dataKeylet = keylet::contractData(account, contractAccount);
        auto const dataSle = view.read(dataKeylet);
        if (!dataSle)
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId_ << "]: "
                            << "getDataArrayElementField: Failed to read contract data";
            return std::unexpected(HostFunctionError::LedgerObjNotFound);
        }

        STJson const data = dataSle->getFieldJson(sfContractJson);

        if (!data.isArray())
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId_ << "]: "
                            << "getDataArrayElementField: Invalid state: not an array";
            return std::unexpected(HostFunctionError::InvalidState);
        }

        // it exists add it to cache and return it
        if (auto const cacheResult = setDataCache(contractCtx_, account, data, j, false);
            !cacheResult)
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId_ << "]: "
                            << "setDataArrayElementField: Failed to set array "
                               "element field";
            return std::unexpected(cacheResult.error());
        }

        auto const fieldValue = data.getArrayElementField(index, std::string(key));
        if (!fieldValue)
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId_ << "]: "
                            << "getDataArrayElementField: Failed to get array "
                               "element field";
            return std::unexpected(HostFunctionError::InvalidField);
        }

        Serializer s;
        fieldValue.value()->add(s);
        return Bytes{s.peekData().data(), s.peekData().data() + s.peekData().size()};
    }
    catch (std::exception const& e)
    {
        JLOG(j.trace()) << "WasmTrace[" << contractId_ << "]: "
                        << "getDataArrayElementField: Exception: " << e.what();
        return std::unexpected(HostFunctionError::InternalFatal);
    }
}

std::expected<Bytes, HostFunctionError>
ContractHostFunctionsImpl::getDataNestedArrayElementField(
    AccountID const& account,
    std::string_view const& key,
    size_t index,
    std::string_view const& nestedKey)
{
    auto const j = getJournal();
    auto const& view = contractCtx_.applyCtx.view();
    AccountID const& contractAccount = contractCtx_.result.contractAccount;
    try
    {
        auto const sleAccount = view.read(keylet::account(account));
        if (!sleAccount)
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId_ << "]: "
                            << "getDataNestedArrayElementField: Account not found";
            return std::unexpected(HostFunctionError::InvalidAccount);
        }

        // if (account != contractCtx.result.otxnAccount)
        // {
        //     JLOG(j.trace()) << "WasmTrace[" << contractId << "]: "
        //                     << "getDataNestedArrayElementField: Unauthorized
        //                     access to account data";
        //     return std::unexpected(HostFunctionError::InvalidAccount);
        // }

        // first check if the requested state was previously cached this session
        auto cacheEntryLookup = getDataCache(contractCtx_, account);
        if (cacheEntryLookup)
        {
            auto const& cacheEntry = cacheEntryLookup->get();
            STJson const data = cacheEntry.second;

            if (!data.isObject())
            {
                JLOG(j.trace()) << "WasmTrace[" << contractId_ << "]: "
                                << "getDataNestedArrayElementField: Invalid state: "
                                   "not an object";
                return std::unexpected(HostFunctionError::InvalidState);
            }

            auto const fieldValue =
                data.getNestedArrayElementField(std::string(key), index, std::string(nestedKey));
            if (!fieldValue)
            {
                JLOG(j.trace()) << "WasmTrace[" << contractId_ << "]: "
                                << "getDataNestedArrayElementField: Failed to get "
                                   "nested array element field";
                return std::unexpected(HostFunctionError::InvalidField);
            }

            Serializer s;
            fieldValue.value()->add(s);
            return Bytes{s.peekData().data(), s.peekData().data() + s.peekData().size()};
        }

        auto const dataKeylet = keylet::contractData(account, contractAccount);
        auto const dataSle = view.read(dataKeylet);
        if (!dataSle)
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId_ << "]: "
                            << "getDataNestedArrayElementField: Failed to read "
                               "contract data";
            return std::unexpected(HostFunctionError::LedgerObjNotFound);
        }

        STJson const data = dataSle->getFieldJson(sfContractJson);

        if (!data.isObject())
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId_ << "]: "
                            << "getDataNestedArrayElementField: Invalid state: "
                               "not an object";
            return std::unexpected(HostFunctionError::InvalidState);
        }

        // it exists add it to cache and return it
        if (auto const cacheResult = setDataCache(contractCtx_, account, data, j, false);
            !cacheResult)
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId_ << "]: "
                            << "setDataNestedArrayElementField: Failed to set "
                               "nested array element field";
            return std::unexpected(cacheResult.error());
        }

        auto const fieldValue =
            data.getNestedArrayElementField(std::string(key), index, std::string(nestedKey));
        if (!fieldValue)
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId_ << "]: "
                            << "getDataNestedArrayElementField: Failed to get "
                               "nested array element field";
            return std::unexpected(HostFunctionError::InvalidField);
        }

        Serializer s;
        fieldValue.value()->add(s);
        return Bytes{s.peekData().data(), s.peekData().data() + s.peekData().size()};
    }
    catch (std::exception const& e)
    {
        JLOG(j.trace()) << "WasmTrace[" << contractId_ << "]: "
                        << "getDataNestedArrayElementField: Exception: " << e.what();
        return std::unexpected(HostFunctionError::InternalFatal);
    }
}

std::expected<int32_t, HostFunctionError>
ContractHostFunctionsImpl::setDataArrayElementField(
    AccountID const& account,
    size_t index,
    std::string_view const& key,
    STJson::Value const& value)
{
    auto const j = getJournal();
    auto [isObject, data] = getDataOrCache(contractCtx_, account);

    try
    {
        // For array operations, we expect isObject to be false (indicating it's
        // an array) But getDataOrCache returns isObject=true for new data, so
        // we need to check the actual type
        if (isObject && !data.getMap().empty())
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId_ << "]: "
                            << "setDataArrayElementField: Invalid state: not an array";
            return std::unexpected(HostFunctionError::InvalidState);
        }

        // If it's a new empty object, convert it to an array
        if (isObject && data.getMap().empty())
        {
            data = STJson(STJson::Array{});
        }

        if (!data.isArray())
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId_ << "]: "
                            << "setDataArrayElementField: Invalid state: not an array";
            return std::unexpected(HostFunctionError::InvalidState);
        }

        data.setArrayElementField(index, std::string(key), value);
        if (auto const ret = setDataCache(contractCtx_, account, data, j, true); !ret)
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId_ << "]: "
                            << "setDataArrayElementField: Failed to set array "
                               "element field";
            return std::unexpected(ret.error());
        }

        return 0;
    }
    catch (std::exception const& e)
    {
        JLOG(j.trace()) << "WasmTrace[" << contractId_ << "]: "
                        << "setDataArrayElementField: Exception: " << e.what();
        return std::unexpected(HostFunctionError::InternalFatal);
    }
}

std::expected<int32_t, HostFunctionError>
ContractHostFunctionsImpl::setDataNestedArrayElementField(
    AccountID const& account,
    std::string_view const& key,
    size_t index,
    std::string_view const& nestedKey,
    STJson::Value const& value)
{
    auto const j = getJournal();
    try
    {
        auto [isObject, data] = getDataOrCache(contractCtx_, account);
        if (!isObject)
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId_ << "]: "
                            << "setDataNestedArrayElementField: Invalid state: "
                               "not an object";
            return std::unexpected(HostFunctionError::InvalidState);
        }

        data.setNestedArrayElementField(std::string(key), index, std::string(nestedKey), value);
        if (auto const ret = setDataCache(contractCtx_, account, data, j, true); !ret)
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId_ << "]: "
                            << "setDataNestedArrayElementField: Failed to set "
                               "nested array element field";
            return std::unexpected(ret.error());
        }

        return 0;
    }
    catch (std::exception const& e)
    {
        JLOG(j.trace()) << "WasmTrace[" << contractId_ << "]: "
                        << "setDataNestedArrayElementField: Exception: " << e.what();
        return std::unexpected(HostFunctionError::InternalFatal);
    }
}

std::expected<int32_t, HostFunctionError>
ContractHostFunctionsImpl::buildTxn(std::uint16_t const& txType)
{
    auto const j = getJournal();
    auto const& app = contractCtx_.applyCtx.registry;

    if (!Emitable::getInstance().isEmitable(Emitable::txToEmitableType(safeCast<TxType>(txType))))
    {
        JLOG(j.trace()) << "Transaction type: " << txType << " is not emitable.";
        return std::unexpected(HostFunctionError::SubmitTxnFailure);
    }

    try
    {
        auto jv = json::Value(json::ValueType::Object);
        auto item = TxFormats::getInstance().findByType(safeCast<TxType>(txType));
        jv[sfTransactionType] = item->getName();
        jv[sfFee] = "0";
        jv[sfFlags] = 1073741824;
        jv[sfSequence] = contractCtx_.result.nextSequence;
        jv[sfAccount] = to_string(contractCtx_.result.contractAccount);
        jv[sfSigningPubKey] = "";
        if (auto const networkID = app.get().getNetworkIDService().getNetworkID(); networkID != 0)
            jv[sfNetworkID] = networkID;

        STParsedJSONObject parsed("txn", jv);
        if (!parsed.object)
            return std::unexpected(HostFunctionError::InternalFatal);  // LCOV_EXCL_LINE
        contractCtx_.builtTxns.push_back(*parsed.object);
        contractCtx_.result.nextSequence += 1;
        return contractCtx_.builtTxns.size() - 1;
    }
    catch (std::exception const& e)
    {
        JLOG(j.trace()) << "WasmTrace[" << contractId_ << "]: Exception in buildTxn: " << e.what();
        return std::unexpected(HostFunctionError::InternalFatal);
    }
}

std::expected<int32_t, HostFunctionError>
ContractHostFunctionsImpl::addTxnField(
    std::uint32_t const& index,
    SField const& field,
    Slice const& data)
{
    auto const j = getJournal();
    try
    {
        if (index >= contractCtx_.builtTxns.size())
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId_ << "]: "
                            << "addTxnField: index out of bounds: " << index;
            return std::unexpected(HostFunctionError::IndexOutOfBounds);
        }

        // Get the transaction STObject
        auto& obj = contractCtx_.builtTxns[index];

        // Ensure the transaction has a TransactionType field
        if (!obj.isFieldPresent(sfTransactionType))
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId_ << "]: "
                            << "addTxnField: TransactionType field not present "
                               "in transaction.";
            return std::unexpected(HostFunctionError::FieldNotFound);
        }

        // Extract the numeric tx type from the STObject and convert to TxType
        auto txTypeVal = obj.getFieldU16(sfTransactionType);
        auto txFormat = TxFormats::getInstance().findByType(safeCast<TxType>(txTypeVal));
        if (txFormat == nullptr)
        {
            JLOG(j.trace()) << "WasmTrace[" << contractId_ << "]: "
                            << "addTxnField: Invalid TransactionType: " << txTypeVal;
            return std::unexpected(HostFunctionError::FieldNotFound);
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
            JLOG(j.trace()) << "WasmTrace[" << contractId_ << "]: " << "addTxnField: Field "
                            << field.getName() << " not allowed in transaction type "
                            << txFormat->getName();
            return std::unexpected(HostFunctionError::FieldNotFound);
        }

        obj.addFieldFromSlice(field, data);
        JLOG(j.trace()) << "WasmTrace[" << contractId_ << "]: " << "addTxnField: TXN: "
                        << obj.getJson(JsonOptions::Values::None).toStyledString();
        return 0;
    }
    catch (std::exception const& e)
    {
        JLOG(j.trace()) << "WasmTrace[" << contractId_
                        << "]: Exception in addTxnField: " << e.what();
        return std::unexpected(HostFunctionError::InternalFatal);
    }
}

std::expected<int32_t, HostFunctionError>
ContractHostFunctionsImpl::emitBuiltTxn(std::uint32_t const& index)
{
    auto const j = getJournal();
    auto const& app = contractCtx_.applyCtx.registry;
    auto& parentTx = contractCtx_.applyCtx.tx;
    auto const parentBatchId = parentTx.getTransactionID();
    try
    {
        if (index >= contractCtx_.builtTxns.size())
        {
            JLOG(j.trace()) << "WasmTrace[" << parentBatchId
                            << "]: " << "emitBuiltTxn: index out of bounds: " << index;
            return std::unexpected(HostFunctionError::IndexOutOfBounds);
        }

        // Ensure tfInnerBatchTxn is always set, even if the contract
        // overwrote sfFlags via addTxnField.
        contractCtx_.builtTxns[index].setFlag(tfInnerBatchTxn);

        // `STTx`'s constructor checks the transaction against its format, and reports a
        // field the type requires but the contract never set by throwing. That is the
        // contract's own mistake, so it is caught here and answered rather than left to
        // the catch below, which would report a node fault and stop the run.
        //
        // The built transaction is copied rather than moved: a run that is told its
        // transaction is malformed can correct it and emit the same index again. `STTx`
        // takes the object by rvalue, so the copy is made here.
        std::shared_ptr<STTx const> stx;
        try
        {
            STObject built = contractCtx_.builtTxns[index];
            stx = std::make_shared<STTx const>(std::move(built));
            (void)stx->getTransactionID();
        }
        catch (std::exception const& e)
        {
            JLOG(j.trace()) << "WasmTrace[" << parentBatchId << "]: "
                            << "emitBuiltTxn: Failed to build transaction: " << e.what();
            return std::unexpected(HostFunctionError::SubmitTxnFailure);
        }

        // Inner transactions skip the signature check, so a contract may emit
        // only from its own account.
        if (stx->getAccountID(sfAccount) != contractCtx_.result.contractAccount)
            return std::unexpected(HostFunctionError::SubmitTxnFailure);

        // Use a persistent emit view that is seeded with the
        // transactor's pending state changes (balances, consumed
        // sequence, etc.) so that each emitted transaction validates
        // against the full current state.  A full apply() is used
        // (matching the Batch inner-transaction pattern) so that
        // sequence numbers, balances, owner counts, and all other
        // ledger state are properly updated between successive emits.
        auto& emitView = contractCtx_.getEmitView();

        OpenView perTxView(kBatchView, emitView);
        auto const ret = apply(app, perTxView, parentBatchId, *stx, TapBatch, j);

        JLOG(j.trace()) << "WasmTrace[" << parentBatchId << "]: " << stx->getTransactionID() << " "
                        << transToken(ret.ter);

        if (ret.applied && (isTesSuccess(ret.ter) || isTecClaim(ret.ter)))
        {
            perTxView.apply(emitView);
            contractCtx_.result.emittedTxns.push(stx);
        }
        return TERtoInt(ret.ter);
    }
    catch (std::exception const& e)
    {
        JLOG(j.trace()) << "WasmTrace[" << parentBatchId
                        << "]: Exception in emitBuiltTxn: " << e.what();
        return std::unexpected(HostFunctionError::InternalFatal);
    }
}

std::expected<int32_t, HostFunctionError>
ContractHostFunctionsImpl::emitTxn(std::shared_ptr<STTx const> const& stxPtr)
{
    auto const& app = contractCtx_.applyCtx.registry;
    auto& parentTx = contractCtx_.applyCtx.tx;
    auto const j = getJournal();

    try
    {
        // Ensure tfInnerBatchTxn is always set on emitted transactions. `STTx` is held
        // const, so the flag goes on a copy.
        //
        // The copy is made through `STTx`'s own copy constructor rather than by rebuilding
        // one from its fields. Rebuilding re-runs the format check, which rejects the
        // defaulted `sfPaths` an ordinary Payment carries harmlessly — so a contract that
        // simply did not set the flag itself could not emit anything at all.
        std::shared_ptr<STTx const> txPtr = stxPtr;
        if (!stxPtr->isFlag(tfInnerBatchTxn))
        {
            auto flagged = std::make_shared<STTx>(*stxPtr);

            // `setFlag` reaches an existing `sfFlags` and cannot add one: a transaction
            // built against a template holds the field as a placeholder until something
            // gives it a value, and that is what `setFieldU32` does.
            flagged->setFieldU32(sfFlags, flagged->getFlags() | tfInnerBatchTxn);
            txPtr = std::move(flagged);
        }

        try
        {
            (void)txPtr->getTransactionID();
        }
        catch (std::exception const&)
        {
            return std::unexpected(HostFunctionError::SubmitTxnFailure);
        }

        // See emitBuiltTxn: a contract may emit only from its own account.
        if (txPtr->getAccountID(sfAccount) != contractCtx_.result.contractAccount)
            return std::unexpected(HostFunctionError::SubmitTxnFailure);

        // Use a persistent emit view seeded with the transactor's
        // pending state, and do a full apply() for each emission
        // (see emitBuiltTxn for detailed rationale).
        auto& emitView = contractCtx_.getEmitView();
        auto const parentBatchId = parentTx.getTransactionID();
        auto const& stx = txPtr;

        OpenView perTxView(kBatchView, emitView);
        auto const ret = apply(app, perTxView, parentBatchId, *stx, TapBatch, j);

        JLOG(j.trace()) << "WasmTrace[" << parentBatchId << "]: " << stx->getTransactionID() << " "
                        << transToken(ret.ter);

        if (ret.applied && (isTesSuccess(ret.ter) || isTecClaim(ret.ter)))
        {
            perTxView.apply(emitView);
            contractCtx_.result.emittedTxns.push(stx);
        }
        return TERtoInt(ret.ter);
    }
    catch (std::exception const& e)
    {
        JLOG(j.trace()) << "WasmTrace[" << parentTx.getTransactionID()
                        << "]: Exception in emitTxn: " << e.what();
        return std::unexpected(HostFunctionError::InternalFatal);
    }
}

std::expected<int32_t, HostFunctionError>
ContractHostFunctionsImpl::emitEvent(std::string_view const& eventName, STJson const& eventData)
{
    auto const j = getJournal();

    try
    {
        // TODO: Validation
        auto& eventMap = contractCtx_.result.eventMap;
        eventMap[std::string(eventName)] = eventData;
        return 0;
    }
    catch (std::exception const& e)
    {
        JLOG(j.trace()) << "WasmTrace[" << contractId_ << "]: Exception in emitEvent: " << e.what();
        return std::unexpected(HostFunctionError::InternalFatal);
    }
}

}  // namespace xrpl
