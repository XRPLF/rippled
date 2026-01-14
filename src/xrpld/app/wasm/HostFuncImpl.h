#pragma once

#include <xrpld/app/tx/detail/ApplyContext.h>
#include <xrpld/app/wasm/HostFunc.h>

namespace xrpl {
class WasmHostFunctionsImpl : public HostFunctions
{
    ApplyContext& ctx;
    Keylet leKey;
    std::shared_ptr<SLE const> currentLedgerObj = nullptr;
    bool isLedgerObjCached = false;

    static int constexpr MAX_CACHE = 256;
    std::array<std::shared_ptr<SLE const>, MAX_CACHE> cache;
    std::optional<Bytes> data_;

    void const* rt_ = nullptr;

    Expected<std::shared_ptr<SLE const>, HostFunctionError>
    getCurrentLedgerObj()
    {
        if (!isLedgerObjCached)
        {
            isLedgerObjCached = true;
            currentLedgerObj = ctx.view().read(leKey);
        }
        if (currentLedgerObj)
            return currentLedgerObj;
        return Unexpected(HostFunctionError::LEDGER_OBJ_NOT_FOUND);
    }

    Expected<int32_t, HostFunctionError>
    normalizeCacheIndex(int32_t cacheIdx)
    {
        --cacheIdx;
        if (cacheIdx < 0 || cacheIdx >= MAX_CACHE)
            return Unexpected(HostFunctionError::SLOT_OUT_RANGE);
        if (!cache[cacheIdx])
            return Unexpected(HostFunctionError::EMPTY_SLOT);
        return cacheIdx;
    }

public:
    WasmHostFunctionsImpl(ApplyContext& ct, Keylet const& leKey)
        : HostFunctions(ct.journal), ctx(ct), leKey(leKey)
    {
    }

    virtual void
    setRT(void const* rt) override
    {
        rt_ = rt;
    }

    virtual void const*
    getRT() const override
    {
        return rt_;
    }

    std::optional<Bytes> const&
    getData() const
    {
        return data_;
    }

#define HOST_FUNCTION_BYTES_RETURN(NAME, ...) \
    Expected<Bytes, HostFunctionError> NAME(__VA_ARGS__) override;

#define HOST_FUNCTION_HASH_RETURN(NAME, ...) \
    Expected<Hash, HostFunctionError> NAME(__VA_ARGS__) override;

#define HOST_FUNCTION_NO_RETURN(NAME, ...) \
    Expected<int32_t, HostFunctionError> NAME(__VA_ARGS__) override;

#define HOST_FUNCTION_INT_RETURN(NAME, ...) \
    Expected<int32_t, HostFunctionError> NAME(__VA_ARGS__) override;

#define HOST_FUNCTION_UINT_RETURN(NAME, ...) \
    Expected<std::uint32_t, HostFunctionError> NAME(__VA_ARGS__) override;

#include <xrpld/app/wasm/host_functions.macro>

#undef HOST_FUNCTION_BYTES_RETURN
#undef HOST_FUNCTION_HASH_RETURN
#undef HOST_FUNCTION_NO_RETURN
#undef HOST_FUNCTION_INT_RETURN
#undef HOST_FUNCTION_UINT_RETURN
};

namespace wasm_float {

// The range for the mantissa and exponent when normalized
static std::int64_t constexpr minMantissa = STAmount::cMinValue;
static std::int64_t constexpr maxMantissa = STAmount::cMaxValue;
static int constexpr minExponent = STAmount::cMinOffset;
static int constexpr maxExponent = STAmount::cMaxOffset;

}  // namespace wasm_float

}  // namespace xrpl
