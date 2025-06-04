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

int32_t
WasmHostFunctionsImpl::getLedgerSqn()
{
    return ctx.view().seq();
}

int32_t
WasmHostFunctionsImpl::getParentLedgerTime()
{
    return ctx.view().parentCloseTime().time_since_epoch().count();  // TODO try
}

Hash
WasmHostFunctionsImpl::getParentLedgerHash()
{
    return ctx.view().info().parentHash;
}

int32_t
WasmHostFunctionsImpl::cacheLedgerObj(Keylet const& keylet, int32_t cacheIdx)
{
    if (cacheIdx < 0 || cacheIdx > MAX_CACHE)
        return HF_ERR_SLOT_OUT_RANGE;

    if (!cacheIdx)
    {
        for (cacheIdx = 0; cacheIdx < MAX_CACHE; ++cacheIdx)
            if (!cache[cacheIdx])
                break;
    }
    else
        --cacheIdx;

    if (cacheIdx >= MAX_CACHE)
        return HF_ERR_SLOTS_FULL;

    cache[cacheIdx] = ctx.view().read(keylet);
    return cache[cacheIdx] ? cacheIdx + 1 : HF_ERR_LEDGER_OBJ_NOT_FOUND;
}

Bytes
getAnyFieldData(STBase const& obj)
{
    // auto const& fname = obj.getFName();
    auto const stype = obj.getSType();
    switch (stype)
    {
        case STI_UNKNOWN:
        case STI_NOTPRESENT:
            return {};
            break;
        case STI_ACCOUNT: {
            auto const& super(static_cast<STAccount const&>(obj));
            auto const& data = super.value();
            return {data.begin(), data.end()};
        }
        break;
        case STI_AMOUNT: {
            auto const& super(static_cast<STAmount const&>(obj));
            int64_t const data = super.xrp().drops();
            auto const* b = reinterpret_cast<uint8_t const*>(&data);
            auto const* e = reinterpret_cast<uint8_t const*>(&data + 1);
            return {b, e};
        }
        break;
        case STI_VL: {
            auto const& super(static_cast<STBlob const&>(obj));
            auto const& data = super.value();
            return {data.begin(), data.end()};
        }
        break;
        case STI_UINT256: {
            auto const& super(static_cast<STBitString<256> const&>(obj));
            auto const& data = super.value();
            return {data.begin(), data.end()};
        }
        break;
        case STI_UINT32: {
            auto const& super(
                static_cast<STInteger<std::uint32_t> const&>(obj));
            std::uint32_t const data = super.value();
            auto const* b = reinterpret_cast<uint8_t const*>(&data);
            auto const* e = reinterpret_cast<uint8_t const*>(&data + 1);
            return {b, e};
        }
        break;
        default:
            break;
    }

    Serializer msg;
    obj.add(msg);

    return msg.getData();
}

Expected<Bytes, int32_t>
WasmHostFunctionsImpl::getTxField(SField const& fname)
{
    auto const* field = ctx.tx.peekAtPField(fname);
    if (!field)
        return Unexpected(HF_ERR_FIELD_NOT_FOUND);
    return getAnyFieldData(*field);
}

Expected<Bytes, int32_t>
WasmHostFunctionsImpl::getCurrentLedgerObjField(SField const& fname)
{
    auto const sle = ctx.view().read(leKey);
    if (!sle)
        return Unexpected(HF_ERR_LEDGER_OBJ_NOT_FOUND);

    auto const* field = sle->peekAtPField(fname);
    if (!field)
        return Unexpected(HF_ERR_FIELD_NOT_FOUND);

    return getAnyFieldData(*field);
}

Expected<Bytes, int32_t>
WasmHostFunctionsImpl::getLedgerObjField(int32_t cacheIdx, SField const& fname)
{
    --cacheIdx;
    if (cacheIdx < 0 || cacheIdx >= MAX_CACHE)
        return Unexpected(HF_ERR_SLOT_OUT_RANGE);

    if (!cache[cacheIdx])
        return Unexpected(HF_ERR_INVALID_SLOT);

    auto const* field = cache[cacheIdx]->peekAtPField(fname);
    if (!field)
        return Unexpected(HF_ERR_FIELD_NOT_FOUND);

    return getAnyFieldData(*field);
}

