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

Expected<std::uint32_t, HostFunctionError>
WasmHostFunctionsImpl::getLedgerSqn()
{
    return ctx.view().seq();
}

Expected<std::uint32_t, HostFunctionError>
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
WasmHostFunctionsImpl::normalizeCacheIndex(int32_t cacheIndex)
{
    --cacheIndex;
    if (cacheIndex < 0 || cacheIndex >= MAX_CACHE)
        return Unexpected(HostFunctionError::SLOT_OUT_RANGE);
    if (!cache[cacheIndex])
        return Unexpected(HostFunctionError::EMPTY_SLOT);
    return cacheIndex;
}

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::cacheLedgerObj(uint256 const& objId, int32_t cacheIndex)
{
    auto const& keylet = keylet::unchecked(objId);
    if (cacheIndex < 0 || cacheIndex > MAX_CACHE)
        return Unexpected(HostFunctionError::SLOT_OUT_RANGE);

    if (cacheIndex == 0)
    {
        for (cacheIndex = 0; cacheIndex < MAX_CACHE; ++cacheIndex)
            if (!cache[cacheIndex])
                break;
    }
    else
    {
        cacheIndex--;  // convert to 0-based index
    }

    if (cacheIndex >= MAX_CACHE)
        return Unexpected(HostFunctionError::SLOTS_FULL);

    cache[cacheIndex] = ctx.view().read(keylet);
    if (!cache[cacheIndex])
        return Unexpected(HostFunctionError::LEDGER_OBJ_NOT_FOUND);
    return cacheIndex + 1;  // return 1-based index
}

