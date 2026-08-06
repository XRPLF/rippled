#include <xrpl/tx/wasm/HostFuncWrapper.h>

#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/tx/wasm/HostFunc.h>
#include <xrpl/tx/wasm/WasmCommon.h>
#include <xrpl/tx/wasm/WasmImportsHelper.h>
#include <xrpl/tx/wasm/WasmVM.h>

#include <wasm.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <expected>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace xrpl {

using SFieldCRef = std::reference_wrapper<SField const>;

constexpr int64_t unalignedGas = 50;

// Charge `delta` gas; returns the remaining gas. Out-of-gas throws hfErrOutOfGas
// (-> tecOUT_OF_GAS); a failed setGas is an xrpld bug, throws hfErrInternal
// (-> tecINTERNAL). HostFuncMain_wrap turns both into traps.
static inline std::int64_t
checkGas(WasmRuntimeWrapper& rt, int64_t delta)
{
    int64_t const gas = rt.getGas();
    if (delta == 0)
        return gas;

    int64_t const x = gas >= delta ? gas - delta : 0;

    if (rt.setGas(x) < 0)
        Throw<std::runtime_error>(std::string(hfErrInternal));  // LCOV_EXCL_LINE

    if (gas < delta)
        Throw<std::runtime_error>(std::string(hfErrOutOfGas));

    return x;
}

// Transfer limit is a separate soft budget: exceeding it is a normal guest-facing
// return code, not a trap. Only a failed setTransferLimit (an xrpld bug) throws.
static inline std::expected<std::int64_t, HostFunctionError>
checkTransfer(WasmRuntimeWrapper& rt, int64_t delta)
{
    auto const transLimit = rt.getTransferLimit();
    int64_t const x = transLimit >= delta ? transLimit - delta : 0;

    if (rt.setTransferLimit(x) < 0)
        Throw<std::runtime_error>(std::string(hfErrInternal));  // LCOV_EXCL_LINE

    if (transLimit < delta)
        return std::unexpected(HostFunctionError::OutOfTransferLimit);

    return x;
}

// On any failure here a C++ exception is thrown; HostFuncMain_wrap's catch-all
// turns it into tecINTERNAL. These conditions are all xrpld-side invariants.
static std::tuple<HostFunctions&, WasmImportFunc const&>
mainCheck(void* env, wasm_val_vec_t const* params, wasm_val_vec_t* results)
{
    if (env == nullptr)
        Throw<std::runtime_error>(std::string(hfErrInternal));  // LCOV_EXCL_LINE

    if (params == nullptr)
        Throw<std::runtime_error>(std::string(hfErrInternal));  // LCOV_EXCL_LINE

    if (results == nullptr)
        Throw<std::runtime_error>(std::string(hfErrInternal));  // LCOV_EXCL_LINE

    WasmUserData const* udata = reinterpret_cast<WasmUserData*>(env);
    HostFunctions& hf = udata->first;
    WasmRuntimeWrapper& rt = hf.getRT();
    WasmImportFunc const& impFunc = udata->second;

    // Charge the per-call gas. Throws (and terminates) if out of gas.
    checkGas(rt, impFunc.gas);

    return std::tie(hf, impFunc);
}

//----------------------------------------------------------------------------------------------------------------------

static int32_t
setData(
    WasmRuntimeWrapper& runtime,
    int32_t dst,
    int32_t dstSize,
    uint8_t const* src,
    int32_t srcSize)
{
    if (srcSize == 0)
        return 0;  // LCOV_EXCL_LINE

    if (dst < 0 || dstSize < 0 || (src == nullptr) || srcSize < 0)
        return hfErrorToInt(HostFunctionError::InvalidParams);

    if (srcSize > kMaxWasmDataLength)
        return hfErrorToInt(HostFunctionError::DataFieldTooLarge);

    auto const memory = runtime.getMem();

    // LCOV_EXCL_START
    if (memory.s == 0u)
        return hfErrorToInt(HostFunctionError::NoMemExported);
    // LCOV_EXCL_STOP
    if (std::cmp_greater((int64_t)dst + dstSize, memory.s))
        return hfErrorToInt(HostFunctionError::PointerOutOfBounds);
    if (srcSize > dstSize)
        return hfErrorToInt(HostFunctionError::BufferTooSmall);

    if (auto t = checkTransfer(runtime, srcSize); !t)
        return hfErrorToInt(t.error());

    memcpy(memory.p + dst, src, srcSize);

    return srcSize;
}

static std::expected<Slice, HostFunctionError>
getDataSlice(WasmRuntimeWrapper& runtime, wasm_val_vec_t const* params, int32_t& i)
{
    int64_t const ptr = params->data[i].of.i32;
    int64_t const size = params->data[i + 1].of.i32;
    i += 2;
    if (ptr < 0 || size < 0)
        return std::unexpected(HostFunctionError::InvalidParams);

    if (size == 0)
        return Slice();

    if (size > kMaxWasmDataLength)
        return std::unexpected(HostFunctionError::DataFieldTooLarge);

    auto const memory = runtime.getMem();
    // LCOV_EXCL_START
    if (memory.s == 0u)
        return std::unexpected(HostFunctionError::NoMemExported);
    // LCOV_EXCL_STOP

    if (std::cmp_greater(ptr + size, memory.s))
        return std::unexpected(HostFunctionError::PointerOutOfBounds);

    Slice const data(memory.p + ptr, size);
    return data;
}

static std::expected<int32_t, HostFunctionError>
getDataInt32(WasmRuntimeWrapper const&, wasm_val_vec_t const* params, int32_t& i)
{
    auto const result = params->data[i].of.i32;
    i++;
    return result;
}

static std::expected<int64_t, HostFunctionError>
getDataInt64(WasmRuntimeWrapper const&, wasm_val_vec_t const* params, int32_t& i)
{
    auto const result = params->data[i].of.i64;
    i++;
    return result;
}

template <class T>
static std::expected<T, HostFunctionError>
getDataUnsigned(WasmRuntimeWrapper& runtime, wasm_val_vec_t const* params, int32_t& i)
{
    static_assert(std::is_unsigned_v<T>);
    auto const r = getDataSlice(runtime, params, i);
    if (!r)
        return std::unexpected(r.error());
    if (r->size() != sizeof(T))
        return std::unexpected(HostFunctionError::InvalidParams);

    T x;
    auto const p = reinterpret_cast<uintptr_t>(r->data());
    if (p & (alignof(T) - 1))  // unaligned
    {
        memcpy(&x, r->data(), sizeof(T));
    }
    else
    {
        x = *reinterpret_cast<T const*>(r->data());
    }
    x = adjustWasmEndianess(x);

    return x;
}

static std::expected<uint32_t, HostFunctionError>
getDataUInt32(WasmRuntimeWrapper& runtime, wasm_val_vec_t const* params, int32_t& i)
{
    return getDataUnsigned<uint32_t>(runtime, params, i);
}

static std::expected<uint64_t, HostFunctionError>
getDataUInt64(WasmRuntimeWrapper& runtime, wasm_val_vec_t const* params, int32_t& i)
{
    return getDataUnsigned<uint64_t>(runtime, params, i);
}

