#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/tx/wasm/HostFunc.h>

#include <boost/function_types/function_arity.hpp>
#include <boost/function_types/parameter_types.hpp>
#include <boost/function_types/result_type.hpp>
#include <boost/mpl/vector.hpp>

#include <wasm.h>

namespace bft = boost::function_types;

namespace xrpl {

using wasmSecondaryCbFuncType =
    wasm_trap_t*(HostFunctions&, wasm_val_vec_t const*, wasm_val_vec_t*);

struct WasmImportFunc
{
    std::string_view name;
    std::optional<WasmTypes> result;
    std::vector<WasmTypes> params;

    wasmSecondaryCbFuncType* wrap = nullptr;
    uint32_t gas = 0;
};

using WasmUserData = std::pair<HFRef, WasmImportFunc>;
// string - import function name
using ImportVec = std::unordered_map<std::string_view, WasmUserData>;

template <int N, int C, typename Mpl>
void
WasmImpArgs(WasmImportFunc& e)
{
    if constexpr (N < C)
    {
        using at = typename boost::mpl::at_c<Mpl, N>::type;
        if constexpr (std::is_pointer_v<at>)
        {
            e.params.push_back(WasmTypes::WtI32);
        }
        else if constexpr (std::is_same_v<at, std::int32_t>)
        {
            e.params.push_back(WasmTypes::WtI32);
        }
        else if constexpr (std::is_same_v<at, std::int64_t>)
        {
            e.params.push_back(WasmTypes::WtI64);
        }
        else
        {
            static_assert(std::is_pointer_v<at>, "Unsupported argument type");
        }

        return WasmImpArgs<N + 1, C, Mpl>(e);
    }
    return;
}

template <typename>
inline constexpr bool wasmDependentFalse = false;

template <typename Rt>
void
WasmImpRet(WasmImportFunc& e)
{
    if constexpr (std::is_pointer_v<Rt>)
    {
        e.result = WasmTypes::WtI32;
    }
    else if constexpr (std::is_same_v<Rt, std::int32_t>)
    {
        e.result = WasmTypes::WtI32;
    }
    else if constexpr (std::is_same_v<Rt, std::int64_t>)
    {
        e.result = WasmTypes::WtI64;
    }
    else if constexpr (std::is_void_v<Rt>)
    {
        e.result.reset();
    }
    else
    {
        static_assert(wasmDependentFalse<Rt>, "Unsupported return type");
    }
}

template <typename F>
void
WasmImpFuncHelper(WasmImportFunc& e)
{
    using rt = typename bft::result_type<F>::type;
    using pt = typename bft::parameter_types<F>::type;
    // typename boost::mpl::at_c<mpl, N>::type

    WasmImpRet<rt>(e);
    WasmImpArgs<0, bft::function_arity<F>::value, pt>(e);
    // WasmImpWrap(e, std::forward<F>(f));
}

// imp_name - string literal, must have static lifetime
template <typename F>
void
WasmImpFunc(
    ImportVec& v,
    std::string_view impName,
    wasmSecondaryCbFuncType* fWrap,
    HostFunctions& hf,
    uint32_t gas = 0)
{
    WasmImportFunc e;
    e.name = impName;
    e.wrap = fWrap;
    e.gas = gas;
    WasmImpFuncHelper<F>(e);
    v.emplace(impName, std::make_pair(HFRef(hf), std::move(e)));
}

#define WASM_IMPORT_FUNC(v, f, ...) WasmImpFunc<f##_proto>(v, #f, &f##_wrap, ##__VA_ARGS__)

// n - string literal name, must have static lifetime
#define WASM_IMPORT_FUNC2(v, f, n, ...) WasmImpFunc<f##_proto>(v, n, &f##_wrap, ##__VA_ARGS__)

}  // namespace xrpl
