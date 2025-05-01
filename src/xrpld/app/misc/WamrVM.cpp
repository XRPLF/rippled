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

#include <memory>

namespace ripple {

//////////////////////////////////////////////////////////////////////////////////////////

namespace {

static void
print_wasm_error(const char* message, wasm_trap_t* trap)
{
    fprintf(stderr, "WAMR error: %s\n", message);
    wasm_byte_vec_t error_message;

    if (trap)
    {
        wasm_trap_message(trap, &error_message);
        wasm_trap_delete(trap);
        fprintf(
            stderr,
            "WAMR trap: %.*s\n",
            (int)error_message.size,
            error_message.data);
        wasm_byte_vec_delete(&error_message);
    }
}

}  // namespace

InstancePtr
InstanceWrapper::init(
    wasm_store_t* s,
    wasm_module_t* m,
    int32_t maxPages,
    wasm_extern_vec_t* expt,
    wasm_extern_vec_t const& imports)
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
        print_wasm_error("can't create instance", trap);
        throw std::runtime_error("WAMR: can't create instance");
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

    return *this;
}

InstanceWrapper::InstanceWrapper(
    wasm_store_t* s,
    wasm_module_t* m,
    int32_t maxPages,
    wasm_extern_vec_t const& imports)
    : exports WASM_EMPTY_VEC, mod_inst(init(s, m, maxPages, &exports, imports))
{
}

InstanceWrapper::~InstanceWrapper()
{
    if (exports.size)
        wasm_extern_vec_delete(&exports);
}

InstanceWrapper::operator bool() const
{
    return static_cast<bool>(mod_inst);
}

wasm_func_t*
InstanceWrapper::getFunc(
    std::string_view funcName,
    wasm_exporttype_vec_t const& export_types) const
{
    wasm_func_t* f = nullptr;

    if (!export_types.size)
        throw std::runtime_error("WAMR: no export");
    if (export_types.size != exports.size)
        throw std::runtime_error("WAMR: invalid export");

    for (unsigned i = 0; i < export_types.size; ++i)
    {
        auto const* exp_type(export_types.data[i]);

        wasm_name_t const* name = wasm_exporttype_name(exp_type);
        const wasm_externtype_t* exn_type = wasm_exporttype_type(exp_type);
        if (wasm_externtype_kind(exn_type) == WASM_EXTERN_FUNC)
        {
            if (funcName == std::string_view(name->data, name->size - 1))
            {
                auto* exn(exports.data[i]);
                if (wasm_extern_kind(exn) != WASM_EXTERN_FUNC)
                    throw std::runtime_error("WAMR: invalid export");

                f = wasm_extern_as_func(exn);
                break;
            }
        }
    }

    if (!f)
        throw std::runtime_error(
            "WAMR: can't find function " + std::string(funcName));

    return f;
}

wmem
InstanceWrapper::getMem() const
{
    wasm_memory_t* mem = nullptr;
    for (unsigned i = 0; i < exports.size; ++i)
    {
        auto* e(exports.data[i]);
        if (wasm_extern_kind(e) == WASM_EXTERN_MEMORY)
        {
            mem = wasm_extern_as_memory(e);
            break;
        }
    }

    if (!mem)
        throw std::runtime_error("WAMR: no memory exported");

    return {
        reinterpret_cast<std::uint8_t*>(wasm_memory_data(mem)),
        wasm_memory_data_size(mem)};
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////

ModulePtr
ModuleWrapper::init(wasm_store_t* s, wbytes const& wasmBin)
{
    wasm_byte_vec_t const code{
        wasmBin.size(),
        (char*)(wasmBin.data()),
        wasmBin.size(),
        sizeof(std::remove_reference_t<decltype(wasmBin)>::value_type),
        nullptr};
    ModulePtr m = ModulePtr(wasm_module_new(s, &code), &wasm_module_delete);
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
    exec_env = o.exec_env;
    o.exec_env = nullptr;
    return *this;
}

ModuleWrapper::ModuleWrapper(
    wasm_store_t* s,
    wbytes const& wasmBin,
    bool instantiate,
    int32_t maxPages,
    std::vector<WasmImportFunc> const& imports)
    : module(init(s, wasmBin)), export_types{0, nullptr, 0, 0, nullptr}
{
    if (!module)
        throw std::runtime_error("WAMR: can't create module");

    wasm_module_exports(module.get(), &export_types);
    if (instantiate)
    {
        auto wimports = buildImports(s, imports);
        addInstance(s, maxPages, wimports);
    }
}

ModuleWrapper::~ModuleWrapper()
{
    if (export_types.size)
        wasm_exporttype_vec_delete(&export_types);
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
                throw std::runtime_error("Invalid import type");
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
                throw std::runtime_error("Invalid return type");
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

    for (unsigned i = 0; i < importTypes.num_elems; ++i)
    {
        wasm_importtype_t const* importtype = importTypes.data[i];
        if (wasm_importtype_is_linked(importtype))
        {
            // create a placeholder
            wimports.data[i] = wasm_extern_new_empty(
                s, wasm_externtype_kind(wasm_importtype_type(importtype)));
            continue;
        }

        // wasm_name_t const* mn = wasm_importtype_module(importtype);
        // auto modName = std::string_view(mn->data, mn->num_elems - 1);
        wasm_name_t const* fn = wasm_importtype_name(importtype);
        auto fieldName = std::string_view(fn->data, fn->num_elems - 1);

        // for multi-module support
        // if ((W_ENV != modName) && (W_HOST_LIB != modName))
        //     continue;

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

            wimports.data[i] = wasm_func_as_extern(func);
            break;
        }
    }

    return wimports;
}