static std::expected<SFieldCRef, HostFunctionError>
getDataSField(WasmRuntimeWrapper& runtime, wasm_val_vec_t const* params, int32_t& i)
{
    auto const& m = SField::getKnownCodeToField();
    auto const it = m.find(params->data[i].of.i32);
    i++;
    if (it == m.end())
        return std::unexpected(HostFunctionError::InvalidField);

    return *it->second;
}

static std::expected<uint256, HostFunctionError>
getDataUInt256(WasmRuntimeWrapper& runtime, wasm_val_vec_t const* params, int32_t& i)
{
    auto const slice = getDataSlice(runtime, params, i);
    if (!slice)
        return std::unexpected(slice.error());

    if (slice->size() != uint256::size())
        return std::unexpected(HostFunctionError::InvalidParams);

    if (auto t = checkTransfer(runtime, uint256::size()); !t)
        return std::unexpected(t.error());

    return uint256::fromVoid(slice->data());
}

static std::expected<AccountID, HostFunctionError>
getDataAccountID(WasmRuntimeWrapper& runtime, wasm_val_vec_t const* params, int32_t& i)
{
    auto const slice = getDataSlice(runtime, params, i);
    if (!slice)
        return std::unexpected(slice.error());

    if (slice->size() != AccountID::size())
        return std::unexpected(HostFunctionError::InvalidParams);

    if (auto t = checkTransfer(runtime, AccountID::size()); !t)
        return std::unexpected(t.error());

    return AccountID::fromVoid(slice->data());
}

static std::expected<Currency, HostFunctionError>
getDataCurrency(WasmRuntimeWrapper& runtime, wasm_val_vec_t const* params, int32_t& i)
{
    auto const slice = getDataSlice(runtime, params, i);
    if (!slice)
        return std::unexpected(slice.error());

    if (slice->size() != Currency::size())
        return std::unexpected(HostFunctionError::InvalidParams);

    if (auto t = checkTransfer(runtime, Currency::size()); !t)
        return std::unexpected(t.error());

    return Currency::fromVoid(slice->data());
}

static std::expected<Asset, HostFunctionError>
getDataAsset(WasmRuntimeWrapper& runtime, wasm_val_vec_t const* params, int32_t& i)
{
    auto const slice = getDataSlice(runtime, params, i);
    if (!slice)
        return std::unexpected(slice.error());

    if (slice->size() == MPTID::size())
    {
        if (auto t = checkTransfer(runtime, slice->size()); !t)
            return std::unexpected(t.error());

        auto const mptid = MPTID::fromVoid(slice->data());
        return Asset{mptid};
    }

    if (slice->size() == Currency::size())
    {
        if (auto t = checkTransfer(runtime, slice->size()); !t)
            return std::unexpected(t.error());

        auto const currency = Currency::fromVoid(slice->data());
        auto const issue = Issue{currency, xrpAccount()};
        if (!issue.native())
            return std::unexpected(HostFunctionError::InvalidParams);

        return Asset{issue};
    }

    if (slice->size() == (Currency::size() + AccountID::size()))
    {
        if (auto t = checkTransfer(runtime, slice->size()); !t)
            return std::unexpected(t.error());

        auto const issue = Issue(
            Currency::fromVoid(slice->data()),
            AccountID::fromVoid(slice->data() + Currency::size()));

        if (issue.native())
            return std::unexpected(HostFunctionError::InvalidParams);

        return Asset{issue};
    }

    return std::unexpected(HostFunctionError::InvalidParams);
}

static std::expected<std::string_view, HostFunctionError>
getDataString(WasmRuntimeWrapper& runtime, wasm_val_vec_t const* params, int32_t& i)
{
    auto const slice = getDataSlice(runtime, params, i);
    if (!slice)
        return std::unexpected(slice.error());

    return std::string_view(reinterpret_cast<char const*>(slice->data()), slice->size());
}

static std::expected<FieldLocator, HostFunctionError>
getDataLocator(WasmRuntimeWrapper& runtime, wasm_val_vec_t const* params, int32_t& i)
{
    static_assert(kMaxWasmDataLength % sizeof(int32_t) == 0);

    auto const slice = getDataSlice(runtime, params, i);
    if (!slice)
        return std::unexpected(slice.error());
    if (slice->empty() || ((slice->size() & 3) != 0u))  // must be multiple of 4
        return std::unexpected(HostFunctionError::LocatorMalformed);

    uint32_t const locSize = slice->size() / sizeof(int32_t);
    auto const p = reinterpret_cast<uintptr_t>(slice->data());

    if ((p & (alignof(int32_t) - 1)) != 0u)
    {  // unaligned

        // Use gas and transfer limit for copying. checkGas throws (and
        // terminates execution) if out of gas; checkTransfer keeps returning a
        // guest-facing code when the transfer limit is exceeded.
        checkGas(runtime, unalignedGas);
        if (auto t = checkTransfer(runtime, slice->size()); !t)
            return std::unexpected(t.error());

        std::vector<int32_t> locBuf(locSize);
        memcpy(&locBuf[0], slice->data(), slice->size());
        FieldLocator locator(std::move(locBuf));

        return locator;
    }

    auto const* locPtr = reinterpret_cast<int32_t const*>(slice->data());
    return FieldLocator(locPtr, locSize);
}

static inline std::nullptr_t
hfResult(wasm_val_vec_t* results, int32_t value)
{
    results->data[0] = WASM_I32_VAL(value);
    // results->size = 1;
    return nullptr;
}

static inline std::nullptr_t
hfResult(wasm_val_vec_t* results, HostFunctionError value)
{
    results->data[0] = WASM_I32_VAL(hfErrorToInt(value));
    // results->size = 1;
    return nullptr;
}

