#include <xrpld/app/tx/detail/NFTokenUtils.h>
#include <xrpld/app/wasm/HostFunc.h>
#include <xrpld/app/wasm/HostFuncWrapper.h>

#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/digest.h>

namespace ripple {

using SFieldCRef = std::reference_wrapper<SField const>;

static int32_t
setData(
    InstanceWrapper const* runtime,
    int32_t dst,
    int32_t dstSize,
    uint8_t const* src,
    int32_t srcSize)
{
    if (!srcSize)
        return 0;  // LCOV_EXCL_LINE

    if (dst < 0 || dstSize < 0 || !src || srcSize < 0)
        return HfErrorToInt(HostFunctionError::INVALID_PARAMS);

    if (srcSize > maxWasmDataLength)
        return HfErrorToInt(HostFunctionError::DATA_FIELD_TOO_LARGE);

    auto const memory = runtime ? runtime->getMem() : wmem();

    // LCOV_EXCL_START
    if (!memory.s)
        return HfErrorToInt(HostFunctionError::NO_MEM_EXPORTED);
    // LCOV_EXCL_STOP
    if ((int64_t)dst + dstSize > memory.s)
        return HfErrorToInt(HostFunctionError::POINTER_OUT_OF_BOUNDS);
    if (srcSize > dstSize)
        return HfErrorToInt(HostFunctionError::BUFFER_TOO_SMALL);

    memcpy(memory.p + dst, src, srcSize);

    return srcSize;
}

template <class IW>
Expected<int32_t, HostFunctionError>
getDataInt32(IW const* _runtime, wasm_val_vec_t const* params, int32_t& i)
{
    auto const result = params->data[i].of.i32;
    i++;
    return result;
}

template <class IW>
Expected<int64_t, HostFunctionError>
getDataInt64(IW const* _runtime, wasm_val_vec_t const* params, int32_t& i)
{
    auto const result = params->data[i].of.i64;
    i++;
    return result;
}

template <class IW>
Expected<uint64_t, HostFunctionError>
getDataUInt64(IW const* runtime, wasm_val_vec_t const* params, int32_t& i)
{
    auto const r = getDataSlice(runtime, params, i);
    if (!r)
        return Unexpected(r.error());
    if (r->size() != sizeof(uint64_t))
        return Unexpected(HostFunctionError::INVALID_PARAMS);

    return *reinterpret_cast<uint64_t const*>(r->data());
}

template <class IW>
Expected<SFieldCRef, HostFunctionError>
getDataSField(IW const* _runtime, wasm_val_vec_t const* params, int32_t& i)
{
    auto const& m = SField::getKnownCodeToField();
    auto const it = m.find(params->data[i].of.i32);
    if (it == m.end())
    {
        return Unexpected(HostFunctionError::INVALID_FIELD);
    }
    i++;
    return *it->second;
}

template <class IW>
Expected<Slice, HostFunctionError>
getDataSlice(
    IW const* runtime,
    wasm_val_vec_t const* params,
    int32_t& i,
    bool isUpdate = false)
{
    int64_t const ptr = params->data[i].of.i32;
    int64_t const size = params->data[i + 1].of.i32;
    if (ptr < 0 || size < 0)
        return Unexpected(HostFunctionError::INVALID_PARAMS);

    if (!size)
        return Slice();

    if (size > (isUpdate ? maxWasmDataLength : maxWasmParamLength))
        return Unexpected(HostFunctionError::DATA_FIELD_TOO_LARGE);

    auto const memory = runtime ? runtime->getMem() : wmem();
    // LCOV_EXCL_START
    if (!memory.s)
        return Unexpected(HostFunctionError::NO_MEM_EXPORTED);
    // LCOV_EXCL_STOP

    if (ptr + size > memory.s)
        return Unexpected(HostFunctionError::POINTER_OUT_OF_BOUNDS);

    Slice data(memory.p + ptr, size);
    i += 2;
    return data;
}

template <class IW>
Expected<uint256, HostFunctionError>
getDataUInt256(IW const* runtime, wasm_val_vec_t const* params, int32_t& i)
{
    auto const slice = getDataSlice(runtime, params, i);
    if (!slice)
    {
        return Unexpected(slice.error());
    }

    if (slice->size() != uint256::bytes)
    {
        return Unexpected(HostFunctionError::INVALID_PARAMS);
    }
    return uint256::fromVoid(slice->data());
}

template <class IW>
Expected<AccountID, HostFunctionError>
getDataAccountID(IW const* runtime, wasm_val_vec_t const* params, int32_t& i)
{
    auto const slice = getDataSlice(runtime, params, i);
    if (!slice)
    {
        return Unexpected(slice.error());
    }

    if (slice->size() != AccountID::bytes)
    {
        return Unexpected(HostFunctionError::INVALID_PARAMS);
    }

    return AccountID::fromVoid(slice->data());
}

template <class IW>
static Expected<Currency, HostFunctionError>
getDataCurrency(IW const* runtime, wasm_val_vec_t const* params, int32_t& i)
{
    auto const slice = getDataSlice(runtime, params, i);
    if (!slice)
    {
        return Unexpected(slice.error());
    }

    if (slice->size() != Currency::bytes)
    {
        return Unexpected(HostFunctionError::INVALID_PARAMS);
    }

    return Currency::fromVoid(slice->data());
}

template <class IW>
static Expected<Asset, HostFunctionError>
getDataAsset(IW const* runtime, wasm_val_vec_t const* params, int32_t& i)
{
    auto const slice = getDataSlice(runtime, params, i);
    if (!slice)
    {
        return Unexpected(slice.error());
    }

    if (slice->size() == MPTID::bytes)
    {
        auto const mptid = MPTID::fromVoid(slice->data());
        return Asset{mptid};
    }

    if (slice->size() == Currency::bytes)
    {
        auto const currency = Currency::fromVoid(slice->data());
        auto const issue = Issue{currency, xrpAccount()};
        if (!issue.native())
            return Unexpected(HostFunctionError::INVALID_PARAMS);
        return Asset{issue};
    }

    if (slice->size() == (AccountID::bytes + Currency::bytes))
    {
        auto const issue = Issue(
            Currency::fromVoid(slice->data()),
            AccountID::fromVoid(slice->data() + Currency::bytes));

        if (issue.native())
            return Unexpected(HostFunctionError::INVALID_PARAMS);
        return Asset{issue};
    }

    return Unexpected(HostFunctionError::INVALID_PARAMS);
}

template <class IW>
Expected<std::string_view, HostFunctionError>
getDataString(IW const* runtime, wasm_val_vec_t const* params, int32_t& i)
{
    auto const slice = getDataSlice(runtime, params, i);
    if (!slice)
        return Unexpected(slice.error());
    return std::string_view(
        reinterpret_cast<char const*>(slice->data()), slice->size());
}

std::nullptr_t
hfResult(wasm_val_vec_t* results, int32_t value)
{
    results->data[0] = WASM_I32_VAL(value);
    // results->size = 1;
    return nullptr;
}

std::nullptr_t
hfResult(wasm_val_vec_t* results, HostFunctionError value)
{
    results->data[0] = WASM_I32_VAL(HfErrorToInt(value));
    // results->size = 1;
    return nullptr;
}

template <typename T>
std::nullptr_t
returnResult(
    InstanceWrapper const* runtime,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results,
    Expected<T, HostFunctionError> const& res,
    int32_t index)
{
    if (!res)
    {
        return hfResult(results, res.error());
    }

    using t = std::decay_t<decltype(*res)>;
    if constexpr (std::is_same_v<t, Bytes>)
    {
        return hfResult(
            results,
            setData(
                runtime,
                params->data[index].of.i32,
                params->data[index + 1].of.i32,
                res->data(),
                res->size()));
    }
    else if constexpr (std::is_same_v<t, Hash>)
    {
        return hfResult(
            results,
            setData(
                runtime,
                params->data[index].of.i32,
                params->data[index + 1].of.i32,
                res->data(),
                res->size()));
    }
    else if constexpr (std::is_same_v<t, int32_t>)
    {
        return hfResult(results, res.value());
    }
    else if constexpr (std::is_same_v<t, std::uint32_t>)
    {
        auto const resultValue = res.value();
        return hfResult(
            results,
            setData(
                runtime,
                params->data[index].of.i32,
                params->data[index + 1].of.i32,
                reinterpret_cast<uint8_t const*>(&resultValue),
                static_cast<int32_t>(sizeof(resultValue))));
    }
    else
    {
        static_assert(
            [] { return false; }(), "Unhandled return type in returnResult");
    }
}

wasm_trap_t*
getLedgerSqn_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    return returnResult(runtime, params, results, hf->getLedgerSqn(), index);
}

