#pragma once

#include <xrpld/app/wasm/HostFunc.h>

#include <xrpl/tx/ApplyContext.h>

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

    template <typename F>
    void
    log(std::string_view const& msg, F&& dataFn)
    {
#ifdef DEBUG_OUTPUT
        auto& j = std::cerr;
#else
        if (!getJournal().active(beast::severities::kTrace))
            return;
        auto j = getJournal().trace();
#endif
        j << "WasmTrace[" << to_short_string(leKey.key) << "]: " << msg << " " << dataFn();

#ifdef DEBUG_OUTPUT
        j << std::endl;
#endif
    }

public:
    WasmHostFunctionsImpl(ApplyContext& ct, Keylet const& leKey) : HostFunctions(ct.journal), ctx(ct), leKey(leKey)
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

#pragma push_macro("HOST_FUNCTION_BYTES_RETURN")
#pragma push_macro("HOST_FUNCTION_HASH_RETURN")
#pragma push_macro("HOST_FUNCTION_NO_RETURN")
#pragma push_macro("HOST_FUNCTION_INT_RETURN")
#pragma push_macro("HOST_FUNCTION_UINT_RETURN")

#define HOST_FUNCTION_BYTES_RETURN(NAME, ...) Expected<Bytes, HostFunctionError> NAME(__VA_ARGS__) override;

#define HOST_FUNCTION_HASH_RETURN(NAME, ...) Expected<Hash, HostFunctionError> NAME(__VA_ARGS__) override;

#define HOST_FUNCTION_NO_RETURN(NAME, ...) Expected<int32_t, HostFunctionError> NAME(__VA_ARGS__) override;

#define HOST_FUNCTION_INT_RETURN(NAME, ...) Expected<int32_t, HostFunctionError> NAME(__VA_ARGS__) override;

#define HOST_FUNCTION_UINT_RETURN(NAME, ...) Expected<std::uint32_t, HostFunctionError> NAME(__VA_ARGS__) override;

#include <xrpld/app/wasm/host_functions.macro>

#pragma pop_macro("HOST_FUNCTION_UINT_RETURN")
#pragma pop_macro("HOST_FUNCTION_INT_RETURN")
#pragma pop_macro("HOST_FUNCTION_NO_RETURN")
#pragma pop_macro("HOST_FUNCTION_HASH_RETURN")
#pragma pop_macro("HOST_FUNCTION_BYTES_RETURN")
};

namespace wasm_float {

// The range for the mantissa and exponent when normalized
static std::int64_t constexpr wasmMinMantissa = 1'000'000'000'000'000ll;
static std::int64_t constexpr wasmMaxMantissa = wasmMinMantissa * 10 - 1;
static int constexpr wasmMinExponent = -96;
static int constexpr wasmMaxExponent = 80;

}  // namespace wasm_float

}  // namespace xrpl