template <typename T>
static std::nullptr_t
returnResult(
    WasmRuntimeWrapper& runtime,
    wasm_val_vec_t const* params,
    wasm_val_vec_t* results,
    std::expected<T, HostFunctionError> const& res,
    int32_t index)
{
    if (!res)
        return hfResult(results, res.error());

    if constexpr (std::is_same_v<T, Bytes>)
    {
        if (index < 0 || index + 1 >= params->size)
            Throw<std::runtime_error>(std::string(hfErrInternal));  // LCOV_EXCL_LINE

        auto const dataResult = setData(
            runtime,
            params->data[index].of.i32,
            params->data[index + 1].of.i32,
            res->data(),
            res->size());
        return hfResult(results, dataResult);
    }
    else if constexpr (std::is_same_v<T, Hash>)
    {
        if (index < 0 || index + 1 >= params->size)
            Throw<std::runtime_error>(std::string(hfErrInternal));  // LCOV_EXCL_LINE

        auto const dataResult = setData(
            runtime,
            params->data[index].of.i32,
            params->data[index + 1].of.i32,
            res->data(),
            res->size());
        return hfResult(results, dataResult);
    }
    else if constexpr (std::is_same_v<T, int32_t>)
    {
        return hfResult(results, res.value());
    }
    else if constexpr (std::is_same_v<T, std::uint32_t>)
    {
        if (index < 0 || index + 1 >= params->size)
            Throw<std::runtime_error>(std::string(hfErrInternal));  // LCOV_EXCL_LINE

        auto const resultValue = adjustWasmEndianess(res.value());
        auto const dataResult = setData(
            runtime,
            params->data[index].of.i32,
            params->data[index + 1].of.i32,
            reinterpret_cast<uint8_t const*>(&resultValue),
            static_cast<int32_t>(sizeof(resultValue)));
        return hfResult(results, dataResult);
    }
    else if constexpr (std::is_same_v<T, int64_t>)
    {
        if (index < 0 || index + 1 >= params->size)
            Throw<std::runtime_error>(std::string(hfErrInternal));  // LCOV_EXCL_LINE

        auto const resultValue = adjustWasmEndianess(res.value());
        auto const dataResult = setData(
            runtime,
            params->data[index].of.i32,
            params->data[index + 1].of.i32,
            reinterpret_cast<uint8_t const*>(&resultValue),
            static_cast<int32_t>(sizeof(resultValue)));
        return hfResult(results, dataResult);
    }
    else if constexpr (std::is_same_v<T, FloatPair>)
    {
        if (index < 0 || index + 3 >= params->size)
            Throw<std::runtime_error>(std::string(hfErrInternal));  // LCOV_EXCL_LINE

        auto const mantissa = adjustWasmEndianess(res->first);
        auto const r1 = setData(
            runtime,
            params->data[index].of.i32,
            params->data[index + 1].of.i32,
            reinterpret_cast<uint8_t const*>(&mantissa),
            static_cast<int32_t>(sizeof(mantissa)));
        if (r1 < 0)
            return hfResult(results, r1);

        index += 2;
        auto const exponent = adjustWasmEndianess(res->second);
        auto const r2 = setData(
            runtime,
            params->data[index].of.i32,
            params->data[index + 1].of.i32,
            reinterpret_cast<uint8_t const*>(&exponent),
            static_cast<int32_t>(sizeof(exponent)));
        if (r2 < 0)
            return hfResult(results, r2);

        return hfResult(results, r1 + r2);  // 12 bytes
    }
    else
    {
        static_assert([] { return false; }(), "Unhandled return type in returnResult");
    }
}

//----------------------------------------------------------------------------------------------------------------------

wasm_trap_t*
HostFuncMain_wrap(WASM_CB_PARAMS_LIST)
{
    [[maybe_unused]] std::string_view hfName;

    try
    {
        auto [hf, impFunc] = mainCheck(env, params, results);
        hfName = impFunc.name;
        auto* fWrap = reinterpret_cast<wasmSecondaryCbFuncType*>(impFunc.wrap);
        return fWrap(hf, params, results);
    }
    catch (std::exception const& e)
    {
#ifdef DEBUG_OUTPUT
        std::cerr << "Hostfunction " << hfName << " exception: " << e.what() << std::endl;
#endif
        // Normalize to the two boundary signals: explicit out-of-gas, else any
        // exception (including stray ones from helpers) is an internal fault.
        bool const oog = std::string_view(e.what()) == hfErrOutOfGas;
        wasm_trap_t* trap = reinterpret_cast<wasm_trap_t*>(  // NOLINT
            WasmEngine::instance().newTrap(std::string(oog ? hfErrOutOfGas : hfErrInternal)));
        return trap;
    }
    catch (...)
    {
#ifdef DEBUG_OUTPUT
        std::cerr << "Hostfunction " << hfName << " unknown exception." << std::endl;
#endif
        wasm_trap_t* trap = reinterpret_cast<wasm_trap_t*>(               // NOLINT
            WasmEngine::instance().newTrap(std::string(hfErrInternal)));  // LCOV_EXCL_LINE
        return trap;
    }

    return nullptr;  // LCOV_EXCL_LINE
}

//----------------------------------------------------------------------------------------------------------------------
wasm_trap_t*
getLedgerSqn_wrap(WASM_SECONDARY_CB_PARAMS_LIST)
{
    int const index = 0;
    WasmRuntimeWrapper& runtime = hf.getRT();

    return returnResult(runtime, params, results, hf.getLedgerSqn(), index);
}

wasm_trap_t*
getParentLedgerTime_wrap(WASM_SECONDARY_CB_PARAMS_LIST)
{
    int const index = 0;
    WasmRuntimeWrapper& runtime = hf.getRT();

    return returnResult(runtime, params, results, hf.getParentLedgerTime(), index);
}

wasm_trap_t*
getParentLedgerHash_wrap(WASM_SECONDARY_CB_PARAMS_LIST)
{
    int const index = 0;
    WasmRuntimeWrapper& runtime = hf.getRT();

    return returnResult(runtime, params, results, hf.getParentLedgerHash(), index);
}

wasm_trap_t*
getBaseFee_wrap(WASM_SECONDARY_CB_PARAMS_LIST)
{
    int const index = 0;
    WasmRuntimeWrapper& runtime = hf.getRT();

    return returnResult(runtime, params, results, hf.getBaseFee(), index);
}

wasm_trap_t*
isAmendmentEnabled_wrap(WASM_SECONDARY_CB_PARAMS_LIST)
{
    int index = 0;
    WasmRuntimeWrapper& runtime = hf.getRT();

    auto const slice = getDataSlice(runtime, params, index);
    if (!slice)
        return hfResult(results, slice.error());

    if (slice->size() == uint256::size())
    {
        if (auto const ret = hf.isAmendmentEnabled(uint256::fromVoid(slice->data()));
            ret && *ret == 1)
            return returnResult(runtime, params, results, ret, index);
        // Fall through to string lookup — the 32 bytes may be an amendment name
    }

    if (slice->size() > 64)
        return hfResult(results, HostFunctionError::DataFieldTooLarge);

    auto const str = std::string_view(reinterpret_cast<char const*>(slice->data()), slice->size());
    return returnResult(runtime, params, results, hf.isAmendmentEnabled(str), index);
}

wasm_trap_t*
cacheLedgerObj_wrap(WASM_SECONDARY_CB_PARAMS_LIST)
{
    int index = 0;
    WasmRuntimeWrapper& runtime = hf.getRT();

    auto const id = getDataUInt256(runtime, params, index);
    if (!id)
        return hfResult(results, id.error());

    auto const cache = getDataInt32(runtime, params, index);
    if (!cache)
        return hfResult(results, cache.error());  // LCOV_EXCL_LINE

    return returnResult(runtime, params, results, hf.cacheLedgerObj(*id, *cache), index);
}

wasm_trap_t*
getTxField_wrap(WASM_SECONDARY_CB_PARAMS_LIST)
{
    int index = 0;
    WasmRuntimeWrapper& runtime = hf.getRT();

    auto const fname = getDataSField(runtime, params, index);
    if (!fname)
        return hfResult(results, fname.error());

    return returnResult(runtime, params, results, hf.getTxField(*fname), index);
}

