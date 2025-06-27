//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2020 Ripple Labs Inc.

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

#include <xrpld/app/misc/WamrVM.h>

#include <xrpl/basics/Log.h>

#include <memory>

#ifdef _DEBUG
// #define DEBUG_OUTPUT 1
// #define DEBUG_OUTPUT_WAMR 1
#endif

// #define SHOW_CALL_TIME 1

namespace ripple {

namespace {

static log_level_t
getLogLevel(beast::severities::Severity severity)
{
    using namespace beast::severities;
    switch (severity)
    {
        case kTrace:
            return WASM_LOG_LEVEL_VERBOSE;
        case kDebug:
            return WASM_LOG_LEVEL_DEBUG;
        case kInfo:
        case kWarning:
            return WASM_LOG_LEVEL_WARNING;
        case kError:
            return WASM_LOG_LEVEL_ERROR;
        default:
            UNREACHABLE("WAMR invalid severity");
            [[fallthrough]];
        case kFatal:
        case kNone:
            break;
    }

    return WASM_LOG_LEVEL_FATAL;
}

static beast::severities::Severity
getLogLevel(uint32_t severity)
{
    using namespace beast::severities;
    switch (severity)
    {
        case WASM_LOG_LEVEL_VERBOSE:
            return kTrace;
        case WASM_LOG_LEVEL_DEBUG:
            return kDebug;
        case WASM_LOG_LEVEL_WARNING:
            return kWarning;
        case WASM_LOG_LEVEL_ERROR:
            return kError;
        default:
            UNREACHABLE("WAMR invalid reverse severity");
            [[fallthrough]];
        case WASM_LOG_LEVEL_FATAL:
            break;
    }

    return kFatal;
}

// This function is called from WAMR to log messages.
extern "C" void
wamr_log_to_rippled(
    uint32_t logLevel,
    char const* file,
    int line,
    char const* fmt,
    ...)
{
    beast::Journal j = WasmEngine::instance().getJournal();

    std::ostringstream oss;

    // Format the variadic args
    if (file)
    {
        oss << "WAMR (" << file << ":" << line << "): ";
    }
    else
    {
        oss << "WAMR: ";
    }

    va_list args;
    va_start(args, fmt);

    char formatted[4096];
    vsnprintf(formatted, sizeof(formatted), fmt, args);
    formatted[sizeof(formatted) - 1] = '\0';

    va_end(args);

    oss << formatted;

    j.stream(getLogLevel(logLevel)) << oss.str();
#ifdef DEBUG_OUTPUT_WAMR
    std::cerr << oss.str() << std::endl;
#endif
}

void
print_wasm_error(std::string_view msg, wasm_trap_t* trap, beast::Journal jlog)
{
#ifdef DEBUG_OUTPUT
    auto& j = std::cerr;
#else
    auto j = jlog.error();
#endif

    j << "WAMR Error: " << msg;

    if (trap)
    {
        wasm_byte_vec_t error_message;

        wasm_trap_message(trap, &error_message);
        if (error_message.num_elems)
        {
            j <<
#ifdef DEBUG_OUTPUT
                "\nWAMR "
#else
                "WAMR "
#endif
              << error_message.data;
        }

        if (error_message.size)
            wasm_byte_vec_delete(&error_message);
        wasm_trap_delete(trap);
    }

#ifdef DEBUG_OUTPUT
    j << std::endl;
#endif
}

}  // namespace

InstancePtr
InstanceWrapper::init(
    wasm_store_t* s,
    wasm_module_t* m,
    int32_t maxPages,
    wasm_extern_vec_t* expt,
    wasm_extern_vec_t const& imports,
    beast::Journal j)
{
    wasm_trap_t* trap = nullptr;
    InstantiationArgs inst_args{
        128 * 1024,
        256 * 1024,
        static_cast<uint32_t>(maxPages > 0 ? maxPages : 0)};

    InstancePtr mi = InstancePtr(
        wasm_instance_new_with_args_ex(s, m, &imports, &trap, &inst_args),
        &wasm_instance_delete);

    if (!mi || trap)
    {
        print_wasm_error("can't create instance", trap, j);
        throw std::runtime_error("WAMR can't create instance");
    }
    wasm_instance_exports(mi.get(), expt);
    return mi;
}

InstanceWrapper::InstanceWrapper()
    : exports{0, nullptr, 0, 0, nullptr}
    , mod_inst(nullptr, &wasm_instance_delete)
{
}

InstanceWrapper::InstanceWrapper(InstanceWrapper&& o)
    : exports{0, nullptr, 0, 0, nullptr}
    , mod_inst(nullptr, &wasm_instance_delete)
{
    *this = std::move(o);
}

InstanceWrapper::InstanceWrapper(
    wasm_store_t* s,
    wasm_module_t* m,
    int32_t maxPages,
    int64_t gas,
    wasm_extern_vec_t const& imports,
    beast::Journal j)
    : exports WASM_EMPTY_VEC
    , mod_inst(init(s, m, maxPages, &exports, imports, j))
    , exec_env(wasm_instance_exec_env(mod_inst.get()))
    , j_(j)
{
    wasm_runtime_set_instruction_count_limit(exec_env, gas);
}

InstanceWrapper::~InstanceWrapper()
{
    if (exports.size)
        wasm_extern_vec_delete(&exports);
}

InstanceWrapper&
InstanceWrapper::operator=(InstanceWrapper&& o)
{
    if (this == &o)
        return *this;

    if (exports.size)
        wasm_extern_vec_delete(&exports);
    exports = o.exports;
    o.exports = {0, nullptr, 0, 0, nullptr};

    mod_inst = std::move(o.mod_inst);
    exec_env = o.exec_env;
    o.exec_env = nullptr;

    j_ = o.j_;

    return *this;
}

InstanceWrapper::operator bool() const
{
    return static_cast<bool>(mod_inst);
}

FuncInfo
InstanceWrapper::getFunc(
    std::string_view funcName,
    wasm_exporttype_vec_t const& export_types) const
{
    wasm_func_t* f = nullptr;
    wasm_functype_t* ft = nullptr;

    if (!mod_inst)
        throw std::runtime_error("WAMR no module instance");

    if (!export_types.num_elems)
        throw std::runtime_error("WAMR no export");
    if (export_types.num_elems != exports.num_elems)
        throw std::runtime_error("WAMR invalid export");

    for (unsigned i = 0; i < export_types.num_elems; ++i)
    {
        auto const* exp_type(export_types.data[i]);

        wasm_name_t const* name = wasm_exporttype_name(exp_type);
        wasm_externtype_t const* exn_type = wasm_exporttype_type(exp_type);
        if (wasm_externtype_kind(exn_type) == WASM_EXTERN_FUNC)
        {
            if (funcName == std::string_view(name->data, name->size - 1))
            {
                auto* exn(exports.data[i]);
                if (wasm_extern_kind(exn) != WASM_EXTERN_FUNC)
                    throw std::runtime_error("WAMR invalid export");

                ft = wasm_externtype_as_functype(
                    const_cast<wasm_externtype_t*>(exn_type));
                f = wasm_extern_as_func(exn);
                break;
            }
        }
    }

    if (!f || !ft)
        throw std::runtime_error(
            "WAMR can't find function <" + std::string(funcName) + ">");

    return {f, ft};
}

wmem
InstanceWrapper::getMem() const
{
    if (!mod_inst)
        throw std::runtime_error("WAMR no module instance");

    wasm_memory_t* mem = nullptr;
    for (unsigned i = 0; i < exports.num_elems; ++i)
    {
        auto* e(exports.data[i]);
        if (wasm_extern_kind(e) == WASM_EXTERN_MEMORY)
        {
            mem = wasm_extern_as_memory(e);
            break;
        }
    }

    if (!mem)
        throw std::runtime_error("WAMR no memory exported");

    return {
        reinterpret_cast<std::uint8_t*>(wasm_memory_data(mem)),
        wasm_memory_data_size(mem)};
}

std::int64_t
InstanceWrapper::getGas() const
{
    return exec_env ? wasm_runtime_get_instruction_count_limit(exec_env) : 0;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////

ModulePtr
ModuleWrapper::init(wasm_store_t* s, Bytes const& wasmBin, beast::Journal j)
{
    wasm_byte_vec_t const code{
        wasmBin.size(),
        (char*)(wasmBin.data()),
        wasmBin.size(),
        sizeof(std::remove_reference_t<decltype(wasmBin)>::value_type),
        nullptr};
    ModulePtr m = ModulePtr(wasm_module_new(s, &code), &wasm_module_delete);
    if (!m)
    {
        print_wasm_error("can't create module", nullptr, j);
        throw std::runtime_error("WAMR can't create module");
    }
    return m;
}

ModuleWrapper::ModuleWrapper()
    : module(nullptr, &wasm_module_delete)
    , export_types{0, nullptr, 0, 0, nullptr}
{
}

ModuleWrapper::ModuleWrapper(ModuleWrapper&& o)
    : module(nullptr, &wasm_module_delete)
    , export_types{0, nullptr, 0, 0, nullptr}
{
    *this = std::move(o);
}

ModuleWrapper::ModuleWrapper(
    wasm_store_t* s,
    Bytes const& wasmBin,
    bool instantiate,
    int32_t maxPages,
    int64_t gas,
    std::vector<WasmImportFunc> const& imports,
    beast::Journal j)
    : module(init(s, wasmBin, j))
    , export_types{0, nullptr, 0, 0, nullptr}
    , j_(j)
{
    wasm_module_exports(module.get(), &export_types);
    if (instantiate)
    {
        auto wimports = buildImports(s, imports);
        addInstance(s, maxPages, gas, wimports);
    }
}

ModuleWrapper::~ModuleWrapper()
{
    if (export_types.size)
        wasm_exporttype_vec_delete(&export_types);
}

ModuleWrapper&
ModuleWrapper::operator=(ModuleWrapper&& o)
{
    if (this == &o)
        return *this;

    module = std::move(o.module);
    mod_inst = std::move(o.mod_inst);
    if (export_types.size)
        wasm_exporttype_vec_delete(&export_types);
    export_types = o.export_types;
    o.export_types = {0, nullptr, 0, 0, nullptr};
    j_ = o.j_;

    return *this;
}

ModuleWrapper::operator bool() const
{
    return mod_inst;
}

void
ModuleWrapper::makeImpParams(wasm_valtype_vec_t& v, WasmImportFunc const& imp)
{
    auto const paramSize = imp.params.size();

    if (paramSize)
    {
        wasm_valtype_vec_new(&v, paramSize, nullptr);
        v.num_elems = paramSize;
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
            case WT_F32:
                v.data[i] = wasm_valtype_new_f32();
                break;
            case WT_F64:
                v.data[i] = wasm_valtype_new_f64();
                break;
            default:
                throw std::runtime_error("WAMR Invalid import type");
        }
    }
}

void
ModuleWrapper::makeImpReturn(wasm_valtype_vec_t& v, WasmImportFunc const& imp)
{
    if (imp.result)
    {
        wasm_valtype_vec_new(&v, 1, nullptr);
        v.num_elems = 1;
        switch (*imp.result)
        {
            case WT_I32:
                v.data[0] = wasm_valtype_new_i32();
                break;
            case WT_I64:
                v.data[0] = wasm_valtype_new_i64();
                break;
            case WT_F32:
                v.data[0] = wasm_valtype_new_f32();
                break;
            case WT_F64:
                v.data[0] = wasm_valtype_new_f64();
                break;
            default:
                throw std::runtime_error("WAMR Invalid return type");
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
    wasm_module_imports(module.get(), &importTypes);
    std::
        unique_ptr<wasm_importtype_vec_t, decltype(&wasm_importtype_vec_delete)>
            itDeleter(&importTypes, &wasm_importtype_vec_delete);

    wasm_extern_vec_t wimports = WASM_EMPTY_VEC;
    if (!importTypes.num_elems)
        return wimports;

    wasm_extern_vec_new(&wimports, importTypes.size, nullptr);
    wimports.num_elems = importTypes.num_elems;

    unsigned impCnt = 0;
    for (unsigned i = 0; i < importTypes.num_elems; ++i)
    {
        wasm_importtype_t const* importtype = importTypes.data[i];
        if (wasm_importtype_is_linked(importtype))
        {
            // create a placeholder
            wimports.data[i] = wasm_extern_new_empty(
                s, wasm_externtype_kind(wasm_importtype_type(importtype)));
            ++impCnt;
            continue;
        }

        // wasm_name_t const* mn = wasm_importtype_module(importtype);
        // auto modName = std::string_view(mn->data, mn->num_elems - 1);
        wasm_name_t const* fn = wasm_importtype_name(importtype);
        auto fieldName = std::string_view(fn->data, fn->num_elems - 1);

        // for multi-module support
        // if ((W_ENV != modName) && (W_HOST_LIB != modName))
        //     continue;

        bool impSet = false;
        for (auto const& imp : imports)
        {
            if (imp.name != fieldName)
                continue;

            wasm_valtype_vec_t params, results;
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
                throw std::runtime_error(
                    "can't create import function " +
                    imp.name);  // LCOV_EXCL_LINE

            if (imp.gas && !wasm_func_set_gas(func, imp.gas))
                throw std::runtime_error(
                    "can't set gas for import function " +
                    imp.name);  // LCOV_EXCL_LINE

            wimports.data[i] = wasm_func_as_extern(func);
            ++impCnt;
            impSet = true;

            break;
        }

        if (!impSet)
        {
            print_wasm_error(
                std::string("Import not found: ") + fieldName.data(),
                nullptr,
                j_);
        }
    }

    if (impCnt != importTypes.num_elems)
    {
        print_wasm_error(
            std::string("Imports not finished: ") + std::to_string(impCnt) +
                "/" + std::to_string(importTypes.num_elems),
            nullptr,
            j_);
    }

    return wimports;
}

FuncInfo
ModuleWrapper::getFunc(std::string_view funcName) const
{
    return mod_inst.getFunc(funcName, export_types);
}

wmem
ModuleWrapper::getMem() const
{
    return mod_inst.getMem();
}

InstanceWrapper const&
ModuleWrapper::getInstance(int) const
{
    return mod_inst;
}

int
ModuleWrapper::addInstance(
    wasm_store_t* s,
    int32_t maxPages,
    int64_t gas,
    wasm_extern_vec_t const& imports)
{
    mod_inst = {s, module.get(), maxPages, gas, imports, j_};
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
    return mod_inst.getGas();
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////

// void
// WamrEngine::clearModules()
// {
//     modules.clear();
//     store.reset();  // to free the memory before creating new store
//     store = {wasm_store_new(engine.get()), &wasm_store_delete};
// }

WamrEngine::WamrEngine()
    : engine(wasm_engine_new(), &wasm_engine_delete)
    , store(nullptr, &wasm_store_delete)
{
    wasm_runtime_set_default_running_mode(Mode_Interp);
    wasm_runtime_set_log_level(WASM_LOG_LEVEL_FATAL);
    // wasm_runtime_set_log_level(WASM_LOG_LEVEL_VERBOSE);
}

int
WamrEngine::addModule(
    Bytes const& wasmCode,
    bool instantiate,
    int64_t gas,
    std::vector<WasmImportFunc> const& imports)
{
    module.reset();
    store.reset();  // to free the memory before creating new store
    store = {wasm_store_new(engine.get()), &wasm_store_delete};
    module = std::make_unique<ModuleWrapper>(
        store.get(), wasmCode, instantiate, defMaxPages, gas, imports, j_);

    if (!module)
    {
        print_wasm_error("can't create module wrapper", nullptr, j_);
        throw std::runtime_error("WAMR can't create module wrapper");
    }

    return module ? 0 : -1;
}

// int
// WamrEngine::addInstance()
// {
//     return module->addInstance(store.get(), defMaxPages);
// }

FuncInfo
WamrEngine::getFunc(std::string_view funcName)
{
    return module->getFunc(funcName);
}

std::vector<wasm_val_t>
WamrEngine::convertParams(std::vector<WasmParam> const& params)
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
            case WT_F32:
                v.push_back(WASM_F32_VAL(p.of.f32));
                break;
            case WT_F64:
                v.push_back(WASM_F64_VAL(p.of.f64));
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
                break;
        }
    }

    return v;
}

void
WamrEngine::add_param(std::vector<wasm_val_t>& in, int32_t p)
{
    in.emplace_back();
    auto& el(in.back());
    memset(&el, 0, sizeof(el));
    el = WASM_I32_VAL(p);  // WASM_I32;
}

void
WamrEngine::add_param(std::vector<wasm_val_t>& in, int64_t p)
{
    in.emplace_back();
    auto& el(in.back());
    el = WASM_I64_VAL(p);
}

template <int NR, class... Types>
WamrResult
WamrEngine::call(std::string_view func, Types&&... args)
{
    // Lookup our export function
    auto f = getFunc(func);
    return call<NR>(f, std::forward<Types>(args)...);
}

template <int NR, class... Types>
WamrResult
WamrEngine::call(FuncInfo const& f, Types&&... args)
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
WamrResult
WamrEngine::call(FuncInfo const& f, std::vector<wasm_val_t>& in)
{
    // wasm_val_t rs[1] = {WASM_I32_VAL(0)};
    WamrResult ret(NR);
    // if (NR)  {   wasm_val_vec_new_uninitialized(&ret, NR);    //
    // wasm_val_vec_new(&ret, NR, &rs[0]);    // ret = WASM_ARRAY_VEC(rs);    }

    auto const* ftp = wasm_functype_params(f.second);
    if (ftp->num_elems != in.size())
    {
        print_wasm_error("invalid num of params to call func", nullptr, j_);
        return ret;
    }

    wasm_val_vec_t const inv = in.empty()
        ? wasm_val_vec_t WASM_EMPTY_VEC
        : wasm_val_vec_t{
              in.size(),
              in.data(),
              in.size(),
              sizeof(std::remove_reference_t<decltype(in)>::value_type),
              nullptr};

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
        print_wasm_error("failed to call func", trap, j_);
    }

