#pragma once

#include <xrpl/protocol/Protocol.h>
#include <xrpl/tx/wasm/WasmVM.h>

#include <wasm.h>
#include <wasmi.h>

#include <optional>

namespace xrpl {

template <class T, void (*Create)(T*, size_t), void (*Destroy)(T*)>
class WasmVec
{
    using TD = std::remove_pointer_t<decltype(T::data)>;
    T vec_;

public:
    WasmVec(size_t s = 0) : vec_ WASM_EMPTY_VEC
    {
        if (s > 0)
            Create(&vec_, s);  // zeroes memory
    }

    ~WasmVec()
    {
        clear();
    }

    WasmVec(WasmVec const&) = delete;
    WasmVec&
    operator=(WasmVec const&) = delete;

    WasmVec(WasmVec&& other) noexcept : vec_ WASM_EMPTY_VEC
    {
        *this = std::move(other);
    }

    WasmVec&
    operator=(WasmVec&& other) noexcept
    {
        if (this != &other)
        {
            clear();
            vec_ = other.vec_;
            other.vec_ = WASM_EMPTY_VEC;
        }
        return *this;
    }

    void
    clear()
    {
        Destroy(&vec_);  // call destructor for every elements too
        vec_ = WASM_EMPTY_VEC;
    }

    T
    release()
    {
        T result = vec_;
        vec_ = WASM_EMPTY_VEC;
        return result;
    }

    T*
    get()
    {
        return &vec_;
    }

    [[nodiscard]] T const*
    get() const
    {
        return &vec_;
    }

    TD&
    operator[](size_t i)
    {
        if (i >= vec_.size)
            Throw<std::runtime_error>("Out of bound");
        return vec_.data[i];
    }

    TD const&
    operator[](size_t i) const
    {
        if (i >= vec_.size)
            Throw<std::runtime_error>("Out of bound");
        return vec_.data[i];
    }

    [[nodiscard]] size_t
    size() const
    {
        return vec_.size;
    }

    [[nodiscard]] bool
    empty() const
    {
        return vec_.size == 0u;
    }
};

using WasmValtypeVec =
    WasmVec<wasm_valtype_vec_t, &wasm_valtype_vec_new_uninitialized, &wasm_valtype_vec_delete>;
using WasmValVec = WasmVec<wasm_val_vec_t, &wasm_val_vec_new_uninitialized, &wasm_val_vec_delete>;
using WasmExternVec =
    WasmVec<wasm_extern_vec_t, &wasm_extern_vec_new_uninitialized, &wasm_extern_vec_delete>;
using WasmExporttypeVec = WasmVec<
    wasm_exporttype_vec_t,
    &wasm_exporttype_vec_new_uninitialized,
    &wasm_exporttype_vec_delete>;
using WasmImporttypeVec = WasmVec<
    wasm_importtype_vec_t,
    &wasm_importtype_vec_new_uninitialized,
    &wasm_importtype_vec_delete>;

struct WasmiResult
{
    WasmValVec r;
    // Set iff the call trapped. Holds the TER the trap was classified into
    // (tecINTERNAL / tecOUT_OF_GAS / tecFAILED_PROCESSING); see
    // WasmiEngine::call. std::nullopt means the call returned normally.
    std::optional<TER> ter;

    WasmiResult(unsigned n = 0) : r(n)
    {
    }

    WasmiResult() = delete;
    ~WasmiResult() = default;
    WasmiResult(WasmiResult&& o) = default;
    WasmiResult&
    operator=(WasmiResult&& o) = default;
};

using ModulePtr = std::unique_ptr<wasm_module_t, decltype(&wasm_module_delete)>;
using InstancePtr = std::unique_ptr<wasm_instance_t, decltype(&wasm_instance_delete)>;
using EnginePtr = std::unique_ptr<wasm_engine_t, decltype(&wasm_engine_delete)>;
using StorePtr = std::unique_ptr<wasm_store_t, decltype(&wasm_store_delete)>;

using FuncInfo = std::pair<wasm_func_t const*, wasm_functype_t const*>;

class InstanceWrapper
{
    wasm_store_t* store_ = nullptr;
    WasmExternVec exports_;
    mutable int memIdx_ = -1;
    InstancePtr instance_;
    beast::Journal j_ = beast::Journal(beast::Journal::getNullSink());
    std::int64_t transferLimit_ = kWasmTransferLimit;

private:
    static InstancePtr
    init(
        StorePtr& s,
        ModulePtr& m,
        WasmExternVec& expt,
        WasmExternVec const& imports,
        beast::Journal j);

public:
    InstanceWrapper() : instance_(nullptr, &wasm_instance_delete) {};

    InstanceWrapper(InstanceWrapper const&) = delete;

    InstanceWrapper(InstanceWrapper&& o) : instance_(nullptr, &wasm_instance_delete)
    {
        *this = std::move(o);  // LCOV_EXCL_LINE
    }

    InstanceWrapper(StorePtr& s, ModulePtr& m, WasmExternVec const& imports, beast::Journal j)
        : store_(s.get()), instance_(init(s, m, exports_, imports, j)), j_(j)
    {
    }

    InstanceWrapper&
    operator=(InstanceWrapper&& o);

    InstanceWrapper&
    operator=(InstanceWrapper const&) = delete;

    operator bool() const
    {
        return static_cast<bool>(instance_);
    }

    FuncInfo
    getFunc(std::string_view funcName, WasmExporttypeVec const& exportTypes) const;

    Wmem
    getMem() const;

    std::int64_t
    getGas() const;

    std::int64_t
    setGas(std::int64_t) const;

    std::int64_t
    getTransferLimit() const;

