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

    Expected<int32_t, HostFuncError>
    getLedgerSqn() override;

    int32_t
    getParentLedgerTime() override;

    Hash
    getParentLedgerHash() override;

    int32_t
    cacheLedgerObj(Keylet const& keylet, int32_t cacheIdx) override;

    Expected<Bytes, int32_t>
    getTxField(SField const& fname) override;

    Expected<Bytes, int32_t>
    getCurrentLedgerObjField(SField const& fname) override;

    Expected<Bytes, int32_t>
    getLedgerObjField(int32_t cacheIdx, SField const& fname) override;

    Expected<Bytes, int32_t>
    getTxNestedField(Bytes const& locator) override;

    Expected<Bytes, int32_t>
    getCurrentLedgerObjNestedField(Bytes const& locator) override;

    Expected<Bytes, int32_t>
    getLedgerObjNestedField(int32_t cacheIdx, Bytes const& locator) override;

    int32_t
    getTxArrayLen(SField const& fname) override;

    int32_t
    getCurrentLedgerObjArrayLen(SField const& fname) override;

    int32_t
    getLedgerObjArrayLen(int32_t cacheIdx, SField const& fname) override;

    int32_t
    getTxNestedArrayLen(Bytes const& locator) override;

    int32_t
    getCurrentLedgerObjNestedArrayLen(Bytes const& locator) override;

    int32_t
    getLedgerObjNestedArrayLen(int32_t cacheIdx, Bytes const& locator) override;

    int32_t
    updateData(Bytes const& data) override;

    Expected<Hash, HostFuncError>
    computeSha512HalfHash(Bytes const& data) override;

    Expected<Bytes, int32_t>
    accountKeylet(AccountID const& account) override;

    Expected<Bytes, int32_t>
    credentialKeylet(
        AccountID const& subject,
        AccountID const& issuer,
        Bytes const& credentialType) override;

    Expected<Bytes, int32_t>
    escrowKeylet(AccountID const& account, std::uint32_t seq) override;

    Expected<Bytes, int32_t>
    oracleKeylet(AccountID const& account, std::uint32_t documentId) override;

    Expected<Bytes, int32_t>
    getNFT(AccountID const& account, uint256 const& nftId) override;

    int32_t
    trace(std::string const& msg, Bytes const& data, bool asHex) override;

    int32_t
    traceNum(std::string const& msg, int64_t data) override;
};

}  // namespace ripple
