//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2025 Ripple Labs Inc.

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

namespace ripple {

using Bytes = std::vector<std::uint8_t>;
using Hash = ripple::uint256;

struct wmem
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
typedef WasmResult<bool> EscrowResult;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

enum WasmTypes { WT_I32, WT_I64, WT_F32, WT_F64, WT_U8V };

struct WasmImportFunc
{
    std::string name;
    std::optional<WasmTypes> result;
    std::vector<WasmTypes> params;
    void* udata = nullptr;
    // wasm_func_callback_with_env_t
    void* wrap = nullptr;
};

#define WASM_IMPORT_FUNC(v, f, ...) \
    WasmImpFunc<f##_proto>(         \
        v, #f, reinterpret_cast<void*>(&f##_wrap), ##__VA_ARGS__)

#define WASM_IMPORT_FUNC2(v, f, n, ...) \
    WasmImpFunc<f##_proto>(             \
        v, n, reinterpret_cast<void*>(&f##_wrap), ##__VA_ARGS__)

template <int N, int C, typename mpl>
void
WasmImpArgs(WasmImportFunc& e)
{
    if constexpr (N < C)
    {
        using at = typename boost::mpl::at_c<mpl, N>::type;
        if constexpr (std::is_pointer_v<at>)
            e.params.push_back(WT_I32);
        else if constexpr (std::is_same_v<at, std::int32_t>)
            e.params.push_back(WT_I32);
        else if constexpr (std::is_same_v<at, std::int64_t>)
            e.params.push_back(WT_I64);
        else if constexpr (std::is_same_v<at, float>)
            e.params.push_back(WT_F32);
        else if constexpr (std::is_same_v<at, double>)
            e.params.push_back(WT_F64);
        else
            static_assert(std::is_pointer_v<at>, "Unsupported argument type");

        return WasmImpArgs<N + 1, C, mpl>(e);
    }
    return;
}

template <typename rt>
void
WasmImpRet(WasmImportFunc& e)
{
    if constexpr (std::is_pointer_v<rt>)
        e.result = WT_I32;
    else if constexpr (std::is_same_v<rt, std::int32_t>)
        e.result = WT_I32;
    else if constexpr (std::is_same_v<rt, std::int64_t>)
        e.result = WT_I64;
    else if constexpr (std::is_same_v<rt, float>)
        e.result = WT_F32;
    else if constexpr (std::is_same_v<rt, double>)
        e.result = WT_F64;
    else if constexpr (std::is_void_v<rt>)
        e.result.reset();
#if (defined(__GNUC__) && (__GNUC__ >= 14)) || \
    ((defined(__clang_major__)) && (__clang_major__ >= 18))
    else
        static_assert(false, "Unsupported return type");
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

template <typename F>
void
WasmImpFunc(
    std::vector<WasmImportFunc>& v,
    std::string_view imp_name,
    void* f_wrap,
    void* data = nullptr)
{
    WasmImportFunc e;
    e.name = imp_name;
    e.udata = data;
    e.wrap = f_wrap;
    WasmImpFuncHelper<F>(e);
    v.push_back(std::move(e));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct WasmParamVec
{
    std::uint8_t const* d = nullptr;
    std::int32_t sz = 0;
};

struct WasmParam
{
    WasmTypes type = WT_I32;
    union
    {
        std::int32_t i32;
        std::int64_t i64 = 0;
        float f32;
        double f64;
        WasmParamVec u8v;
    } of;
};

template <class... Types>
inline void
wasmParamsHlp(std::vector<WasmParam>& v, std::int32_t p, Types&&... args)
{
    v.push_back({.type = WT_I32, .of = {.i32 = p}});
    wasmParamsHlp(v, std::forward<Types>(args)...);
}

template <class... Types>
inline void
wasmParamsHlp(std::vector<WasmParam>& v, std::int64_t p, Types&&... args)
{
    v.push_back({.type = WT_I64, .of = {.i64 = p}});
    wasmParamsHlp(v, std::forward<Types>(args)...);
}

template <class... Types>
inline void
wasmParamsHlp(std::vector<WasmParam>& v, float p, Types&&... args)
{
    v.push_back({.type = WT_F32, .of = {.f32 = p}});
    wasmParamsHlp(v, std::forward<Types>(args)...);
}

template <class... Types>
inline void
wasmParamsHlp(std::vector<WasmParam>& v, double p, Types&&... args)
{
    v.push_back({.type = WT_F64, .of = {.f64 = p}});
    wasmParamsHlp(v, std::forward<Types>(args)...);
}

template <class... Types>
inline void
wasmParamsHlp(
    std::vector<WasmParam>& v,
    std::uint8_t const* dt,
    std::int32_t sz,
    Types&&... args)
{
    v.push_back({.type = WT_U8V, .of = {.u8v = {.d = dt, .sz = sz}}});
    wasmParamsHlp(v, std::forward<Types>(args)...);
}

template <class... Types>
inline void
wasmParamsHlp(std::vector<WasmParam>& v, Bytes const& p, Types&&... args)
{
    wasmParamsHlp(
        v,
        p.data(),
        static_cast<std::int32_t>(p.size()),
        std::forward<Types>(args)...);
}

template <class... Types>
inline void
wasmParamsHlp(
    std::vector<WasmParam>& v,
    std::string_view const& p,
    Types&&... args)
{
    wasmParamsHlp(
        v,
        reinterpret_cast<std::uint8_t const*>(p.data()),
        static_cast<std::int32_t>(p.size()),
        std::forward<Types>(args)...);
}

template <class... Types>
inline void
wasmParamsHlp(std::vector<WasmParam>& v, std::string const& p, Types&&... args)
{
    wasmParamsHlp(
        v,
        reinterpret_cast<std::uint8_t const*>(p.c_str()),
        static_cast<std::int32_t>(p.size()),
        std::forward<Types>(args)...);
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

}  // namespace ripple
