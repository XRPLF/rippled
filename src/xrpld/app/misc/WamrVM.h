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

#include <xrpld/app/misc/WasmVM.h>

#include <xrpl/beast/utility/Journal.h>

#include <iwasm/wasm_c_api.h>
#include <iwasm/wasm_export.h>

namespace ripple {

// clang-format off

struct WamrResult
{
    wasm_val_vec_t r;
    WamrResult(unsigned N = 0):r{0, nullptr, 0, 0, nullptr} {if (N) wasm_val_vec_new_uninitialized(&r, N);}
    ~WamrResult() { if (r.size) wasm_val_vec_delete(&r); }
    WamrResult(WamrResult const &) = delete;
    WamrResult& operator=(WamrResult const &) = delete;

    WamrResult(WamrResult &&o) {*this = std::move(o);}
    WamrResult& operator=(WamrResult  &&o){r = o.r; o.r = {0, nullptr, 0, 0, nullptr}; return *this;}
    //operator wasm_val_vec_t &() {return r;}
};

using ModulePtr = std::unique_ptr<wasm_module_t, decltype(&wasm_module_delete)>;
using InstancePtr = std::unique_ptr<wasm_instance_t, decltype(&wasm_instance_delete)>;

// clang-format on

struct InstanceWrapper
{
    wasm_extern_vec_t exports;
    InstancePtr mod_inst;

private:
    static InstancePtr
    init(
        wasm_store_t* s,
        wasm_module_t* m,
        int32_t maxPages,
        wasm_extern_vec_t* expt,
        wasm_extern_vec_t const& imports = WASM_EMPTY_VEC);

public:
    InstanceWrapper();

    InstanceWrapper(InstanceWrapper&& o);

    InstanceWrapper&
    operator=(InstanceWrapper&& o);

    InstanceWrapper(
        wasm_store_t* s,
        wasm_module_t* m,
        int32_t maxPages,
        wasm_extern_vec_t const& imports = WASM_EMPTY_VEC);

    ~InstanceWrapper();

    operator bool() const;

    wasm_func_t*
    getFunc(
        std::string_view funcName,
        wasm_exporttype_vec_t const& export_types) const;

    wmem
    getMem() const;
};

struct ModuleWrapper
{
    ModulePtr module;
    wasm_exec_env_t exec_env = nullptr;
    InstanceWrapper mod_inst;
    wasm_exporttype_vec_t export_types;

private:
    static ModulePtr
    init(wasm_store_t* s, wbytes const& wasmBin);

public:
    ModuleWrapper();
    ModuleWrapper(ModuleWrapper&& o);
    ModuleWrapper&
    operator=(ModuleWrapper&& o);
    ModuleWrapper(
        wasm_store_t* s,
        wbytes const& wasmBin,
        bool instantiate,
        int32_t maxPages,
        std::vector<WasmImportFunc> const& imports = {});
    ~ModuleWrapper();

    operator bool() const;

    wasm_func_t*
    getFunc(std::string_view funcName) const;
    wmem
    getMem() const;

    int
    addInstance(
        wasm_store_t* s,
        int32_t maxPages,
        wasm_extern_vec_t const& imports = WASM_EMPTY_VEC);

    std::int64_t
    setGas(std::int64_t gas);

    std::int64_t
    getGas();

private:
    static void
    makeImpParams(wasm_valtype_vec_t& v, WasmImportFunc const& imp);
    static void
    makeImpReturn(wasm_valtype_vec_t& v, WasmImportFunc const& imp);
    wasm_extern_vec_t
    buildImports(wasm_store_t* s, std::vector<WasmImportFunc> const& imports);
};

class WamrEngine
{
    std::unique_ptr<wasm_engine_t, decltype(&wasm_engine_delete)> engine;
    std::unique_ptr<wasm_store_t, decltype(&wasm_store_delete)> store;
    std::unique_ptr<ModuleWrapper> module;
    wasm_trap_t* trap = nullptr;
    std::int64_t defGas = -1;
    std::int32_t defMaxPages = -1;
    beast::Journal j_ = beast::Journal(beast::Journal::getNullSink());

public:
    WamrEngine();
    ~WamrEngine() = default;

    Expected<int32_t, TER>
    run(wbytes const& wasmCode,
        std::string_view funcName,
        std::vector<WasmImportFunc> const& imports,
        std::vector<WasmParam> const& params);

    std::int64_t
    initGas(std::int64_t def);

    std::int64_t
    setGas(std::int64_t gas);

    std::int32_t
    initMaxPages(std::int32_t def);

    std::int64_t
    getGas();

    // Host functions helper functionality
    wmem
    getMem() const;

    int32_t
    allocate(int32_t size);

    wasm_trap_t*
    newTrap(std::string_view msg);

private:
    Expected<int32_t, TER>
    runHlp(
        wbytes const& wasmCode,
        std::string_view funcName,
        std::vector<WasmImportFunc> const& imports,
        std::vector<WasmParam> const& params);

    int
    addModule(
        wbytes const& wasmCode,
        bool instantiate,
        std::vector<WasmImportFunc> const& imports);
    void
    clearModules();
    int
    addInstance();
    int32_t
    runFunc(std::string_view const funcName, int32_t p);

    int32_t
    makeModule(
        wbytes const& wasmCode,
        wasm_extern_vec_t const& imports = WASM_EMPTY_VEC);

    wasm_func_t*
    getFunc(std::string_view funcName);

    std::vector<wasm_val_t>
    convertParams(std::vector<WasmParam> const& params);

    void
    add_param(std::vector<wasm_val_t>& in, int32_t p);
    void
    add_param(std::vector<wasm_val_t>& in, int64_t p);

    template <int NR, class... Types>
    inline WamrResult
    call(std::string_view func, Types... args);

    template <int NR, class... Types>
    inline WamrResult
    call(wasm_func_t* func, Types... args);

    template <int NR, class... Types>
    inline WamrResult
    call(wasm_func_t* f, std::vector<wasm_val_t>& in);

    template <int NR, class... Types>
    inline WamrResult
    call(
        wasm_func_t* func,
        std::vector<wasm_val_t>& in,
        std::int32_t p,
        Types... args);

    template <int NR, class... Types>
    inline WamrResult
    call(
        wasm_func_t* func,
        std::vector<wasm_val_t>& in,
        std::int64_t p,
        Types... args);

    template <int NR, class... Types>
    inline WamrResult
    call(
        wasm_func_t* func,
        std::vector<wasm_val_t>& in,
        uint8_t const* d,
        std::size_t sz,
        Types... args);

    template <int NR, class... Types>
    inline WamrResult
    call(
        wasm_func_t* func,
        std::vector<wasm_val_t>& in,
        wbytes const& p,
        Types... args);
};

}  // namespace ripple
