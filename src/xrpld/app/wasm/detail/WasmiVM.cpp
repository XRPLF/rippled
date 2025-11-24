#include <xrpld/app/wasm/WasmiVM.h>

#include <xrpl/basics/Log.h>

#include <memory>

#ifdef _DEBUG
// #define DEBUG_OUTPUT 1
#endif
// #define SHOW_CALL_TIME 1

namespace ripple {

namespace {

void
print_wasm_error(std::string_view msg, wasm_trap_t* trap, beast::Journal jlog)
{
#ifdef DEBUG_OUTPUT
    auto& j = std::cerr;
#else
    auto j = jlog.warn();
#endif

    wasm_byte_vec_t error_message WASM_EMPTY_VEC;

    if (trap)
        wasm_trap_message(trap, &error_message);

    if (error_message.size)
    {
        j << "WASMI Error: " << msg << ", "
          << std::string_view(error_message.data, error_message.size - 1);
    }
    else
        j << "WASMI Error: " << msg;

    if (error_message.size)
        wasm_byte_vec_delete(&error_message);

    if (trap)
        wasm_trap_delete(trap);

#ifdef DEBUG_OUTPUT
    j << std::endl;
#endif
}
// LCOV_EXCL_STOP

}  // namespace

InstancePtr
InstanceWrapper::init(
    wasm_store_t* s,
    wasm_module_t* m,
    wasm_extern_vec_t* expt,
    wasm_extern_vec_t const& imports,
    beast::Journal j)
{
    wasm_trap_t* trap = nullptr;
    InstancePtr mi = InstancePtr(
        wasm_instance_new(s, m, &imports, &trap), &wasm_instance_delete);

    if (!mi || trap)
    {
        print_wasm_error("can't create instance", trap, j);
        throw std::runtime_error("can't create instance");
    }
    wasm_instance_exports(mi.get(), expt);
    return mi;
}

InstanceWrapper::InstanceWrapper()
    : exports_{0, nullptr}, instance_(nullptr, &wasm_instance_delete)
{
}

InstanceWrapper::InstanceWrapper(InstanceWrapper&& o)
    : exports_{0, nullptr}, instance_(nullptr, &wasm_instance_delete)
{
    *this = std::move(o);
}

InstanceWrapper::InstanceWrapper(
    wasm_store_t* s,
    wasm_module_t* m,
    wasm_extern_vec_t const& imports,
    beast::Journal j)
    : exports_ WASM_EMPTY_VEC
    , instance_(init(s, m, &exports_, imports, j))
    , j_(j)
{
}

InstanceWrapper::~InstanceWrapper()
{
    if (exports_.size)
        wasm_extern_vec_delete(&exports_);
}

InstanceWrapper&
InstanceWrapper::operator=(InstanceWrapper&& o)
{
    if (this == &o)
        return *this;

    if (exports_.size)
        wasm_extern_vec_delete(&exports_);
    exports_ = o.exports_;
    o.exports_ = {0, nullptr};

    instance_ = std::move(o.instance_);

    j_ = o.j_;

    return *this;
}

InstanceWrapper::operator bool() const
{
    return static_cast<bool>(instance_);
}

FuncInfo
InstanceWrapper::getFunc(
    std::string_view funcName,
    wasm_exporttype_vec_t const& export_types) const
{
    wasm_func_t* f = nullptr;
    wasm_functype_t* ft = nullptr;

    if (!instance_)
        throw std::runtime_error("no instance");

    if (!export_types.size)
        throw std::runtime_error("no export");
    if (export_types.size != exports_.size)
        throw std::runtime_error("invalid export");

    for (unsigned i = 0; i < export_types.size; ++i)
    {
        auto const* exp_type(export_types.data[i]);

        wasm_name_t const* name = wasm_exporttype_name(exp_type);
        wasm_externtype_t const* exn_type = wasm_exporttype_type(exp_type);
        if (wasm_externtype_kind(exn_type) == WASM_EXTERN_FUNC)
        {
            if (funcName == std::string_view(name->data, name->size))
            {
                auto* exn(exports_.data[i]);
                if (wasm_extern_kind(exn) != WASM_EXTERN_FUNC)
                    throw std::runtime_error("invalid export");

                ft = wasm_externtype_as_functype(
                    const_cast<wasm_externtype_t*>(exn_type));
                f = wasm_extern_as_func(exn);
                break;
            }
        }
    }

    if (!f || !ft)
        throw std::runtime_error(
            "can't find function <" + std::string(funcName) + ">");

    return {f, ft};
}

wmem
InstanceWrapper::getMem() const
{
    if (!instance_)
        throw std::runtime_error("no instance");

    wasm_memory_t* mem = nullptr;
    for (unsigned i = 0; i < exports_.size; ++i)
    {
        auto* e(exports_.data[i]);
        if (wasm_extern_kind(e) == WASM_EXTERN_MEMORY)
        {
            mem = wasm_extern_as_memory(e);
            break;
        }
    }

    if (!mem)
        throw std::runtime_error("no memory exported");

    return {
        reinterpret_cast<std::uint8_t*>(wasm_memory_data(mem)),
        wasm_memory_data_size(mem)};
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////

ModulePtr
ModuleWrapper::init(wasm_store_t* s, Bytes const& wasmBin, beast::Journal j)
{
    wasm_byte_vec_t const code{wasmBin.size(), (char*)(wasmBin.data())};
    ModulePtr m = ModulePtr(wasm_module_new(s, &code), &wasm_module_delete);
    if (!m)
        throw std::runtime_error("can't create module");

    return m;
}

ModuleWrapper::ModuleWrapper()
    : module_(nullptr, &wasm_module_delete), exportTypes_{0, nullptr}
{
}

ModuleWrapper::ModuleWrapper(ModuleWrapper&& o)
    : module_(nullptr, &wasm_module_delete), exportTypes_{0, nullptr}
{
    *this = std::move(o);
}

ModuleWrapper::ModuleWrapper(
    wasm_store_t* s,
    Bytes const& wasmBin,
    bool instantiate,
    std::vector<WasmImportFunc> const& imports,
    beast::Journal j)
    : store_(s), module_(init(s, wasmBin, j)), exportTypes_{0, nullptr}, j_(j)
{
    wasm_module_exports(module_.get(), &exportTypes_);
    if (instantiate)
    {
        auto wimports = buildImports(s, imports);
        addInstance(s, wimports);
        wasm_extern_vec_delete(&wimports);
    }
}

ModuleWrapper::~ModuleWrapper()
{
    if (exportTypes_.size)
        wasm_exporttype_vec_delete(&exportTypes_);
}

ModuleWrapper&
ModuleWrapper::operator=(ModuleWrapper&& o)
{
    if (this == &o)
        return *this;

    store_ = o.store_;
    o.store_ = nullptr;
    module_ = std::move(o.module_);
    instanceWrap_ = std::move(o.instanceWrap_);
    if (exportTypes_.size)
        wasm_exporttype_vec_delete(&exportTypes_);
    exportTypes_ = o.exportTypes_;
    o.exportTypes_ = {0, nullptr};
    j_ = o.j_;

    return *this;
}

ModuleWrapper::operator bool() const
{
    return instanceWrap_;
}

void
ModuleWrapper::makeImpParams(wasm_valtype_vec_t& v, WasmImportFunc const& imp)
{
    auto const paramSize = imp.params.size();

    if (paramSize)
    {
        wasm_valtype_vec_new_uninitialized(&v, paramSize);
    }
    else
        v = WASM_EMPTY_VEC;
    for (unsigned i = 0; i < paramSize; ++i)
    {
        auto const vt = imp.params[i];
        switch (vt)
        {
            case WT_I32:
                v.data[i] = wasm_valtype_new_i32();
                break;
            case WT_I64:
                v.data[i] = wasm_valtype_new_i64();
                break;
            default:
                throw std::runtime_error("invalid import type");
        }
    }
}

void
ModuleWrapper::makeImpReturn(wasm_valtype_vec_t& v, WasmImportFunc const& imp)
{
    if (imp.result)
    {
        wasm_valtype_vec_new_uninitialized(&v, 1);
        switch (*imp.result)
        {
            case WT_I32:
                v.data[0] = wasm_valtype_new_i32();
                break;
            case WT_I64:
                v.data[0] = wasm_valtype_new_i64();
                break;
            default:
                throw std::runtime_error("invalid return type");
        }
    }
    else
        v = WASM_EMPTY_VEC;
}

wasm_extern_vec_t
ModuleWrapper::buildImports(
    wasm_store_t* s,
    std::vector<WasmImportFunc> const& imports)
{
    wasm_importtype_vec_t importTypes = WASM_EMPTY_VEC;
    wasm_module_imports(module_.get(), &importTypes);
    std::
        unique_ptr<wasm_importtype_vec_t, decltype(&wasm_importtype_vec_delete)>
            itDeleter(&importTypes, &wasm_importtype_vec_delete);

    wasm_extern_vec_t wimports = WASM_EMPTY_VEC;
    if (!importTypes.size)
        return wimports;

    wasm_extern_vec_new_uninitialized(&wimports, importTypes.size);

    unsigned impCnt = 0;
    for (unsigned i = 0; i < importTypes.size; ++i)
    {
        wasm_importtype_t const* importtype = importTypes.data[i];

        // wasm_name_t const* mn = wasm_importtype_module(importtype);
        // auto modName = std::string_view(mn->data, mn->num_elems);
        wasm_name_t const* fn = wasm_importtype_name(importtype);
        auto fieldName = std::string_view(fn->data, fn->size);

        wasm_externkind_t const itype =
            wasm_externtype_kind(wasm_importtype_type(importtype));
        if ((itype) != WASM_EXTERN_FUNC)
            throw std::runtime_error(
                "Invalid import type " + std::to_string(itype));

        // for multi-module support
        // if ((W_ENV != modName) && (W_HOST_LIB != modName))
        //     continue;

        bool impSet = false;
        for (auto const& imp : imports)
        {
            if (imp.name != fieldName)
                continue;

            wasm_valtype_vec_t params = WASM_EMPTY_VEC,
                               results = WASM_EMPTY_VEC;
            makeImpReturn(results, imp);
            makeImpParams(params, imp);

            using ftype_ptr = std::
                unique_ptr<wasm_functype_t, decltype(&wasm_functype_delete)>;
            ftype_ptr ftype(
                wasm_functype_new(&params, &results), &wasm_functype_delete);
            wasm_func_t* func = wasm_func_new_with_env(
                s,
                ftype.get(),
                reinterpret_cast<wasm_func_callback_with_env_t>(imp.wrap),
                imp.udata,
                nullptr);
            if (!func)
            {
                // LCOV_EXCL_START
                throw std::runtime_error(
                    "can't create import function " + imp.name);
                // LCOV_EXCL_STOP
            }

            // if (imp.gas && !wasm_func_set_gas(func, imp.gas))
            // {
            //     // LCOV_EXCL_START
            //     throw std::runtime_error(
            //         "can't set gas for import function " + imp.name);
            //     // LCOV_EXCL_STOP
            // }

            wimports.data[i] = wasm_func_as_extern(func);
            ++impCnt;
            impSet = true;

            break;
        }

        if (!impSet)
        {
            print_wasm_error(
                "Import not found: " + std::string(fieldName), nullptr, j_);
        }
    }

    if (impCnt != importTypes.size)
    {
        print_wasm_error(
            std::string("Imports not finished: ") + std::to_string(impCnt) +
                "/" + std::to_string(importTypes.size),
            nullptr,
            j_);
    }

    return wimports;
}

FuncInfo
ModuleWrapper::getFunc(std::string_view funcName) const
{
    return instanceWrap_.getFunc(funcName, exportTypes_);
}

wmem
ModuleWrapper::getMem() const
{
    return instanceWrap_.getMem();
}

InstanceWrapper const&
ModuleWrapper::getInstance(int) const
{
    return instanceWrap_;
}

int
ModuleWrapper::addInstance(wasm_store_t* s, wasm_extern_vec_t const& imports)
{
    instanceWrap_ = {s, module_.get(), imports, j_};
    return 0;
}

// int
// my_module_t::delInstance(int i)
// {
//     if (i >= mod_inst.size())
//         return -1;
//     if (!mod_inst[i])
//         mod_inst[i] = my_mod_inst_t();
//     return i;
// }

std::int64_t
ModuleWrapper::getGas()
{
    if (!store_)
        return 0;
    std::uint64_t gas = 0;
    wasm_store_get_fuel(store_, &gas);
    return static_cast<std::int64_t>(gas);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////

// void
// WasmiEngine::clearModules()
// {
//     modules.clear();
//     store.reset();  // to free the memory before creating new store
//     store = {wasm_store_new(engine.get()), &wasm_store_delete};
// }

std::unique_ptr<wasm_engine_t, decltype(&wasm_engine_delete)>
WasmiEngine::init()
{
    wasm_config_t* config = wasm_config_new();
    if (!config)
        return std::unique_ptr<wasm_engine_t, decltype(&wasm_engine_delete)>{
            nullptr, &wasm_engine_delete};
    wasmi_config_consume_fuel_set(config, true);

    return std::unique_ptr<wasm_engine_t, decltype(&wasm_engine_delete)>(
        wasm_engine_new_with_config(config), &wasm_engine_delete);
}

WasmiEngine::WasmiEngine()
    : engine_(init()), store_(nullptr, &wasm_store_delete)
{
}

int
WasmiEngine::addModule(
    Bytes const& wasmCode,
    bool instantiate,
    int64_t gas,
    std::vector<WasmImportFunc> const& imports)
{
    moduleWrap_.reset();
    store_.reset();  // to free the memory before creating new store
    store_ = {wasm_store_new(engine_.get()), &wasm_store_delete};

    if (gas < 0)
        gas = std::numeric_limits<decltype(gas)>::max();
    wasmi_error_t* err =
        wasm_store_set_fuel(store_.get(), static_cast<std::uint64_t>(gas));
    if (err)
    {
        print_wasm_error("Error setting gas", nullptr, j_);
        throw std::runtime_error("can't set gas");
    }

    moduleWrap_ = std::make_unique<ModuleWrapper>(
        store_.get(), wasmCode, instantiate, imports, j_);

    if (!moduleWrap_)
        throw std::runtime_error("can't create module wrapper");

    return moduleWrap_ ? 0 : -1;
}

// int
// WasmiEngine::addInstance()
// {
//     return module->addInstance(store.get());
// }

FuncInfo
WasmiEngine::getFunc(std::string_view funcName)
{
    return moduleWrap_->getFunc(funcName);
}

std::vector<wasm_val_t>
WasmiEngine::convertParams(std::vector<WasmParam> const& params)
{
    std::vector<wasm_val_t> v;
    v.reserve(params.size());
    for (auto const& p : params)
    {
        switch (p.type)
        {
            case WT_I32:
                v.push_back(WASM_I32_VAL(p.of.i32));
                break;
            case WT_I64:
                v.push_back(WASM_I64_VAL(p.of.i64));
                break;
            case WT_U8V: {
                auto const sz = p.of.u8v.sz;
                auto const ptr = allocate(sz);
                auto mem = getMem();
                memcpy(mem.p + ptr, p.of.u8v.d, sz);

                v.push_back(WASM_I32_VAL(ptr));
                v.push_back(WASM_I32_VAL(sz));
            }
            break;
            default:
                throw std::runtime_error(
                    "unknown parameter type: " + std::to_string(p.type));
                break;
        }
    }

    return v;
}

int
WasmiEngine::compareParamTypes(
    wasm_valtype_vec_t const* ftp,
    std::vector<wasm_val_t> const& p)
{
    if (ftp->size != p.size())
        return std::min(ftp->size, p.size());

    for (unsigned i = 0; i < ftp->size; ++i)
    {
        auto const t1 = wasm_valtype_kind(ftp->data[i]);
        auto const t2 = p[i].kind;
        if (t1 != t2)
            return i;
    }

    return -1;
}

void
WasmiEngine::add_param(std::vector<wasm_val_t>& in, int32_t p)
{
    in.emplace_back();
    auto& el(in.back());
    memset(&el, 0, sizeof(el));
    el = WASM_I32_VAL(p);  // WASM_I32;
}

void
WasmiEngine::add_param(std::vector<wasm_val_t>& in, int64_t p)
{
    in.emplace_back();
    auto& el(in.back());
    el = WASM_I64_VAL(p);
}

template <int NR, class... Types>
WasmiResult
WasmiEngine::call(std::string_view func, Types&&... args)
{
    // Lookup our export function
    auto f = getFunc(func);
    return call<NR>(f, std::forward<Types>(args)...);
}

template <int NR, class... Types>
WasmiResult
WasmiEngine::call(FuncInfo const& f, Types&&... args)
{
    std::vector<wasm_val_t> in;
    return call<NR>(f, in, std::forward<Types>(args)...);
}

#ifdef SHOW_CALL_TIME
static inline uint64_t
usecs()
{
    uint64_t x =
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch())
            .count();
    return x;
}
#endif

template <int NR, class... Types>
WasmiResult
WasmiEngine::call(FuncInfo const& f, std::vector<wasm_val_t>& in)
{
    // wasm_val_t rs[1] = {WASM_I32_VAL(0)};
    WasmiResult ret(NR);
    // if (NR)  {   wasm_val_vec_new_uninitialized(&ret, NR);    //
    // wasm_val_vec_new(&ret, NR, &rs[0]);    // ret = WASM_ARRAY_VEC(rs);    }

    wasm_val_vec_t const inv = in.empty()
        ? wasm_val_vec_t WASM_EMPTY_VEC
        : wasm_val_vec_t{in.size(), in.data()};

#ifdef SHOW_CALL_TIME
    auto const start = usecs();
#endif

    wasm_trap_t* trap = wasm_func_call(f.first, &inv, &ret.r);

#ifdef SHOW_CALL_TIME
    auto const finish = usecs();
    auto const delta_ms = (finish - start) / 1000;
    std::cout << "wasm_func_call: " << delta_ms << "ms" << std::endl;
#endif

    if (trap)
    {
        ret.f = true;
        print_wasm_error("failure to call func", trap, j_);
    }

    // assert(results[0].kind == WASM_I32);
    // if (NR) printf("Result P5: %d\n", ret[0].of.i32);

    return ret;
}

template <int NR, class... Types>
WasmiResult
WasmiEngine::call(
    FuncInfo const& f,
    std::vector<wasm_val_t>& in,
    std::int32_t p,
    Types&&... args)
{
    add_param(in, p);
    return call<NR>(f, in, std::forward<Types>(args)...);
}

template <int NR, class... Types>
WasmiResult
WasmiEngine::call(
    FuncInfo const& f,
    std::vector<wasm_val_t>& in,
    std::int64_t p,
    Types&&... args)
{
    add_param(in, p);
    return call<NR>(f, in, std::forward<Types>(args)...);
}

template <int NR, class... Types>
WasmiResult
WasmiEngine::call(
    FuncInfo const& f,
    std::vector<wasm_val_t>& in,
    uint8_t const* d,
    std::size_t sz,
    Types&&... args)
{
    auto const ptr = allocate(sz);
    auto mem = getMem();
    memcpy(mem.p + ptr, d, sz);

    add_param(in, ptr);
    add_param(in, static_cast<int32_t>(sz));
    return call<NR>(f, in, std::forward<Types>(args)...);
}

template <int NR, class... Types>
WasmiResult
WasmiEngine::call(
    FuncInfo const& f,
    std::vector<wasm_val_t>& in,
    Bytes const& p,
    Types&&... args)
{
    return call<NR>(f, in, p.data(), p.size(), std::forward<Types>(args)...);
}

Expected<WasmResult<int32_t>, TER>
WasmiEngine::run(
    Bytes const& wasmCode,
    std::string_view funcName,
    std::vector<WasmParam> const& params,
    std::vector<WasmImportFunc> const& imports,
    HostFunctions* hfs,
    int64_t gas,
    beast::Journal j)
{
    j_ = j;
    try
    {
        return runHlp(wasmCode, funcName, params, imports, hfs, gas);
    }
    catch (std::exception const& e)
    {
        print_wasm_error(std::string("exception: ") + e.what(), nullptr, j_);
    }
    catch (...)
    {
        print_wasm_error(std::string("exception: unknown"), nullptr, j_);
    }
    return Unexpected<TER>(tecFAILED_PROCESSING);
}

Expected<WasmResult<int32_t>, TER>
WasmiEngine::runHlp(
    Bytes const& wasmCode,
    std::string_view funcName,
    std::vector<WasmParam> const& params,
    std::vector<WasmImportFunc> const& imports,
    HostFunctions* hfs,
    int64_t gas)
{
    // currently only 1 module support, possible parallel UT run
    std::lock_guard<decltype(m_)> lg(m_);

    // Create and instantiate the module.
    if (!wasmCode.empty())
    {
        [[maybe_unused]] int const m = addModule(wasmCode, true, gas, imports);
    }

    if (!moduleWrap_ || !moduleWrap_->instanceWrap_)
        throw std::runtime_error("no instance");

    if (hfs)
        hfs->setRT(&getRT());

    // Call main
    auto const f = getFunc(!funcName.empty() ? funcName : "_start");
    auto const* ftp = wasm_functype_params(f.second);

    // not const because passed directly to VM function (which accept non
    // const)
    auto p = convertParams(params);

    if (int const comp = compareParamTypes(ftp, p); comp >= 0)
        throw std::runtime_error(
            "invalid parameter type #" + std::to_string(comp));

    auto const res = call<1>(f, p);

    if (res.f)
        throw std::runtime_error("<" + std::string(funcName) + "> failure");
    else if (!res.r.size)
        throw std::runtime_error(
            "<" + std::string(funcName) + "> return nothing");

    assert(res.r.data[0].kind == WASM_I32);
    if (gas == -1)
        gas = std::numeric_limits<decltype(gas)>::max();
    WasmResult<int32_t> const ret{
        res.r.data[0].of.i32, gas - moduleWrap_->getGas()};

    // #ifdef DEBUG_OUTPUT
    //     auto& j = std::cerr;
    // #else
    //     auto j = j_.debug();
    // #endif
    // j << "WASMI Res: " << ret.result << " cost: " << ret.cost << std::endl;

    return ret;
}

NotTEC
WasmiEngine::check(
    Bytes const& wasmCode,
    std::string_view funcName,
    std::vector<WasmParam> const& params,
    std::vector<WasmImportFunc> const& imports,
    beast::Journal j)
{
    j_ = j;

    try
    {
        return checkHlp(wasmCode, funcName, params, imports);
    }
    catch (std::exception const& e)
    {
        print_wasm_error(std::string("exception: ") + e.what(), nullptr, j_);
    }
    catch (...)
    {
        print_wasm_error(std::string("exception: unknown"), nullptr, j_);
    }

    return temBAD_WASM;
}

NotTEC
WasmiEngine::checkHlp(
    Bytes const& wasmCode,
    std::string_view funcName,
    std::vector<WasmParam> const& params,
    std::vector<WasmImportFunc> const& imports)
{
    // currently only 1 module support, possible parallel UT run
    std::lock_guard<decltype(m_)> lg(m_);

    // Create and instantiate the module.
    if (wasmCode.empty())
        throw std::runtime_error("empty nodule");

    int const m = addModule(wasmCode, true, -1, imports);
    if ((m < 0) || !moduleWrap_ || !moduleWrap_->instanceWrap_)
        throw std::runtime_error("no instance");

    // Looking for a func and compare parameter types
    auto const f = getFunc(!funcName.empty() ? funcName : "_start");
    auto const* ftp = wasm_functype_params(f.second);
    auto const p = convertParams(params);

    if (int const comp = compareParamTypes(ftp, p); comp >= 0)
        throw std::runtime_error(
            "invalid parameter type #" + std::to_string(comp));

    return tesSUCCESS;
}

std::int64_t
WasmiEngine::getGas()
{
    return moduleWrap_ ? moduleWrap_->getGas() : 0;
}

wmem
WasmiEngine::getMem() const
{
    return moduleWrap_ ? moduleWrap_->getMem() : wmem();
}

InstanceWrapper const&
WasmiEngine::getRT(int m, int i)
{
    if (!moduleWrap_)
        throw std::runtime_error("no module");
    return moduleWrap_->getInstance(i);
}

int32_t
WasmiEngine::allocate(int32_t sz)
{
    auto res = call<1>(W_ALLOC, static_cast<int32_t>(sz));

    if (res.f || !res.r.size || (res.r.data[0].kind != WASM_I32) ||
        !res.r.data[0].of.i32)
        throw std::runtime_error(
            "can't allocate memory, " + std::to_string(sz) + " bytes");
    return res.r.data[0].of.i32;
}

wasm_trap_t*
WasmiEngine::newTrap(std::string_view txt)
{
    wasm_message_t msg = WASM_EMPTY_VEC;

    if (!txt.empty())
        wasm_name_new(&msg, txt.size(), txt.data());

    return wasm_trap_new(store_.get(), &msg);
}

beast::Journal
WasmiEngine::getJournal() const
{
    return j_;
}

}  // namespace ripple