static Expected<STBase const*, int32_t>
locateField(STObject const* obj, Slice const& loc)
{
    if (loc.size() % 4)
        return Unexpected(HF_ERR_LOCATOR_MALFORMED);

    int32_t const* l = reinterpret_cast<int32_t const*>(loc.data());
    int32_t const sz = loc.size() / 4;
    STBase const* field = nullptr;
    auto const& m = SField::getKnownCodeToField();

    for (int i = 0; i < sz; ++i)
    {
        int32_t const c = l[i];

        if (!field)
        {
            auto const it = m.find(c);
            if (it == m.end())
                return Unexpected(HF_ERR_FIELD_NOT_FOUND);
            auto const& fname(*it->second);

            field = obj->peekAtPField(fname);
            if (!field)
                return Unexpected(HF_ERR_FIELD_NOT_FOUND);
        }
        else if (STI_ARRAY == field->getSType())
        {
            auto const* arr = static_cast<STArray const*>(field);
            if (c >= arr->size())
                return Unexpected(HF_ERR_LOCATOR_MALFORMED);
            field = &(arr->operator[](c));
        }
        else if (STI_OBJECT == field->getSType())
        {
            auto const* o = static_cast<STObject const*>(field);

            auto const it = m.find(c);
            if (it == m.end())
                return Unexpected(HF_ERR_FIELD_NOT_FOUND);
            auto const& fname(*it->second);

            field = o->peekAtPField(fname);
            if (!field)
                return Unexpected(HF_ERR_FIELD_NOT_FOUND);
        }
    }

    if (!field || (STI_OBJECT == field->getSType()) ||
        (STI_ARRAY == field->getSType()))
        return Unexpected(HF_ERR_LOCATOR_MALFORMED);

    return field;
}

Expected<Bytes, int32_t>
WasmHostFunctionsImpl::getTxNestedField(Slice const& locator)
{
    auto const r = locateField(&ctx.tx, locator);
    if (!r.has_value())
        return Unexpected(r.error());

    auto const* field = r.value();
    return getAnyFieldData(*field);
}

Expected<Bytes, int32_t>
WasmHostFunctionsImpl::getCurrentLedgerObjNestedField(Slice const& locator)
{
    auto const sle = ctx.view().read(leKey);
    if (!sle)
        return Unexpected(HF_ERR_LEDGER_OBJ_NOT_FOUND);

    auto const r = locateField(sle.get(), locator);
    if (!r.has_value())
        return Unexpected(r.error());

    auto const* field = r.value();
    return getAnyFieldData(*field);
}

Expected<Bytes, int32_t>
WasmHostFunctionsImpl::getLedgerObjNestedField(
    int32_t cacheIdx,
    Slice const& locator)
{
    --cacheIdx;
    if (cacheIdx < 0 || cacheIdx >= MAX_CACHE)
        return Unexpected(HF_ERR_SLOT_OUT_RANGE);

    if (!cache[cacheIdx])
        return Unexpected(HF_ERR_INVALID_SLOT);

    auto const r = locateField(cache[cacheIdx].get(), locator);
    if (!r.has_value())
        return Unexpected(r.error());

    auto const* field = r.value();
    return getAnyFieldData(*field);
}

int32_t
WasmHostFunctionsImpl::getTxArrayLen(SField const& fname)
{
    if (fname.fieldType != STI_ARRAY)
        return HF_ERR_NO_ARRAY;

    auto const* field = ctx.tx.peekAtPField(fname);
    if (!field)
        return HF_ERR_FIELD_NOT_FOUND;

    if (field->getSType() != STI_ARRAY)
        return HF_ERR_NO_ARRAY;
    int32_t const sz = static_cast<STArray const*>(field)->size();

    return sz;
}

int32_t
WasmHostFunctionsImpl::getCurrentLedgerObjArrayLen(SField const& fname)
{
    if (fname.fieldType != STI_ARRAY)
        return HF_ERR_NO_ARRAY;

    auto const sle = ctx.view().read(leKey);
    if (!sle)
        return HF_ERR_LEDGER_OBJ_NOT_FOUND;

    auto const* field = sle->peekAtPField(fname);
    if (!field)
        return HF_ERR_FIELD_NOT_FOUND;

    if (field->getSType() != STI_ARRAY)
        return HF_ERR_NO_ARRAY;
    int32_t const sz = static_cast<STArray const*>(field)->size();

    return sz;
}

int32_t
WasmHostFunctionsImpl::getLedgerObjArrayLen(
    int32_t cacheIdx,
    SField const& fname)
{
    if (fname.fieldType != STI_ARRAY)
        return HF_ERR_NO_ARRAY;

    if (cacheIdx < 0 || cacheIdx >= MAX_CACHE)
        return HF_ERR_SLOT_OUT_RANGE;

    if (!cache[cacheIdx])
        return HF_ERR_INVALID_SLOT;

    auto const* field = cache[cacheIdx]->peekAtPField(fname);
    if (!field)
        return HF_ERR_FIELD_NOT_FOUND;

    if (field->getSType() != STI_ARRAY)
        return HF_ERR_NO_ARRAY;
    int32_t const sz = static_cast<STArray const*>(field)->size();

    return sz;
}

