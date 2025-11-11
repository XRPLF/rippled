#include <xrpld/app/misc/AmendmentTable.h>
#include <xrpld/app/tx/detail/NFTokenUtils.h>
#include <xrpld/app/wasm/HostFuncImpl.h>

#include <xrpl/protocol/STBitString.h>
#include <xrpl/protocol/digest.h>

#ifdef _DEBUG
// #define DEBUG_OUTPUT 1
#endif

namespace ripple {

Expected<std::int32_t, HostFunctionError>
WasmHostFunctionsImpl::getLedgerSqn()
{
    auto seq = ctx.view().seq();
    if (seq > std::numeric_limits<int32_t>::max())
        return Unexpected(HostFunctionError::INTERNAL);  // LCOV_EXCL_LINE
    return static_cast<int32_t>(seq);
}

Expected<std::int32_t, HostFunctionError>
WasmHostFunctionsImpl::getParentLedgerTime()
{
    auto time = ctx.view().parentCloseTime().time_since_epoch().count();
    if (time > std::numeric_limits<int32_t>::max())
        return Unexpected(HostFunctionError::INTERNAL);
    return static_cast<int32_t>(time);
}

Expected<Hash, HostFunctionError>
WasmHostFunctionsImpl::getParentLedgerHash()
{
    return ctx.view().info().parentHash;
}

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::getBaseFee()
{
    auto fee = ctx.view().fees().base.drops();
    if (fee > std::numeric_limits<int32_t>::max())
        return Unexpected(HostFunctionError::INTERNAL);
    return static_cast<int32_t>(fee);
}

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::isAmendmentEnabled(uint256 const& amendmentId)
{
    return ctx.view().rules().enabled(amendmentId);
}

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::isAmendmentEnabled(std::string_view const& amendmentName)
{
    auto const& table = ctx.app.getAmendmentTable();
    auto const amendment = table.find(std::string(amendmentName));
    return ctx.view().rules().enabled(amendment);
}

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::cacheLedgerObj(uint256 const& objId, int32_t cacheIdx)
{
    auto const& keylet = keylet::unchecked(objId);
    if (cacheIdx < 0 || cacheIdx > MAX_CACHE)
        return Unexpected(HostFunctionError::SLOT_OUT_RANGE);

    if (cacheIdx == 0)
    {
        for (cacheIdx = 0; cacheIdx < MAX_CACHE; ++cacheIdx)
            if (!cache[cacheIdx])
                break;
    }
    else
    {
        cacheIdx--;  // convert to 0-based index
    }

    if (cacheIdx >= MAX_CACHE)
        return Unexpected(HostFunctionError::SLOTS_FULL);

    cache[cacheIdx] = ctx.view().read(keylet);
    if (!cache[cacheIdx])
        return Unexpected(HostFunctionError::LEDGER_OBJ_NOT_FOUND);
    return cacheIdx + 1;  // return 1-based index
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
        default:
            break;  // default to serializer
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

    int32_t const* locPtr = reinterpret_cast<int32_t const*>(locator.data());
    int32_t const locSize = locator.size() / 4;
    STBase const* field = nullptr;
    auto const& knownSFields = SField::getKnownCodeToField();

    {
        int32_t const sfieldCode = locPtr[0];
        auto const it = knownSFields.find(sfieldCode);
        if (it == knownSFields.end())
            return Unexpected(HostFunctionError::INVALID_FIELD);

        auto const& fname(*it->second);
        field = obj.peekAtPField(fname);
        if (noField(field))
            return Unexpected(HostFunctionError::FIELD_NOT_FOUND);
    }

    for (int i = 1; i < locSize; ++i)
    {
        int32_t const sfieldCode = locPtr[i];

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
    data_ = Bytes(data.begin(), data.end());
    return data_->size();
}

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::checkSignature(
    Slice const& message,
    Slice const& signature,
    Slice const& pubkey)
{
    if (!publicKeyType(pubkey))
        return Unexpected(HostFunctionError::INVALID_PARAMS);

    PublicKey const pk(pubkey);
    return verify(pk, message, signature);
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
WasmHostFunctionsImpl::ammKeylet(Asset const& issue1, Asset const& issue2)
{
    if (issue1 == issue2)
        return Unexpected(HostFunctionError::INVALID_PARAMS);

    // note: this should be removed with the MPT DEX amendment
    if (issue1.holds<MPTIssue>() || issue2.holds<MPTIssue>())
        return Unexpected(HostFunctionError::INVALID_PARAMS);

    auto const keylet = keylet::amm(issue1, issue2);
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
WasmHostFunctionsImpl::mptIssuanceKeylet(
    AccountID const& issuer,
    std::uint32_t seq)
{
    if (!issuer)
        return Unexpected(HostFunctionError::INVALID_ACCOUNT);

    auto const keylet = keylet::mptIssuance(seq, issuer);
    return Bytes{keylet.key.begin(), keylet.key.end()};
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::mptokenKeylet(
    MPTID const& mptid,
    AccountID const& holder)
{
    if (!mptid)
        return Unexpected(HostFunctionError::INVALID_PARAMS);
    if (!holder)
        return Unexpected(HostFunctionError::INVALID_ACCOUNT);

    auto const keylet = keylet::mptoken(mptid, holder);
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
WasmHostFunctionsImpl::permissionedDomainKeylet(
    AccountID const& account,
    std::uint32_t seq)
{
    if (!account)
        return Unexpected(HostFunctionError::INVALID_ACCOUNT);
    auto const keylet = keylet::permissionedDomain(account, seq);
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
WasmHostFunctionsImpl::vaultKeylet(AccountID const& account, std::uint32_t seq)
{
    if (!account)
        return Unexpected(HostFunctionError::INVALID_ACCOUNT);
    auto const keylet = keylet::vault(account, seq);
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

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::getNFTIssuer(uint256 const& nftId)
{
    auto const issuer = nft::getIssuer(nftId);
    if (!issuer)
        return Unexpected(HostFunctionError::INVALID_PARAMS);

    return Bytes{issuer.begin(), issuer.end()};
}

Expected<std::uint32_t, HostFunctionError>
WasmHostFunctionsImpl::getNFTTaxon(uint256 const& nftId)
{
    return nft::toUInt32(nft::getTaxon(nftId));
}

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::getNFTFlags(uint256 const& nftId)
{
    return nft::getFlags(nftId);
}

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::getNFTTransferFee(uint256 const& nftId)
{
    return nft::getTransferFee(nftId);
}

Expected<std::uint32_t, HostFunctionError>
WasmHostFunctionsImpl::getNFTSerial(uint256 const& nftId)
{
    return nft::getSerial(nftId);
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
        j << "HF TRACE (" << leKey.key << "): " << msg << " "
          << std::string_view(
                 reinterpret_cast<char const*>(data.data()), data.size());
    }
    else
    {
        std::string hex;
        hex.reserve(data.size() * 2);
        boost::algorithm::hex(
            data.begin(), data.end(), std::back_inserter(hex));
        j << "HF DEV TRACE (" << leKey.key << "): " << msg << " " << hex;
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
    j << "HF TRACE NUM(" << leKey.key << "): " << msg << " " << data;
    return msg.size() + sizeof(data);
}

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::traceAccount(
    std::string_view const& msg,
    AccountID const& account)
{
#ifdef DEBUG_OUTPUT
    auto j = getJournal().error();
#else
    auto j = getJournal().trace();
#endif

    auto const accountStr = toBase58(account);

    j << "HF TRACE ACCOUNT(" << leKey.key << "): " << msg << " " << accountStr;
    return msg.size() + accountStr.size();
}

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::traceFloat(
    std::string_view const& msg,
    Slice const& data)
{
#ifdef DEBUG_OUTPUT
    auto j = getJournal().error();
#else
    auto j = getJournal().trace();
#endif
    auto const s = floatToString(data);
    j << "HF TRACE FLOAT(" << leKey.key << "): " << msg << " " << s;
    return msg.size() + s.size();
}

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::traceAmount(
    std::string_view const& msg,
    STAmount const& amount)
{
#ifdef DEBUG_OUTPUT
    auto j = getJournal().error();
#else
    auto j = getJournal().trace();
#endif
    auto const amountStr = amount.getFullText();
    j << "HF TRACE AMOUNT(" << leKey.key << "): " << msg << " " << amountStr;
    return msg.size() + amountStr.size();
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::floatFromInt(int64_t x, int32_t mode)
{
    return floatFromIntImpl(x, mode);
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::floatFromUint(uint64_t x, int32_t mode)
{
    return floatFromUintImpl(x, mode);
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::floatSet(
    int64_t mantissa,
    int32_t exponent,
    int32_t mode)
{
    return floatSetImpl(mantissa, exponent, mode);
}

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::floatCompare(Slice const& x, Slice const& y)
{
    return floatCompareImpl(x, y);
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::floatAdd(Slice const& x, Slice const& y, int32_t mode)
{
    return floatAddImpl(x, y, mode);
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::floatSubtract(
    Slice const& x,
    Slice const& y,
    int32_t mode)
{
    return floatSubtractImpl(x, y, mode);
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::floatMultiply(
    Slice const& x,
    Slice const& y,
    int32_t mode)
{
    return floatMultiplyImpl(x, y, mode);
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::floatDivide(Slice const& x, Slice const& y, int32_t mode)
{
    return floatDivideImpl(x, y, mode);
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::floatRoot(Slice const& x, int32_t n, int32_t mode)
{
    return floatRootImpl(x, n, mode);
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::floatPower(Slice const& x, int32_t n, int32_t mode)
{
    return floatPowerImpl(x, n, mode);
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::floatLog(Slice const& x, int32_t mode)
{
    return floatLogImpl(x, mode);
}

class Number2 : public Number
{
protected:
    static Bytes const FLOAT_NULL;

    bool good_;

public:
    Number2(Slice const& data) : Number(), good_(false)
    {
        if (data.size() != 8)
            return;

        if (std::ranges::equal(FLOAT_NULL, data))
        {
            good_ = true;
            return;
        }

        uint64_t const v = SerialIter(data).get64();
        if (!(v & STAmount::cIssuedCurrency))
            return;

        int64_t const neg = (v & STAmount::cPositive) ? 1 : -1;
        int32_t const e = static_cast<uint8_t>((v >> (64 - 10)) & 0xFFull);
        if (e < 1 || e > 177)
            return;

        int64_t const m = neg * static_cast<int64_t>(v & ((1ull << 54) - 1));
        if (!m)
            return;

        Number x(m, e + IOUAmount::minExponent - 1);
        *static_cast<Number*>(this) = x;
        good_ = true;
    }

    Number2() : Number(), good_(true)
    {
    }

    Number2(int64_t x) : Number(x), good_(true)
    {
    }

    Number2(uint64_t x) : Number(0), good_(false)
    {
        using mtype = std::invoke_result_t<decltype(&Number::mantissa), Number>;
        if (x <= std::numeric_limits<mtype>::max())
            *this = Number(x);
        else
            *this = Number(x / 10, 1) + Number(x % 10);

        good_ = true;
    }

    Number2(int64_t mantissa, int32_t exponent)
        : Number(mantissa, exponent), good_(true)
    {
    }

    Number2(Number const& n) : Number(n), good_(true)
    {
    }

    operator bool() const
    {
        return good_;
    }

    Expected<Bytes, HostFunctionError>
    toBytes() const
    {
        uint64_t v = mantissa() >= 0 ? STAmount::cPositive : 0;
        v |= STAmount::cIssuedCurrency;

        uint64_t const absM = mantissa() >= 0 ? mantissa() : -mantissa();
        if (!absM)
        {
            using etype =
                std::invoke_result_t<decltype(&Number::exponent), Number>;
            if (exponent() != std::numeric_limits<etype>::lowest())
            {
                return Unexpected(
                    HostFunctionError::
                        FLOAT_COMPUTATION_ERROR);  // LCOV_EXCL_LINE
            }
            return FLOAT_NULL;
        }
        else if (absM > ((1ull << 54) - 1))
        {
            return Unexpected(
                HostFunctionError::FLOAT_COMPUTATION_ERROR);  // LCOV_EXCL_LINE
        }
        else if (exponent() > IOUAmount::maxExponent)
            return Unexpected(HostFunctionError::FLOAT_COMPUTATION_ERROR);
        else if (exponent() < IOUAmount::minExponent)
            return FLOAT_NULL;

        int const e = exponent() - IOUAmount::minExponent + 1;  //+97
        v |= absM;
        v |= ((uint64_t)e) << 54;

        Serializer msg;
        msg.add64(v);
        auto const data = msg.getData();

#ifdef DEBUG_OUTPUT
        std::cout << "m: " << std::setw(20) << mantissa()
                  << ", e: " << std::setw(12) << exponent() << ", hex: ";
        std::cout << std::hex << std::uppercase << std::setfill('0');
        for (auto const& c : data)
            std::cout << std::setw(2) << (unsigned)c << " ";
        std::cout << std::dec << std::setfill(' ') << std::endl;
#endif

        return data;
    }
};

Bytes const Number2::FLOAT_NULL =
    {0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

struct SetRound
{
    Number::rounding_mode oldMode_;
    bool good_;

    SetRound(int32_t mode) : oldMode_(Number::getround()), good_(false)
    {
        if (mode < Number::rounding_mode::to_nearest ||
            mode > Number::rounding_mode::upward)
            return;

        Number::setround(static_cast<Number::rounding_mode>(mode));
        good_ = true;
    }

    ~SetRound()
    {
        Number::setround(oldMode_);
    }

    operator bool() const
    {
        return good_;
    }
};

std::string
floatToString(Slice const& data)
{
    Number2 const num(data);
    if (!num)
    {
        std::string hex;
        hex.reserve(data.size() * 2);
        boost::algorithm::hex(
            data.begin(), data.end(), std::back_inserter(hex));
        return "Invalid data: " + hex;
    }

    auto const s = to_string(num);
    return s;
}

Expected<Bytes, HostFunctionError>
floatFromIntImpl(int64_t x, int32_t mode)
{
    try
    {
        SetRound rm(mode);
        if (!rm)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);

        Number2 num(x);
        return num.toBytes();
    }
    // LCOV_EXCL_START
    catch (...)
    {
    }
    return Unexpected(HostFunctionError::FLOAT_COMPUTATION_ERROR);
    // LCOV_EXCL_STOP
}

Expected<Bytes, HostFunctionError>
floatFromUintImpl(uint64_t x, int32_t mode)
{
    try
    {
        SetRound rm(mode);
        if (!rm)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);

        Number2 num(x);
        return num.toBytes();
    }
    // LCOV_EXCL_START
    catch (...)
    {
    }
    return Unexpected(HostFunctionError::FLOAT_COMPUTATION_ERROR);
    // LCOV_EXCL_STOP
}

Expected<Bytes, HostFunctionError>
floatSetImpl(int64_t mantissa, int32_t exponent, int32_t mode)
{
    try
    {
        SetRound rm(mode);
        if (!rm)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);
        Number2 num(mantissa, exponent);
        return num.toBytes();
    }
    catch (...)
    {
    }
    return Unexpected(HostFunctionError::FLOAT_COMPUTATION_ERROR);
}

Expected<int32_t, HostFunctionError>
floatCompareImpl(Slice const& x, Slice const& y)
{
    try
    {
        Number2 xx(x);
        if (!xx)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);
        Number2 yy(y);
        if (!yy)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);
        return xx < yy ? 2 : (xx == yy ? 0 : 1);
    }
    // LCOV_EXCL_START
    catch (...)
    {
    }
    return Unexpected(HostFunctionError::FLOAT_COMPUTATION_ERROR);
    // LCOV_EXCL_STOP
}

Expected<Bytes, HostFunctionError>
floatAddImpl(Slice const& x, Slice const& y, int32_t mode)
{
    try
    {
        SetRound rm(mode);
        if (!rm)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);

        Number2 xx(x);
        if (!xx)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);
        Number2 yy(y);
        if (!yy)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);
        Number2 res = xx + yy;

        return res.toBytes();
    }
    // LCOV_EXCL_START
    catch (...)
    {
    }
    return Unexpected(HostFunctionError::FLOAT_COMPUTATION_ERROR);
    // LCOV_EXCL_STOP
}

Expected<Bytes, HostFunctionError>
floatSubtractImpl(Slice const& x, Slice const& y, int32_t mode)
{
    try
    {
        SetRound rm(mode);
        if (!rm)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);
        Number2 xx(x);
        if (!xx)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);
        Number2 yy(y);
        if (!yy)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);
        Number2 res = xx - yy;

        return res.toBytes();
    }
    // LCOV_EXCL_START
    catch (...)
    {
    }
    return Unexpected(HostFunctionError::FLOAT_COMPUTATION_ERROR);
    // LCOV_EXCL_STOP
}

Expected<Bytes, HostFunctionError>
floatMultiplyImpl(Slice const& x, Slice const& y, int32_t mode)
{
    try
    {
        SetRound rm(mode);
        if (!rm)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);
        Number2 xx(x);
        if (!xx)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);
        Number2 yy(y);
        if (!yy)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);
        Number2 res = xx * yy;

        return res.toBytes();
    }
    // LCOV_EXCL_START
    catch (...)
    {
    }
    return Unexpected(HostFunctionError::FLOAT_COMPUTATION_ERROR);
    // LCOV_EXCL_STOP
}

Expected<Bytes, HostFunctionError>
floatDivideImpl(Slice const& x, Slice const& y, int32_t mode)
{
    try
    {
        SetRound rm(mode);
        if (!rm)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);
        Number2 xx(x);
        if (!xx)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);
        Number2 yy(y);
        if (!yy)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);
        Number2 res = xx / yy;

        return res.toBytes();
    }
    catch (...)
    {
    }
    return Unexpected(HostFunctionError::FLOAT_COMPUTATION_ERROR);
}

Expected<Bytes, HostFunctionError>
floatRootImpl(Slice const& x, int32_t n, int32_t mode)
{
    try
    {
        if (n < 1)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);

        SetRound rm(mode);
        if (!rm)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);

        Number2 xx(x);
        if (!xx)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);