    // assert(results[0].kind == WASM_I32);
    // if (NR) printf("Result P5: %d\n", ret[0].of.i32);

    return ret;
}

template <int NR, class... Types>
WamrResult
WamrEngine::call(
    FuncInfo const& f,
    std::vector<wasm_val_t>& in,
    std::int32_t p,
    Types&&... args)
{
    add_param(in, p);
    return call<NR>(f, in, std::forward<Types>(args)...);
}

template <int NR, class... Types>
WamrResult
WamrEngine::call(
    FuncInfo const& f,
    std::vector<wasm_val_t>& in,
    std::int64_t p,
    Types&&... args)
{
    add_param(in, p);
    return call<NR>(f, in, std::forward<Types>(args)...);
}

template <int NR, class... Types>
WamrResult
WamrEngine::call(
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
WamrResult
WamrEngine::call(
    FuncInfo const& f,
    std::vector<wasm_val_t>& in,
    Bytes const& p,
    Types&&... args)
{
    return call<NR>(f, in, p.data(), p.size(), std::forward<Types>(args)...);
}

Expected<WasmResult<int32_t>, TER>
WamrEngine::run(
    Bytes const& wasmCode,
    std::string_view funcName,
    std::vector<WasmParam> const& params,
    std::vector<WasmImportFunc> const& imports,
    HostFunctions* hfs,
    int64_t gas,
    beast::Journal j)
{
    j_ = j;
    wasm_runtime_set_log_level(
        std::min(getLogLevel(j_.sink().threshold()), WASM_LOG_LEVEL_ERROR));
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
        print_wasm_error(std::string("unknown exception"), nullptr, j_);
    }
    return Unexpected<TER>(tecFAILED_PROCESSING);
}

