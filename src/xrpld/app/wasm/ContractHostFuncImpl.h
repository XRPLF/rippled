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

#pragma once

#include <xrpld/app/wasm/ContractContext.h>
#include <xrpld/app/wasm/HostFunc.h>
#include <xrpld/app/wasm/HostFuncImpl.h>

namespace xrpl {
class ContractHostFunctionsImpl : public WasmHostFunctionsImpl
{
    ContractContext& contractCtx;
    uint256 const contractId = contractCtx.result.contractKeylet.key;

public:
    // Constructor for contract-specific functionality
    ContractHostFunctionsImpl(ContractContext& contractContext)
        : WasmHostFunctionsImpl(
              contractContext.applyCtx,
              contractContext.result.contractKeylet)
        , contractCtx(contractContext)
    {
    }

    // Expected<Bytes, HostFunctionError>
    // getFieldBytesFromSTData(xrpl::STData const& funcParam, std::uint32_t
    // stTypeId);

    Expected<Bytes, HostFunctionError>
    instanceParam(std::uint32_t index, std::uint32_t stTypeId) override;

    Expected<Bytes, HostFunctionError>
    functionParam(std::uint32_t index, std::uint32_t stTypeId) override;

    Expected<Bytes, HostFunctionError>
    getDataObjectField(AccountID const& account, std::string_view const& key)
        override;

    Expected<Bytes, HostFunctionError>
    getDataNestedObjectField(
        AccountID const& account,
        std::string_view const& key,
        std::string_view const& nestedKey) override;

    Expected<Bytes, HostFunctionError>
    getDataArrayElementField(
        AccountID const& account,
        size_t index,
        std::string_view const& key) override;

    Expected<Bytes, HostFunctionError>
    getDataNestedArrayElementField(
        AccountID const& account,
        std::string_view const& key,
        size_t index,
        std::string_view const& nestedKey) override;

    Expected<int32_t, HostFunctionError>
    setDataObjectField(
        AccountID const& account,
        std::string_view const& key,
        STJson::Value const& value) override;

    Expected<int32_t, HostFunctionError>
    setDataNestedObjectField(
        AccountID const& account,
        std::string_view const& nestedKey,
        std::string_view const& key,
        STJson::Value const& value) override;

    Expected<int32_t, HostFunctionError>
    setDataArrayElementField(
        AccountID const& account,
        size_t index,
        std::string_view const& key,
        STJson::Value const& value) override;

    Expected<int32_t, HostFunctionError>
    setDataNestedArrayElementField(
        AccountID const& account,
        std::string_view const& key,
        size_t index,
        std::string_view const& nestedKey,
        STJson::Value const& value) override;

    Expected<int32_t, HostFunctionError>
    buildTxn(std::uint16_t const& txType) override;

    Expected<int32_t, HostFunctionError>
    addTxnField(
        std::uint32_t const& index,
        SField const& field,
        Slice const& data) override;

    Expected<int32_t, HostFunctionError>
    emitBuiltTxn(std::uint32_t const& index) override;

    Expected<int32_t, HostFunctionError>
    emitTxn(std::shared_ptr<STTx const> const& stxPtr) override;

    Expected<int32_t, HostFunctionError>
    emitEvent(std::string_view const& eventName, STJson const& eventData)
        override;
};

}  // namespace xrpl