wasm_trap_t*
getParentLedgerTime_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    return returnResult(
        runtime, params, results, hf->getParentLedgerTime(), index);
}

wasm_trap_t*
getParentLedgerHash_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    return returnResult(
        runtime, params, results, hf->getParentLedgerHash(), index);
}

wasm_trap_t*
getBaseFee_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    return returnResult(runtime, params, results, hf->getBaseFee(), index);
}

wasm_trap_t*
isAmendmentEnabled_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const slice = getDataSlice(runtime, params, index);
    if (!slice)
    {
        return hfResult(results, slice.error());
    }

    if (slice->size() == uint256::bytes)
    {
        if (auto ret = hf->isAmendmentEnabled(uint256::fromVoid(slice->data()));
            *ret == 1)
        {
            return returnResult(runtime, params, results, ret, index);
        }
    }

    if (slice->size() > 64)
    {
        return hfResult(results, HostFunctionError::DATA_FIELD_TOO_LARGE);
    }

    auto const str = std::string_view(
        reinterpret_cast<char const*>(slice->data()), slice->size());
    return returnResult(
        runtime, params, results, hf->isAmendmentEnabled(str), index);
}

wasm_trap_t*
cacheLedgerObj_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const id = getDataUInt256(runtime, params, index);
    if (!id)
    {
        return hfResult(results, id.error());
    }

    auto const cache = getDataInt32(runtime, params, index);
    if (!cache)
    {
        return hfResult(results, cache.error());  // LCOV_EXCL_LINE
    }

    return returnResult(
        runtime, params, results, hf->cacheLedgerObj(*id, *cache), index);
}

