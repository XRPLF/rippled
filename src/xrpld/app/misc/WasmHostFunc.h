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

enum HostFunctionError : int32_t {
    HF_ERR_INTERNAL = -1,
    HF_ERR_FIELD_NOT_FOUND = -2,
    HF_ERR_BUFFER_TOO_SMALL = -3,
    HF_ERR_NO_ARRAY = -4,
    HF_ERR_NOT_LEAF_FIELD = -5,
    HF_ERR_LOCATOR_MALFORMED = -6,
    HF_ERR_SLOT_OUT_RANGE = -7,
    HF_ERR_SLOTS_FULL = -8,
    HF_ERR_EMPTY_SLOT = -9,
    HF_ERR_LEDGER_OBJ_NOT_FOUND = -10,
    HF_ERR_DECODING = -11,
    HF_ERR_DATA_FIELD_TOO_LARGE = -12,
    HF_ERR_POINTER_OUT_OF_BOUNDS = -13,
    HF_ERR_NO_MEM_EXPORTED = -14,
    HF_ERR_INVALID_PARAMS = -15,
    HF_ERR_INVALID_ACCOUNT = -16,
    HF_ERR_INVALID_FIELD = -17,
    HF_ERR_INDEX_OUT_OF_BOUNDS = -18,
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

    virtual Expected<int32_t, HostFunctionError>
    getLedgerSqn()
    {
        return 1;
    }

    virtual Expected<int32_t, HostFunctionError>
    getParentLedgerTime()
    {
        return 1;
    }

    virtual Expected<Hash, HostFunctionError>
    getParentLedgerHash()
    {
        return Hash{};
    }

    virtual Expected<int32_t, HostFunctionError>
    cacheLedgerObj(uint256 const& objId, int32_t cacheIdx)
    {
        return Unexpected(HF_ERR_INTERNAL);
    }

    virtual Expected<Bytes, HostFunctionError>
    getTxField(SField const& fname)
    {
        return Unexpected(HF_ERR_INTERNAL);
    }

    virtual Expected<Bytes, HostFunctionError>
    getCurrentLedgerObjField(SField const& fname)
    {
        return Unexpected(HF_ERR_INTERNAL);
    }

    virtual Expected<Bytes, HostFunctionError>
    getLedgerObjField(int32_t cacheIdx, SField const& fname)
    {
        return Unexpected(HF_ERR_INTERNAL);
    }

    virtual Expected<Bytes, HostFunctionError>
    getTxNestedField(Slice const& locator)
    {
        return Unexpected(HF_ERR_INTERNAL);
    }

    virtual Expected<Bytes, HostFunctionError>
    getCurrentLedgerObjNestedField(Slice const& locator)
    {
        return Unexpected(HF_ERR_INTERNAL);
    }

    virtual Expected<Bytes, HostFunctionError>
    getLedgerObjNestedField(int32_t cacheIdx, Slice const& locator)
    {
        return Unexpected(HF_ERR_INTERNAL);
    }

    virtual Expected<int32_t, HostFunctionError>
    getTxArrayLen(SField const& fname)
    {
        return Unexpected(HF_ERR_INTERNAL);
    }

    virtual Expected<int32_t, HostFunctionError>
    getCurrentLedgerObjArrayLen(SField const& fname)
    {
        return Unexpected(HF_ERR_INTERNAL);
    }

    virtual Expected<int32_t, HostFunctionError>
    getLedgerObjArrayLen(int32_t cacheIdx, SField const& fname)
    {
        return Unexpected(HF_ERR_INTERNAL);
    }

    virtual Expected<int32_t, HostFunctionError>
    getTxNestedArrayLen(Slice const& locator)
    {
        return Unexpected(HF_ERR_INTERNAL);
    }

    virtual Expected<int32_t, HostFunctionError>
    getCurrentLedgerObjNestedArrayLen(Slice const& locator)
    {
        return Unexpected(HF_ERR_INTERNAL);
    }

    virtual Expected<int32_t, HostFunctionError>
    getLedgerObjNestedArrayLen(int32_t cacheIdx, Slice const& locator)
    {
        return Unexpected(HF_ERR_INTERNAL);
    }

    virtual Expected<int32_t, HostFunctionError>
    updateData(Slice const& data)
    {
        return Unexpected(HF_ERR_INTERNAL);
    }

    virtual Expected<Hash, HostFunctionError>
    computeSha512HalfHash(Slice const& data)
    {
        return Hash{};
    }

    virtual Expected<Bytes, HostFunctionError>
    accountKeylet(AccountID const& account)
    {
        return Bytes{};
    }

    virtual Expected<Bytes, HostFunctionError>
    credentialKeylet(
        AccountID const& subject,
        AccountID const& issuer,
        Slice const& credentialType)
    {
        return Unexpected(HF_ERR_INTERNAL);
    }

    virtual Expected<Bytes, HostFunctionError>
    escrowKeylet(AccountID const& account, std::uint32_t seq)
    {
        return Unexpected(HF_ERR_INTERNAL);
    }

    virtual Expected<Bytes, HostFunctionError>
    oracleKeylet(AccountID const& account, std::uint32_t docId)
    {
        return Unexpected(HF_ERR_INTERNAL);
    }

    virtual Expected<Bytes, HostFunctionError>
    getNFT(AccountID const& account, uint256 const& nftId)
    {
        return Unexpected(HF_ERR_INTERNAL);
    }

    virtual Expected<int32_t, HostFunctionError>
    trace(std::string const& msg, Slice const& data, bool asHex)
    {
        return Unexpected(HF_ERR_INTERNAL);
    }

    virtual Expected<int32_t, HostFunctionError>
    traceNum(std::string const& msg, int64_t data)
    {
        return Unexpected(HF_ERR_INTERNAL);
    }

    virtual ~HostFunctions() = default;
};

}  // namespace ripple