wasm_trap_t*
getCurrentLedgerObjField_wrap(WASM_SECONDARY_CB_PARAMS_LIST)
{
    int index = 0;
    WasmRuntimeWrapper& runtime = hf.getRT();

    auto const fname = getDataSField(runtime, params, index);
    if (!fname)
        return hfResult(results, fname.error());

    return returnResult(runtime, params, results, hf.getCurrentLedgerObjField(*fname), index);
}

wasm_trap_t*
getLedgerObjField_wrap(WASM_SECONDARY_CB_PARAMS_LIST)
{
    int index = 0;
    WasmRuntimeWrapper& runtime = hf.getRT();

    auto const cache = getDataInt32(runtime, params, index);
    if (!cache)
        return hfResult(results, cache.error());  // LCOV_EXCL_LINE

    auto const fname = getDataSField(runtime, params, index);
    if (!fname)
        return hfResult(results, fname.error());

    return returnResult(runtime, params, results, hf.getLedgerObjField(*cache, *fname), index);
}

wasm_trap_t*
getTxNestedField_wrap(WASM_SECONDARY_CB_PARAMS_LIST)
{
    int index = 0;
    WasmRuntimeWrapper& runtime = hf.getRT();

    auto const locator = getDataLocator(runtime, params, index);
    if (!locator)
        return hfResult(results, locator.error());

    return returnResult(runtime, params, results, hf.getTxNestedField(*locator), index);
}

wasm_trap_t*
getCurrentLedgerObjNestedField_wrap(WASM_SECONDARY_CB_PARAMS_LIST)
{
    int index = 0;
    WasmRuntimeWrapper& runtime = hf.getRT();

    auto const locator = getDataLocator(runtime, params, index);
    if (!locator)
        return hfResult(results, locator.error());

    return returnResult(
        runtime, params, results, hf.getCurrentLedgerObjNestedField(*locator), index);
}

wasm_trap_t*
getLedgerObjNestedField_wrap(WASM_SECONDARY_CB_PARAMS_LIST)
{
    int index = 0;
    WasmRuntimeWrapper& runtime = hf.getRT();

    auto const cache = getDataInt32(runtime, params, index);
    if (!cache)
        return hfResult(results, cache.error());  // LCOV_EXCL_LINE

    auto const locator = getDataLocator(runtime, params, index);
    if (!locator)
        return hfResult(results, locator.error());

    return returnResult(
        runtime, params, results, hf.getLedgerObjNestedField(*cache, *locator), index);
}

wasm_trap_t*
getTxArrayLen_wrap(WASM_SECONDARY_CB_PARAMS_LIST)
{
    int index = 0;
    WasmRuntimeWrapper& runtime = hf.getRT();

    auto const fname = getDataSField(runtime, params, index);
    if (!fname)
        return hfResult(results, fname.error());

    return returnResult(runtime, params, results, hf.getTxArrayLen(*fname), index);
}

wasm_trap_t*
getCurrentLedgerObjArrayLen_wrap(WASM_SECONDARY_CB_PARAMS_LIST)
{
    int index = 0;
    WasmRuntimeWrapper& runtime = hf.getRT();

    auto const fname = getDataSField(runtime, params, index);
    if (!fname)
        return hfResult(results, fname.error());

    return returnResult(runtime, params, results, hf.getCurrentLedgerObjArrayLen(*fname), index);
}

wasm_trap_t*
getLedgerObjArrayLen_wrap(WASM_SECONDARY_CB_PARAMS_LIST)
{
    int index = 0;
    WasmRuntimeWrapper& runtime = hf.getRT();

    auto const cache = getDataInt32(runtime, params, index);
    if (!cache)
        return hfResult(results, cache.error());  // LCOV_EXCL_LINE

    auto const fname = getDataSField(runtime, params, index);
    if (!fname)
        return hfResult(results, fname.error());

    return returnResult(runtime, params, results, hf.getLedgerObjArrayLen(*cache, *fname), index);
}

wasm_trap_t*
getTxNestedArrayLen_wrap(WASM_SECONDARY_CB_PARAMS_LIST)
{
    int index = 0;
    WasmRuntimeWrapper& runtime = hf.getRT();

    auto const locator = getDataLocator(runtime, params, index);
    if (!locator)
        return hfResult(results, locator.error());

    return returnResult(runtime, params, results, hf.getTxNestedArrayLen(*locator), index);
}

wasm_trap_t*
getCurrentLedgerObjNestedArrayLen_wrap(WASM_SECONDARY_CB_PARAMS_LIST)
{
    int index = 0;
    WasmRuntimeWrapper& runtime = hf.getRT();

    auto const locator = getDataLocator(runtime, params, index);
    if (!locator)
        return hfResult(results, locator.error());

    return returnResult(
        runtime, params, results, hf.getCurrentLedgerObjNestedArrayLen(*locator), index);
}
wasm_trap_t*
getLedgerObjNestedArrayLen_wrap(WASM_SECONDARY_CB_PARAMS_LIST)
{
    int index = 0;
    WasmRuntimeWrapper& runtime = hf.getRT();

    auto const cache = getDataInt32(runtime, params, index);
    if (!cache)
        return hfResult(results, cache.error());  // LCOV_EXCL_LINE

    auto const locator = getDataLocator(runtime, params, index);
    if (!locator)
        return hfResult(results, locator.error());

    return returnResult(
        runtime, params, results, hf.getLedgerObjNestedArrayLen(*cache, *locator), index);
}

wasm_trap_t*
updateData_wrap(WASM_SECONDARY_CB_PARAMS_LIST)
{
    int index = 0;
    WasmRuntimeWrapper& runtime = hf.getRT();

    auto const bytes = getDataSlice(runtime, params, index);
    if (!bytes)
        return hfResult(results, bytes.error());

    return returnResult(runtime, params, results, hf.updateData(*bytes), index);
}

wasm_trap_t*
checkSignature_wrap(WASM_SECONDARY_CB_PARAMS_LIST)
{
    int index = 0;
    WasmRuntimeWrapper& runtime = hf.getRT();

    auto const message = getDataSlice(runtime, params, index);
    if (!message)
        return hfResult(results, message.error());

    auto const signature = getDataSlice(runtime, params, index);
    if (!signature)
        return hfResult(results, signature.error());

    auto const pubkey = getDataSlice(runtime, params, index);
    if (!pubkey)
        return hfResult(results, pubkey.error());

    return returnResult(
        runtime, params, results, hf.checkSignature(*message, *signature, *pubkey), index);
}

wasm_trap_t*
computeSha512HalfHash_wrap(WASM_SECONDARY_CB_PARAMS_LIST)
{
    int index = 0;
    WasmRuntimeWrapper& runtime = hf.getRT();

    auto const bytes = getDataSlice(runtime, params, index);
    if (!bytes)
        return hfResult(results, bytes.error());

    return returnResult(runtime, params, results, hf.computeSha512HalfHash(*bytes), index);
}