wasm_func_t*
ModuleWrapper::getFunc(std::string_view funcName) const
{
    return mod_inst.getFunc(funcName, export_types);
}

wmem
ModuleWrapper::getMem() const
{
    return mod_inst.getMem();
}

int
ModuleWrapper::addInstance(
    wasm_store_t* s,
    int32_t maxPages,
    wasm_extern_vec_t const& imports)
{
    mod_inst = {s, module.get(), maxPages, imports};
    exec_env = wasm_instance_exec_env(mod_inst.mod_inst.get());

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
ModuleWrapper::setGas(std::int64_t gas)
{
    if (exec_env)
    {
        wasm_runtime_set_instruction_count_limit(exec_env, gas);
        return gas;
    }
    return 0;
}

std::int64_t
ModuleWrapper::getGas()
{
    return exec_env ? wasm_runtime_get_instruction_count_limit(exec_env) : 0;
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
    wbytes const& wasmCode,
    bool instantiate,
    std::vector<WasmImportFunc> const& imports)
{
    module.reset();
    store.reset();  // to free the memory before creating new store
    store = {wasm_store_new(engine.get()), &wasm_store_delete};
    module = std::make_unique<ModuleWrapper>(
        store.get(), wasmCode, instantiate, defMaxPages, imports);
    setGas(defGas);
    return module ? 0 : -1;
}

int
WamrEngine::addInstance()
{
    return module->addInstance(store.get(), defMaxPages);
}

wasm_func_t*
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
WamrEngine::call(std::string_view func, Types... args)
{
    // Lookup our export function
    auto* f = getFunc(func);
    return call<NR>(f, std::forward<Types>(args)...);
}

template <int NR, class... Types>
WamrResult
WamrEngine::call(wasm_func_t* func, Types... args)
{
    std::vector<wasm_val_t> in;
    return call<NR>(func, in, std::forward<Types>(args)...);
}

template <int NR, class... Types>
WamrResult
WamrEngine::call(wasm_func_t* func, std::vector<wasm_val_t>& in)
{
    // wasm_val_t rs[1] = {WASM_I32_VAL(0)};
    WamrResult ret(NR);
    // if (NR)  {   wasm_val_vec_new_uninitialized(&ret, NR);    //
    // wasm_val_vec_new(&ret, NR, &rs[0]);    // ret = WASM_ARRAY_VEC(rs);    }

    wasm_val_vec_t const inv = in.empty()
        ? wasm_val_vec_t WASM_EMPTY_VEC
        : wasm_val_vec_t{
              in.size(),
              in.data(),
              in.size(),
              sizeof(std::remove_reference_t<decltype(in)>::value_type),
              nullptr};
    trap = wasm_func_call(func, &inv, &ret.r);
    if (trap)
        print_wasm_error("failed to call func", trap);

    // assert(results[0].kind == WASM_I32);
    // if (NR) printf("Result P5: %d\n", ret[0].of.i32);

    return ret;
}

template <int NR, class... Types>
WamrResult
WamrEngine::call(
    wasm_func_t* func,
    std::vector<wasm_val_t>& in,
    std::int32_t p,
    Types... args)
{
    add_param(in, p);
    return call<NR>(func, in, std::forward<Types>(args)...);
}

template <int NR, class... Types>
WamrResult
WamrEngine::call(
    wasm_func_t* func,

    std::vector<wasm_val_t>& in,
    std::int64_t p,
    Types... args)
{
    add_param(in, p);
    return call<NR>(func, in, std::forward<Types>(args)...);
}

template <int NR, class... Types>
WamrResult
WamrEngine::call(
    wasm_func_t* func,
    std::vector<wasm_val_t>& in,
    uint8_t const* d,
    std::size_t sz,
    Types... args)
{
    auto res = call<1>(W_ALLOC, static_cast<int32_t>(sz));

    if (trap || (res.r.data[0].kind != WASM_I32))
        return {};
    auto const ptr = res.r.data[0].of.i32;
    if (!ptr)
        throw std::runtime_error(
            "WAMR: can't allocate memory, " + std::to_string(sz) + " bytes");

    auto mem = getMem();
    memcpy(mem.p + ptr, d, sz);

    add_param(in, ptr);
    add_param(in, static_cast<int32_t>(sz));
    return call<NR>(func, in, std::forward<Types>(args)...);
}

template <int NR, class... Types>
WamrResult
WamrEngine::call(
    wasm_func_t* func,
    std::vector<wasm_val_t>& in,
    wbytes const& p,
    Types... args)
{
    return call<NR>(func, in, p.data(), p.size(), std::forward<Types>(args)...);
}

Expected<int32_t, TER>
WamrEngine::run(
    wbytes const& wasmCode,
    std::string_view funcName,
    std::vector<WasmImportFunc> const& imports,
    std::vector<WasmParam> const& params)
{
    try
    {
        return runHlp(wasmCode, funcName, imports, params);
    }
    catch (std::exception const&)
    {
    }
    catch (...)
    {
    }
    return Unexpected<TER>(tecFAILED_PROCESSING);
}

Expected<int32_t, TER>
WamrEngine::runHlp(
    wbytes const& wasmCode,
    std::string_view funcName,
    std::vector<WasmImportFunc> const& imports,
    std::vector<WasmParam> const& params)
{
    // Create and instantiate the module.
    if (!wasmCode.empty())
    {
        int const m = addModule(wasmCode, true, imports);
        if (m < 0)
            return Unexpected<TER>(tecFAILED_PROCESSING);
    }

    if (!module)
        return Unexpected<TER>(tecFAILED_PROCESSING);

    // Call main
    auto* f = getFunc(!funcName.empty() ? funcName : "_start");
    auto p = convertParams(params);
    auto res = call<1>(f, p);
    if (!res.r.size || trap)
        return Unexpected<TER>(tecFAILED_PROCESSING);

    assert(res.r.data[0].kind == WASM_I32);
    // printf("Result: %d\n", results[0].of.i32);
    // return res.r.data[0].of.i32 != 0;
    return res.r.data[0].of.i32;
}

std::int64_t
WamrEngine::initGas(std::int64_t def)
{
    defGas = def;
    return def;
}

std::int32_t
WamrEngine::initMaxPages(std::int32_t def)
{
    defMaxPages = def;
    return def;
}

std::int64_t
WamrEngine::setGas(std::int64_t gas)
{
    if (module)
    {
        module->setGas(gas);
        return gas;
    }
    return 0;
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

int32_t
WamrEngine::allocate(int32_t sz)
{
    auto res = call<1>(W_ALLOC, static_cast<int32_t>(sz));
    if (trap || (res.r.data[0].kind != WASM_I32))
        return {};
    auto const ptr = res.r.data[0].of.i32;
    if (!ptr)
        throw std::runtime_error(
            "WAMR: can't allocate memory, " + std::to_string(sz) + " bytes");
    return ptr;
}

wasm_trap_t*
WamrEngine::newTrap(std::string_view txt)
{
    wasm_message_t msg = WASM_EMPTY_VEC;

    if (!txt.empty())
        wasm_name_new(&msg, txt.size(), txt.data());

    return wasm_trap_new(store.get(), &msg);
}

}  // namespace ripple
