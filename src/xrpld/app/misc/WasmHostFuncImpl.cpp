//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#include <xrpld/app/misc/WasmHostFuncImpl.h>
#include <xrpld/app/tx/detail/NFTokenUtils.h>

#include <xrpl/protocol/STBitString.h>
#include <xrpl/protocol/digest.h>

#ifdef _DEBUG
// #define DEBUG_OUTPUT 1
// #define DEBUG_OUTPUT_WAMR 1
#endif

namespace ripple {

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::getLedgerSqn()
{
    return ctx.view().seq();
}

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::getParentLedgerTime()
{
    return ctx.view().parentCloseTime().time_since_epoch().count();
}

Expected<Hash, HostFunctionError>
WasmHostFunctionsImpl::getParentLedgerHash()
{
    return ctx.view().info().parentHash;
}

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::cacheLedgerObj(uint256 const& objId, int32_t cacheIdx)
{
    auto const& keylet = keylet::unchecked(objId);
    if (cacheIdx < 0 || cacheIdx > MAX_CACHE)
        return Unexpected(HF_ERR_SLOT_OUT_RANGE);

    if (!cacheIdx)
    {
        for (cacheIdx = 0; cacheIdx < MAX_CACHE; ++cacheIdx)
            if (!cache[cacheIdx])
                break;
    }
    else
        --cacheIdx;

    if (cacheIdx >= MAX_CACHE)
        return Unexpected(HF_ERR_SLOTS_FULL);

    cache[cacheIdx] = ctx.view().read(keylet);
    if (!cache[cacheIdx])
        return Unexpected(HF_ERR_LEDGER_OBJ_NOT_FOUND);
    return cacheIdx + 1;
}

static Expected<Bytes, HostFunctionError>
getAnyFieldData(STBase const* obj)
{
    // auto const& fname = obj.getFName();
    if (!obj)
        return Unexpected(HF_ERR_FIELD_NOT_FOUND);

    auto const stype = obj->getSType();
    switch (stype)
    {
        case STI_UNKNOWN:
        case STI_NOTPRESENT:
            return Unexpected(HF_ERR_FIELD_NOT_FOUND);
            break;
        case STI_OBJECT:
        case STI_ARRAY:
            return Unexpected(HF_ERR_NOT_LEAF_FIELD);
            break;
        case STI_ACCOUNT: {
            auto const& super(static_cast<STAccount const*>(obj));
            auto const& data = super->value();
            return Bytes{data.begin(), data.end()};
        }
        break;
        case STI_AMOUNT: {
            auto const& super(static_cast<STAmount const*>(obj));
            int64_t const data = super->xrp().drops();
            auto const* b = reinterpret_cast<uint8_t const*>(&data);
            auto const* e = reinterpret_cast<uint8_t const*>(&data + 1);
            return Bytes{b, e};
        }
        break;
        case STI_VL: {
            auto const& super(static_cast<STBlob const*>(obj));
            auto const& data = super->value();
            return Bytes{data.begin(), data.end()};
        }
        break;
        case STI_UINT256: {
            auto const& super(static_cast<STBitString<256> const*>(obj));
            auto const& data = super->value();
            return Bytes{data.begin(), data.end()};
        }
        break;
        case STI_UINT32: {
            auto const& super(
                static_cast<STInteger<std::uint32_t> const*>(obj));
            std::uint32_t const data = super->value();
            auto const* b = reinterpret_cast<uint8_t const*>(&data);
            auto const* e = reinterpret_cast<uint8_t const*>(&data + 1);
            return Bytes{b, e};
        }
        break;
        default:
            break;
    }

    Serializer msg;
    obj->add(msg);

    return msg.getData();
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::getTxField(SField const& fname)
{
    return getAnyFieldData(ctx.tx.peekAtPField(fname));
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::getCurrentLedgerObjField(SField const& fname)
{
    auto const sle = ctx.view().read(leKey);
    if (!sle)
        return Unexpected(HF_ERR_LEDGER_OBJ_NOT_FOUND);
    return getAnyFieldData(sle->peekAtPField(fname));
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::getLedgerObjField(int32_t cacheIdx, SField const& fname)
{
    --cacheIdx;
    if (cacheIdx < 0 || cacheIdx >= MAX_CACHE)
        return Unexpected(HF_ERR_SLOT_OUT_RANGE);
    if (!cache[cacheIdx])
        return Unexpected(HF_ERR_EMPTY_SLOT);
    return getAnyFieldData(cache[cacheIdx]->peekAtPField(fname));
}

static inline bool
noField(STBase const* field)
{
    return !field || (STI_NOTPRESENT == field->getSType()) ||
        (STI_UNKNOWN == field->getSType());
}

static Expected<STBase const*, HostFunctionError>
locateField(STObject const& obj, Bytes const& bytesLoc)
{
    Slice const& loc = makeSlice(bytesLoc);
    if (loc.empty() || (loc.size() & 3))  // must be multiple of 4
        return Unexpected(HF_ERR_LOCATOR_MALFORMED);

    int32_t const* l = reinterpret_cast<int32_t const*>(loc.data());
    int32_t const sz = loc.size() / 4;
    STBase const* field = nullptr;
    auto const& m = SField::getKnownCodeToField();

    {
        int32_t const c = l[0];
        auto const it = m.find(c);
        if (it == m.end())
            return Unexpected(HF_ERR_INVALID_FIELD);

        auto const& fname(*it->second);
        field = obj.peekAtPField(fname);
        if (noField(field))
            return Unexpected(HF_ERR_FIELD_NOT_FOUND);
    }

    for (int i = 1; i < sz; ++i)
    {
        int32_t const c = l[i];

        if (STI_ARRAY == field->getSType())
        {
            auto const* arr = static_cast<STArray const*>(field);
            if (c >= arr->size())
                return Unexpected(HF_ERR_INDEX_OUT_OF_BOUNDS);
            field = &(arr->operator[](c));
        }
        else if (STI_OBJECT == field->getSType())
        {
            auto const* o = static_cast<STObject const*>(field);

            auto const it = m.find(c);
            if (it == m.end())
                return Unexpected(HF_ERR_INVALID_FIELD);

            auto const& fname(*it->second);
            field = o->peekAtPField(fname);
        }
        else  // simple field must be the last one
        {
            return Unexpected(HF_ERR_LOCATOR_MALFORMED);
        }

        if (noField(field))
            return Unexpected(HF_ERR_FIELD_NOT_FOUND);
    }

    return field;
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::getTxNestedField(Bytes const& locator)
{
    auto const r = locateField(ctx.tx, locator);
    if (!r)
        return Unexpected(r.error());

    return getAnyFieldData(r.value());
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::getCurrentLedgerObjNestedField(Bytes const& locator)
{
    auto const sle = ctx.view().read(leKey);
    if (!sle)
        return Unexpected(HF_ERR_LEDGER_OBJ_NOT_FOUND);

    auto const r = locateField(*sle, locator);
    if (!r)
        return Unexpected(r.error());

    return getAnyFieldData(r.value());
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::getLedgerObjNestedField(
    int32_t cacheIdx,
    Bytes const& locator)
{
    --cacheIdx;
    if (cacheIdx < 0 || cacheIdx >= MAX_CACHE)
        return Unexpected(HF_ERR_SLOT_OUT_RANGE);

    if (!cache[cacheIdx])
        return Unexpected(HF_ERR_EMPTY_SLOT);

    auto const r = locateField(*cache[cacheIdx], locator);
    if (!r)
        return Unexpected(r.error());

    return getAnyFieldData(r.value());
}

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::getTxArrayLen(SField const& fname)
{
    if (fname.fieldType != STI_ARRAY)
        return Unexpected(HF_ERR_NO_ARRAY);

    auto const* field = ctx.tx.peekAtPField(fname);
    if (noField(field))
        return Unexpected(HF_ERR_FIELD_NOT_FOUND);

    if (field->getSType() != STI_ARRAY)
        return Unexpected(HF_ERR_NO_ARRAY);
    int32_t const sz = static_cast<STArray const*>(field)->size();

    return sz;
}

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::getCurrentLedgerObjArrayLen(SField const& fname)
{
    if (fname.fieldType != STI_ARRAY)
        return Unexpected(HF_ERR_NO_ARRAY);

    auto const sle = ctx.view().read(leKey);
    if (!sle)
        return Unexpected(HF_ERR_LEDGER_OBJ_NOT_FOUND);

    auto const* field = sle->peekAtPField(fname);
    if (noField(field))
        return Unexpected(HF_ERR_FIELD_NOT_FOUND);

    if (field->getSType() != STI_ARRAY)
        return Unexpected(HF_ERR_NO_ARRAY);
    int32_t const sz = static_cast<STArray const*>(field)->size();

    return sz;
}

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::getLedgerObjArrayLen(
    int32_t cacheIdx,
    SField const& fname)
{
    if (fname.fieldType != STI_ARRAY)
        return Unexpected(HF_ERR_NO_ARRAY);

    if (cacheIdx < 0 || cacheIdx >= MAX_CACHE)
        return Unexpected(HF_ERR_SLOT_OUT_RANGE);

    if (!cache[cacheIdx])
        return Unexpected(HF_ERR_EMPTY_SLOT);

    auto const* field = cache[cacheIdx]->peekAtPField(fname);
    if (noField(field))
        return Unexpected(HF_ERR_FIELD_NOT_FOUND);

    if (field->getSType() != STI_ARRAY)
        return Unexpected(HF_ERR_NO_ARRAY);
    int32_t const sz = static_cast<STArray const*>(field)->size();

    return sz;
}

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::getTxNestedArrayLen(Bytes const& locator)
{
    auto const r = locateField(ctx.tx, locator);
    if (!r)
        return r.error();

    auto const* field = r.value();
    if (field->getSType() != STI_ARRAY)
        return Unexpected(HF_ERR_NO_ARRAY);
    int32_t const sz = static_cast<STArray const*>(field)->size();

    return sz;
}

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::getCurrentLedgerObjNestedArrayLen(Bytes const& locator)
{
    auto const sle = ctx.view().read(leKey);
    if (!sle)
        return Unexpected(HF_ERR_LEDGER_OBJ_NOT_FOUND);
    auto const r = locateField(*sle, locator);
    if (!r)
        return r.error();

    auto const* field = r.value();
    if (field->getSType() != STI_ARRAY)
        return Unexpected(HF_ERR_NO_ARRAY);
    int32_t const sz = static_cast<STArray const*>(field)->size();

    return sz;
}

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::getLedgerObjNestedArrayLen(
    int32_t cacheIdx,
    Bytes const& locator)
{
    --cacheIdx;
    if (cacheIdx < 0 || cacheIdx >= MAX_CACHE)
        return Unexpected(HF_ERR_SLOT_OUT_RANGE);

    if (!cache[cacheIdx])
        return Unexpected(HF_ERR_EMPTY_SLOT);

    auto const r = locateField(*cache[cacheIdx], locator);
    if (!r)
        return r.error();

    auto const* field = r.value();
    if (field->getSType() != STI_ARRAY)
        return Unexpected(HF_ERR_NO_ARRAY);
    int32_t const sz = static_cast<STArray const*>(field)->size();

    return sz;
}

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::updateData(Bytes const& data)
{
    if (data.size() > maxWasmDataLength)
    {
        return Unexpected(HF_ERR_DATA_FIELD_TOO_LARGE);
    }
    auto sle = ctx.view().peek(leKey);
    if (!sle)
        return Unexpected(HF_ERR_LEDGER_OBJ_NOT_FOUND);
    sle->setFieldVL(sfData, data);
    ctx.view().update(sle);
    return 0;
}

Expected<Hash, HostFunctionError>
WasmHostFunctionsImpl::computeSha512HalfHash(Bytes const& data)
{
    auto const hash = sha512Half(data);
    return hash;
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::accountKeylet(AccountID const& account)
{
    if (!account)
        return Unexpected(HF_ERR_INVALID_ACCOUNT);
    auto const keylet = keylet::account(account);
    return Bytes{keylet.key.begin(), keylet.key.end()};
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::credentialKeylet(
    AccountID const& subject,
    AccountID const& issuer,
    Bytes const& credentialType)
{
    if (!subject || !issuer)
        return Unexpected(HF_ERR_INVALID_ACCOUNT);

    if (credentialType.empty() ||
        credentialType.size() > maxCredentialTypeLength)
        return Unexpected(HF_ERR_INVALID_PARAMS);

    auto const keylet =
        keylet::credential(subject, issuer, makeSlice(credentialType));

    return Bytes{keylet.key.begin(), keylet.key.end()};
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::escrowKeylet(AccountID const& account, std::uint32_t seq)
{
    if (!account)
        return Unexpected(HF_ERR_INVALID_ACCOUNT);
    auto const keylet = keylet::escrow(account, seq);
    return Bytes{keylet.key.begin(), keylet.key.end()};
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::oracleKeylet(
    AccountID const& account,
    std::uint32_t documentId)
{
    if (!account)
        return Unexpected(HF_ERR_INVALID_ACCOUNT);
    auto const keylet = keylet::oracle(account, documentId);
    return Bytes{keylet.key.begin(), keylet.key.end()};
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::getNFT(AccountID const& account, uint256 const& nftId)
{
    if (!account)
        return Unexpected(HF_ERR_INVALID_ACCOUNT);

    if (!nftId)
        return Unexpected(HF_ERR_INVALID_PARAMS);

    auto obj = nft::findToken(ctx.view(), account, nftId);
    if (!obj)
        return Unexpected(HF_ERR_LEDGER_OBJ_NOT_FOUND);

    auto ouri = obj->at(~sfURI);
    if (!ouri)
        return Unexpected(HF_ERR_FIELD_NOT_FOUND);

    Slice const s = ouri->value();
    return Bytes(s.begin(), s.end());
}

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::trace(
    std::string const& msg,
    Bytes const& data,
    bool asHex)
{
#ifdef DEBUG_OUTPUT
    auto j = ctx.journal.error();
#else
    auto j = ctx.journal.trace();
#endif
    if (!asHex)
        j << "WAMR TRACE (" << leKey.key << "): " << msg << " - "
          << std::string_view(
                 reinterpret_cast<char const*>(data.data()), data.size());
    else
    {
        auto const hex =
            boost::algorithm::hex(std::string(data.begin(), data.end()));
        j << "WAMR DEV TRACE (" << leKey.key << "): " << msg << " - " << hex;
    }

    return msg.size() + data.size() * (asHex ? 2 : 1);
}

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::traceNum(std::string const& msg, int64_t data)
{
#ifdef DEBUG_OUTPUT
    auto j = ctx.journal.error();
#else
    auto j = ctx.journal.trace();
#endif

    j << "WAMR DEV TRACE (" << leKey.key << "): " << msg << " - " << data;

    return msg.size() + sizeof(data);
}

}  // namespace ripple
