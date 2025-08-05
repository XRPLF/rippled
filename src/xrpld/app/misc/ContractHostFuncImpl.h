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

#include <xrpld/app/contract/ContractContext.h>
#include <xrpld/app/misc/WasmHostFunc.h>
#include <xrpld/app/misc/WasmHostFuncImpl.h>

namespace ripple {
class ContractHostFunctionsImpl : public WasmHostFunctionsImpl
{
    ContractContext& contractCtx;

public:
    // Constructor for contract-specific functionality
    ContractHostFunctionsImpl(ContractContext& contractContext)
        : WasmHostFunctionsImpl(
              contractContext.applyCtx,
              contractContext.result.contractSourceKeylet)
        , contractCtx(contractContext)
    {
    }

    Expected<Bytes, HostFunctionError>
    contractFuncParam(std::uint32_t index, std::uint32_t stTypeId) override;

    Expected<Bytes, HostFunctionError>
    otxnCallParam(std::uint32_t index, std::uint32_t stTypeId) override;

    Expected<Bytes, HostFunctionError>
    getContractData(AccountID const& account) override;

    Expected<int32_t, HostFunctionError>
    setContractData(AccountID const& account, STJson const& data) override;

    Expected<int32_t, HostFunctionError>
    emitTxn(std::shared_ptr<STTx const> const& stxPtr) override;

    Expected<int32_t, HostFunctionError>
    emitEvent(std::string_view const& eventName, STJson const& eventData)
        override;
};

}  // namespace ripple