    std::int64_t
    setTransferLimit(std::int64_t);
};

class ModuleWrapper
{
    ModulePtr module_;
    InstanceWrapper instanceWrap_;
    WasmExporttypeVec exportTypes_;
    beast::Journal j_ = beast::Journal(beast::Journal::getNullSink());

public:
    // LCOV_EXCL_START
    ModuleWrapper() : module_(nullptr, &wasm_module_delete)
    {
    }

    ModuleWrapper(ModuleWrapper&& o) : module_(nullptr, &wasm_module_delete)
    {
        *this = std::move(o);
    }
    // LCOV_EXCL_STOP

    ModuleWrapper&
    operator=(ModuleWrapper&& o);
    ModuleWrapper(
        StorePtr& s,
        Bytes const& wasmBin,
        bool instantiate,
        ImportVec const& imports,
        beast::Journal j);
    ~ModuleWrapper() = default;

    operator bool() const
    {
        return instanceWrap_;
    }

    FuncInfo
    getFunc(std::string_view funcName) const
    {
        return instanceWrap_.getFunc(funcName, exportTypes_);
    }

    wasm_functype_t*
    getFuncType(std::string_view funcName) const;

    Wmem
    getMem() const
    {
        return instanceWrap_.getMem();
    }

    InstanceWrapper&
    getInstance(int i = 0)
    {
        return instanceWrap_;
    }

    InstanceWrapper const&
    getInstance(int i = 0) const
    {
        return instanceWrap_;
    }

    int
    addInstance(StorePtr& s, WasmExternVec const& imports)
    {
        instanceWrap_ = {s, module_, imports, j_};
        return 0;
    }

    std::int64_t
    getGas() const
    {
        return instanceWrap_ ? instanceWrap_.getGas() : -1;
    }

private:
    static ModulePtr
    init(StorePtr& s, Bytes const& wasmBin, beast::Journal j);

    WasmExternVec
    buildImports(StorePtr& s, ImportVec const& imports) const;
};

class WasmiEngine
{
    EnginePtr engine_;
    StorePtr store_;
    std::unique_ptr<ModuleWrapper> moduleWrap_;
    beast::Journal j_ = beast::Journal(beast::Journal::getNullSink());

    std::mutex m_;  // 1 instance mutex

public:
    WasmiEngine() : engine_(init()), store_(nullptr, &wasm_store_delete)
    {
    }

    ~WasmiEngine() = default;

    static EnginePtr
    init();

    std::expected<WasmResult<int32_t>, WasmTER>
    run(Bytes const& wasmCode,
        HostFunctions& hfs,
        int64_t gas,
        std::string_view funcName,
        std::vector<WasmParam> const& params,
        ImportVec const& imports,
        beast::Journal j);

    NotTEC
    check(
        Bytes const& wasmCode,
        HostFunctions& hfs,
        std::string_view funcName,
        std::vector<WasmParam> const& params,
        ImportVec const& imports,
        beast::Journal j);

    [[nodiscard]] std::int64_t
    getGas() const
    {
        return moduleWrap_ ? moduleWrap_->getGas() : -1;  // LCOV_EXCL_LINE
    }

    // Host functions helper functionality
    wasm_trap_t*
    newTrap(std::string const& msg);

    // LCOV_EXCL_START
    [[nodiscard]] beast::Journal
    getJournal() const
    {
        return j_;
    }
    // LCOV_EXCL_STOP

private:
    [[nodiscard]] InstanceWrapper&
    getRT(int m = 0, int i = 0) const
    {
        if (!moduleWrap_)
            Throw<std::runtime_error>("no module");
        return moduleWrap_->getInstance(i);
    }

    [[nodiscard]] Wmem
    getMem() const
    {
        return moduleWrap_ ? moduleWrap_->getMem() : Wmem();
    }

    std::expected<WasmResult<int32_t>, WasmTER>
    runHlp(
        Bytes const& wasmCode,
        HostFunctions& hfs,
        int64_t gas,
        std::string_view funcName,
        std::vector<WasmParam> const& params,
        ImportVec const& imports,
        beast::Journal j);

    NotTEC
    checkHlp(
        Bytes const& wasmCode,
        HostFunctions& hfs,
        std::string_view funcName,
        std::vector<WasmParam> const& params,
        ImportVec const& imports,
        beast::Journal j);

    int
    addModule(Bytes const& wasmCode, bool instantiate, ImportVec const& imports, int64_t gas);
    void
    clearModules();

    // int  addInstance();

    int32_t
    runFunc(std::string_view const funcName, int32_t p);

    int32_t
    makeModule(Bytes const& wasmCode, WasmExternVec const& imports = {});

    [[nodiscard]] FuncInfo
    getFunc(std::string_view funcName) const
    {
        return moduleWrap_->getFunc(funcName);
    }

    static std::vector<wasm_val_t>
    convertParams(std::vector<WasmParam> const& params);

    static int
    compareParamTypes(wasm_valtype_vec_t const* ftp, std::vector<wasm_val_t> const& p);

    static void
    addParam(std::vector<wasm_val_t>& in, int32_t p);
    static void
    addParam(std::vector<wasm_val_t>& in, int64_t p);

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
    call(FuncInfo const& f, std::vector<wasm_val_t>& in, std::int32_t p, Types&&... args);

    template <int NR, class... Types>
    inline WasmiResult
    call(FuncInfo const& f, std::vector<wasm_val_t>& in, std::int64_t p, Types&&... args);

    template <int NR, class... Types>
    inline WasmiResult
    call(
        FuncInfo const& f,
        std::vector<wasm_val_t>& in,
        uint8_t const* d,
        int32_t sz,
        Types&&... args);

    template <int NR, class... Types>
    inline WasmiResult
    call(FuncInfo const& f, std::vector<wasm_val_t>& in, Bytes const& p, Types&&... args);
};

}  // namespace xrpl