        Number2 res(root(xx, n));

        return res.toBytes();
    }
    // LCOV_EXCL_START
    catch (...)
    {
    }
    return Unexpected(HostFunctionError::FLOAT_COMPUTATION_ERROR);
    // LCOV_EXCL_STOP
}

Expected<Bytes, HostFunctionError>
floatPowerImpl(Slice const& x, int32_t n, int32_t mode)
{
    try
    {
        if ((n < 0) || (n > IOUAmount::maxExponent))
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);

        SetRound rm(mode);
        if (!rm)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);

        Number2 xx(x);
        if (!xx)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);
        if (xx == Number() && !n)
            return Unexpected(HostFunctionError::INVALID_PARAMS);

        Number2 res(power(xx, n, 1));

        return res.toBytes();
    }
    catch (...)
    {
    }
    return Unexpected(HostFunctionError::FLOAT_COMPUTATION_ERROR);
}

Expected<Bytes, HostFunctionError>
floatLogImpl(Slice const& x, int32_t mode)
{
    try
    {
        SetRound rm(mode);
        if (!rm)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);

        Number2 xx(x);
        if (!xx)
            return Unexpected(HostFunctionError::FLOAT_INPUT_MALFORMED);

        Number2 res(lg(xx));

        return res.toBytes();
    }
    // LCOV_EXCL_START
    catch (...)
    {
    }
    return Unexpected(HostFunctionError::FLOAT_COMPUTATION_ERROR);
    // LCOV_EXCL_STOP
}

}  // namespace ripple
