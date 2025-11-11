#pragma once

#include <xrpld/app/wasm/WasmVM.h>

#include <wasm.h>
#include <wasmi.h>

namespace ripple {

struct WasmiResult
{
    wasm_val_vec_t r;
    bool f;  // failure flag

    WasmiResult(unsigned N = 0) : r{0, nullptr}, f(false)
    {
        if (N)
            wasm_val_vec_new_uninitialized(&r, N);
    }

    ~WasmiResult()
    {
        if (r.size)
            wasm_val_vec_delete(&r);
    }

    WasmiResult(WasmiResult const&) = delete;
    WasmiResult&
    operator=(WasmiResult const&) = delete;

    WasmiResult(WasmiResult&& o)
    {
        *this = std::move(o);
    }

    WasmiResult&
    operator=(WasmiResult&& o)
    {
        r = o.r;
        o.r = {0, nullptr};
        f = o.f;
        o.f = false;
        return *this;
    }
    // operator wasm_val_vec_t &() {return r;}
};

using ModulePtr = std::unique_ptr<wasm_module_t, decltype(&wasm_module_delete)>;
using InstancePtr =
    std::unique_ptr<wasm_instance_t, decltype(&wasm_instance_delete)>;

using FuncInfo = std::pair<wasm_func_t const*, wasm_functype_t const*>;

struct InstanceWrapper
{
    wasm_extern_vec_t exports_;
    InstancePtr instance_;
    beast::Journal j_ = beast::Journal(beast::Journal::getNullSink());

private:
    static InstancePtr
    init(
        wasm_store_t* s,
        wasm_module_t* m,
        wasm_extern_vec_t* expt,
        wasm_extern_vec_t const& imports,
        beast::Journal j);

public:
    InstanceWrapper();

    InstanceWrapper(InstanceWrapper&& o);

    InstanceWrapper&
    operator=(InstanceWrapper&& o);

    InstanceWrapper(
        wasm_store_t* s,
        wasm_module_t* m,
        wasm_extern_vec_t const& imports,
        beast::Journal j);

    ~InstanceWrapper();

    operator bool() const;

    FuncInfo
    getFunc(
        std::string_view funcName,
        wasm_exporttype_vec_t const& export_types) const;

    wmem
    getMem() const;
};

struct ModuleWrapper
{
    wasm_store_t* store_ = nullptr;
    ModulePtr module_;
    InstanceWrapper instanceWrap_;
    wasm_exporttype_vec_t exportTypes_;
    beast::Journal j_ = beast::Journal(beast::Journal::getNullSink());

private:
    static ModulePtr
    init(wasm_store_t* s, Bytes const& wasmBin, beast::Journal j);

public:
    ModuleWrapper();
    ModuleWrapper(ModuleWrapper&& o);
    ModuleWrapper&
    operator=(ModuleWrapper&& o);
    ModuleWrapper(
        wasm_store_t* s,
        Bytes const& wasmBin,
        bool instantiate,
        std::vector<WasmImportFunc> const& imports,
        beast::Journal j);
    ~ModuleWrapper();

    operator bool() const;

    FuncInfo
    getFunc(std::string_view funcName) const;
    wmem
    getMem() const;

    InstanceWrapper const&
    getInstance(int i = 0) const;

    int
    addInstance(
        wasm_store_t* s,
        wasm_extern_vec_t const& imports = WASM_EMPTY_VEC);

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

class WasmiEngine
{
    std::unique_ptr<wasm_engine_t, decltype(&wasm_engine_delete)> engine_;
    std::unique_ptr<wasm_store_t, decltype(&wasm_store_delete)> store_;
    std::unique_ptr<ModuleWrapper> moduleWrap_;
    beast::Journal j_ = beast::Journal(beast::Journal::getNullSink());

    std::mutex m_;  // 1 instance mutex

public:
    WasmiEngine();
    ~WasmiEngine() = default;

    static std::unique_ptr<wasm_engine_t, decltype(&wasm_engine_delete)>
    init();

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

    std::int64_t
    getGas();

    // Host functions helper functionality
    wasm_trap_t*
    newTrap(std::string_view msg);

    beast::Journal
    getJournal() const;

private:
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

    int32_t
    makeModule(
        Bytes const& wasmCode,
        wasm_extern_vec_t const& imports = WASM_EMPTY_VEC);

    FuncInfo
    getFunc(std::string_view funcName);

    std::vector<wasm_val_t>
    convertParams(std::vector<WasmParam> const& params);

    static int
    compareParamTypes(
        wasm_valtype_vec_t const* ftp,
        std::vector<wasm_val_t> const& p);

    static void
    add_param(std::vector<wasm_val_t>& in, int32_t p);
    static void
    add_param(std::vector<wasm_val_t>& in, int64_t p);

    template <int NR, class... Types>
    inline WasmiResult
    call(std::string_view func, Types&&... args);

    template <int NR, class... Types>
    inline WasmiResult
    call(FuncInfo const& f, Types&&... args);

    template <int NR, class... Types>
    inline WasmiResult
    call(FuncInfo const& f, std::vector<wasm_val_t>& in);

    template <int NR, class... Types>
    inline WasmiResult
    call(
        FuncInfo const& f,
        std::vector<wasm_val_t>& in,
        std::int32_t p,
        Types&&... args);

    template <int NR, class... Types>
    inline WasmiResult
    call(
        FuncInfo const& f,
        std::vector<wasm_val_t>& in,
        std::int64_t p,
        Types&&... args);

    template <int NR, class... Types>
    inline WasmiResult
    call(
        FuncInfo const& f,
        std::vector<wasm_val_t>& in,
        uint8_t const* d,
        std::size_t sz,
        Types&&... args);

    template <int NR, class... Types>
    inline WasmiResult
    call(
        FuncInfo const& f,
        std::vector<wasm_val_t>& in,
        Bytes const& p,
        Types&&... args);
};

}  // namespace ripple