wasm_trap_t*
getTxField_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const fname = getDataSField(runtime, params, index);
    if (!fname)
    {
        return hfResult(results, fname.error());
    }
    return returnResult(
        runtime, params, results, hf->getTxField(*fname), index);
}

wasm_trap_t*
getCurrentLedgerObjField_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const fname = getDataSField(runtime, params, index);
    if (!fname)
    {
        return hfResult(results, fname.error());
    }

    return returnResult(
        runtime, params, results, hf->getCurrentLedgerObjField(*fname), index);
}

wasm_trap_t*
getLedgerObjField_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const cache = getDataInt32(runtime, params, index);
    if (!cache)
    {
        return hfResult(results, cache.error());  // LCOV_EXCL_LINE
    }

    auto const fname = getDataSField(runtime, params, index);
    if (!fname)
    {
        return hfResult(results, fname.error());
    }

    return returnResult(
        runtime, params, results, hf->getLedgerObjField(*cache, *fname), index);
}

wasm_trap_t*
getTxNestedField_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const bytes = getDataSlice(runtime, params, index);
    if (!bytes)
    {
        return hfResult(results, bytes.error());
    }

    return returnResult(
        runtime, params, results, hf->getTxNestedField(*bytes), index);
}

wasm_trap_t*
getCurrentLedgerObjNestedField_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const bytes = getDataSlice(runtime, params, index);
    if (!bytes)
    {
        return hfResult(results, bytes.error());
    }
    return returnResult(
        runtime,
        params,
        results,
        hf->getCurrentLedgerObjNestedField(*bytes),
        index);
}

wasm_trap_t*
getLedgerObjNestedField_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const cache = getDataInt32(runtime, params, index);
    if (!cache)
    {
        return hfResult(results, cache.error());  // LCOV_EXCL_LINE
    }

    auto const bytes = getDataSlice(runtime, params, index);
    if (!bytes)
    {
        return hfResult(results, bytes.error());
    }

    return returnResult(
        runtime,
        params,
        results,
        hf->getLedgerObjNestedField(*cache, *bytes),
        index);
}

wasm_trap_t*
getTxArrayLen_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const fname = getDataSField(runtime, params, index);
    if (!fname)
    {
        return hfResult(results, fname.error());
    }

    return returnResult(
        runtime, params, results, hf->getTxArrayLen(*fname), index);
}

wasm_trap_t*
getCurrentLedgerObjArrayLen_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const fname = getDataSField(runtime, params, index);
    if (!fname)
    {
        return hfResult(results, fname.error());
    }

    return returnResult(
        runtime,
        params,
        results,
        hf->getCurrentLedgerObjArrayLen(*fname),
        index);
}

wasm_trap_t*
getLedgerObjArrayLen_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const cache = getDataInt32(runtime, params, index);
    if (!cache)
    {
        return hfResult(results, cache.error());  // LCOV_EXCL_LINE
    }

    auto const fname = getDataSField(runtime, params, index);
    if (!fname)
    {
        return hfResult(results, fname.error());
    }

    return returnResult(
        runtime,
        params,
        results,
        hf->getLedgerObjArrayLen(*cache, *fname),
        index);
}

wasm_trap_t*
getTxNestedArrayLen_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const bytes = getDataSlice(runtime, params, index);
    if (!bytes)
    {
        return hfResult(results, bytes.error());
    }

    return returnResult(
        runtime, params, results, hf->getTxNestedArrayLen(*bytes), index);
}

wasm_trap_t*
getCurrentLedgerObjNestedArrayLen_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const bytes = getDataSlice(runtime, params, index);
    if (!bytes)
    {
        return hfResult(results, bytes.error());
    }

    return returnResult(
        runtime,
        params,
        results,
        hf->getCurrentLedgerObjNestedArrayLen(*bytes),
        index);
}
wasm_trap_t*
getLedgerObjNestedArrayLen_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const cache = getDataInt32(runtime, params, index);
    if (!cache)
    {
        return hfResult(results, cache.error());  // LCOV_EXCL_LINE
    }

    auto const bytes = getDataSlice(runtime, params, index);
    if (!bytes)
    {
        return hfResult(results, bytes.error());
    }
    return returnResult(
        runtime,
        params,
        results,
        hf->getLedgerObjNestedArrayLen(*cache, *bytes),
        index);
}

wasm_trap_t*
updateData_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const bytes = getDataSlice(runtime, params, index, true);
    if (!bytes)
    {
        return hfResult(results, bytes.error());
    }

    return returnResult(
        runtime, params, results, hf->updateData(*bytes), index);
}

