#pragma once

#include <xrpl/basics/base_uint.h>

#include <boost/function_types/function_arity.hpp>
#include <boost/function_types/parameter_types.hpp>
#include <boost/function_types/result_type.hpp>
#include <boost/mpl/vector.hpp>

#include <optional>
#include <string>
#include <vector>

namespace bft = boost::function_types;

namespace xrpl {

using Bytes = std::vector<std::uint8_t>;
using Hash = xrpl::uint256;

struct Wmem
{
    std::uint8_t* p = nullptr;
    std::size_t s = 0;
};

template <typename T>
struct WasmResult
{
    T result;
    int64_t cost;
};
using EscrowResult = WasmResult<int32_t>;

struct WasmRuntimeWrapper
{
    virtual ~WasmRuntimeWrapper() = default;

    virtual Wmem
    getMem() = 0;

    virtual std::int64_t
    getGas() = 0;

    virtual std::int64_t
    setGas(std::int64_t gas) = 0;
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

enum WasmTypes { WtI32, WtI64 };

struct WasmImportFunc
{
    std::string_view name;
    std::optional<WasmTypes> result;
    std::vector<WasmTypes> params;
    // void* udata = nullptr;
    // wasm_func_callback_with_env_t
    void* wrap = nullptr;
    uint32_t gas = 0;
};

using WasmUserData = std::pair<void*, WasmImportFunc>;
using ImportVec = std::unordered_map<std::string_view, WasmUserData>;

#define WASM_IMPORT_FUNC(v, f, ...) \
    WasmImpFunc<f##_proto>(v, #f, reinterpret_cast<void*>(&f##_wrap), ##__VA_ARGS__)

// n - string literal name, must have static lifetime
#define WASM_IMPORT_FUNC2(v, f, n, ...) \
    WasmImpFunc<f##_proto>(v, n, reinterpret_cast<void*>(&f##_wrap), ##__VA_ARGS__)

template <int N, int C, typename Mpl>
void
WasmImpArgs(WasmImportFunc& e)
{
    if constexpr (N < C)
    {
        using at = typename boost::mpl::at_c<Mpl, N>::type;
        if constexpr (std::is_pointer_v<at>)
        {
            e.params.push_back(WtI32);
        }
        else if constexpr (std::is_same_v<at, std::int32_t>)
        {
            e.params.push_back(WtI32);
        }
        else if constexpr (std::is_same_v<at, std::int64_t>)
        {
            e.params.push_back(WtI64);
        }
        else
        {
            static_assert(std::is_pointer_v<at>, "Unsupported argument type");
        }

        return WasmImpArgs<N + 1, C, Mpl>(e);
    }
    return;
}

template <typename Rt>
void
WasmImpRet(WasmImportFunc& e)
{
    if constexpr (std::is_pointer_v<Rt>)
    {
        e.result = WtI32;
    }
    else if constexpr (std::is_same_v<Rt, std::int32_t>)
    {
        e.result = WtI32;
    }
    else if constexpr (std::is_same_v<Rt, std::int64_t>)
    {
        e.result = WtI64;
    }
    else if constexpr (std::is_void_v<Rt>)
    {
        e.result.reset();
#if (defined(__GNUC__) && (__GNUC__ >= 14)) || \
    ((defined(__clang_major__)) && (__clang_major__ >= 18))
    }
    else
    {
        static_assert(false, "Unsupported return type");
    }
#endif
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
    void* fWrap,
    void* data = nullptr,
    uint32_t gas = 0)
{
    WasmImportFunc e;
    e.name = impName;
    e.wrap = fWrap;
    e.gas = gas;
    WasmImpFuncHelper<F>(e);
    v.emplace(impName, std::make_pair(data, std::move(e)));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct WasmParam
{
    // We are not supporting float/double

    WasmTypes type = WtI32;
    union
    {
        std::int32_t i32;
        std::int64_t i64 = 0;
    } of;
};

template <class... Types>
inline void
wasmParamsHlp(std::vector<WasmParam>& v, std::int32_t p, Types&&... args)
{
    v.push_back({.type = WtI32, .of = {.i32 = p}});
    wasmParamsHlp(v, std::forward<Types>(args)...);
}

template <class... Types>
inline void
wasmParamsHlp(std::vector<WasmParam>& v, std::int64_t p, Types&&... args)
{
    v.push_back({.type = WtI64, .of = {.i64 = p}});
    wasmParamsHlp(v, std::forward<Types>(args)...);
}

inline void
wasmParamsHlp(std::vector<WasmParam>& v)
{
    return;
}

template <class... Types>
inline std::vector<WasmParam>
wasmParams(Types&&... args)
{
    std::vector<WasmParam> v;
    v.reserve(sizeof...(args));
    wasmParamsHlp(v, std::forward<Types>(args)...);
    return v;
}

template <typename T, size_t Size = sizeof(T)>
constexpr T
adjustWasmEndianessHlp(T x)
{
    static_assert(std::is_integral_v<T>, "Only integral types");
    if constexpr (Size > 1)
    {
        using U = std::make_unsigned_t<T>;
        U u = static_cast<U>(x);
        U const low = (u & 0xFF) << ((Size - 1) << 3);
        u = adjustWasmEndianessHlp<U, Size - 1>(u >> 8);
        return static_cast<T>(low | u);
    }

    return x;
}

template <typename T, size_t Size = sizeof(T)>
constexpr T
adjustWasmEndianess(T x)
{
    // LCOV_EXCL_START
    static_assert(std::is_integral_v<T>, "Only integral types");
    if constexpr (std::endian::native == std::endian::big)
    {
        return adjustWasmEndianessHlp(x);
    }
    return x;
    // LCOV_EXCL_STOP
}

}  // namespace xrpl