wasm_trap_t*
accountKeylet_wrap(WASM_SECONDARY_CB_PARAMS_LIST)
{
    int index = 0;
    WasmRuntimeWrapper& runtime = hf.getRT();

    auto const acc = getDataAccountID(runtime, params, index);
    if (!acc)
        return hfResult(results, acc.error());

    return returnResult(runtime, params, results, hf.accountKeylet(*acc), index);
}

wasm_trap_t*
ammKeylet_wrap(WASM_SECONDARY_CB_PARAMS_LIST)
{
    int index = 0;
    WasmRuntimeWrapper& runtime = hf.getRT();

    auto const issue1 = getDataAsset(runtime, params, index);
    if (!issue1)
        return hfResult(results, issue1.error());

    auto const issue2 = getDataAsset(runtime, params, index);
    if (!issue2)
        return hfResult(results, issue2.error());

    return returnResult(
        runtime, params, results, hf.ammKeylet(issue1.value(), issue2.value()), index);
}

wasm_trap_t*
checkKeylet_wrap(WASM_SECONDARY_CB_PARAMS_LIST)
{
    int index = 0;
    WasmRuntimeWrapper& runtime = hf.getRT();

    auto const acc = getDataAccountID(runtime, params, index);
    if (!acc)
        return hfResult(results, acc.error());

    auto const seq = getDataUInt32(runtime, params, index);
    if (!seq)
        return hfResult(results, seq.error());

    return returnResult(runtime, params, results, hf.checkKeylet(acc.value(), *seq), index);
}

wasm_trap_t*
credentialKeylet_wrap(WASM_SECONDARY_CB_PARAMS_LIST)
{
    int index = 0;
    WasmRuntimeWrapper& runtime = hf.getRT();

    auto const subj = getDataAccountID(runtime, params, index);
    if (!subj)
        return hfResult(results, subj.error());

    auto const iss = getDataAccountID(runtime, params, index);
    if (!iss)
        return hfResult(results, iss.error());

    auto const credType = getDataSlice(runtime, params, index);
    if (!credType)
        return hfResult(results, credType.error());

    return returnResult(
        runtime, params, results, hf.credentialKeylet(*subj, *iss, *credType), index);
}

wasm_trap_t*
delegateKeylet_wrap(WASM_SECONDARY_CB_PARAMS_LIST)
{
    int index = 0;
    WasmRuntimeWrapper& runtime = hf.getRT();

    auto const acc = getDataAccountID(runtime, params, index);
    if (!acc)
        return hfResult(results, acc.error());

    auto const authorize = getDataAccountID(runtime, params, index);
    if (!authorize)
        return hfResult(results, authorize.error());

    return returnResult(
        runtime, params, results, hf.delegateKeylet(acc.value(), authorize.value()), index);
}

wasm_trap_t*
depositPreauthKeylet_wrap(WASM_SECONDARY_CB_PARAMS_LIST)
{
    int index = 0;
    WasmRuntimeWrapper& runtime = hf.getRT();

    auto const acc = getDataAccountID(runtime, params, index);
    if (!acc)
        return hfResult(results, acc.error());

    auto const authorize = getDataAccountID(runtime, params, index);
    if (!authorize)
        return hfResult(results, authorize.error());

    return returnResult(
        runtime, params, results, hf.depositPreauthKeylet(acc.value(), authorize.value()), index);
}

wasm_trap_t*
didKeylet_wrap(WASM_SECONDARY_CB_PARAMS_LIST)
{
    int index = 0;
    WasmRuntimeWrapper& runtime = hf.getRT();

    auto const acc = getDataAccountID(runtime, params, index);
    if (!acc)
        return hfResult(results, acc.error());

    return returnResult(runtime, params, results, hf.didKeylet(acc.value()), index);
}

wasm_trap_t*
escrowKeylet_wrap(WASM_SECONDARY_CB_PARAMS_LIST)
{
    int index = 0;
    WasmRuntimeWrapper& runtime = hf.getRT();

    auto const acc = getDataAccountID(runtime, params, index);
    if (!acc)
        return hfResult(results, acc.error());

    auto const seq = getDataUInt32(runtime, params, index);
    if (!seq)
        return hfResult(results, seq.error());

    return returnResult(runtime, params, results, hf.escrowKeylet(*acc, *seq), index);
}

wasm_trap_t*
trustLineKeylet_wrap(WASM_SECONDARY_CB_PARAMS_LIST)
{
    int index = 0;
    WasmRuntimeWrapper& runtime = hf.getRT();

    auto const acc1 = getDataAccountID(runtime, params, index);
    if (!acc1)
        return hfResult(results, acc1.error());

    auto const acc2 = getDataAccountID(runtime, params, index);
    if (!acc2)
        return hfResult(results, acc2.error());

    auto const currency = getDataCurrency(runtime, params, index);
    if (!currency)
        return hfResult(results, currency.error());

    return returnResult(
        runtime,
        params,
        results,
        hf.trustLineKeylet(acc1.value(), acc2.value(), currency.value()),
        index);
}

wasm_trap_t*
mptokenIssuanceKeylet_wrap(WASM_SECONDARY_CB_PARAMS_LIST)
{
    int index = 0;
    WasmRuntimeWrapper& runtime = hf.getRT();

    auto const acc = getDataAccountID(runtime, params, index);
    if (!acc)
        return hfResult(results, acc.error());

    auto const seq = getDataUInt32(runtime, params, index);
    if (!seq)
        return hfResult(results, seq.error());

    return returnResult(
        runtime, params, results, hf.mptokenIssuanceKeylet(acc.value(), seq.value()), index);
}

wasm_trap_t*
mptokenKeylet_wrap(WASM_SECONDARY_CB_PARAMS_LIST)
{
    int index = 0;
    WasmRuntimeWrapper& runtime = hf.getRT();

    auto const slice = getDataSlice(runtime, params, index);
    if (!slice)
        return hfResult(results, slice.error());

    if (slice->size() != MPTID::size())
        return hfResult(results, HostFunctionError::InvalidParams);
    auto const mptid = MPTID::fromVoid(slice->data());

    auto const holder = getDataAccountID(runtime, params, index);
    if (!holder)
        return hfResult(results, holder.error());

    return returnResult(runtime, params, results, hf.mptokenKeylet(mptid, holder.value()), index);
}

wasm_trap_t*
nftokenOfferKeylet_wrap(WASM_SECONDARY_CB_PARAMS_LIST)
{
    int index = 0;
    WasmRuntimeWrapper& runtime = hf.getRT();

    auto const acc = getDataAccountID(runtime, params, index);
    if (!acc)
        return hfResult(results, acc.error());

    auto const seq = getDataUInt32(runtime, params, index);
    if (!seq)
        return hfResult(results, seq.error());

    return returnResult(
        runtime, params, results, hf.nftokenOfferKeylet(acc.value(), seq.value()), index);
}

wasm_trap_t*
offerKeylet_wrap(WASM_SECONDARY_CB_PARAMS_LIST)
{
    int index = 0;
    WasmRuntimeWrapper& runtime = hf.getRT();

    auto const acc = getDataAccountID(runtime, params, index);
    if (!acc)
        return hfResult(results, acc.error());

    auto const seq = getDataUInt32(runtime, params, index);
    if (!seq)
        return hfResult(results, seq.error());

    return returnResult(runtime, params, results, hf.offerKeylet(acc.value(), seq.value()), index);
}