wasm_trap_t*
checkSignature_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const message = getDataSlice(runtime, params, index);
    if (!message)
    {
        return hfResult(results, message.error());
    }

    auto const signature = getDataSlice(runtime, params, index);
    if (!signature)
    {
        return hfResult(results, signature.error());
    }

    auto const pubkey = getDataSlice(runtime, params, index);
    if (!pubkey)
    {
        return hfResult(results, pubkey.error());
    }

    return returnResult(
        runtime,
        params,
        results,
        hf->checkSignature(*message, *signature, *pubkey),
        index);
}

wasm_trap_t*
computeSha512HalfHash_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const bytes = getDataSlice(runtime, params, index);
    if (!bytes)
    {
        return hfResult(results, bytes.error());
    }
    return returnResult(
        runtime, params, results, hf->computeSha512HalfHash(*bytes), index);
}

wasm_trap_t*
accountKeylet_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const acc = getDataAccountID(runtime, params, index);
    if (!acc)
    {
        return hfResult(results, acc.error());
    }

    return returnResult(
        runtime, params, results, hf->accountKeylet(*acc), index);
}

wasm_trap_t*
ammKeylet_wrap(void* env, wasm_val_vec_t const* params, wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const issue1 = getDataAsset(runtime, params, index);
    if (!issue1)
    {
        return hfResult(results, issue1.error());
    }

    auto const issue2 = getDataAsset(runtime, params, index);
    if (!issue2)
    {
        return hfResult(results, issue2.error());
    }

    return returnResult(
        runtime,
        params,
        results,
        hf->ammKeylet(issue1.value(), issue2.value()),
        index);
}

wasm_trap_t*
checkKeylet_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const acc = getDataAccountID(runtime, params, index);
    if (!acc)
    {
        return hfResult(results, acc.error());
    }

    auto const seq = getDataInt32(runtime, params, index);
    if (!seq)
    {
        return hfResult(results, seq.error());  // LCOV_EXCL_LINE
    }

    return returnResult(
        runtime, params, results, hf->checkKeylet(acc.value(), *seq), index);
}

wasm_trap_t*
credentialKeylet_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const subj = getDataAccountID(runtime, params, index);
    if (!subj)
    {
        return hfResult(results, subj.error());
    }

    auto const iss = getDataAccountID(runtime, params, index);
    if (!iss)
    {
        return hfResult(results, iss.error());
    }

    auto const credType = getDataSlice(runtime, params, index);
    if (!credType)
    {
        return hfResult(results, credType.error());
    }

    return returnResult(
        runtime,
        params,
        results,
        hf->credentialKeylet(*subj, *iss, *credType),
        index);
}

wasm_trap_t*
delegateKeylet_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const acc = getDataAccountID(runtime, params, index);
    if (!acc)
    {
        return hfResult(results, acc.error());
    }

    auto const authorize = getDataAccountID(runtime, params, index);
    if (!authorize)
    {
        return hfResult(results, authorize.error());
    }

    return returnResult(
        runtime,
        params,
        results,
        hf->delegateKeylet(acc.value(), authorize.value()),
        index);
}

wasm_trap_t*
depositPreauthKeylet_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const acc = getDataAccountID(runtime, params, index);
    if (!acc)
    {
        return hfResult(results, acc.error());
    }

    auto const authorize = getDataAccountID(runtime, params, index);
    if (!authorize)
    {
        return hfResult(results, authorize.error());
    }

    return returnResult(
        runtime,
        params,
        results,
        hf->depositPreauthKeylet(acc.value(), authorize.value()),
        index);
}

wasm_trap_t*
didKeylet_wrap(void* env, wasm_val_vec_t const* params, wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const acc = getDataAccountID(runtime, params, index);
    if (!acc)
    {
        return hfResult(results, acc.error());
    }

    return returnResult(
        runtime, params, results, hf->didKeylet(acc.value()), index);
}

wasm_trap_t*
escrowKeylet_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const acc = getDataAccountID(runtime, params, index);
    if (!acc)
    {
        return hfResult(results, acc.error());
    }

    auto const seq = getDataInt32(runtime, params, index);
    if (!seq)
    {
        return hfResult(results, seq.error());  // LCOV_EXCL_LINE
    }

    return returnResult(
        runtime, params, results, hf->escrowKeylet(*acc, *seq), index);
}

wasm_trap_t*
lineKeylet_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const acc1 = getDataAccountID(runtime, params, index);
    if (!acc1)
    {
        return hfResult(results, acc1.error());
    }

    auto const acc2 = getDataAccountID(runtime, params, index);
    if (!acc2)
    {
        return hfResult(results, acc2.error());
    }

    auto const currency = getDataCurrency(runtime, params, index);
    if (!currency)
    {
        return hfResult(results, currency.error());
    }

    return returnResult(
        runtime,
        params,
        results,
        hf->lineKeylet(acc1.value(), acc2.value(), currency.value()),
        index);
}

wasm_trap_t*
mptIssuanceKeylet_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const acc = getDataAccountID(runtime, params, index);
    if (!acc)
    {
        return hfResult(results, acc.error());
    }

    auto const seq = getDataInt32(runtime, params, index);
    if (!seq)
    {
        return hfResult(results, seq.error());  // LCOV_EXCL_LINE
    }

    return returnResult(
        runtime,
        params,
        results,
        hf->mptIssuanceKeylet(acc.value(), seq.value()),
        index);
}

