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

#pragma once

#include <xrpld/app/misc/WasmHostFunc.h>
#include <xrpld/app/tx/detail/ApplyContext.h>

namespace ripple {
class WasmHostFunctionsImpl : public HostFunctions
{
    ApplyContext& ctx;
    Keylet leKey;

    static int constexpr MAX_CACHE = 256;
    std::array<std::shared_ptr<SLE const>, MAX_CACHE> cache;

    void const* rt_ = nullptr;

public:
    WasmHostFunctionsImpl(ApplyContext& ctx, Keylet const& leKey)
        : ctx(ctx), leKey(leKey)
    {
    }

    virtual void
    setRT(void const* rt) override
    {
        rt_ = rt;
    }

    virtual void const*
    getRT() const override
    {
        return rt_;
    }

    beast::Journal
    getJournal() override
    {
        return ctx.journal;
    }

    Expected<int32_t, HostFunctionError>
    getLedgerSqn() override;

    Expected<int32_t, HostFunctionError>
    getParentLedgerTime() override;

    Expected<Hash, HostFunctionError>
    getParentLedgerHash() override;

    Expected<int32_t, HostFunctionError>
    cacheLedgerObj(uint256 const& objId, int32_t cacheIdx) override;

    Expected<Bytes, HostFunctionError>
    getTxField(SField const& fname) override;

    Expected<Bytes, HostFunctionError>
    getCurrentLedgerObjField(SField const& fname) override;

    Expected<Bytes, HostFunctionError>
    getLedgerObjField(int32_t cacheIdx, SField const& fname) override;

    Expected<Bytes, HostFunctionError>
    getTxNestedField(Bytes const& locator) override;

    Expected<Bytes, HostFunctionError>
    getCurrentLedgerObjNestedField(Bytes const& locator) override;

    Expected<Bytes, HostFunctionError>
    getLedgerObjNestedField(int32_t cacheIdx, Bytes const& locator) override;

    Expected<int32_t, HostFunctionError>
    getTxArrayLen(SField const& fname) override;

    Expected<int32_t, HostFunctionError>
    getCurrentLedgerObjArrayLen(SField const& fname) override;

    Expected<int32_t, HostFunctionError>
    getLedgerObjArrayLen(int32_t cacheIdx, SField const& fname) override;

    Expected<int32_t, HostFunctionError>
    getTxNestedArrayLen(Bytes const& locator) override;

    Expected<int32_t, HostFunctionError>
    getCurrentLedgerObjNestedArrayLen(Bytes const& locator) override;

    Expected<int32_t, HostFunctionError>
    getLedgerObjNestedArrayLen(int32_t cacheIdx, Bytes const& locator) override;

    Expected<int32_t, HostFunctionError>
    updateData(Bytes const& data) override;

    Expected<Hash, HostFunctionError>
    computeSha512HalfHash(Bytes const& data) override;

    Expected<Bytes, HostFunctionError>
    accountKeylet(AccountID const& account) override;

    Expected<Bytes, HostFunctionError>
    credentialKeylet(
        AccountID const& subject,
        AccountID const& issuer,
        Bytes const& credentialType) override;

    Expected<Bytes, HostFunctionError>
    escrowKeylet(AccountID const& account, std::uint32_t seq) override;

    Expected<Bytes, HostFunctionError>
    oracleKeylet(AccountID const& account, std::uint32_t documentId) override;

    Expected<Bytes, HostFunctionError>
    getNFT(AccountID const& account, uint256 const& nftId) override;

    Expected<int32_t, HostFunctionError>
    trace(std::string const& msg, Bytes const& data, bool asHex) override;

    Expected<int32_t, HostFunctionError>
    traceNum(std::string const& msg, int64_t data) override;
};

}  // namespace ripple