wasm_trap_t*
oracleKeylet_wrap(WASM_SECONDARY_CB_PARAMS_LIST)
{
    int index = 0;
    WasmRuntimeWrapper& runtime = hf.getRT();

    auto const acc = getDataAccountID(runtime, params, index);
    if (!acc)
        return hfResult(results, acc.error());

    auto const documentId = getDataUInt32(runtime, params, index);
    if (!documentId)
        return hfResult(results, documentId.error());

    return returnResult(runtime, params, results, hf.oracleKeylet(*acc, *documentId), index);
}

wasm_trap_t*
paychannelKeylet_wrap(WASM_SECONDARY_CB_PARAMS_LIST)
{
    int index = 0;
    WasmRuntimeWrapper& runtime = hf.getRT();

    auto const acc = getDataAccountID(runtime, params, index);
    if (!acc)
        return hfResult(results, acc.error());

    auto const dest = getDataAccountID(runtime, params, index);
    if (!dest)
        return hfResult(results, dest.error());

    auto const seq = getDataUInt32(runtime, params, index);
    if (!seq)
        return hfResult(results, seq.error());

    return returnResult(
        runtime,
        params,
        results,
        hf.paychannelKeylet(acc.value(), dest.value(), seq.value()),
        index);
}

wasm_trap_t*
permissionedDomainKeylet_wrap(WASM_SECONDARY_CB_PARAMS_LIST)
{
    int index = 0;
    WasmRuntimeWrapper& runtime = hf.getRT();

    auto const acc = getDataAccountID(runtime, params, index);
    if (!acc)
        return hfResult(results, acc.error());

    auto const seq = getDataUInt32(runtime, params, index);
    if (!seq)
        return hfResult(results, seq.error());

    return returnResult(
        runtime, params, results, hf.permissionedDomainKeylet(acc.value(), seq.value()), index);
}

wasm_trap_t*
signerListKeylet_wrap(WASM_SECONDARY_CB_PARAMS_LIST)
{
    int index = 0;
    WasmRuntimeWrapper& runtime = hf.getRT();

    auto const acc = getDataAccountID(runtime, params, index);
    if (!acc)
        return hfResult(results, acc.error());

    return returnResult(runtime, params, results, hf.signerListKeylet(acc.value()), index);
}

wasm_trap_t*
ticketKeylet_wrap(WASM_SECONDARY_CB_PARAMS_LIST)
{
    int index = 0;
    WasmRuntimeWrapper& runtime = hf.getRT();

    auto const acc = getDataAccountID(runtime, params, index);
    if (!acc)
        return hfResult(results, acc.error());

    auto const seq = getDataUInt32(runtime, params, index);
    if (!seq)
        return hfResult(results, seq.error());

    return returnResult(runtime, params, results, hf.ticketKeylet(acc.value(), seq.value()), index);
}

wasm_trap_t*
vaultKeylet_wrap(WASM_SECONDARY_CB_PARAMS_LIST)
{
    int index = 0;
    WasmRuntimeWrapper& runtime = hf.getRT();

    auto const acc = getDataAccountID(runtime, params, index);
    if (!acc)
        return hfResult(results, acc.error());

    auto const seq = getDataUInt32(runtime, params, index);
    if (!seq)
        return hfResult(results, seq.error());

    return returnResult(runtime, params, results, hf.vaultKeylet(acc.value(), seq.value()), index);
}

wasm_trap_t*
getNFT_wrap(WASM_SECONDARY_CB_PARAMS_LIST)
{
    int index = 0;
    WasmRuntimeWrapper& runtime = hf.getRT();

    auto const acc = getDataAccountID(runtime, params, index);
    if (!acc)
        return hfResult(results, acc.error());

    auto const nftId = getDataUInt256(runtime, params, index);
    if (!nftId)
        return hfResult(results, nftId.error());

    return returnResult(runtime, params, results, hf.getNFT(*acc, *nftId), index);
}

wasm_trap_t*
getNFTIssuer_wrap(WASM_SECONDARY_CB_PARAMS_LIST)
{
    int index = 0;
    WasmRuntimeWrapper& runtime = hf.getRT();

    auto const nftId = getDataUInt256(runtime, params, index);
    if (!nftId)
        return hfResult(results, nftId.error());

    return returnResult(runtime, params, results, hf.getNFTIssuer(*nftId), index);
}

wasm_trap_t*
getNFTTaxon_wrap(WASM_SECONDARY_CB_PARAMS_LIST)
{
    int index = 0;
    WasmRuntimeWrapper& runtime = hf.getRT();

    auto const nftId = getDataUInt256(runtime, params, index);
    if (!nftId)
        return hfResult(results, nftId.error());

    return returnResult(runtime, params, results, hf.getNFTTaxon(*nftId), index);
}

wasm_trap_t*
getNFTFlags_wrap(WASM_SECONDARY_CB_PARAMS_LIST)
{
    int index = 0;
    WasmRuntimeWrapper& runtime = hf.getRT();

    auto const nftId = getDataUInt256(runtime, params, index);
    if (!nftId)
        return hfResult(results, nftId.error());

    return returnResult(runtime, params, results, hf.getNFTFlags(*nftId), index);
}

wasm_trap_t*
getNFTTransferFee_wrap(WASM_SECONDARY_CB_PARAMS_LIST)
{
    int index = 0;
    WasmRuntimeWrapper& runtime = hf.getRT();

    auto const nftId = getDataUInt256(runtime, params, index);
    if (!nftId)
        return hfResult(results, nftId.error());

    return returnResult(runtime, params, results, hf.getNFTTransferFee(*nftId), index);
}

wasm_trap_t*
getNFTSequence_wrap(WASM_SECONDARY_CB_PARAMS_LIST)
{
    int index = 0;
    WasmRuntimeWrapper& runtime = hf.getRT();

    auto const nftId = getDataUInt256(runtime, params, index);
    if (!nftId)
        return hfResult(results, nftId.error());

    return returnResult(runtime, params, results, hf.getNFTSequence(*nftId), index);
}

// log() ignores the journal under DEBUG_OUTPUT, so the gate must not either.
static inline bool
traceActive([[maybe_unused]] HostFunctions const& hf)
{
#ifdef DEBUG_OUTPUT
    return true;
#else
    return hf.getJournal().active(beast::Severity::Trace);
#endif
}

// Not getDataUnsigned: that branches on pointer alignment, and trace must cost
// the same regardless of how the guest laid out its buffer.
template <class T>
static std::optional<T>
traceInt(Slice const& data)
{
    static_assert(std::is_integral_v<T>);
    if (data.size() != sizeof(T))
        return std::nullopt;

    T x;
    memcpy(&x, data.data(), sizeof(T));
    return adjustWasmEndianess(x);
}