wasm_trap_t*
mptokenKeylet_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const slice = getDataSlice(runtime, params, index);
    if (!slice)
    {
        return hfResult(results, slice.error());
    }

    if (slice->size() != MPTID::bytes)
    {
        return hfResult(results, HostFunctionError::INVALID_PARAMS);
    }
    auto const mptid = MPTID::fromVoid(slice->data());

    auto const holder = getDataAccountID(runtime, params, index);
    if (!holder)
    {
        return hfResult(results, holder.error());
    }

    return returnResult(
        runtime,
        params,
        results,
        hf->mptokenKeylet(mptid, holder.value()),
        index);
}

wasm_trap_t*
nftOfferKeylet_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const acc = getDataAccountID(runtime, params, index);
    if (!acc)
    {
        return hfResult(results, acc.error());
    }

    auto const seq = getDataInt32(runtime, params, index);
    if (!seq)
    {
        return hfResult(results, seq.error());  // LCOV_EXCL_LINE
    }

    return returnResult(
        runtime,
        params,
        results,
        hf->nftOfferKeylet(acc.value(), seq.value()),
        index);
}

wasm_trap_t*
offerKeylet_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const acc = getDataAccountID(runtime, params, index);
    if (!acc)
    {
        return hfResult(results, acc.error());
    }

    auto const seq = getDataInt32(runtime, params, index);
    if (!seq)
    {
        return hfResult(results, seq.error());  // LCOV_EXCL_LINE
    }

    return returnResult(
        runtime,
        params,
        results,
        hf->offerKeylet(acc.value(), seq.value()),
        index);
}

wasm_trap_t*
oracleKeylet_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const acc = getDataAccountID(runtime, params, index);
    if (!acc)
    {
        return hfResult(results, acc.error());
    }

    auto const documentId = getDataInt32(runtime, params, index);
    if (!documentId)
    {
        return hfResult(results, documentId.error());  // LCOV_EXCL_LINE
    }
    return returnResult(
        runtime, params, results, hf->oracleKeylet(*acc, *documentId), index);
}

wasm_trap_t*
paychanKeylet_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const acc = getDataAccountID(runtime, params, index);
    if (!acc)
    {
        return hfResult(results, acc.error());
    }

    auto const dest = getDataAccountID(runtime, params, index);
    if (!dest)
    {
        return hfResult(results, dest.error());
    }

    auto const seq = getDataInt32(runtime, params, index);
    if (!seq)
    {
        return hfResult(results, seq.error());  // LCOV_EXCL_LINE
    }

    return returnResult(
        runtime,
        params,
        results,
        hf->paychanKeylet(acc.value(), dest.value(), seq.value()),
        index);
}

wasm_trap_t*
permissionedDomainKeylet_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const acc = getDataAccountID(runtime, params, index);
    if (!acc)
    {
        return hfResult(results, acc.error());
    }

    auto const seq = getDataInt32(runtime, params, index);
    if (!seq)
    {
        return hfResult(results, seq.error());  // LCOV_EXCL_LINE
    }

    return returnResult(
        runtime,
        params,
        results,
        hf->permissionedDomainKeylet(acc.value(), seq.value()),
        index);
}

wasm_trap_t*
signersKeylet_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const acc = getDataAccountID(runtime, params, index);
    if (!acc)
    {
        return hfResult(results, acc.error());
    }

    return returnResult(
        runtime, params, results, hf->signersKeylet(acc.value()), index);
}

wasm_trap_t*
ticketKeylet_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const acc = getDataAccountID(runtime, params, index);
    if (!acc)
    {
        return hfResult(results, acc.error());
    }

    auto const seq = getDataInt32(runtime, params, index);
    if (!seq)
    {
        return hfResult(results, seq.error());  // LCOV_EXCL_LINE
    }

    return returnResult(
        runtime,
        params,
        results,
        hf->ticketKeylet(acc.value(), seq.value()),
        index);
}

wasm_trap_t*
vaultKeylet_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const acc = getDataAccountID(runtime, params, index);
    if (!acc)
    {
        return hfResult(results, acc.error());
    }

    auto const seq = getDataInt32(runtime, params, index);
    if (!seq)
    {
        return hfResult(results, seq.error());  // LCOV_EXCL_LINE
    }

    return returnResult(
        runtime,
        params,
        results,
        hf->vaultKeylet(acc.value(), seq.value()),
        index);
}

wasm_trap_t*
getNFT_wrap(void* env, wasm_val_vec_t const* params, wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const acc = getDataAccountID(runtime, params, index);
    if (!acc)
    {
        return hfResult(results, acc.error());
    }

    auto const nftId = getDataUInt256(runtime, params, index);
    if (!nftId)
    {
        return hfResult(results, nftId.error());
    }

    return returnResult(
        runtime, params, results, hf->getNFT(*acc, *nftId), index);
}

