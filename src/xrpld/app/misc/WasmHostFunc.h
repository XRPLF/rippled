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

#include <xrpld/app/misc/WasmParamsHelper.h>

#include <xrpl/basics/Expected.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/TER.h>

namespace ripple {

enum HostFuncError : int32_t {
    HF_ERR_INTERNAL = -1,
    HF_ERR_FIELD_NOT_FOUND = -2,
    HF_ERR_BUFFER_TOO_SMALL = -3,
    HF_ERR_NO_ARRAY = -4,
    HF_ERR_NOT_LEAF_FIELD = -5,
    HF_ERR_LOCATOR_MALFORMED = -6,
    HF_ERR_SLOT_OUT_RANGE = -7,
    HF_ERR_SLOTS_FULL = -8,
    HF_ERR_INVALID_SLOT = -9,
    HF_ERR_LEDGER_OBJ_NOT_FOUND = -10,
    HF_ERR_DECODING = -11,
    HF_ERR_DATA_FIELD_TOO_LARGE = -12,
    HF_ERR_OUT_OF_BOUNDS = -13,
    HF_ERR_NO_MEM_EXPORTED = -14,
    HF_ERR_INVALID_PARAMS = -15,
    HF_ERR_INVALID_ACCOUNT = -16
};

struct HostFunctions
{
    virtual void
    setRT(void const*)
    {
    }

    virtual void const*
    getRT() const
    {
        return nullptr;
    }

    virtual beast::Journal
    getJournal()
    {
        return beast::Journal{beast::Journal::getNullSink()};
    }

    virtual int32_t
    getLedgerSqn()
    {
        return 1;
    }

    virtual int32_t
    getParentLedgerTime()
    {
        return 1;
    }

    virtual Hash
    getParentLedgerHash()
    {
        return {};
    }

    virtual int32_t
    cacheLedgerObj(Keylet const& keylet, int32_t cacheIdx)
    {
        return HF_ERR_INTERNAL;
    }

    virtual Expected<Bytes, int32_t>
    getTxField(SField const& fname)
    {
        return Unexpected(HF_ERR_INTERNAL);
    }

    virtual Expected<Bytes, int32_t>
    getCurrentLedgerObjField(SField const& fname)
    {
        return Unexpected(HF_ERR_INTERNAL);
    }

    virtual Expected<Bytes, int32_t>
    getLedgerObjField(int32_t cacheIdx, SField const& fname)
    {
        return Unexpected(HF_ERR_INTERNAL);
    }

    virtual Expected<Bytes, int32_t>
    getTxNestedField(Bytes const& locator)
    {
        return Unexpected(HF_ERR_INTERNAL);
    }

    virtual Expected<Bytes, int32_t>
    getCurrentLedgerObjNestedField(Bytes const& locator)
    {
        return Unexpected(HF_ERR_INTERNAL);
    }

    virtual Expected<Bytes, int32_t>
    getLedgerObjNestedField(int32_t cacheIdx, Bytes const& locator)
    {
        return Unexpected(HF_ERR_INTERNAL);
    }

    virtual int32_t
    getTxArrayLen(SField const& fname)
    {
        return HF_ERR_INTERNAL;
    }

    virtual int32_t
    getCurrentLedgerObjArrayLen(SField const& fname)
    {
        return HF_ERR_INTERNAL;
    }

    virtual int32_t
    getLedgerObjArrayLen(int32_t cacheIdx, SField const& fname)
    {
        return HF_ERR_INTERNAL;
    }

    virtual int32_t
    getTxNestedArrayLen(Bytes const& locator)
    {
        return HF_ERR_INTERNAL;
    }

    virtual int32_t
    getCurrentLedgerObjNestedArrayLen(Bytes const& locator)
    {
        return HF_ERR_INTERNAL;
    }

    virtual int32_t
    getLedgerObjNestedArrayLen(int32_t cacheIdx, Bytes const& locator)
    {
        return HF_ERR_INTERNAL;
    }

    virtual int32_t
    updateData(Bytes const& data)
    {
        return HF_ERR_INTERNAL;
    }

    virtual Hash
    computeSha512HalfHash(Bytes const& data)
    {
        return Hash{};
    }

    virtual Expected<Bytes, int32_t>
    accountKeylet(AccountID const& account)
    {
        return Bytes{};
    }

    virtual Expected<Bytes, int32_t>
    credentialKeylet(
        AccountID const& subject,
        AccountID const& issuer,
        Bytes const& credentialType)
    {
        return Unexpected(HF_ERR_INTERNAL);
    }

    virtual Expected<Bytes, int32_t>
    escrowKeylet(AccountID const& account, std::uint32_t seq)
    {
        return Unexpected(HF_ERR_INTERNAL);
    }

    virtual Expected<Bytes, int32_t>
    oracleKeylet(AccountID const& account, std::uint32_t docId)
    {
        return Unexpected(HF_ERR_INTERNAL);
    }

    virtual Expected<Bytes, int32_t>
    getNFT(AccountID const& account, uint256 const& nftId)
    {
        return Unexpected(HF_ERR_INTERNAL);
    }

    virtual int32_t
    trace(std::string const& msg, Bytes const& data, bool asHex)
    {
        return HF_ERR_INTERNAL;
    }

    virtual int32_t
    traceNum(std::string const& msg, int64_t data)
    {
        return HF_ERR_INTERNAL;
    }

    virtual ~HostFunctions() = default;
};

}  // namespace ripple