Expected<WasmResult<int32_t>, TER>
WamrEngine::runHlp(
    Bytes const& wasmCode,
    std::string_view funcName,
    std::vector<WasmParam> const& params,
    std::vector<WasmImportFunc> const& imports,
    HostFunctions* hfs,
    int64_t gas)
{
    // #ifdef DEBUG_OUTPUT
    //     auto& j = std::cerr;
    // #else
    //     auto j = j_.debug();
    // #endif

    // currently only 1 module support, possible parallel UT run
    static std::mutex m;
    std::lock_guard<decltype(m)> lg(m);

    // Create and instantiate the module.
    if (!wasmCode.empty())
    {
        [[maybe_unused]] int const m = addModule(wasmCode, true, gas, imports);
    }

    if (!module || !module->mod_inst)
    {
        print_wasm_error("no instance to run", nullptr, j_);
        return Unexpected<TER>(tecFAILED_PROCESSING);
    }

    if (hfs)
        hfs->setRT(&getRT());

    // Call main
    auto f = getFunc(!funcName.empty() ? funcName : "_start");
    auto p = convertParams(params);
    auto res = call<1>(f, p);

    if (res.f)
    {
        return Unexpected<TER>(tecFAILED_PROCESSING);
    }
    else if (!res.r.num_elems)
    {
        print_wasm_error(
            "<" + std::string(funcName) + "> return nothing", nullptr, j_);
        return Unexpected<TER>(tecFAILED_PROCESSING);
    }

    assert(res.r.data[0].kind == WASM_I32);
    if (gas == -1)
        gas = std::numeric_limits<decltype(gas)>::max();
    WasmResult<int32_t> const ret{res.r.data[0].of.i32, gas - module->getGas()};

    // j << "WAMR Res: " << ret.result << " cost: " << ret.cost << std::endl;
    return ret;
}