wasm_trap_t*
getNFTIssuer_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const nftId = getDataUInt256(runtime, params, index);
    if (!nftId)
    {
        return hfResult(results, nftId.error());
    }

    return returnResult(
        runtime, params, results, hf->getNFTIssuer(*nftId), index);
}

wasm_trap_t*
getNFTTaxon_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const nftId = getDataUInt256(runtime, params, index);
    if (!nftId)
    {
        return hfResult(results, nftId.error());
    }

    return returnResult(
        runtime, params, results, hf->getNFTTaxon(*nftId), index);
}

wasm_trap_t*
getNFTFlags_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const nftId = getDataUInt256(runtime, params, index);
    if (!nftId)
    {
        return hfResult(results, nftId.error());
    }

    return returnResult(
        runtime, params, results, hf->getNFTFlags(*nftId), index);
}

wasm_trap_t*
getNFTTransferFee_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const nftId = getDataUInt256(runtime, params, index);
    if (!nftId)
    {
        return hfResult(results, nftId.error());
    }

    return returnResult(
        runtime, params, results, hf->getNFTTransferFee(*nftId), index);
}

wasm_trap_t*
getNFTSerial_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    auto const nftId = getDataUInt256(runtime, params, index);
    if (!nftId)
    {
        return hfResult(results, nftId.error());
    }

    return returnResult(
        runtime, params, results, hf->getNFTSerial(*nftId), index);
}

wasm_trap_t*
trace_wrap(void* env, wasm_val_vec_t const* params, wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;

    if (params->data[1].of.i32 + params->data[3].of.i32 > maxWasmParamLength)
    {
        return hfResult(results, HostFunctionError::DATA_FIELD_TOO_LARGE);
    }

    auto const msg = getDataString(runtime, params, index);
    if (!msg)
    {
        return hfResult(results, msg.error());
    }

    auto const data = getDataSlice(runtime, params, index);
    if (!data)
    {
        return hfResult(results, data.error());
    }

    auto const asHex = getDataInt32(runtime, params, index);
    if (!asHex)
    {
        return hfResult(results, asHex.error());  // LCOV_EXCL_LINE
    }

    return returnResult(
        runtime, params, results, hf->trace(*msg, *data, *asHex), index);
}

wasm_trap_t*
traceNum_wrap(void* env, wasm_val_vec_t const* params, wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());
    int index = 0;
    if (params->data[1].of.i32 > maxWasmParamLength)
    {
        return hfResult(results, HostFunctionError::DATA_FIELD_TOO_LARGE);
    }

    auto const msg = getDataString(runtime, params, index);
    if (!msg)
    {
        return hfResult(results, msg.error());
    }

    auto const number = getDataInt64(runtime, params, index);
    if (!number)
    {
        return hfResult(results, number.error());  // LCOV_EXCL_LINE
    }

    return returnResult(
        runtime, params, results, hf->traceNum(*msg, *number), index);
}

wasm_trap_t*
traceAccount_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());

    if (params->data[1].of.i32 > maxWasmParamLength)
        return hfResult(results, HostFunctionError::DATA_FIELD_TOO_LARGE);

    int i = 0;
    auto const msg = getDataString(runtime, params, i);
    if (!msg)
        return hfResult(results, msg.error());

    auto const account = getDataAccountID(runtime, params, i);
    if (!account)
        return hfResult(results, account.error());

    return returnResult(
        runtime, params, results, hf->traceAccount(*msg, *account), i);
}

wasm_trap_t*
traceFloat_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());

    if (params->data[1].of.i32 > maxWasmParamLength)
        return hfResult(results, HostFunctionError::DATA_FIELD_TOO_LARGE);

    int i = 0;
    auto const msg = getDataString(runtime, params, i);
    if (!msg)
        return hfResult(results, msg.error());

    auto const number = getDataSlice(runtime, params, i);
    if (!number)
        return hfResult(results, number.error());

    return returnResult(
        runtime, params, results, hf->traceFloat(*msg, *number), i);
}

wasm_trap_t*
traceAmount_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());

    if (params->data[1].of.i32 > maxWasmParamLength)
        return hfResult(results, HostFunctionError::DATA_FIELD_TOO_LARGE);

    int i = 0;
    auto const msg = getDataString(runtime, params, i);
    if (!msg)
        return hfResult(results, msg.error());

    auto const amountSliceOpt = getDataSlice(runtime, params, i);
    if (!amountSliceOpt)
        return hfResult(results, amountSliceOpt.error());

    auto const amountSlice = amountSliceOpt.value();
    auto serialIter = SerialIter(amountSlice);

    std::optional<STAmount> amount;
    try
    {
        amount = STAmount(serialIter, sfGeneric);
    }
    catch (std::exception const&)
    {
        amount = std::nullopt;
    }
    if (!amount)
        return hfResult(results, HostFunctionError::INVALID_PARAMS);

    return returnResult(
        runtime, params, results, hf->traceAmount(*msg, *amount), i);
}