// std::nullopt means the buffer does not match the type. May throw.
static std::optional<std::string>
traceFormat(TraceDataType type, Slice const& data)
{
    switch (type)
    {
        case TraceDataType::Int64:
            if (auto const x = traceInt<std::int64_t>(data))
                return std::to_string(*x);
            return std::nullopt;

        case TraceDataType::Uint64:
            if (auto const x = traceInt<std::uint64_t>(data))
                return std::to_string(*x);
            return std::nullopt;

        case TraceDataType::Xfloat:
            return wasm_float::floatToString(data);

        case TraceDataType::Account:
            // Not getDataAccountID: it charges the transfer limit.
            if (data.size() != AccountID::size())
                return std::nullopt;
            return toBase58(AccountID::fromVoid(data.data()));

        case TraceDataType::Amount: {
            auto serialIter = SerialIter(data);
            STAmount const amount(serialIter, sfGeneric);  // may throw
            return amount.getFullText();
        }

        case TraceDataType::AsHex: {
            std::string hex;
            hex.reserve(data.size() * 2);
            boost::algorithm::hex(data.begin(), data.end(), std::back_inserter(hex));
            return hex;
        }

        case TraceDataType::AsText:
            // An empty Slice has a null data(), which std::string may not take.
            if (data.empty())
                return std::string();
            return std::string(reinterpret_cast<char const*>(data.data()), data.size());
    }

    return std::nullopt;  // unknown data_type
}

// trace's only effect is this node's local log, so nothing observable may depend
// on the log level: gas is charged in mainCheck before this runs, no transfer
// limit is charged, and errors are logged rather than trapped.
wasm_trap_t*
trace_wrap(WASM_SECONDARY_CB_PARAMS_LIST)
{
    if (!traceActive(hf))
        return nullptr;

    try
    {
        int index = 0;
        WasmRuntimeWrapper& runtime = hf.getRT();
        auto const msg = getDataString(runtime, params, index);
        auto const type = getDataInt32(runtime, params, index);
        auto const data = getDataSlice(runtime, params, index);
        if (!msg || !type || !data || msg->size() + data->size() > kMaxWasmDataLength)
        {
            hf.getJournal().trace() << "WasmTrace: invalid arguments";
            return nullptr;
        }

        auto const text = traceFormat(static_cast<TraceDataType>(*type), *data);
        if (!text)
        {
            hf.getJournal().trace() << "WasmTrace: invalid arguments";
            return nullptr;
        }
        hf.trace(*msg, *text);
    }
    catch (std::exception const& e)
    {
        hf.getJournal().trace() << "WasmTrace: error: " << e.what();
    }
    return nullptr;
}

wasm_trap_t*
floatFromInt_wrap(WASM_SECONDARY_CB_PARAMS_LIST)
{
    int i = 0;
    WasmRuntimeWrapper& runtime = hf.getRT();

    auto const x = getDataInt64(runtime, params, i);
    if (!x)
        return hfResult(results, x.error());  // LCOV_EXCL_LINE

    i = 3;
    auto const rounding = getDataInt32(runtime, params, i);
    if (!rounding)
        return hfResult(results, rounding.error());  // LCOV_EXCL_LINE

    i = 1;
    return returnResult(runtime, params, results, hf.floatFromInt(*x, *rounding), i);
}

wasm_trap_t*
floatFromUint_wrap(WASM_SECONDARY_CB_PARAMS_LIST)
{
    int i = 0;
    WasmRuntimeWrapper& runtime = hf.getRT();

    auto const x = getDataUInt64(runtime, params, i);
    if (!x)
        return hfResult(results, x.error());

    i = 4;
    auto const rounding = getDataInt32(runtime, params, i);
    if (!rounding)
        return hfResult(results, rounding.error());  // LCOV_EXCL_LINE

    i = 2;
    return returnResult(runtime, params, results, hf.floatFromUint(*x, *rounding), i);
}

wasm_trap_t*
floatFromSTAmount_wrap(WASM_SECONDARY_CB_PARAMS_LIST)
{
    int i = 0;
    WasmRuntimeWrapper& runtime = hf.getRT();

    auto const x = getDataSlice(runtime, params, i);
    if (!x)
        return hfResult(results, x.error());

    auto serialIter = SerialIter(*x);
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
        return hfResult(results, HostFunctionError::InvalidParams);

    i = 4;
    auto const rounding = getDataInt32(runtime, params, i);
    if (!rounding)
        return hfResult(results, rounding.error());  // LCOV_EXCL_LINE

    i = 2;
    return returnResult(runtime, params, results, hf.floatFromSTAmount(*amount, *rounding), i);
}

wasm_trap_t*
floatFromSTNumber_wrap(WASM_SECONDARY_CB_PARAMS_LIST)
{
    int i = 0;
    WasmRuntimeWrapper& runtime = hf.getRT();

    auto const x = getDataSlice(runtime, params, i);
    if (!x)
        return hfResult(results, x.error());

    auto serialIter = SerialIter(*x);
    std::optional<STNumber> num;
    try
    {
        num = STNumber(serialIter, sfGeneric);
    }
    catch (std::exception const&)
    {
        num = std::nullopt;
    }
    if (!num)
        return hfResult(results, HostFunctionError::InvalidParams);

    i = 4;
    auto const rounding = getDataInt32(runtime, params, i);
    if (!rounding)
        return hfResult(results, rounding.error());  // LCOV_EXCL_LINE

    i = 2;
    return returnResult(runtime, params, results, hf.floatFromSTNumber(*num, *rounding), i);
}

wasm_trap_t*
floatToInt_wrap(WASM_SECONDARY_CB_PARAMS_LIST)
{
    int i = 0;
    WasmRuntimeWrapper& runtime = hf.getRT();

    auto const x = getDataSlice(runtime, params, i);
    if (!x)
        return hfResult(results, x.error());

    i = 4;
    auto const rounding = getDataInt32(runtime, params, i);
    if (!rounding)
        return hfResult(results, rounding.error());  // LCOV_EXCL_LINE

    i = 2;
    return returnResult(runtime, params, results, hf.floatToInt(*x, *rounding), i);
}

wasm_trap_t*
floatToMantExp_wrap(WASM_SECONDARY_CB_PARAMS_LIST)
{
    int i = 0;
    WasmRuntimeWrapper& runtime = hf.getRT();

    auto const x = getDataSlice(runtime, params, i);
    if (!x)
        return hfResult(results, x.error());

    i = 2;
    return returnResult(runtime, params, results, hf.floatToMantExp(*x), i);
}

wasm_trap_t*
floatFromMantExp_wrap(WASM_SECONDARY_CB_PARAMS_LIST)
{
    int i = 0;
    WasmRuntimeWrapper& runtime = hf.getRT();

    auto const mant = getDataInt64(runtime, params, i);
    if (!mant)
        return hfResult(results, mant.error());  // LCOV_EXCL_LINE

    auto const exp = getDataInt32(runtime, params, i);
    if (!exp)
        return hfResult(results, exp.error());  // LCOV_EXCL_LINE

    i = 4;
    auto const rounding = getDataInt32(runtime, params, i);
    if (!rounding)
        return hfResult(results, rounding.error());  // LCOV_EXCL_LINE

    i = 2;
    return returnResult(runtime, params, results, hf.floatFromMantExp(*mant, *exp, *rounding), i);
}

