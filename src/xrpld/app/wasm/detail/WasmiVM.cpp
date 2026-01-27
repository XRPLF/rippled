#include <xrpld/app/wasm/WasmiVM.h>

#include <xrpl/basics/Log.h>

#include <memory>

#ifdef _DEBUG
// #define DEBUG_OUTPUT 1
#endif
// #define SHOW_CALL_TIME 1

namespace xrpl {

namespace {

void
print_wasm_error(std::string_view msg, wasm_trap_t* trap, beast::Journal jlog)
{
#ifdef DEBUG_OUTPUT
    auto& j = std::cerr;
#else
    auto j = jlog.warn();
    if (jlog.active(beast::severities::kWarning))
#endif
    {
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
    }

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
    StorePtr& s,
    ModulePtr& m,
    WasmExternVec& expt,
    WasmExternVec const& imports,
    beast::Journal j)
{
    wasm_trap_t* trap = nullptr;
    InstancePtr mi = InstancePtr(
        wasm_instance_new(s.get(), m.get(), &imports.vec_, &trap),
        &wasm_instance_delete);

    if (!mi || trap)
    {
        print_wasm_error("can't create instance", trap, j);
        throw std::runtime_error("can't create instance");
    }
    wasm_instance_exports(mi.get(), &expt.vec_);
    return mi;
}

InstanceWrapper::InstanceWrapper() : instance_(nullptr, &wasm_instance_delete)
{
}

// LCOV_EXCL_START
InstanceWrapper::InstanceWrapper(InstanceWrapper&& o)
    : instance_(nullptr, &wasm_instance_delete)
{
    *this = std::move(o);
}
// LCOV_EXCL_STOP

InstanceWrapper::InstanceWrapper(
    StorePtr& s,
    ModulePtr& m,
    WasmExternVec const& imports,
    beast::Journal j)
    : store_(s.get()), instance_(init(s, m, exports_, imports, j)), j_(j)
{
}

InstanceWrapper&
InstanceWrapper::operator=(InstanceWrapper&& o)
{
    if (this == &o)
        return *this;  // LCOV_EXCL_LINE

    store_ = o.store_;
    o.store_ = nullptr;
    exports_ = std::move(o.exports_);
    memIdx_ = o.memIdx_;
    o.memIdx_ = -1;
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
    WasmExporttypeVec const& exportTypes) const
{
    wasm_func_t const* f = nullptr;
    wasm_functype_t const* ft = nullptr;

    if (!instance_)
        throw std::runtime_error("no instance");  // LCOV_EXCL_LINE

    if (!exportTypes.vec_.size)
        throw std::runtime_error("no export");  // LCOV_EXCL_LINE
    if (exportTypes.vec_.size != exports_.vec_.size)
        throw std::runtime_error("invalid export");  // LCOV_EXCL_LINE

    for (unsigned i = 0; i < exportTypes.vec_.size; ++i)
    {
        auto const* expType(exportTypes.vec_.data[i]);

        wasm_name_t const* name = wasm_exporttype_name(expType);
        wasm_externtype_t const* exnType = wasm_exporttype_type(expType);
        if (wasm_externtype_kind(exnType) == WASM_EXTERN_FUNC)
        {
            if (funcName != std::string_view(name->data, name->size))
                continue;

            auto const* exn(exports_.vec_.data[i]);
            if (wasm_extern_kind(exn) != WASM_EXTERN_FUNC)
                throw std::runtime_error("invalid export");  // LCOV_EXCL_LINE

            ft = wasm_externtype_as_functype_const(exnType);
            f = wasm_extern_as_func_const(exn);
            break;
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
    if (memIdx_ >= 0)
    {
        auto* e(exports_.vec_.data[memIdx_]);
        wasm_memory_t* mem = wasm_extern_as_memory(e);
        return {
            reinterpret_cast<std::uint8_t*>(wasm_memory_data(mem)),
            wasm_memory_data_size(mem)};
    }

    wasm_memory_t* mem = nullptr;
    for (int i = 0; i < exports_.vec_.size; ++i)
    {
        auto* e(exports_.vec_.data[i]);
        if (wasm_extern_kind(e) == WASM_EXTERN_MEMORY)
        {
            memIdx_ = i;
            mem = wasm_extern_as_memory(e);
            break;
        }
    }

    if (!mem)
        return {};  // LCOV_EXCL_LINE

    return {
        reinterpret_cast<std::uint8_t*>(wasm_memory_data(mem)),
        wasm_memory_data_size(mem)};
}

std::int64_t
InstanceWrapper::getGas() const
{
    if (!store_)
        return -1;  // LCOV_EXCL_LINE
    std::uint64_t gas = 0;
    wasm_store_get_fuel(store_, &gas);
    return static_cast<std::int64_t>(gas);
}

std::int64_t
InstanceWrapper::setGas(std::int64_t gas) const
{
    if (!store_)
        return -1;  // LCOV_EXCL_LINE

    if (gas < 0)
        gas = std::numeric_limits<decltype(gas)>::max();
    wasmi_error_t* err =
        wasm_store_set_fuel(store_, static_cast<std::uint64_t>(gas));
    if (err)
    {
        // LCOV_EXCL_START
        print_wasm_error("Can't set instance gas", nullptr, j_);
        wasmi_error_delete(err);
        return -1;
        // LCOV_EXCL_STOP
    }

    return gas;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////

ModulePtr
ModuleWrapper::init(StorePtr& s, Bytes const& wasmBin, beast::Journal j)
{
    wasm_byte_vec_t const code{wasmBin.size(), (char*)(wasmBin.data())};
    ModulePtr m =
        ModulePtr(wasm_module_new(s.get(), &code), &wasm_module_delete);
    if (!m)
        throw std::runtime_error("can't create module");

    return m;
}

// LCOV_EXCL_START
ModuleWrapper::ModuleWrapper() : module_(nullptr, &wasm_module_delete)
{
}

ModuleWrapper::ModuleWrapper(ModuleWrapper&& o)
    : module_(nullptr, &wasm_module_delete)
{
    *this = std::move(o);
}
// LCOV_EXCL_STOP

ModuleWrapper::ModuleWrapper(
    StorePtr& s,
    Bytes const& wasmBin,
    bool instantiate,
    std::shared_ptr<ImportVec> const& imports,
    beast::Journal j)
    : module_(init(s, wasmBin, j)), j_(j)
{
    wasm_module_exports(module_.get(), &exportTypes_.vec_);
    auto wimports = buildImports(s, imports);
    if (instantiate)
    {
        addInstance(s, wimports);
    }
}

// LCOV_EXCL_START
ModuleWrapper&
ModuleWrapper::operator=(ModuleWrapper&& o)
{
    if (this == &o)
        return *this;

    module_ = std::move(o.module_);
    instanceWrap_ = std::move(o.instanceWrap_);
    exportTypes_ = std::move(o.exportTypes_);
    j_ = o.j_;

    return *this;
}

ModuleWrapper::operator bool() const
{
    return instanceWrap_;
}

// LCOV_EXCL_STOP

static WasmValtypeVec
makeImpParams(WasmImportFunc const& imp)
{
    auto const paramSize = imp.params.size();
    if (!paramSize)
        return {};

    WasmValtypeVec v(paramSize);

    for (unsigned i = 0; i < paramSize; ++i)
    {
        auto const vt = imp.params[i];
        switch (vt)
        {
            case WT_I32:
                v.vec_.data[i] = wasm_valtype_new_i32();
                break;
            case WT_I64:
                v.vec_.data[i] = wasm_valtype_new_i64();
                break;
                // LCOV_EXCL_START
            default:
                throw std::runtime_error("invalid import type");
                // LCOV_EXCL_STOP
        }
    }
    return v;
}

static WasmValtypeVec
makeImpReturn(WasmImportFunc const& imp)
{
    if (!imp.result)
        return {};  // LCOV_EXCL_LINE

    WasmValtypeVec v(1);
    switch (*imp.result)
    {
        case WT_I32:
            v.vec_.data[0] = wasm_valtype_new_i32();
            break;
            // LCOV_EXCL_START
        case WT_I64:
            v.vec_.data[0] = wasm_valtype_new_i64();
            break;
        default:
            throw std::runtime_error("invalid return type");
            // LCOV_EXCL_STOP
    }
    return v;
}

WasmExternVec
ModuleWrapper::buildImports(
    StorePtr& s,
    std::shared_ptr<ImportVec> const& imports)
{
    WasmImporttypeVec importTypes;
    wasm_module_imports(module_.get(), &importTypes.vec_);

    if (!importTypes.vec_.size)
        return {};
    if (!imports)
        throw std::runtime_error("Missing imports");

    WasmExternVec wimports(importTypes.vec_.size);

    unsigned impCnt = 0;
    for (unsigned i = 0; i < importTypes.vec_.size; ++i)
    {
        wasm_importtype_t const* importType = importTypes.vec_.data[i];

        // wasm_name_t const* mn = wasm_importtype_module(importtype);
        // auto modName = std::string_view(mn->data, mn->num_elems);
        wasm_name_t const* fn = wasm_importtype_name(importType);
        auto fieldName = std::string_view(fn->data, fn->size);

        wasm_externkind_t const itype =
            wasm_externtype_kind(wasm_importtype_type(importType));
        if ((itype) != WASM_EXTERN_FUNC)
            throw std::runtime_error(
                "Invalid import type " +
                std::to_string(itype));  // LCOV_EXCL_LINE

        // for multi-module support
        // if ((W_ENV != modName) && (W_HOST_LIB != modName))
        //     continue;

        bool impSet = false;
        for (auto const& obj : *imports)
        {
            auto const& imp = obj.second;
            if (imp.name != fieldName)
                continue;

            WasmValtypeVec params(makeImpParams(imp));
            WasmValtypeVec results(makeImpReturn(imp));

            std::unique_ptr<wasm_functype_t, decltype(&wasm_functype_delete)>
                ftype(
                    wasm_functype_new(&params.vec_, &results.vec_),
                    &wasm_functype_delete);

            params.release();
            results.release();

            wasm_func_t* func = wasm_func_new_with_env(
                s.get(),
                ftype.get(),
                reinterpret_cast<wasm_func_callback_with_env_t>(imp.wrap),
                (void*)&obj,
                nullptr);
            if (!func)
            {
                // LCOV_EXCL_START
                throw std::runtime_error(
                    "can't create import function " + imp.name);
                // LCOV_EXCL_STOP
            }

            wimports.vec_.data[i] = wasm_func_as_extern(func);
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

    if (impCnt != importTypes.vec_.size)
    {
        print_wasm_error(
            std::string("Imports not finished: ") + std::to_string(impCnt) +
                "/" + std::to_string(importTypes.vec_.size),
            nullptr,
            j_);
        throw std::runtime_error("Missing imports");
    }

    return wimports;
}

FuncInfo
ModuleWrapper::getFunc(std::string_view funcName) const
{
    return instanceWrap_.getFunc(funcName, exportTypes_);
}

wasm_functype_t*
ModuleWrapper::getFuncType(std::string_view funcName) const
{
    for (size_t i = 0; i < exportTypes_.vec_.size; i++)
    {
        auto const* exp_type(exportTypes_.vec_.data[i]);
        wasm_name_t const* name = wasm_exporttype_name(exp_type);
        wasm_externtype_t const* exn_type = wasm_exporttype_type(exp_type);
        if (wasm_externtype_kind(exn_type) == WASM_EXTERN_FUNC &&
            funcName == std::string_view(name->data, name->size))
        {
            return wasm_externtype_as_functype(
                const_cast<wasm_externtype_t*>(exn_type));
        }
    }

    throw std::runtime_error(
        "can't find function <" + std::string(funcName) + ">");
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
ModuleWrapper::addInstance(StorePtr& s, WasmExternVec const& imports)
{
    instanceWrap_ = {s, module_, imports, j_};
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
    return instanceWrap_ ? instanceWrap_.getGas() : -1;
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
            nullptr, &wasm_engine_delete};  // LCOV_EXCL_LINE
    wasmi_config_consume_fuel_set(config, true);
    wasmi_config_ignore_custom_sections_set(config, true);
    wasmi_config_wasm_mutable_globals_set(config, false);
    wasmi_config_wasm_multi_value_set(config, false);
    wasmi_config_wasm_sign_extension_set(config, false);
    wasmi_config_wasm_saturating_float_to_int_set(config, false);
    wasmi_config_wasm_bulk_memory_set(config, false);
    wasmi_config_wasm_reference_types_set(config, false);
    wasmi_config_wasm_tail_call_set(config, false);
    wasmi_config_wasm_extended_const_set(config, false);
    wasmi_config_floats_set(config, false);
    wasmi_config_wasm_multi_memory_set(config, false);
    wasmi_config_wasm_custom_page_sizes_set(config, false);
    wasmi_config_wasm_memory64_set(config, false);
    wasmi_config_wasm_wide_arithmetic_set(config, false);

    return std::unique_ptr<wasm_engine_t, decltype(&wasm_engine_delete)>(
        wasm_engine_new_with_config(config), &wasm_engine_delete);
}

WasmiEngine::WasmiEngine()
    : engine_(init()), store_(nullptr, &wasm_store_delete)
{
}

int
WasmiEngine::addModule(Bytes const& wasmCode, bool instantiate, int64_t gas)
{
    moduleWrap_.reset();
    store_.reset();  // to free the memory before creating new store
    store_ = {
        wasm_store_new_with_memory_max_pages(engine_.get(), MAX_PAGES),
        &wasm_store_delete};

    if (gas < 0)
        gas = std::numeric_limits<decltype(gas)>::max();
    wasmi_error_t* err =
        wasm_store_set_fuel(store_.get(), static_cast<std::uint64_t>(gas));
    if (err)
    {
        // LCOV_EXCL_START
        print_wasm_error("Error setting gas", nullptr, j_);
        wasmi_error_delete(err);
        throw std::runtime_error("can't set gas");
        // LCOV_EXCL_STOP
    }

    moduleWrap_ = std::make_unique<ModuleWrapper>(
        store_, wasmCode, instantiate, imports_, j_);

    if (!moduleWrap_)
        throw std::runtime_error(
            "can't create module wrapper");  // LCOV_EXCL_LINE

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
            // LCOV_EXCL_START
            case WT_I64:
                v.push_back(WASM_I64_VAL(p.of.i64));
                break;
            // LCOV_EXCL_STOP
            case WT_U8V: {
                auto mem = getMem();
                if (!mem.s)
                    throw std::runtime_error(
                        "no memory exported");  // LCOV_EXCL_LINE
                auto const sz = p.of.u8v.sz;
                auto const ptr = allocate(sz);
                memcpy(mem.p + ptr, p.of.u8v.d, sz);
                v.push_back(WASM_I32_VAL(ptr));
                v.push_back(WASM_I32_VAL(sz));
            }
            break;
            // LCOV_EXCL_START
            default:
                throw std::runtime_error(
                    "unknown parameter type: " + std::to_string(p.type));
                break;
                // LCOV_EXCL_STOP
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

// LCOV_EXCL_START
void
WasmiEngine::add_param(std::vector<wasm_val_t>& in, int32_t p)
{
    in.emplace_back();
    auto& el(in.back());
    memset(&el, 0, sizeof(el));
    el = WASM_I32_VAL(p);  // WASM_I32;
}

// LCOV_EXCL_STOP

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

    wasm_trap_t* trap = wasm_func_call(f.first, &inv, &ret.r.vec_);

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
    int32_t sz,
    Types&&... args)
{
    auto mem = getMem();
    if (!mem.s)
        throw std::runtime_error("no memory exported");  // LCOV_EXCL_LINE

    auto const ptr = allocate(sz);
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

static inline void
checkImports(ImportVec const& imports, HostFunctions* hfs)
{
    for (auto const& obj : imports)
    {
        if (hfs != obj.first)
            Throw<std::runtime_error>("Imports hf unsync");
    }
}

Expected<WasmResult<int32_t>, TER>
WasmiEngine::run(
    Bytes const& wasmCode,
    std::string_view funcName,
    std::vector<WasmParam> const& params,
    std::shared_ptr<ImportVec> const& imports,
    std::shared_ptr<HostFunctions> const& hfs,
    int64_t gas,
    beast::Journal j)
{
    j_ = j;

    if (!wasmCode.empty())
    {  // save values for reuse
        imports_ = imports;
        hfs_ = hfs;
    }

    try
    {
        if (imports_)
            checkImports(*imports_, hfs.get());
        return runHlp(wasmCode, funcName, params, gas);
    }
    catch (std::exception const& e)
    {
        print_wasm_error(std::string("exception: ") + e.what(), nullptr, j_);
    }
    // LCOV_EXCL_START
    catch (...)
    {
        print_wasm_error(std::string("exception: unknown"), nullptr, j_);
    }
    // LCOV_EXCL_STOP
    return Unexpected<TER>(tecFAILED_PROCESSING);
}

Expected<WasmResult<int32_t>, TER>
WasmiEngine::runHlp(
    Bytes const& wasmCode,
    std::string_view funcName,
    std::vector<WasmParam> const& params,
    int64_t gas)
{
    // currently only 1 module support, possible parallel UT run
    std::lock_guard<decltype(m_)> lg(m_);

    // Create and instantiate the module.
    if (!wasmCode.empty())
    {
        [[maybe_unused]] int const m = addModule(wasmCode, true, gas);
    }

    if (!moduleWrap_ || !moduleWrap_->instanceWrap_)
        throw std::runtime_error("no instance");  // LCOV_EXCL_LINE

    if (hfs_)
        hfs_->setRT(&getRT());

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
    else if (!res.r.vec_.size)
        throw std::runtime_error(
            "<" + std::string(funcName) +
            "> return nothing");  // LCOV_EXCL_LINE
    else if (res.r.vec_.data[0].kind != WASM_I32)
        throw std::runtime_error(
            "<" + std::string(funcName) + "> return type mismatch, ret: " +
            std::to_string(static_cast<int>(res.r.vec_.data[0].kind)));

    if (gas == -1)
        gas = std::numeric_limits<decltype(gas)>::max();
    WasmResult<int32_t> const ret{
        res.r.vec_.data[0].of.i32, gas - moduleWrap_->getGas()};

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
    std::shared_ptr<ImportVec> const& imports,
    std::shared_ptr<HostFunctions> const& hfs,
    beast::Journal j)
{
    j_ = j;

    if (!wasmCode.empty())
    {
        imports_ = imports;
        hfs_ = hfs;
    }

    try
    {
        if (imports_)
            checkImports(*imports_, hfs_.get());
        return checkHlp(wasmCode, funcName, params);
    }
    catch (std::exception const& e)
    {
        print_wasm_error(std::string("exception: ") + e.what(), nullptr, j_);
    }
    // LCOV_EXCL_START
    catch (...)
    {
        print_wasm_error(std::string("exception: unknown"), nullptr, j_);
    }
    // LCOV_EXCL_STOP

    return temBAD_WASM;
}

NotTEC
WasmiEngine::checkHlp(
    Bytes const& wasmCode,
    std::string_view funcName,
    std::vector<WasmParam> const& params)
{
    // currently only 1 module support, possible parallel UT run
    std::lock_guard<decltype(m_)> lg(m_);

    // Create and instantiate the module.
    if (wasmCode.empty())
        throw std::runtime_error("empty nodule");

    int const m = addModule(wasmCode, false, -1);
    if ((m < 0) || !moduleWrap_)
        throw std::runtime_error("no module");  // LCOV_EXCL_LINE

    // Looking for a func and compare parameter types
    auto const f =
        moduleWrap_->getFuncType(!funcName.empty() ? funcName : "_start");
    auto const* ftp = wasm_functype_params(f);
    auto const p = convertParams(params);

    if (int const comp = compareParamTypes(ftp, p); comp >= 0)
        throw std::runtime_error(
            "invalid parameter type #" + std::to_string(comp));

    return tesSUCCESS;
}

// LCOV_EXCL_START
std::int64_t
WasmiEngine::getGas()
{
    return moduleWrap_ ? moduleWrap_->getGas() : -1;
}
// LCOV_EXCL_STOP

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
    if (sz <= 0)
        throw std::runtime_error(
            "can't allocate memory, " + std::to_string(sz) + " bytes");

    auto res = call<1>(W_ALLOC, sz);

    if (res.f || !res.r.vec_.size || (res.r.vec_.data[0].kind != WASM_I32))
        throw std::runtime_error(
            "can't allocate memory, " + std::to_string(sz) +
            " bytes");  // LCOV_EXCL_LINE

    int32_t const p = res.r.vec_.data[0].of.i32;
    auto const mem = getMem();
    if (p <= 0 || p + sz > mem.s)
        throw std::runtime_error(
            "invalid memory allocation, " + std::to_string(sz) + " bytes");

    return p;
}

wasm_trap_t*
WasmiEngine::newTrap(std::string const& txt)
{
    static char empty[1] = {0};
    wasm_message_t msg = {1, empty};

    if (!txt.empty())
        wasm_name_new(&msg, txt.size() + 1, txt.c_str());  // include 0

    wasm_trap_t* trap = wasm_trap_new(store_.get(), &msg);

    if (!txt.empty())
        wasm_byte_vec_delete(&msg);

    return trap;
}

// LCOV_EXCL_START
beast::Journal
WasmiEngine::getJournal() const
{
    return j_;
}
// LCOV_EXCL_STOP

}  // namespace xrpl