wasm_trap_t*
floatFromInt_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());

    int i = 0;
    auto const x = getDataInt64(runtime, params, i);
    if (!x)
        return hfResult(results, x.error());  // LCOV_EXCL_LINE

    i = 3;
    auto const rounding = getDataInt32(runtime, params, i);
    if (!rounding)
        return hfResult(results, rounding.error());  // LCOV_EXCL_LINE

    i = 1;
    return returnResult(
        runtime, params, results, hf->floatFromInt(*x, *rounding), i);
}

wasm_trap_t*
floatFromUint_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());

    int i = 0;
    auto const x = getDataUInt64(runtime, params, i);
    if (!x)
        return hfResult(results, x.error());

    i = 4;
    auto const rounding = getDataInt32(runtime, params, i);
    if (!rounding)
        return hfResult(results, rounding.error());  // LCOV_EXCL_LINE

    i = 2;
    return returnResult(
        runtime, params, results, hf->floatFromUint(*x, *rounding), i);
}

wasm_trap_t*
floatSet_wrap(void* env, wasm_val_vec_t const* params, wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());

    int i = 0;
    auto const exp = getDataInt32(runtime, params, i);
    if (!exp)
        return hfResult(results, exp.error());  // LCOV_EXCL_LINE

    auto const mant = getDataInt64(runtime, params, i);
    if (!mant)
        return hfResult(results, mant.error());  // LCOV_EXCL_LINE

    i = 4;
    auto const rounding = getDataInt32(runtime, params, i);
    if (!rounding)
        return hfResult(results, rounding.error());  // LCOV_EXCL_LINE

    i = 2;
    return returnResult(
        runtime, params, results, hf->floatSet(*mant, *exp, *rounding), i);
}

wasm_trap_t*
floatCompare_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());

    int i = 0;
    auto const x = getDataSlice(runtime, params, i);
    if (!x)
        return hfResult(results, x.error());

    auto const y = getDataSlice(runtime, params, i);
    if (!y)
        return hfResult(results, y.error());

    return returnResult(runtime, params, results, hf->floatCompare(*x, *y), i);
}

wasm_trap_t*
floatAdd_wrap(void* env, wasm_val_vec_t const* params, wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());

    int i = 0;
    auto const x = getDataSlice(runtime, params, i);
    if (!x)
        return hfResult(results, x.error());

    auto const y = getDataSlice(runtime, params, i);
    if (!y)
        return hfResult(results, y.error());

    i = 6;
    auto const rounding = getDataInt32(runtime, params, i);
    if (!rounding)
        return hfResult(results, rounding.error());  // LCOV_EXCL_LINE

    i = 4;
    return returnResult(
        runtime, params, results, hf->floatAdd(*x, *y, *rounding), i);
}

wasm_trap_t*
floatSubtract_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());

    int i = 0;
    auto const x = getDataSlice(runtime, params, i);
    if (!x)
        return hfResult(results, x.error());

    auto const y = getDataSlice(runtime, params, i);
    if (!y)
        return hfResult(results, y.error());

    i = 6;
    auto const rounding = getDataInt32(runtime, params, i);
    if (!rounding)
        return hfResult(results, rounding.error());  // LCOV_EXCL_LINE

    i = 4;
    return returnResult(
        runtime, params, results, hf->floatSubtract(*x, *y, *rounding), i);
}

wasm_trap_t*
floatMultiply_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());

    int i = 0;
    auto const x = getDataSlice(runtime, params, i);
    if (!x)
        return hfResult(results, x.error());

    auto const y = getDataSlice(runtime, params, i);
    if (!y)
        return hfResult(results, y.error());

    i = 6;
    auto const rounding = getDataInt32(runtime, params, i);
    if (!rounding)
        return hfResult(results, rounding.error());  // LCOV_EXCL_LINE

    i = 4;
    return returnResult(
        runtime, params, results, hf->floatMultiply(*x, *y, *rounding), i);
}

wasm_trap_t*
floatDivide_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());

    int i = 0;
    auto const x = getDataSlice(runtime, params, i);
    if (!x)
        return hfResult(results, x.error());

    auto const y = getDataSlice(runtime, params, i);
    if (!y)
        return hfResult(results, y.error());

    i = 6;
    auto const rounding = getDataInt32(runtime, params, i);
    if (!rounding)
        return hfResult(results, rounding.error());  // LCOV_EXCL_LINE

    i = 4;
    return returnResult(
        runtime, params, results, hf->floatDivide(*x, *y, *rounding), i);
}

wasm_trap_t*
floatRoot_wrap(void* env, wasm_val_vec_t const* params, wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());

    int i = 0;
    auto const x = getDataSlice(runtime, params, i);
    if (!x)
        return hfResult(results, x.error());

    auto const n = getDataInt32(runtime, params, i);
    if (!n)
        return hfResult(results, n.error());  // LCOV_EXCL_LINE

    i = 5;
    auto const rounding = getDataInt32(runtime, params, i);
    if (!rounding)
        return hfResult(results, rounding.error());  // LCOV_EXCL_LINE

    i = 3;
    return returnResult(
        runtime, params, results, hf->floatRoot(*x, *n, *rounding), i);
}