int32_t
WasmHostFunctionsImpl::getTxNestedArrayLen(Slice const& locator)
{
    auto const r = locateField(&ctx.tx, locator);
    if (!r.has_value())
        return r.error();
    auto const* field = r.value();

    if (field->getSType() != STI_ARRAY)
        return HF_ERR_NO_ARRAY;
    int32_t const sz = static_cast<STArray const*>(field)->size();

    return sz;
}

int32_t
WasmHostFunctionsImpl::getCurrentLedgerObjNestedArrayLen(Slice const& locator)
{
    auto const sle = ctx.view().read(leKey);
    if (!sle)
        return HF_ERR_LEDGER_OBJ_NOT_FOUND;
    auto const r = locateField(sle.get(), locator);
    if (!r.has_value())
        return r.error();
    auto const* field = r.value();

    if (field->getSType() != STI_ARRAY)
        return HF_ERR_NO_ARRAY;
    int32_t const sz = static_cast<STArray const*>(field)->size();

    return sz;
}

int32_t
WasmHostFunctionsImpl::getLedgerObjNestedArrayLen(
    int32_t cacheIdx,
    Slice const& locator)
{
    --cacheIdx;
    if (cacheIdx < 0 || cacheIdx >= MAX_CACHE)
        return HF_ERR_SLOT_OUT_RANGE;

    if (!cache[cacheIdx])
        return HF_ERR_INVALID_SLOT;

    auto const r = locateField(cache[cacheIdx].get(), locator);
    if (!r.has_value())
        return r.error();

    auto const* field = r.value();

    if (field->getSType() != STI_ARRAY)
        return HF_ERR_NO_ARRAY;
    int32_t const sz = static_cast<STArray const*>(field)->size();

    return sz;
}

int32_t
WasmHostFunctionsImpl::updateData(Bytes const& data)
{
    auto sle = ctx.view().peek(leKey);
    if (!sle)
        return HF_ERR_LEDGER_OBJ_NOT_FOUND;
    sle->setFieldVL(sfData, data);
    ctx.view().update(sle);
    return 0;
}

Hash
WasmHostFunctionsImpl::computeSha512HalfHash(Bytes const& data)
{
    auto const hash = sha512Half(data);
    return hash;
}

Expected<Bytes, int32_t>
WasmHostFunctionsImpl::accountKeylet(AccountID const& account)
{
    if (!account)
        return Unexpected(HF_ERR_INVALID_ACCOUNT);
    auto const keylet = keylet::account(account);
    return Bytes{keylet.key.begin(), keylet.key.end()};
}

Expected<Bytes, int32_t>
WasmHostFunctionsImpl::credentialKeylet(
    AccountID const& subject,
    AccountID const& issuer,
    Bytes const& credentialType)
{
    if (!subject || !issuer || credentialType.empty() ||
        credentialType.size() > maxCredentialTypeLength)
        return Unexpected(HF_ERR_INVALID_PARAMS);

    auto const keylet =
        keylet::credential(subject, issuer, makeSlice(credentialType));

    return Bytes{keylet.key.begin(), keylet.key.end()};
}

Expected<Bytes, int32_t>
WasmHostFunctionsImpl::escrowKeylet(AccountID const& account, std::uint32_t seq)
{
    if (!account)
        return Unexpected(HF_ERR_INVALID_ACCOUNT);
    auto const keylet = keylet::escrow(account, seq);
    return Bytes{keylet.key.begin(), keylet.key.end()};
}

Expected<Bytes, int32_t>
WasmHostFunctionsImpl::oracleKeylet(
    AccountID const& account,
    std::uint32_t documentId)
{
    if (!account || account.isZero())
        return Unexpected(HF_ERR_INVALID_ACCOUNT);
    auto const keylet = keylet::oracle(account, documentId);
    return Bytes{keylet.key.begin(), keylet.key.end()};
}

Expected<Bytes, int32_t>
WasmHostFunctionsImpl::getNFT(AccountID const& account, uint256 const& nftId)
{
    if (!account || !nftId)
        return Unexpected(HF_ERR_INVALID_PARAMS);

    auto obj = nft::findToken(ctx.view(), account, nftId);
    if (!obj)
        return Unexpected(HF_ERR_LEDGER_OBJ_NOT_FOUND);

    Slice const s = obj->at(sfURI);
    return Bytes(s.begin(), s.end());
}

int32_t
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
    j << msg;
    if (!asHex)
        j << std::string_view(
            reinterpret_cast<char const*>(data.data()), data.size());
    else
    {
        auto const hex =
            boost::algorithm::hex(std::string(data.begin(), data.end()));
        j << hex;
    }

    return msg.size() + data.size() * (asHex ? 2 : 1);
}

int32_t
WasmHostFunctionsImpl::traceNum(std::string const& msg, int64_t data)
{
#ifdef DEBUG_OUTPUT
    auto j = ctx.journal.error();
#else
    auto j = ctx.journal.trace();
#endif

    j << msg << data;

    return msg.size() + sizeof(data);
}

}  // namespace ripple
