#include <xrpld/app/wasm/HostFuncImpl.h>

#include <xrpl/protocol/STBitString.h>
#include <xrpl/protocol/digest.h>

namespace xrpl {

typedef std::variant<STBase const*, uint256 const*> FieldValue;

namespace detail {

template <class T>
Bytes
getIntBytes(STBase const* obj)
{
    static_assert(std::is_integral<T>::value, "Only integral types");

    auto const& num(static_cast<STInteger<T> const*>(obj));
    T const data = adjustWasmEndianess(num->value());
    auto const* b = reinterpret_cast<uint8_t const*>(&data);
    auto const* e = reinterpret_cast<uint8_t const*>(&data + 1);
    return Bytes{b, e};
}

static Expected<Bytes, HostFunctionError>
getAnyFieldData(STBase const* obj)
{
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
        case STI_VECTOR256:
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
            return getIntBytes<std::uint16_t>(obj);
        }
        break;
        case STI_UINT32: {
            return getIntBytes<std::uint32_t>(obj);
        }
        // LCOV_EXCL_START
        case STI_UINT64: {
            return getIntBytes<std::uint64_t>(obj);
        }
        break;
        case STI_INT32: {
            return getIntBytes<std::int32_t>(obj);
        }
        break;
        case STI_INT64: {
            return getIntBytes<std::int64_t>(obj);
        }
        // LCOV_EXCL_STOP
        break;
        case STI_UINT256: {
            auto const* uint256Obj(static_cast<STUInt256 const*>(obj));
            auto const& data = uint256Obj->value();
            return Bytes{data.begin(), data.end()};
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

static Expected<Bytes, HostFunctionError>
getAnyFieldData(FieldValue const& variantObj)
{
    if (STBase const* const* obj = std::get_if<STBase const*>(&variantObj))
    {
        return getAnyFieldData(*obj);
    }
    else if (uint256 const* const* u = std::get_if<uint256 const*>(&variantObj))
    {
        return Bytes((*u)->begin(), (*u)->end());
    }

    return Unexpected(HostFunctionError::INTERNAL);  // LCOV_EXCL_LINE
}

static inline bool
noField(STBase const* field)
{
    return !field || (STI_NOTPRESENT == field->getSType()) || (STI_UNKNOWN == field->getSType());
}

static Expected<FieldValue, HostFunctionError>
locateField(STObject const& obj, Slice const& locator)
{
    if (locator.empty() || (locator.size() & 3))  // must be multiple of 4
        return Unexpected(HostFunctionError::LOCATOR_MALFORMED);

    static_assert(maxWasmParamLength % sizeof(int32_t) == 0);
    int32_t locBuf[maxWasmParamLength / sizeof(int32_t)];
    int32_t const* locPtr = &locBuf[0];
    int32_t const locSize = locator.size() / sizeof(int32_t);

    {
        uintptr_t const p = reinterpret_cast<uintptr_t>(locator.data());
        if (p & (alignof(int32_t) - 1))  // unaligned
            memcpy(&locBuf[0], locator.data(), locator.size());
        else
            locPtr = reinterpret_cast<int32_t const*>(locator.data());
    }

    STBase const* field = nullptr;
    auto const& knownSFields = SField::getKnownCodeToField();

    {
        int32_t const sfieldCode = adjustWasmEndianess(locPtr[0]);
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
        int32_t const sfieldCode = adjustWasmEndianess(locPtr[i]);

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
        else if (STI_VECTOR256 == field->getSType())
        {
            auto const* v = static_cast<STVector256 const*>(field);
            if (sfieldCode >= v->size())
                return Unexpected(HostFunctionError::INDEX_OUT_OF_BOUNDS);
            return FieldValue(&(v->operator[](sfieldCode)));
        }
        else  // simple field must be the last one
        {
            return Unexpected(HostFunctionError::LOCATOR_MALFORMED);
        }

        if (noField(field))
            return Unexpected(HostFunctionError::FIELD_NOT_FOUND);
    }

    return FieldValue(field);
}

static inline Expected<int32_t, HostFunctionError>
getArrayLen(FieldValue const& variantField)
{
    if (STBase const* const* field = std::get_if<STBase const*>(&variantField))
    {
        if ((*field)->getSType() == STI_VECTOR256)
            return static_cast<STVector256 const*>(*field)->size();
        if ((*field)->getSType() == STI_ARRAY)
            return static_cast<STArray const*>(*field)->size();
    }
    // uint256 is not an array so that variant should still return NO_ARRAY

    return Unexpected(HostFunctionError::NO_ARRAY);  // LCOV_EXCL_LINE
}

}  // namespace detail

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

// Subsection: top level getters

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::getTxField(SField const& fname)
{
    return detail::getAnyFieldData(ctx.tx.peekAtPField(fname));
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::getCurrentLedgerObjField(SField const& fname)
{
    auto const sle = getCurrentLedgerObj();
    if (!sle.has_value())
        return Unexpected(sle.error());
    return detail::getAnyFieldData(sle.value()->peekAtPField(fname));
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::getLedgerObjField(int32_t cacheIdx, SField const& fname)
{
    auto const normalizedIdx = normalizeCacheIndex(cacheIdx);
    if (!normalizedIdx.has_value())
        return Unexpected(normalizedIdx.error());
    return detail::getAnyFieldData(cache[normalizedIdx.value()]->peekAtPField(fname));
}

// Subsection: nested getters

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::getTxNestedField(Slice const& locator)
{
    auto const r = detail::locateField(ctx.tx, locator);
    if (!r)
        return Unexpected(r.error());

    return detail::getAnyFieldData(r.value());
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::getCurrentLedgerObjNestedField(Slice const& locator)
{
    auto const sle = getCurrentLedgerObj();
    if (!sle.has_value())
        return Unexpected(sle.error());

    auto const r = detail::locateField(*sle.value(), locator);
    if (!r)
        return Unexpected(r.error());

    return detail::getAnyFieldData(r.value());
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::getLedgerObjNestedField(int32_t cacheIdx, Slice const& locator)
{
    auto const normalizedIdx = normalizeCacheIndex(cacheIdx);
    if (!normalizedIdx.has_value())
        return Unexpected(normalizedIdx.error());

    auto const r = detail::locateField(*cache[normalizedIdx.value()], locator);
    if (!r)
        return Unexpected(r.error());

    return detail::getAnyFieldData(r.value());
}

// Subsection: array length getters

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::getTxArrayLen(SField const& fname)
{
    if (fname.fieldType != STI_ARRAY && fname.fieldType != STI_VECTOR256)
        return Unexpected(HostFunctionError::NO_ARRAY);

    auto const* field = ctx.tx.peekAtPField(fname);
    if (detail::noField(field))
        return Unexpected(HostFunctionError::FIELD_NOT_FOUND);

    return detail::getArrayLen(field);
}

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::getCurrentLedgerObjArrayLen(SField const& fname)
{
    if (fname.fieldType != STI_ARRAY && fname.fieldType != STI_VECTOR256)
        return Unexpected(HostFunctionError::NO_ARRAY);

    auto const sle = getCurrentLedgerObj();
    if (!sle.has_value())
        return Unexpected(sle.error());

    auto const* field = sle.value()->peekAtPField(fname);
    if (detail::noField(field))
        return Unexpected(HostFunctionError::FIELD_NOT_FOUND);

    return detail::getArrayLen(field);
}

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::getLedgerObjArrayLen(int32_t cacheIdx, SField const& fname)
{
    if (fname.fieldType != STI_ARRAY && fname.fieldType != STI_VECTOR256)
        return Unexpected(HostFunctionError::NO_ARRAY);

    auto const normalizedIdx = normalizeCacheIndex(cacheIdx);
    if (!normalizedIdx.has_value())
        return Unexpected(normalizedIdx.error());

    auto const* field = cache[normalizedIdx.value()]->peekAtPField(fname);
    if (detail::noField(field))
        return Unexpected(HostFunctionError::FIELD_NOT_FOUND);

    return detail::getArrayLen(field);
}

// Subsection: nested array length getters

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::getTxNestedArrayLen(Slice const& locator)
{
    auto const r = detail::locateField(ctx.tx, locator);
    if (!r)
        return Unexpected(r.error());

    auto const& field = r.value();
    return detail::getArrayLen(field);
}

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::getCurrentLedgerObjNestedArrayLen(Slice const& locator)
{
    auto const sle = getCurrentLedgerObj();
    if (!sle.has_value())
        return Unexpected(sle.error());
    auto const r = detail::locateField(*sle.value(), locator);
    if (!r)
        return Unexpected(r.error());

    auto const& field = r.value();
    return detail::getArrayLen(field);
}

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::getLedgerObjNestedArrayLen(int32_t cacheIdx, Slice const& locator)
{
    auto const normalizedIdx = normalizeCacheIndex(cacheIdx);
    if (!normalizedIdx.has_value())
        return Unexpected(normalizedIdx.error());

    auto const r = detail::locateField(*cache[normalizedIdx.value()], locator);
    if (!r)
        return Unexpected(r.error());

    auto const& field = r.value();
    return detail::getArrayLen(field);
}

}  // namespace xrpl