wasm_trap_t*
floatPower_wrap(
    void* env,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());

    int i = 0;
    auto const x = getDataSlice(runtime, params, i);
    if (!x)
        return hfResult(results, x.error());

    auto const n = getDataInt32(runtime, params, i);
    if (!n)
        return hfResult(results, n.error());  // LCOV_EXCL_LINE

    i = 5;
    auto const rounding = getDataInt32(runtime, params, i);
    if (!rounding)
        return hfResult(results, rounding.error());  // LCOV_EXCL_LINE

    i = 3;
    return returnResult(
        runtime, params, results, hf->floatPower(*x, *n, *rounding), i);
}

wasm_trap_t*
floatLog_wrap(void* env, wasm_val_vec_t const* params, wasm_val_vec_t* results)
{
    auto* hf = reinterpret_cast<HostFunctions*>(env);
    auto const* runtime = reinterpret_cast<InstanceWrapper const*>(hf->getRT());

    int i = 0;
    auto const x = getDataSlice(runtime, params, i);
    if (!x)
        return hfResult(results, x.error());

    i = 4;
    auto const rounding = getDataInt32(runtime, params, i);
    if (!rounding)
        return hfResult(results, rounding.error());  // LCOV_EXCL_LINE

    i = 2;
    return returnResult(
        runtime, params, results, hf->floatLog(*x, *rounding), i);
}

// LCOV_EXCL_START
namespace test {

class MockInstanceWrapper
{
    wmem mem_;

public:
    MockInstanceWrapper(wmem memory) : mem_(memory)
    {
    }

    // Mock methods to simulate the behavior of InstanceWrapper
    wmem
    getMem() const
    {
        return mem_;
    }
};

bool
testGetDataIncrement()
{
    wasm_val_t values[4];

    std::array<std::uint8_t, 128> buffer = {
        'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h'};
    MockInstanceWrapper runtime(wmem{buffer.data(), buffer.size()});

    {
        // test int32_t
        wasm_val_vec_t params = {1, &values[0]};

        values[0] = WASM_I32_VAL(42);

        int index = 0;
        auto const result = getDataInt32(&runtime, &params, index);
        if (!result || result.value() != 42 || index != 1)
            return false;
    }

    {
        // test int64_t
        wasm_val_vec_t params = {1, &values[0]};

        values[0] = WASM_I64_VAL(1234);

        int index = 0;
        auto const result = getDataInt64(&runtime, &params, index);
        if (!result || result.value() != 1234 || index != 1)
            return false;
    }

    {
        // test SFieldCRef
        wasm_val_vec_t params = {1, &values[0]};

        values[0] = WASM_I32_VAL(sfAccount.fieldCode);

        int index = 0;
        auto const result = getDataSField(&runtime, &params, index);
        if (!result || result.value().get() != sfAccount || index != 1)
            return false;
    }

    {
        // test Slice
        wasm_val_vec_t params = {2, &values[0]};

        values[0] = WASM_I32_VAL(0);
        values[1] = WASM_I32_VAL(3);

        int index = 0;
        auto const result = getDataSlice(&runtime, &params, index);
        if (!result || result.value() != Slice(buffer.data(), 3) || index != 2)
            return false;
    }

    {
        // test string
        wasm_val_vec_t params = {2, &values[0]};

        values[0] = WASM_I32_VAL(0);
        values[1] = WASM_I32_VAL(5);

        int index = 0;
        auto const result = getDataString(&runtime, &params, index);
        if (!result ||
            result.value() !=
                std::string_view(
                    reinterpret_cast<char const*>(buffer.data()), 5) ||
            index != 2)
            return false;
    }

    {
        // test account
        AccountID const id(calcAccountID(
            generateKeyPair(KeyType::secp256k1, generateSeed("alice")).first));

        wasm_val_vec_t params = {2, &values[0]};

        values[0] = WASM_I32_VAL(0);
        values[1] = WASM_I32_VAL(id.bytes);
        memcpy(&buffer[0], id.data(), id.bytes);

        int index = 0;
        auto const result = getDataAccountID(&runtime, &params, index);
        if (!result || result.value() != id || index != 2)
            return false;
    }

    {
        // test uint256

        Hash h1 = sha512Half(Slice(buffer.data(), 8));
        wasm_val_vec_t params = {2, &values[0]};

        values[0] = WASM_I32_VAL(0);
        values[1] = WASM_I32_VAL(h1.bytes);
        memcpy(&buffer[0], h1.data(), h1.bytes);

        int index = 0;
        auto const result = getDataUInt256(&runtime, &params, index);
        if (!result || result.value() != h1 || index != 2)
            return false;
    }

    {
        // test Currency

        Currency const c = xrpCurrency();
        wasm_val_vec_t params = {2, &values[0]};

        values[0] = WASM_I32_VAL(0);
        values[1] = WASM_I32_VAL(c.bytes);
        memcpy(&buffer[0], c.data(), c.bytes);

        int index = 0;
        auto const result = getDataCurrency(&runtime, &params, index);
        if (!result || result.value() != c || index != 2)
            return false;
    }

    return true;
}

}  // namespace test
// LCOV_EXCL_STOP

}  // namespace ripple
