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

#include <xrpl/basics/Expected.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/TER.h>

#include <boost/function_types/function_arity.hpp>
#include <boost/function_types/parameter_types.hpp>
#include <boost/function_types/result_type.hpp>
#include <boost/mpl/vector.hpp>

// #include <iwasm/wasm_c_api.h>

#include <string_view>

namespace bft = boost::function_types;

namespace ripple {

static const std::string_view W_ENV = "env";
static const std::string_view W_HOST_LIB = "host_lib";
static const std::string_view W_MEM = "memory";
static const std::string_view W_STORE = "store";
static const std::string_view W_LOAD = "load";
static const std::string_view W_SIZE = "size";
static const std::string_view W_ALLOC = "allocate";
static const std::string_view W_DEALLOC = "deallocate";
static const std::string_view W_PROC_EXIT = "proc_exit";

using wbytes = std::vector<std::uint8_t>;
struct wmem
{
    std::uint8_t* p = nullptr;
    std::size_t s = 0;
};

const uint32_t MAX_PAGES = 128;  // 8MB = 64KB*128

typedef std::vector<uint8_t> Bytes;
typedef ripple::uint256 Hash;

template <typename T>
struct WasmResult
{
    T result;
    uint64_t cost;
};
typedef WasmResult<bool> EscrowResult;

struct HostFunctions
{
    virtual beast::Journal
    getJournal()
    {
        return beast::Journal{beast::Journal::getNullSink()};
    }

    virtual int32_t
    getLedgerSqn()
    {
        return 1;
    }

    virtual int32_t
    getParentLedgerTime()
    {
        return 1;
    }

    virtual std::optional<Bytes>
    getTxField(std::string const& fname)
    {
        return Bytes{};
    }

    virtual std::optional<Bytes>
    getLedgerEntryField(
        int32_t type,
        Bytes const& kdata,
        std::string const& fname)
    {
        return Bytes{};
    }

    virtual std::optional<Bytes>
    getCurrentLedgerEntryField(std::string const& fname)
    {
        return Bytes{};
    }

    virtual std::optional<Bytes>
    getNFT(std::string const& account, std::string const& nftId)
    {
        return Bytes{};
    }

    virtual bool
    updateData(Bytes const& data)
    {
        return true;
    }

    virtual Hash
    computeSha512HalfHash(Bytes const& data)
    {
        return Hash{};
    }

    virtual std::optional<Bytes>
    accountKeylet(std::string const& account)
    {
        return Bytes{};
    }

    virtual std::optional<Bytes>
    credentialKeylet(
        std::string const& subject,
        std::string const& issuer,
        std::string const& credentialType)
    {
        return Bytes{};
    }

    virtual std::optional<Bytes>
    escrowKeylet(std::string const& account, std::uint32_t const& seq)
    {
        return Bytes{};
    }

    virtual std::optional<Bytes>
    oracleKeylet(std::string const& account, std::uint32_t const& docId)
    {
        return Bytes{};
    }

    virtual ~HostFunctions() = default;
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

enum WasmTypes { WT_I32, WT_I64, WT_F32, WT_F64 };

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
        v, #f, reinterpret_cast<void*>(&f##_wrap), ##__VA_ARGS__);

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

struct HostFunctionsDummy : public HostFunctions
{
    HostFunctionsDummy()
    {
    }
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct WasmParam
{
    WasmTypes type = WT_I32;
    union
    {
        std::int32_t i32;
        std::int64_t i64 = 0;
        float f32;
        double f64;
    } of;
};

template <class... Types>
inline std::vector<WasmParam>
wasmParams(Types... args)
{
    std::vector<WasmParam> v;
    v.reserve(sizeof...(args));
    return wasmParams(v, std::forward<Types>(args)...);
}

template <class... Types>
inline std::vector<WasmParam>
wasmParams(std::vector<WasmParam>& v, std::int32_t p, Types... args)
{
    v.push_back({.type = WT_I32, .of = {.i32 = p}});
    return wasmParams(v, std::forward<Types>(args)...);
}

template <class... Types>
inline std::vector<WasmParam>
wasmParams(std::vector<WasmParam>& v, std::int64_t p, Types... args)
{
    v.push_back({.type = WT_I64, .of = {.i64 = p}});
    return wasmParams(v, std::forward<Types>(args)...);
}

template <class... Types>
inline std::vector<WasmParam>
wasmParams(std::vector<WasmParam>& v, float p, Types... args)
{
    v.push_back({.type = WT_F32, .of = {.f32 = p}});
    return wasmParams(v, std::forward<Types>(args)...);
}

template <class... Types>
inline std::vector<WasmParam>
wasmParams(std::vector<WasmParam>& v, double p, Types... args)
{
    v.push_back({.type = WT_F64, .of = {.f64 = p}});
    return wasmParams(v, std::forward<Types>(args)...);
}

inline std::vector<WasmParam>
wasmParams(std::vector<WasmParam>& v)
{
    return v;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class WamrEngine;
class WasmEngine
{
    std::unique_ptr<WamrEngine> const impl;

    WasmEngine();

    WasmEngine(WasmEngine const&) = delete;
    WasmEngine(WasmEngine&&) = delete;
    WasmEngine&
    operator=(WasmEngine const&) = delete;
    WasmEngine&
    operator=(WasmEngine&&) = delete;

public:
    ~WasmEngine() = default;

    static WasmEngine&
    instance();

    Expected<int32_t, NotTEC>
    preflight(
        wbytes const& wasmCode,
        std::string_view funcName = {},
        std::vector<WasmImportFunc> const& imports = {},
        std::vector<WasmParam> const& params = {});

    Expected<int32_t, TER>
    run(wbytes const& wasmCode,
        std::string_view funcName = {},
        std::vector<WasmImportFunc> const& imports = {},
        std::vector<WasmParam> const& params = {},
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()});

    std::int64_t
    initGas(std::int64_t def = 1'000'000'000'000LL);

    std::int32_t
    initMaxPages(std::int32_t def);

    std::int64_t
    setGas(std::int64_t gas);

    std::int64_t
    getGas();

    // for host functions usage
    wmem
    getMem() const;

    int32_t
    allocate(int32_t size);

    void*
    newTrap(std::string_view msg = {});
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

NotTEC
preflightEscrowWasm(
    Bytes const& wasmCode,
    std::string_view funcName,
    HostFunctions* hfs,
    uint64_t gasLimit);

Expected<EscrowResult, TER>
runEscrowWasm(
    Bytes const& wasmCode,
    std::string_view funcName,
    HostFunctions* hfs,
    uint64_t gasLimit);

}  // namespace ripple