wasm_trap_t*
floatCompare_wrap(WASM_SECONDARY_CB_PARAMS_LIST)
{
    int i = 0;
    WasmRuntimeWrapper& runtime = hf.getRT();

    auto const x = getDataSlice(runtime, params, i);
    if (!x)
        return hfResult(results, x.error());

    auto const y = getDataSlice(runtime, params, i);
    if (!y)
        return hfResult(results, y.error());

    return returnResult(runtime, params, results, hf.floatCompare(*x, *y), i);
}

wasm_trap_t*
floatAdd_wrap(WASM_SECONDARY_CB_PARAMS_LIST)
{
    int i = 0;
    WasmRuntimeWrapper& runtime = hf.getRT();

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
    return returnResult(runtime, params, results, hf.floatAdd(*x, *y, *rounding), i);
}

wasm_trap_t*
floatSubtract_wrap(WASM_SECONDARY_CB_PARAMS_LIST)
{
    int i = 0;
    WasmRuntimeWrapper& runtime = hf.getRT();

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
    return returnResult(runtime, params, results, hf.floatSubtract(*x, *y, *rounding), i);
}

wasm_trap_t*
floatMultiply_wrap(WASM_SECONDARY_CB_PARAMS_LIST)
{
    int i = 0;
    WasmRuntimeWrapper& runtime = hf.getRT();

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
    return returnResult(runtime, params, results, hf.floatMultiply(*x, *y, *rounding), i);
}

wasm_trap_t*
floatDivide_wrap(WASM_SECONDARY_CB_PARAMS_LIST)
{
    int i = 0;
    WasmRuntimeWrapper& runtime = hf.getRT();

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
    return returnResult(runtime, params, results, hf.floatDivide(*x, *y, *rounding), i);
}

wasm_trap_t*
floatRoot_wrap(WASM_SECONDARY_CB_PARAMS_LIST)
{
    int i = 0;
    WasmRuntimeWrapper& runtime = hf.getRT();

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
    return returnResult(runtime, params, results, hf.floatRoot(*x, *n, *rounding), i);
}

wasm_trap_t*
floatPower_wrap(WASM_SECONDARY_CB_PARAMS_LIST)
{
    int i = 0;
    WasmRuntimeWrapper& runtime = hf.getRT();

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
    return returnResult(runtime, params, results, hf.floatPower(*x, *n, *rounding), i);
}

// LCOV_EXCL_START
namespace test {

class MockWasmRuntimeWrapper : public WasmRuntimeWrapper
{
    Wmem mem_;

    std::int64_t gas_ = 1'000'000;
    std::int64_t transferLimit_ = kWasmTransferLimit;

public:
    MockWasmRuntimeWrapper(Wmem memory) : mem_(memory)
    {
    }

    // Mock methods to simulate the behavior of WasmRuntimeWrapper
    [[nodiscard]] Wmem
    getMem() override
    {
        return mem_;
    }

    std::int64_t
    getGas() override
    {
        return gas_;
    }

    std::int64_t
    setGas(std::int64_t gas) override
    {
        gas_ = gas;
        return gas_;
    }

    std::int64_t
    getTransferLimit() override
    {
        return transferLimit_;
    }

    std::int64_t
    setTransferLimit(std::int64_t x) override
    {
        transferLimit_ = x;
        return transferLimit_;
    }
};

bool
testGetDataIncrement()
{
    wasm_val_t values[4];

    std::array<std::uint8_t, 128> buffer = {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h'};
    MockWasmRuntimeWrapper runtime(Wmem(buffer.data(), buffer.size()));

    {
        // test int32_t
        wasm_val_vec_t const params = {.size = 1, .data = &values[0]};

        values[0] = WASM_I32_VAL(42);

        int index = 0;
        auto const result = getDataInt32(runtime, &params, index);
        if (!result || result.value() != 42 || index != 1)
            return false;
    }

    {
        // test int64_t
        wasm_val_vec_t const params = {.size = 1, .data = &values[0]};

        values[0] = WASM_I64_VAL(1234);

        int index = 0;
        auto const result = getDataInt64(runtime, &params, index);
        if (!result || result.value() != 1234 || index != 1)
            return false;
    }

    {
        // test SFieldCRef
        wasm_val_vec_t const params = {.size = 1, .data = &values[0]};

        values[0] = WASM_I32_VAL(sfAccount.getCode());

        int index = 0;
        auto const result = getDataSField(runtime, &params, index);
        if (!result || result.value().get() != sfAccount || index != 1)
            return false;
    }

    {
        // test Slice
        wasm_val_vec_t const params = {.size = 2, .data = &values[0]};

        values[0] = WASM_I32_VAL(0);
        values[1] = WASM_I32_VAL(3);

        int index = 0;
        auto const result = getDataSlice(runtime, &params, index);
        if (!result || result.value() != Slice(buffer.data(), 3) || index != 2)
            return false;
    }

    {
        // test string
        wasm_val_vec_t const params = {.size = 2, .data = &values[0]};

        values[0] = WASM_I32_VAL(0);
        values[1] = WASM_I32_VAL(5);

        int index = 0;
        auto const result = getDataString(runtime, &params, index);
        if (!result ||
            result.value() != std::string_view(reinterpret_cast<char const*>(buffer.data()), 5) ||
            index != 2)
            return false;
    }

    {
        // test account
        AccountID const id(
            calcAccountID(generateKeyPair(KeyType::Secp256k1, generateSeed("alice")).first));

        wasm_val_vec_t const params = {.size = 2, .data = &values[0]};

        values[0] = WASM_I32_VAL(0);
        values[1] = WASM_I32_VAL(AccountID::size());
        memcpy(&buffer[0], id.data(), AccountID::size());

        int index = 0;
        auto const result = getDataAccountID(runtime, &params, index);
        if (!result || result.value() != id || index != 2)
            return false;
    }

    {
        // test uint256

        Hash h1 = sha512Half(Slice(buffer.data(), 8));
        wasm_val_vec_t const params = {.size = 2, .data = &values[0]};

        values[0] = WASM_I32_VAL(0);
        values[1] = WASM_I32_VAL(Hash::size());
        memcpy(&buffer[0], h1.data(), Hash::size());

        int index = 0;
        auto const result = getDataUInt256(runtime, &params, index);
        if (!result || result.value() != h1 || index != 2)
            return false;
    }

    {
        // test Currency

        Currency const c = xrpCurrency();
        wasm_val_vec_t const params = {.size = 2, .data = &values[0]};

        values[0] = WASM_I32_VAL(0);
        values[1] = WASM_I32_VAL(Currency::size());
        memcpy(&buffer[0], c.data(), Currency::size());

        int index = 0;
        auto const result = getDataCurrency(runtime, &params, index);
        if (!result || result.value() != c || index != 2)
            return false;
    }

    return true;
}

}  // namespace test
// LCOV_EXCL_STOP

}  // namespace xrpl
