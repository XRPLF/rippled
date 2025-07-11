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
#include <xrpl/protocol/UintTypes.h>

namespace ripple {

enum class HostFunctionError : int32_t {
    INTERNAL = -1,
    FIELD_NOT_FOUND = -2,
    BUFFER_TOO_SMALL = -3,
    NO_ARRAY = -4,
    NOT_LEAF_FIELD = -5,
    LOCATOR_MALFORMED = -6,
    SLOT_OUT_RANGE = -7,
    SLOTS_FULL = -8,
    EMPTY_SLOT = -9,
    LEDGER_OBJ_NOT_FOUND = -10,
    DECODING = -11,
    DATA_FIELD_TOO_LARGE = -12,
    POINTER_OUT_OF_BOUNDS = -13,
    NO_MEM_EXPORTED = -14,
    INVALID_PARAMS = -15,
    INVALID_ACCOUNT = -16,
    INVALID_FIELD = -17,
    INDEX_OUT_OF_BOUNDS = -18,
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

    virtual Expected<std::uint32_t, HostFunctionError>
    getLedgerSqn()
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<std::uint32_t, HostFunctionError>
    getParentLedgerTime()
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<Hash, HostFunctionError>
    getParentLedgerHash()
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<Hash, HostFunctionError>
    getLedgerAccountHash()
    {
        return Unexpected(HF_ERR_INTERNAL);
    }

    virtual Expected<Hash, HostFunctionError>
    getLedgerTransactionHash()
    {
        return Unexpected(HF_ERR_INTERNAL);
    }

    virtual Expected<int32_t, HostFunctionError>
    cacheLedgerObj(uint256 const& objId, int32_t cacheIdx)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<Bytes, HostFunctionError>
    getTxField(SField const& fname)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<Bytes, HostFunctionError>
    getCurrentLedgerObjField(SField const& fname)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<Bytes, HostFunctionError>
    getLedgerObjField(int32_t cacheIdx, SField const& fname)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<Bytes, HostFunctionError>
    getTxNestedField(Slice const& locator)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<Bytes, HostFunctionError>
    getCurrentLedgerObjNestedField(Slice const& locator)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<Bytes, HostFunctionError>
    getLedgerObjNestedField(int32_t cacheIdx, Slice const& locator)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<int32_t, HostFunctionError>
    getTxArrayLen(SField const& fname)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<int32_t, HostFunctionError>
    getCurrentLedgerObjArrayLen(SField const& fname)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<int32_t, HostFunctionError>
    getLedgerObjArrayLen(int32_t cacheIdx, SField const& fname)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<int32_t, HostFunctionError>
    getTxNestedArrayLen(Slice const& locator)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<int32_t, HostFunctionError>
    getCurrentLedgerObjNestedArrayLen(Slice const& locator)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<int32_t, HostFunctionError>
    getLedgerObjNestedArrayLen(int32_t cacheIdx, Slice const& locator)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<int32_t, HostFunctionError>
    updateData(Slice const& data)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<int32_t, HostFunctionError>
    checkSignature(
        Slice const& message,
        Slice const& signature,
        Slice const& pubkey)
    {
        return Unexpected(HF_ERR_INTERNAL);
    }

    virtual Expected<Hash, HostFunctionError>
    computeSha512HalfHash(Slice const& data)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<Bytes, HostFunctionError>
    accountKeylet(AccountID const& account)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<Bytes, HostFunctionError>
    checkKeylet(AccountID const& account, std::uint32_t seq)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<Bytes, HostFunctionError>
    credentialKeylet(
        AccountID const& subject,
        AccountID const& issuer,
        Slice const& credentialType)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<Bytes, HostFunctionError>
    didKeylet(AccountID const& account)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<Bytes, HostFunctionError>
    delegateKeylet(AccountID const& account, AccountID const& authorize)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<Bytes, HostFunctionError>
    depositPreauthKeylet(AccountID const& account, AccountID const& authorize)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<Bytes, HostFunctionError>
    escrowKeylet(AccountID const& account, std::uint32_t seq)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<Bytes, HostFunctionError>
    lineKeylet(
        AccountID const& account1,
        AccountID const& account2,
        Currency const& currency)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<Bytes, HostFunctionError>
    nftOfferKeylet(AccountID const& account, std::uint32_t seq)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<Bytes, HostFunctionError>
    offerKeylet(AccountID const& account, std::uint32_t seq)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<Bytes, HostFunctionError>
    oracleKeylet(AccountID const& account, std::uint32_t docId)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<Bytes, HostFunctionError>
    paychanKeylet(
        AccountID const& account,
        AccountID const& destination,
        std::uint32_t seq)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<Bytes, HostFunctionError>
    signersKeylet(AccountID const& account)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<Bytes, HostFunctionError>
    ticketKeylet(AccountID const& account, std::uint32_t seq)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<Bytes, HostFunctionError>
    getNFT(AccountID const& account, uint256 const& nftId)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<Bytes, HostFunctionError>
    getNFTIssuer(uint256 const& nftId)
    {
        return Unexpected(HF_ERR_INTERNAL);
    }

    virtual Expected<std::uint32_t, HostFunctionError>
    getNFTTaxon(uint256 const& nftId)
    {
        return Unexpected(HF_ERR_INTERNAL);
    }

    virtual Expected<int32_t, HostFunctionError>
    getNFTFlags(uint256 const& nftId)
    {
        return Unexpected(HF_ERR_INTERNAL);
    }

    virtual Expected<int32_t, HostFunctionError>
    getNFTTransferFee(uint256 const& nftId)
    {
        return Unexpected(HF_ERR_INTERNAL);
    }

    virtual Expected<std::uint32_t, HostFunctionError>
    getNFTSerial(uint256 const& nftId)
    {
        return Unexpected(HF_ERR_INTERNAL);
    }

    virtual Expected<int32_t, HostFunctionError>
    trace(std::string_view const& msg, Slice const& data, bool asHex)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual Expected<int32_t, HostFunctionError>
    traceNum(std::string_view const& msg, int64_t data)
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual ~HostFunctions() = default;
};

}  // namespace ripple
