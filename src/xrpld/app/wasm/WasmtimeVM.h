//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2023 Ripple Labs Inc.

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

#include <xrpld/app/wasm/WasmVM.h>

#define WASMTIME_FEATURE_COMPILER 1

#include <wasm.h>
#include <wasmtime.h>

namespace ripple {

struct WasmtimeResult
{
    std::vector<wasmtime_val_t> r;
    bool f;  // failure flag

    WasmtimeResult(unsigned N = 0) : f(false)
    {
        if (N)
            r.resize(N);
    }

    ~WasmtimeResult() = default;

    WasmtimeResult(WasmtimeResult const&) = delete;
    WasmtimeResult&
    operator=(WasmtimeResult const&) = delete;

    WasmtimeResult(WasmtimeResult&& o)
    {
        *this = std::move(o);
    }

    WasmtimeResult&
    operator=(WasmtimeResult&& o)
    {
        r = std::move(o.r);
        f = o.f;
        o.f = false;
        return *this;
    }
    // operator wasm_val_vec_t &() {return r;}
};

using ModulePtr =
    std::unique_ptr<wasmtime_module_t, decltype(&wasmtime_module_delete)>;
using InstancePtr = wasmtime_instance_t;

using FuncInfo = std::pair<wasmtime_func_t, wasm_functype_t const*>;

struct InstanceWrapper
{
    wasmtime_context_t* ctx_ = nullptr;
    // wasm_extern_vec_t exports_;
    InstancePtr instance_ = {0, 0};

    beast::Journal j_ = beast::Journal(beast::Journal::getNullSink());

private:
    static void
    checkImport(
        std::vector<wasmtime_extern_t>& out,
        wasmtime_context_t* ct,
        wasmtime_module_t* m,
        std::vector<wasmtime_extern_t> const& in);

    static InstancePtr
    init(
        wasmtime_context_t* c,
        wasmtime_module_t* m,
        int32_t maxPages,
        wasm_extern_vec_t* expt,
        std::vector<wasmtime_extern_t> const& imports,
        beast::Journal j);

public:
    InstanceWrapper() = default;

    InstanceWrapper(InstanceWrapper&& o);

    InstanceWrapper&
    operator=(InstanceWrapper&& o);

    InstanceWrapper(
        wasmtime_context_t* c,
        wasmtime_module_t* m,
        int32_t maxPages,
        int64_t gas,
        std::vector<wasmtime_extern_t> const& imports,
        beast::Journal j);

    ~InstanceWrapper();

    operator bool() const;

    FuncInfo
    getFunc(std::string_view funcName) const;

    wmem
    getMem() const;

    std::int64_t
    getGas() const;
};

struct ModuleWrapper
{
    wasmtime_context_t* ctx_ = nullptr;

    ModulePtr module_;
    InstanceWrapper instanceWrap_;
    // wasm_exporttype_vec_t exportTypes_;

    beast::Journal j_ = beast::Journal(beast::Journal::getNullSink());

private:
    static ModulePtr
    init(wasm_engine_t* e, Bytes const& wasmBin, beast::Journal j);

public:
    ModuleWrapper();
    ModuleWrapper(ModuleWrapper&& o);
    ModuleWrapper&
    operator=(ModuleWrapper&& o);
    ModuleWrapper(
        wasm_engine_t* e,
        wasmtime_context_t* c,
        Bytes const& wasmBin,
        bool instantiate,
        int32_t maxPages,
        int64_t gas,
        std::vector<WasmImportFunc> const& imports,
        beast::Journal j);
    ~ModuleWrapper() = default;

    operator bool() const;

    FuncInfo
    getFunc(std::string_view funcName) const;
    wmem
    getMem() const;

    InstanceWrapper const&
    getInstance(int i = 0) const;

    int
    addInstance(
        int32_t maxPages,
        int64_t gas,
        std::vector<wasmtime_extern_t> const& imports = {});

    std::int64_t
    getGas();

private:
    static void
    makeImpParams(wasm_valtype_vec_t& v, WasmImportFunc const& imp);
    static void
    makeImpReturn(wasm_valtype_vec_t& v, WasmImportFunc const& imp);
    std::vector<wasmtime_extern_t>
    buildImports(std::vector<WasmImportFunc> const& imports);
};

class WasmtimeEngine
{
    std::unique_ptr<wasm_engine_t, decltype(&wasm_engine_delete)> engine_;
    std::unique_ptr<wasmtime_store_t, decltype(&wasmtime_store_delete)> store_;
    wasmtime_context_t* ctx_ = nullptr;
    std::unique_ptr<ModuleWrapper> moduleWrap_;