static Expected<Bytes, HostFunctionError>
getAnyFieldData(STBase const* obj)
{
    // auto const& fname = obj.getFName();
    if (!obj)
        return Unexpected(HostFunctionError::FIELD_NOT_FOUND);

    auto const stype = obj->getSType();
    switch (stype)
    {
        // LCOV_EXCL_START
        case STI_UNKNOWN:
        case STI_NOTPRESENT:
            return Unexpected(HostFunctionError::FIELD_NOT_FOUND);
            break;
        // LCOV_EXCL_STOP
        case STI_OBJECT:
        case STI_ARRAY:
            return Unexpected(HostFunctionError::NOT_LEAF_FIELD);
            break;
        case STI_ACCOUNT: {
            auto const* account(static_cast<STAccount const*>(obj));
            auto const& data = account->value();
            return Bytes{data.begin(), data.end()};
        }
        break;
        case STI_AMOUNT:
            // will be processed by serializer
            break;
        case STI_ISSUE: {
            auto const* issue(static_cast<STIssue const*>(obj));
            Asset const& asset(issue->value());
            // XRP and IOU will be processed by serializer
            if (asset.holds<MPTIssue>())
            {
                // MPT
                auto const& mptIssue = asset.get<MPTIssue>();
                auto const& mptID = mptIssue.getMptID();
                return Bytes{mptID.cbegin(), mptID.cend()};
            }
        }
        break;
        case STI_VL: {
            auto const* vl(static_cast<STBlob const*>(obj));
            auto const& data = vl->value();
            return Bytes{data.begin(), data.end()};
        }
        break;
        case STI_UINT256: {
            auto const* num(static_cast<STBitString<256> const*>(obj));
            auto const& data = num->value();
            return Bytes{data.begin(), data.end()};
        }
        break;
        case STI_UINT16: {
            auto const& num(static_cast<STInteger<std::uint16_t> const*>(obj));
            std::uint16_t const data = num->value();
            auto const* b = reinterpret_cast<uint8_t const*>(&data);
            auto const* e = reinterpret_cast<uint8_t const*>(&data + 1);
            return Bytes{b, e};
        }
        case STI_UINT32: {
            auto const* num(static_cast<STInteger<std::uint32_t> const*>(obj));
            std::uint32_t const data = num->value();
            auto const* b = reinterpret_cast<uint8_t const*>(&data);
            auto const* e = reinterpret_cast<uint8_t const*>(&data + 1);
            return Bytes{b, e};
        }
        break;
        // LCOV_EXCL_START
        default:
            return Unexpected(HostFunctionError::INTERNAL);
            // LCOV_EXCL_STOP
    }

    Serializer msg;
    obj->add(msg);
    auto const data = msg.getData();

    return data;
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::getTxField(SField const& fname)
{
    return getAnyFieldData(ctx.tx.peekAtPField(fname));
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::getCurrentLedgerObjField(SField const& fname)
{
    auto const sle = getCurrentLedgerObj();
    if (!sle.has_value())
        return Unexpected(sle.error());
    return getAnyFieldData(sle.value()->peekAtPField(fname));
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::getLedgerObjField(int32_t cacheIdx, SField const& fname)
{
    auto const normalizedIdx = normalizeCacheIndex(cacheIdx);
    if (!normalizedIdx.has_value())
        return Unexpected(normalizedIdx.error());
    return getAnyFieldData(cache[normalizedIdx.value()]->peekAtPField(fname));
}

static inline bool
noField(STBase const* field)
{
    return !field || (STI_NOTPRESENT == field->getSType()) ||
        (STI_UNKNOWN == field->getSType());
}

static Expected<STBase const*, HostFunctionError>
locateField(STObject const& obj, Slice const& locator)
{
    if (locator.empty() || (locator.size() & 3))  // must be multiple of 4
        return Unexpected(HostFunctionError::LOCATOR_MALFORMED);

    int32_t const* loc = reinterpret_cast<int32_t const*>(locator.data());
    int32_t const sz = locator.size() / 4;
    STBase const* field = nullptr;
    auto const& knownSFields = SField::getKnownCodeToField();

    {
        int32_t const sfieldCode = loc[0];
        auto const it = knownSFields.find(sfieldCode);
        if (it == knownSFields.end())
            return Unexpected(HostFunctionError::INVALID_FIELD);

        auto const& fname(*it->second);
        field = obj.peekAtPField(fname);
        if (noField(field))
            return Unexpected(HostFunctionError::FIELD_NOT_FOUND);
    }

    for (int i = 1; i < sz; ++i)
    {
        int32_t const sfieldCode = loc[i];

        if (STI_ARRAY == field->getSType())
        {
            auto const* arr = static_cast<STArray const*>(field);
            if (sfieldCode >= arr->size())
                return Unexpected(HostFunctionError::INDEX_OUT_OF_BOUNDS);
            field = &(arr->operator[](sfieldCode));
        }
        else if (STI_OBJECT == field->getSType())
        {
            auto const* o = static_cast<STObject const*>(field);

            auto const it = knownSFields.find(sfieldCode);
            if (it == knownSFields.end())
                return Unexpected(HostFunctionError::INVALID_FIELD);

            auto const& fname(*it->second);
            field = o->peekAtPField(fname);
        }
        else  // simple field must be the last one
        {
            return Unexpected(HostFunctionError::LOCATOR_MALFORMED);
        }

        if (noField(field))
            return Unexpected(HostFunctionError::FIELD_NOT_FOUND);
    }

    return field;
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::getTxNestedField(Slice const& locator)
{
    auto const r = locateField(ctx.tx, locator);
    if (!r)
        return Unexpected(r.error());

    return getAnyFieldData(r.value());
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::getCurrentLedgerObjNestedField(Slice const& locator)
{
    auto const sle = getCurrentLedgerObj();
    if (!sle.has_value())
        return Unexpected(sle.error());

    auto const r = locateField(*sle.value(), locator);
    if (!r)
        return Unexpected(r.error());

    return getAnyFieldData(r.value());
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::getLedgerObjNestedField(
    int32_t cacheIdx,
    Slice const& locator)
{
    auto const normalizedIdx = normalizeCacheIndex(cacheIdx);
    if (!normalizedIdx.has_value())
        return Unexpected(normalizedIdx.error());

    auto const r = locateField(*cache[normalizedIdx.value()], locator);
    if (!r)
        return Unexpected(r.error());

    return getAnyFieldData(r.value());
}

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::getTxArrayLen(SField const& fname)
{
    if (fname.fieldType != STI_ARRAY)
        return Unexpected(HostFunctionError::NO_ARRAY);

    auto const* field = ctx.tx.peekAtPField(fname);
    if (noField(field))
        return Unexpected(HostFunctionError::FIELD_NOT_FOUND);

    if (field->getSType() != STI_ARRAY)
        return Unexpected(HostFunctionError::NO_ARRAY);  // LCOV_EXCL_LINE
    int32_t const sz = static_cast<STArray const*>(field)->size();

    return sz;
}

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::getCurrentLedgerObjArrayLen(SField const& fname)
{
    if (fname.fieldType != STI_ARRAY)
        return Unexpected(HostFunctionError::NO_ARRAY);

    auto const sle = getCurrentLedgerObj();
    if (!sle.has_value())
        return Unexpected(sle.error());

    auto const* field = sle.value()->peekAtPField(fname);
    if (noField(field))
        return Unexpected(HostFunctionError::FIELD_NOT_FOUND);

    if (field->getSType() != STI_ARRAY)
        return Unexpected(HostFunctionError::NO_ARRAY);  // LCOV_EXCL_LINE
    int32_t const sz = static_cast<STArray const*>(field)->size();

    return sz;
}

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::getLedgerObjArrayLen(
    int32_t cacheIdx,
    SField const& fname)
{
    if (fname.fieldType != STI_ARRAY)
        return Unexpected(HostFunctionError::NO_ARRAY);

    auto const normalizedIdx = normalizeCacheIndex(cacheIdx);
    if (!normalizedIdx.has_value())
        return Unexpected(normalizedIdx.error());

    auto const* field = cache[normalizedIdx.value()]->peekAtPField(fname);
    if (noField(field))
        return Unexpected(HostFunctionError::FIELD_NOT_FOUND);

    if (field->getSType() != STI_ARRAY)
        return Unexpected(HostFunctionError::NO_ARRAY);  // LCOV_EXCL_LINE

    int32_t const sz = static_cast<STArray const*>(field)->size();

    return sz;
}

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::getTxNestedArrayLen(Slice const& locator)
{
    auto const r = locateField(ctx.tx, locator);
    if (!r)
        return Unexpected(r.error());

    auto const* field = r.value();
    if (field->getSType() != STI_ARRAY)
        return Unexpected(HostFunctionError::NO_ARRAY);
    int32_t const sz = static_cast<STArray const*>(field)->size();

    return sz;
}

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::getCurrentLedgerObjNestedArrayLen(Slice const& locator)
{
    auto const sle = getCurrentLedgerObj();
    if (!sle.has_value())
        return Unexpected(sle.error());
    auto const r = locateField(*sle.value(), locator);
    if (!r)
        return Unexpected(r.error());

    auto const* field = r.value();
    if (field->getSType() != STI_ARRAY)
        return Unexpected(HostFunctionError::NO_ARRAY);
    int32_t const sz = static_cast<STArray const*>(field)->size();

    return sz;
}

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::getLedgerObjNestedArrayLen(
    int32_t cacheIdx,
    Slice const& locator)
{
    auto const normalizedIdx = normalizeCacheIndex(cacheIdx);
    if (!normalizedIdx.has_value())
        return Unexpected(normalizedIdx.error());

    auto const r = locateField(*cache[normalizedIdx.value()], locator);
    if (!r)
        return Unexpected(r.error());

    auto const* field = r.value();
    if (field->getSType() != STI_ARRAY)
        return Unexpected(HostFunctionError::NO_ARRAY);
    int32_t const sz = static_cast<STArray const*>(field)->size();

    return sz;
}

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::updateData(Slice const& data)
{
    if (data.size() > maxWasmDataLength)
    {
        return Unexpected(HostFunctionError::DATA_FIELD_TOO_LARGE);
    }
    auto sle = ctx.view().peek(leKey);
    if (!sle)
        return Unexpected(HostFunctionError::LEDGER_OBJ_NOT_FOUND);
    sle->setFieldVL(sfData, data);
    ctx.view().update(sle);
    return 0;
}

Expected<Hash, HostFunctionError>
WasmHostFunctionsImpl::computeSha512HalfHash(Slice const& data)
{
    auto const hash = sha512Half(data);
    return hash;
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::accountKeylet(AccountID const& account)
{
    if (!account)
        return Unexpected(HostFunctionError::INVALID_ACCOUNT);
    auto const keylet = keylet::account(account);
    return Bytes{keylet.key.begin(), keylet.key.end()};
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::checkKeylet(AccountID const& account, std::uint32_t seq)
{
    if (!account)
        return Unexpected(HostFunctionError::INVALID_ACCOUNT);
    auto const keylet = keylet::check(account, seq);
    return Bytes{keylet.key.begin(), keylet.key.end()};
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::credentialKeylet(
    AccountID const& subject,
    AccountID const& issuer,
    Slice const& credentialType)
{
    if (!subject || !issuer)
        return Unexpected(HostFunctionError::INVALID_ACCOUNT);

    if (credentialType.empty() ||
        credentialType.size() > maxCredentialTypeLength)
        return Unexpected(HostFunctionError::INVALID_PARAMS);

    auto const keylet = keylet::credential(subject, issuer, credentialType);

    return Bytes{keylet.key.begin(), keylet.key.end()};
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::didKeylet(AccountID const& account)
{
    if (!account)
        return Unexpected(HostFunctionError::INVALID_ACCOUNT);
    auto const keylet = keylet::did(account);
    return Bytes{keylet.key.begin(), keylet.key.end()};
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::delegateKeylet(
    AccountID const& account,
    AccountID const& authorize)
{
    if (!account || !authorize)
        return Unexpected(HostFunctionError::INVALID_ACCOUNT);
    if (account == authorize)
        return Unexpected(HostFunctionError::INVALID_PARAMS);
    auto const keylet = keylet::delegate(account, authorize);
    return Bytes{keylet.key.begin(), keylet.key.end()};
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::depositPreauthKeylet(
    AccountID const& account,
    AccountID const& authorize)
{
    if (!account || !authorize)
        return Unexpected(HostFunctionError::INVALID_ACCOUNT);
    if (account == authorize)
        return Unexpected(HostFunctionError::INVALID_PARAMS);
    auto const keylet = keylet::depositPreauth(account, authorize);
    return Bytes{keylet.key.begin(), keylet.key.end()};
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::escrowKeylet(AccountID const& account, std::uint32_t seq)
{
    if (!account)
        return Unexpected(HostFunctionError::INVALID_ACCOUNT);
    auto const keylet = keylet::escrow(account, seq);
    return Bytes{keylet.key.begin(), keylet.key.end()};
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::lineKeylet(
    AccountID const& account1,
    AccountID const& account2,
    Currency const& currency)
{
    if (!account1 || !account2)
        return Unexpected(HostFunctionError::INVALID_ACCOUNT);
    if (account1 == account2)
        return Unexpected(HostFunctionError::INVALID_PARAMS);
    if (currency.isZero())
        return Unexpected(HostFunctionError::INVALID_PARAMS);

    auto const keylet = keylet::line(account1, account2, currency);
    return Bytes{keylet.key.begin(), keylet.key.end()};
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::nftOfferKeylet(
    AccountID const& account,
    std::uint32_t seq)
{
    if (!account)
        return Unexpected(HostFunctionError::INVALID_ACCOUNT);
    auto const keylet = keylet::nftoffer(account, seq);
    return Bytes{keylet.key.begin(), keylet.key.end()};
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::offerKeylet(AccountID const& account, std::uint32_t seq)
{
    if (!account)
        return Unexpected(HostFunctionError::INVALID_ACCOUNT);
    auto const keylet = keylet::offer(account, seq);
    return Bytes{keylet.key.begin(), keylet.key.end()};
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::oracleKeylet(
    AccountID const& account,
    std::uint32_t documentId)
{
    if (!account)
        return Unexpected(HostFunctionError::INVALID_ACCOUNT);
    auto const keylet = keylet::oracle(account, documentId);
    return Bytes{keylet.key.begin(), keylet.key.end()};
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::paychanKeylet(
    AccountID const& account,
    AccountID const& destination,
    std::uint32_t seq)
{
    if (!account || !destination)
        return Unexpected(HostFunctionError::INVALID_ACCOUNT);
    if (account == destination)
        return Unexpected(HostFunctionError::INVALID_PARAMS);
    auto const keylet = keylet::payChan(account, destination, seq);
    return Bytes{keylet.key.begin(), keylet.key.end()};
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::signersKeylet(AccountID const& account)
{
    if (!account)
        return Unexpected(HostFunctionError::INVALID_ACCOUNT);
    auto const keylet = keylet::signers(account);
    return Bytes{keylet.key.begin(), keylet.key.end()};
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::ticketKeylet(AccountID const& account, std::uint32_t seq)
{
    if (!account)
        return Unexpected(HostFunctionError::INVALID_ACCOUNT);
    auto const keylet = keylet::ticket(account, seq);
    return Bytes{keylet.key.begin(), keylet.key.end()};
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::getNFT(AccountID const& account, uint256 const& nftId)
{
    if (!account)
        return Unexpected(HostFunctionError::INVALID_ACCOUNT);

    if (!nftId)
        return Unexpected(HostFunctionError::INVALID_PARAMS);

    auto obj = nft::findToken(ctx.view(), account, nftId);
    if (!obj)
        return Unexpected(HostFunctionError::LEDGER_OBJ_NOT_FOUND);

    auto ouri = obj->at(~sfURI);
    if (!ouri)
        return Unexpected(HostFunctionError::FIELD_NOT_FOUND);

    Slice const s = ouri->value();
    return Bytes(s.begin(), s.end());
}

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::trace(
    std::string_view const& msg,
    Slice const& data,
    bool asHex)
{
#ifdef DEBUG_OUTPUT
    auto j = getJournal().error();
#else
    auto j = getJournal().trace();
#endif
    if (!asHex)
    {
        j << "WAMR TRACE (" << leKey.key << "): " << msg << " "
          << std::string_view(
                 reinterpret_cast<char const*>(data.data()), data.size());
    }
    else
    {
        std::string hex;
        hex.reserve(data.size() * 2);
        boost::algorithm::hex(
            data.begin(), data.end(), std::back_inserter(hex));
        j << "WAMR DEV TRACE (" << leKey.key << "): " << msg << " " << hex;
    }

    return msg.size() + data.size() * (asHex ? 2 : 1);
}

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::traceNum(std::string_view const& msg, int64_t data)
{
#ifdef DEBUG_OUTPUT
    auto j = getJournal().error();
#else
    auto j = getJournal().trace();
#endif
    j << "WAMR TRACE NUM(" << leKey.key << "): " << msg << " " << data;
    return msg.size() + sizeof(data);
}

}  // namespace ripple