std::int32_t
WamrEngine::initMaxPages(std::int32_t def)
{
    defMaxPages = def;
    return def;
}

std::int64_t
WamrEngine::getGas()
{
    return module ? module->getGas() : 0;
}

wmem
WamrEngine::getMem() const
{
    return module ? module->getMem() : wmem();
}

InstanceWrapper const&
WamrEngine::getRT(int m, int i)
{
    if (!module)
        throw std::runtime_error("WAMR no module");
    return module->getInstance(i);
}

int32_t
WamrEngine::allocate(int32_t sz)
{
    auto res = call<1>(W_ALLOC, static_cast<int32_t>(sz));

    if (res.f || !res.r.num_elems || (res.r.data[0].kind != WASM_I32) ||
        !res.r.data[0].of.i32)
        throw std::runtime_error(
            "WAMR can't allocate memory, " + std::to_string(sz) + " bytes");
    return res.r.data[0].of.i32;
}

wasm_trap_t*
WamrEngine::newTrap(std::string_view txt)
{
    wasm_message_t msg = WASM_EMPTY_VEC;

    if (!txt.empty())
        wasm_name_new(&msg, txt.size(), txt.data());

    return wasm_trap_new(store.get(), &msg);
}

beast::Journal
WamrEngine::getJournal() const
{
    return j_;
}

}  // namespace ripple