    std::int32_t defMaxPages_ = -1;
    beast::Journal j_ = beast::Journal(beast::Journal::getNullSink());

    std::mutex m_;  // 1 instance mutex

public:
    WasmtimeEngine();
    ~WasmtimeEngine() = default;

    Expected<WasmResult<int32_t>, TER>
    run(Bytes const& wasmCode,
        std::string_view funcName,
        std::vector<WasmParam> const& params,
        std::vector<WasmImportFunc> const& imports,
        HostFunctions* hfs,
        int64_t gas,
        beast::Journal j);

    NotTEC
    check(
        Bytes const& wasmCode,
        std::string_view funcName,
        std::vector<WasmParam> const& params,
        std::vector<WasmImportFunc> const& imports,
        beast::Journal j);

    std::int32_t
    initMaxPages(std::int32_t def);

    std::int64_t
    getGas();

    // Host functions helper functionality
    wasm_trap_t*
    newTrap(std::string_view msg);

    beast::Journal
    getJournal() const;

private:
    static wasm_engine_t*
    init(beast::Journal j);

    InstanceWrapper const&
    getRT(int m = 0, int i = 0);

    wmem
    getMem() const;

    int32_t
    allocate(int32_t size);

    Expected<WasmResult<int32_t>, TER>
    runHlp(
        Bytes const& wasmCode,
        std::string_view funcName,
        std::vector<WasmParam> const& params,
        std::vector<WasmImportFunc> const& imports,
        HostFunctions* hfs,
        int64_t gas);

    NotTEC
    checkHlp(
        Bytes const& wasmCode,
        std::string_view funcName,
        std::vector<WasmParam> const& params,
        std::vector<WasmImportFunc> const& imports);

    int
    addModule(
        Bytes const& wasmCode,
        bool instantiate,
        int64_t gas,
        std::vector<WasmImportFunc> const& imports);
    void
    clearModules();

    // int  addInstance();

    int32_t
    runFunc(std::string_view const funcName, int32_t p);

    static bool
    setInterp(wasm_config_t* c, beast::Journal j);

    int32_t
    makeModule(
        Bytes const& wasmCode,
        wasm_extern_vec_t const& imports = WASM_EMPTY_VEC);

    FuncInfo
    getFunc(std::string_view funcName);

    std::vector<wasmtime_val_t>
    convertParams(std::vector<WasmParam> const& params);

    static int
    compareParamTypes(
        wasm_valtype_vec_t const* ftp,
        std::vector<wasmtime_val_t> const& p);

    static void
    add_param(std::vector<wasmtime_val_t>& in, int32_t p);
    static void
    add_param(std::vector<wasmtime_val_t>& in, int64_t p);

    template <int NR, class... Types>
    inline WasmtimeResult
    call(std::string_view func, Types&&... args);

    template <int NR, class... Types>
    inline WasmtimeResult
    call(FuncInfo const& f, Types&&... args);

    template <int NR, class... Types>
    inline WasmtimeResult
    call(FuncInfo const& f, std::vector<wasmtime_val_t>& in);

    template <int NR, class... Types>
    inline WasmtimeResult
    call(
        FuncInfo const& f,
        std::vector<wasmtime_val_t>& in,
        std::int32_t p,
        Types&&... args);

    template <int NR, class... Types>
    inline WasmtimeResult
    call(
        FuncInfo const& f,
        std::vector<wasmtime_val_t>& in,
        std::int64_t p,
        Types&&... args);

    template <int NR, class... Types>
    inline WasmtimeResult
    call(
        FuncInfo const& f,
        std::vector<wasmtime_val_t>& in,
        uint8_t const* d,
        std::size_t sz,
        Types&&... args);

    template <int NR, class... Types>
    inline WasmtimeResult
    call(
        FuncInfo const& f,
        std::vector<wasmtime_val_t>& in,
        Bytes const& p,
        Types&&... args);
};

}  // namespace ripple
