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

#include <xrpld/app/wasm/WasmtimeVM.h>

#include <xrpl/basics/Log.h>

// #include <cstdarg>
#include <memory>

#ifdef _DEBUG
// #define DEBUG_OUTPUT 1
// #define DEBUG_OUTPUT_Wasmtime 1
#endif

// #define SHOW_CALL_TIME 1

namespace ripple {

namespace {

// LCOV_EXCL_START
// static log_level_t
// getLogLevel(beast::severities::Severity severity)
// {
//     using namespace beast::severities;
//     switch (severity)
//     {
//         case kTrace:
//             return WASM_LOG_LEVEL_VERBOSE;
//         case kDebug:
//             return WASM_LOG_LEVEL_DEBUG;
//         case kInfo:
//         case kWarning:
//             return WASM_LOG_LEVEL_WARNING;
//         case kError:
//             return WASM_LOG_LEVEL_ERROR;
//         default:
//             UNREACHABLE("Wasmtime invalid severity");
//             [[fallthrough]];
//         case kFatal:
//         case kNone:
//             break;
//     }

//     return WASM_LOG_LEVEL_FATAL;
// }

// static beast::severities::Severity
// getLogLevel(uint32_t severity)
// {
//     using namespace beast::severities;
//     switch (severity)
//     {
//         case WASM_LOG_LEVEL_VERBOSE:
//             return kTrace;
//         case WASM_LOG_LEVEL_DEBUG:
//             return kDebug;
//         case WASM_LOG_LEVEL_WARNING:
//             return kWarning;
//         case WASM_LOG_LEVEL_ERROR:
//             return kError;
//         default:
//             UNREACHABLE("Wasmtime invalid reverse severity");
//             [[fallthrough]];
//         case WASM_LOG_LEVEL_FATAL:
//             break;
//     }

//     return kFatal;
// }

// // This function is called from Wasmtime to log messages.
// extern "C" void
// Wasmtime_log_to_rippled(
//     uint32_t logLevel,
//     char const* file,
//     int line,
//     char const* fmt,
//     ...)
// {
//     beast::Journal j = WasmEngine::instance().getJournal();

//     std::ostringstream oss;

//     // Format the variadic args
//     if (file)
//     {
//         oss << "Wasmtime (" << file << ":" << line << "): ";
//     }
//     else
//     {
//         oss << "Wasmtime: ";
//     }

//     va_list args;
//     va_start(args, fmt);

//     char formatted[4096];
//     vsnprintf(formatted, sizeof(formatted), fmt, args);
//     formatted[sizeof(formatted) - 1] = '\0';

//     va_end(args);

//     oss << formatted;

//     j.stream(getLogLevel(logLevel)) << oss.str();
// #ifdef DEBUG_OUTPUT_Wasmtime
//     std::cerr << oss.str() << std::endl;
// #endif
// }

void
print_wasm_error(
    std::string_view msg,
    wasm_trap_t* trap,
    beast::Journal jlog,
    wasmtime_error_t* err = nullptr)
{
#ifdef DEBUG_OUTPUT
    auto& j = std::cerr;
#else
    auto j = jlog.warn();
#endif

    wasm_byte_vec_t error_message WASM_EMPTY_VEC;

    if (trap)
        wasm_trap_message(trap, &error_message);
    else if (err)
        wasmtime_error_message(err, &error_message);

    if (error_message.size)
    {
        j << "Wasmtime Error: " << msg << ", "
          << std::string_view(error_message.data, error_message.size);
    }
    else
        j << "Wasmtime Error: " << msg;

    if (error_message.size)
        wasm_byte_vec_delete(&error_message);

    if (trap)
        wasm_trap_delete(trap);
    if (err)
        wasmtime_error_delete(err);

#ifdef DEBUG_OUTPUT
    j << std::endl;
#endif
}

static wasm_trap_t*
_proc_exit(
    void* env,
    wasmtime_caller_t* caller,
    wasmtime_val_t const* args,
    size_t nargs,
    wasmtime_val_t* results,
    size_t nresults)
{
    if (nargs)
        std::cout << "Exit called: " << std::to_string(args[0].of.i32)
                  << std::endl;
    return nullptr;
}

// LCOV_EXCL_STOP

}  // namespace

void
InstanceWrapper::checkImport(
    std::vector<wasmtime_extern_t>& out,
    wasmtime_context_t* ct,
    wasmtime_module_t* m,
    std::vector<wasmtime_extern_t> const& in)
{
    wasm_importtype_vec_t impts = WASM_EMPTY_VEC;
    wasmtime_module_imports(m, &impts);

    for (int i = 0; i < impts.size; ++i)
    {
        auto const* impt(impts.data[i]);

        wasm_name_t const* name = wasm_importtype_name(impt);
        wasm_externtype_t const* xtype = wasm_importtype_type(impt);
        if ((wasm_externtype_kind(xtype) == WASM_EXTERN_FUNC) &&
            (W_PROC_EXIT == std::string_view(name->data, name->size)))
        {
            std::unique_ptr<wasm_functype_t, decltype(&wasm_functype_delete)>
                ftype(
                    wasm_functype_new_1_0(wasm_valtype_new(WASM_I32)),
                    &wasm_functype_delete);

            wasmtime_func_t func;
            wasmtime_func_new(
                ct, ftype.get(), &_proc_exit, nullptr, nullptr, &func);

            out.push_back({.kind = WASMTIME_EXTERN_FUNC, .of = {.func = func}});

            break;
        }
    }

    for (auto const& imp : in)
        out.push_back(imp);
}

InstancePtr
InstanceWrapper::init(
    wasmtime_context_t* c,
    wasmtime_module_t* m,
    int32_t maxPages,
    wasm_extern_vec_t* expt,
    std::vector<wasmtime_extern_t> const& imports,
    beast::Journal j)
{
    wasm_trap_t* trap = nullptr;
    // InstantiationArgs inst_args{128 * 1024, 256 * 1024,
    // static_cast<uint32_t>(maxPages > 0 ? maxPages : 0)};

    InstancePtr mi = {0, 0};

    std::vector<wasmtime_extern_t> imports2;
    checkImport(imports2, c, m, imports);

    auto* e = wasmtime_instance_new(
        c, m, imports.data(), imports2.size(), &mi, &trap);
    if (e || trap)
    {
        print_wasm_error("can't create instance", trap, j);
        throw std::runtime_error("can't create instance");
    }
    // wasm_instance_exports(mi.get(), expt);
    // char* name;
    // std::size_t len;
    // wasmtime_extern_t item;
    // for (unsigned idx = 0;
    //      wasmtime3_instance_export_nth(ct, &mi, idx, &name, &len, &item);
    //      ++idx)
    //     expt.emplace_back(std::string(name, len), item);

    return mi;
}

InstanceWrapper::InstanceWrapper(InstanceWrapper&& o)
    : ctx_(nullptr), instance_(0, 0)
{
    *this = std::move(o);
}

InstanceWrapper::InstanceWrapper(
    wasmtime_context_t* c,
    wasmtime_module_t* m,
    int32_t maxPages,
    int64_t gas,
    std::vector<wasmtime_extern_t> const& imports,
    beast::Journal j)
    : ctx_(c), instance_(init(c, m, maxPages, nullptr, imports, j)), j_(j)
{
    // wasm_runtime_set_instruction_count_limit(execEnv_, gas);
}

InstanceWrapper::~InstanceWrapper()
{
    // if (exports.size) wasmtime2_extern_vec_delete(&exports);
}

InstanceWrapper&
InstanceWrapper::operator=(InstanceWrapper&& o)
{
    if (this == &o)
        return *this;

    // if (exports.size) wasmtime2_extern_vec_delete(&exports);
    // exports = std::move(o.exports);
    // o.exports = {0, nullptr}

    ctx_ = o.ctx_;
    o.ctx_ = nullptr;
    instance_ = o.instance_;
    o.instance_ = {0, 0};

    j_ = o.j_;

    return *this;
}

InstanceWrapper::operator bool() const
{
    return true;  // static_cast<bool>(instance_.store_id >= 0);
}

FuncInfo
InstanceWrapper::getFunc(std::string_view funcName) const
{
    wasmtime_extern_t item;
    bool ok = wasmtime_instance_export_get(
        ctx_, &instance_, funcName.data(), funcName.size(), &item);

    // if (exports.empty())
    //     throw std::runtime_error(
    //         std::string(engineName(wasmEngines::Time)) + ": no export");

    // for (auto& [name, ext] : exports)
    // {
    //     if ((ext.kind == WASMTIME_EXTERN_FUNC) && (funcName == name))
    //     {
    //         f = &ext.of.func;
    //         break;
    //     }
    // }

    if (!(ok && item.kind == WASMTIME_EXTERN_FUNC))
        throw std::runtime_error(
            "can't find function <" + std::string(funcName) + ">");

    return {item.of.func, wasmtime_func_type(ctx_, &item.of.func)};
}

wmem
InstanceWrapper::getMem() const
{
    wasmtime_extern_t item;
    bool ok = wasmtime_instance_export_get(
        ctx_, &instance_, W_MEM.data(), W_MEM.size(), &item);

    // if (exports.empty())
    //     throw std::runtime_error(
    //         std::string(engineName(wasmEngines::Time)) + ": no export");

    // wasmtime_memory_t* mem = nullptr;
    // for (auto& [name, ext] : exports)
    // {
    //     if ((ext.kind == WASMTIME_EXTERN_MEMORY))
    //     {
    //         mem = &ext.of.memory;
    //         break;
    //     }
    // }

    // if (!mem)
    if (!(ok && item.kind == WASMTIME_EXTERN_MEMORY))
        throw std::runtime_error("no memory exported");

    auto& mem(item.of.memory);
    return {
        wasmtime_memory_data(ctx_, &mem),
        wasmtime_memory_data_size(ctx_, &mem)};
}

std::int64_t
InstanceWrapper::getGas() const
{
    return 0;  // wasm_runtime_get_instruction_count_limit(execEnv_)
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////

ModulePtr
ModuleWrapper::init(wasm_engine_t* e, Bytes const& wasmBin, beast::Journal j)
{
    wasmtime_module_t* m = nullptr;
    auto* err = wasmtime_module_new(e, wasmBin.data(), wasmBin.size(), &m);
    if (err || !m)
        throw std::runtime_error("can't create module");

    return {m, &wasmtime_module_delete};
}

ModuleWrapper::ModuleWrapper()
    : ctx_(nullptr), module_(nullptr, &wasmtime_module_delete)
{
}

ModuleWrapper::ModuleWrapper(ModuleWrapper&& o)
    : ctx_(nullptr), module_(nullptr, &wasmtime_module_delete)
{
    *this = std::move(o);
}

ModuleWrapper::ModuleWrapper(
    wasm_engine_t* e,
    wasmtime_context_t* c,
    Bytes const& wasmBin,
    bool instantiate,
    int32_t maxPages,
    int64_t gas,
    std::vector<WasmImportFunc> const& imports,
    beast::Journal j)
    : ctx_(c), module_(init(e, wasmBin, j)), j_(j)
{
    // wasmtime2_module_exports(module.get(), &export_types);
    if (instantiate)
    {
        auto wimports = buildImports(imports);
        addInstance(maxPages, gas, wimports);
    }
}

ModuleWrapper&
ModuleWrapper::operator=(ModuleWrapper&& o)
{
    if (this == &o)
        return *this;

    ctx_ = o.ctx_;
    o.ctx_ = nullptr;
    module_ = std::move(o.module_);
    instanceWrap_ = std::move(o.instanceWrap_);
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
        // v.size = paramSize;
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
        // v.size = 1;
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

std::vector<wasmtime_extern_t>
ModuleWrapper::buildImports(std::vector<WasmImportFunc> const& imports)
{
    std::vector<wasmtime_extern_t> wimports;

    wasm_importtype_vec_t importTypes = WASM_EMPTY_VEC;
    wasmtime_module_imports(module_.get(), &importTypes);
    std::
        unique_ptr<wasm_importtype_vec_t, decltype(&wasm_importtype_vec_delete)>
            itDeleter(&importTypes, &wasm_importtype_vec_delete);

    if (!importTypes.size)
        return wimports;

    wimports.resize(importTypes.size);
    std::memset(wimports.data(), 0, wimports.size() * sizeof(wimports[0]));

    unsigned impCnt = 0;
    for (unsigned i = 0; i < importTypes.size; ++i)
    {
        wasm_importtype_t const* importtype = importTypes.data[i];
        wasm_name_t const* fn = wasm_importtype_name(importtype);
        auto fieldName = std::string_view(fn->data, fn->size);

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
            wasmtime_func_t func;
            wasmtime_func_new(
                ctx_,
                ftype.get(),
                reinterpret_cast<wasmtime_func_callback_t>(imp.wrap),
                imp.udata,
                nullptr,
                &func);

            // TODO WASM
            // if (imp.gas && !wasm_func_set_gas(func, imp.gas))
            // {
            //     // LCOV_EXCL_START
            //     throw std::runtime_error(
            //         "can't set gas for import function " + imp.name);
            //     // LCOV_EXCL_STOP
            // }

            wimports[i] = {.kind = WASMTIME_EXTERN_FUNC, .of = {.func = func}};
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
    return instanceWrap_.getFunc(funcName);
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
ModuleWrapper::addInstance(
    int32_t maxPages,
    int64_t gas,
    std::vector<wasmtime_extern_t> const& imports)
{
    instanceWrap_ = {ctx_, module_.get(), maxPages, gas, imports, j_};
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
    return instanceWrap_.getGas();
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////

// void
// WasmtimeEngine::clearModules()
// {
//     modules.clear();
//     store.reset();  // to free the memory before creating new store
//     store = {wasm_store_new(engine.get()), &wasm_store_delete};
//     ctx_ = wasmtime_store_context(store_.get());
// }

bool
WasmtimeEngine::setInterp(wasm_config_t* c, beast::Journal j)
{
    // return true;  // enable jit

    wasmtime_error_t* err = wasmtime_config_target_set(c, "pulley64");
    if (err)
    {
        print_wasm_error("failed to set pulley", nullptr, j, err);
        return false;
    }
    return true;
}

wasm_engine_t*
WasmtimeEngine::init(beast::Journal j)
{
    wasm_config_t* config = wasm_config_new();
    if (!config)
        return nullptr;

    wasmtime_config_consume_fuel_set(config, true);

    if (!setInterp(config, j))
        return nullptr;

    auto* e = wasm_engine_new_with_config(config);

    return e;
}

WasmtimeEngine::WasmtimeEngine()
    : engine_(
          init(beast::Journal(beast::Journal::getNullSink())),
          &wasm_engine_delete)
    , store_(
          wasmtime_store_new(engine_.get(), nullptr, nullptr),
          &wasmtime_store_delete)
    , ctx_(wasmtime_store_context(store_.get()))
{
    // wasm_runtime_set_default_running_mode(Mode_Interp);

    // TODO WASM
    // wasm_runtime_set_log_level(WASM_LOG_LEVEL_FATAL);
    // wasm_runtime_set_log_level(WASM_LOG_LEVEL_VERBOSE);
}

int
WasmtimeEngine::addModule(
    Bytes const& wasmCode,
    bool instantiate,
    int64_t gas,
    std::vector<WasmImportFunc> const& imports)
{
    moduleWrap_.reset();
    store_.reset();  // to free the memory before creating new store
    store_ = {
        wasmtime_store_new(engine_.get(), nullptr, nullptr),
        &wasmtime_store_delete};
    ctx_ = wasmtime_store_context(store_.get());
    moduleWrap_ = std::make_unique<ModuleWrapper>(
        engine_.get(),
        ctx_,
        wasmCode,
        instantiate,
        defMaxPages_,
        gas,
        imports,
        j_);

    if (!moduleWrap_)
        throw std::runtime_error("can't create module wrapper");

    if (gas < 0)
        gas = std::numeric_limits<decltype(gas)>::max();
    wasmtime_context_set_fuel(ctx_, static_cast<std::uint64_t>(gas));

    return moduleWrap_ ? 0 : -1;
}

// int
// WasmtimeEngine::addInstance()
// {
//     return module->addInstance(store.get(), defMaxPages);
// }

FuncInfo
WasmtimeEngine::getFunc(std::string_view funcName)
{
    return moduleWrap_->getFunc(funcName);
}

std::vector<wasmtime_val_t>
WasmtimeEngine::convertParams(std::vector<WasmParam> const& params)
{
    std::vector<wasmtime_val_t> v;
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
WasmtimeEngine::compareParamTypes(
    wasm_valtype_vec_t const* ftp,
    std::vector<wasmtime_val_t> const& p)
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
WasmtimeEngine::add_param(std::vector<wasmtime_val_t>& in, int32_t p)
{
    in.emplace_back();
    auto& el(in.back());
    memset(&el, 0, sizeof(el));
    el = WASM_I32_VAL(p);  // WASM_I32;
}

void
WasmtimeEngine::add_param(std::vector<wasmtime_val_t>& in, int64_t p)
{
    in.emplace_back();
    auto& el(in.back());
    el = WASM_I64_VAL(p);
}

template <int NR, class... Types>
WasmtimeResult
WasmtimeEngine::call(std::string_view func, Types&&... args)
{
    // Lookup our export function
    auto f = getFunc(func);
    return call<NR>(f, std::forward<Types>(args)...);
}

template <int NR, class... Types>
WasmtimeResult
WasmtimeEngine::call(FuncInfo const& f, Types&&... args)
{
    std::vector<wasmtime_val_t> in;
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
WasmtimeResult
WasmtimeEngine::call(FuncInfo const& f, std::vector<wasmtime_val_t>& in)
{
    WasmtimeResult ret(NR);

#ifdef SHOW_CALL_TIME
    auto const start = usecs();
#endif

    wasm_trap_t* trap = nullptr;
    wasmtime_error_t* err = wasmtime_func_call(
        ctx_,
        &f.first,
        in.data(),
        in.size(),
        ret.r.data(),
        ret.r.size(),
        &trap);

#ifdef SHOW_CALL_TIME
    auto const finish = usecs();
    auto const delta_ms = (finish - start) / 1000;
    std::cout << "wasm_func_call: " << delta_ms << "ms" << std::endl;
#endif

    if (err || trap)
    {
        ret.f = true;
        print_wasm_error("failure to call func", trap, j_, err);
    }

    // assert(results[0].kind == WASM_I32);
    // if (NR) printf("Result P5: %d\n", ret[0].of.i32);

    return ret;
}

template <int NR, class... Types>
WasmtimeResult
WasmtimeEngine::call(
    FuncInfo const& f,
    std::vector<wasmtime_val_t>& in,
    std::int32_t p,
    Types&&... args)
{
    add_param(in, p);
    return call<NR>(f, in, std::forward<Types>(args)...);
}

template <int NR, class... Types>
WasmtimeResult
WasmtimeEngine::call(
    FuncInfo const& f,
    std::vector<wasmtime_val_t>& in,
    std::int64_t p,
    Types&&... args)
{
    add_param(in, p);
    return call<NR>(f, in, std::forward<Types>(args)...);
}

template <int NR, class... Types>
WasmtimeResult
WasmtimeEngine::call(
    FuncInfo const& f,
    std::vector<wasmtime_val_t>& in,
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
WasmtimeResult
WasmtimeEngine::call(
    FuncInfo const& f,
    std::vector<wasmtime_val_t>& in,
    Bytes const& p,
    Types&&... args)
{
    return call<NR>(f, in, p.data(), p.size(), std::forward<Types>(args)...);
}

Expected<WasmResult<int32_t>, TER>
WasmtimeEngine::run(
    Bytes const& wasmCode,
    std::string_view funcName,
    std::vector<WasmParam> const& params,
    std::vector<WasmImportFunc> const& imports,
    HostFunctions* hfs,
    int64_t gas,
    beast::Journal j)
{
    j_ = j;
    // wasm_runtime_set_log_level(std::min(getLogLevel(j_.sink().threshold()),
    // WASM_LOG_LEVEL_ERROR));
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
WasmtimeEngine::runHlp(
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

    auto p = convertParams(params);

    if (int const comp = compareParamTypes(ftp, p); comp >= 0)
        throw std::runtime_error(
            "invalid parameter type #" + std::to_string(comp));

    auto const res = call<1>(f, p);

    if (res.f)
        throw std::runtime_error("<" + std::string(funcName) + "> failure");
    else if (res.r.empty())
        throw std::runtime_error(
            "<" + std::string(funcName) + "> return nothing");

    assert(res.r[0].kind == WASM_I32);
    if (gas == -1)
        gas = std::numeric_limits<decltype(gas)>::max();
    WasmResult<int32_t> const ret{res.r[0].of.i32, gas - getGas()};

    // #ifdef DEBUG_OUTPUT
    //     auto& j = std::cerr;
    // #else
    //     auto j = j_.debug();
    // #endif
    // j << "Wasmtime Res: " << ret.result << " cost: " << ret.cost <<
    // std::endl;

    return ret;
}

NotTEC
WasmtimeEngine::check(
    Bytes const& wasmCode,
    std::string_view funcName,
    std::vector<WasmParam> const& params,
    std::vector<WasmImportFunc> const& imports,
    beast::Journal j)
{
    j_ = j;
    // wasm_runtime_set_log_level(std::min(getLogLevel(j_.sink().threshold()),
    // WASM_LOG_LEVEL_ERROR));
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
WasmtimeEngine::checkHlp(
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

std::int32_t
WasmtimeEngine::initMaxPages(std::int32_t def)
{
    defMaxPages_ = def;
    return def;
}

std::int64_t
WasmtimeEngine::getGas()
{
    std::uint64_t gas = 0;
    if (ctx_)
        wasmtime_context_get_fuel(ctx_, &gas);
    return static_cast<std::int64_t>(gas);
}

wmem
WasmtimeEngine::getMem() const
{
    return moduleWrap_ ? moduleWrap_->getMem() : wmem();
}

InstanceWrapper const&
WasmtimeEngine::getRT(int m, int i)
{
    if (!moduleWrap_)
        throw std::runtime_error("no module");
    return moduleWrap_->getInstance(i);
}

int32_t
WasmtimeEngine::allocate(int32_t sz)
{
    auto res = call<1>(W_ALLOC, static_cast<int32_t>(sz));

    if (res.f || res.r.empty() || (res.r[0].kind != WASM_I32) ||
        !res.r[0].of.i32)
        throw std::runtime_error(
            "can't allocate memory, " + std::to_string(sz) + " bytes");
    return res.r[0].of.i32;
}

wasm_trap_t*
WasmtimeEngine::newTrap(std::string_view txt)
{
    wasm_message_t msg = WASM_EMPTY_VEC;

    if (!txt.empty())
        wasm_name_new(&msg, txt.size(), txt.data());

    return nullptr;  // wasm_trap_new(store_.get(), &msg);
}

beast::Journal
WasmtimeEngine::getJournal() const
{
    return j_;
}

}  // namespace ripple
