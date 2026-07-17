#include <xrpl/tx/wasm/WasmiVM.h>

#include <xrpl/basics/Expected.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/tx/wasm/HostFunc.h>
#include <xrpl/tx/wasm/ParamsHelper.h>
#include <xrpl/tx/wasm/WasmVM.h>

#include <wasmi/config.h>
#include <wasmi/error.h>

#include <wasm.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifdef _DEBUG
// #define DEBUG_OUTPUT 1
#endif
// #define SHOW_CALL_TIME 1

namespace xrpl {

namespace {

void
printWasmError(std::string_view msg, wasm_trap_t* trap, beast::Journal jlog)
{
#ifdef DEBUG_OUTPUT
    auto& j = std::cerr;
#else
    auto j = jlog.warn();
    if (jlog.active(beast::Severity::Warning))
#endif
    {
        wasm_byte_vec_t errorMessage WASM_EMPTY_VEC;

        if (trap != nullptr)
            wasm_trap_message(trap, &errorMessage);

        if (errorMessage.size != 0u)
        {
            j << "WASMI Error: " << msg << ", "
              << std::string_view(errorMessage.data, errorMessage.size - 1);
        }
        else
        {
            j << "WASMI Error: " << msg;
        }

        if (errorMessage.size != 0u)
            wasm_byte_vec_delete(&errorMessage);
    }

    if (trap != nullptr)
        wasm_trap_delete(trap);

#ifdef DEBUG_OUTPUT
    j << std::endl;
#endif
}
// LCOV_EXCL_STOP

}  // namespace

struct WasmiRuntimeWrapper : public WasmRuntimeWrapper
{
    InstanceWrapper& iw;

    WasmiRuntimeWrapper(InstanceWrapper& iw) : iw(iw)
    {
    }

    Wmem
    getMem() override
    {
        return iw.getMem();
    }

    std::int64_t
    getGas() override
    {
        return iw.getGas();
    }

    std::int64_t
    setGas(std::int64_t gas) override
    {
        return iw.setGas(gas);
    }
};

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
        wasm_instance_new(s.get(), m.get(), imports.get(), &trap), &wasm_instance_delete);

    if (!mi || (trap != nullptr))
    {
        printWasmError("can't create instance", trap, j);
        throw std::runtime_error("can't create instance");
    }
    wasm_instance_exports(mi.get(), expt.get());
    return mi;
}

InstanceWrapper::InstanceWrapper() : instance(nullptr, &wasm_instance_delete)
{
}

// LCOV_EXCL_START
InstanceWrapper::InstanceWrapper(InstanceWrapper&& o) : instance(nullptr, &wasm_instance_delete)
{
    *this = std::move(o);
}
// LCOV_EXCL_STOP

InstanceWrapper::InstanceWrapper(
    StorePtr& s,
    ModulePtr& m,
    WasmExternVec const& imports,
    beast::Journal j)
    : store(s.get()), instance(init(s, m, exports, imports, j)), j(j)
{
}

InstanceWrapper&
InstanceWrapper::operator=(InstanceWrapper&& o)
{
    if (this == &o)
        return *this;  // LCOV_EXCL_LINE

    store = o.store;
    o.store = nullptr;
    exports = std::move(o.exports);
    memIdx = o.memIdx;
    o.memIdx = -1;
    instance = std::move(o.instance);

    j = o.j;

    return *this;
}

InstanceWrapper::
operator bool() const
{
    return static_cast<bool>(instance);
}

FuncInfo
InstanceWrapper::getFunc(std::string_view funcName, WasmExporttypeVec const& exportTypes) const
{
    wasm_func_t const* f = nullptr;
    wasm_functype_t const* ft = nullptr;

    if (!instance)
        throw std::runtime_error("no instance");  // LCOV_EXCL_LINE

    if (exportTypes.empty())
        throw std::runtime_error("no export");  // LCOV_EXCL_LINE
    if (exportTypes.size() != exports.size())
        throw std::runtime_error("invalid export");  // LCOV_EXCL_LINE

    for (unsigned i = 0; i < exportTypes.size(); ++i)
    {
        auto const* expType(exportTypes[i]);

        wasm_name_t const* name = wasm_exporttype_name(expType);
        wasm_externtype_t const* exnType = wasm_exporttype_type(expType);
        if (wasm_externtype_kind(exnType) == WASM_EXTERN_FUNC)
        {
            if (funcName != std::string_view(name->data, name->size))
                continue;

            auto const* exn(exports[i]);
            if (wasm_extern_kind(exn) != WASM_EXTERN_FUNC)
                throw std::runtime_error("invalid export");  // LCOV_EXCL_LINE

            ft = wasm_externtype_as_functype_const(exnType);
            f = wasm_extern_as_func_const(exn);
            break;
        }
    }

    if ((f == nullptr) || (ft == nullptr))
        throw std::runtime_error("can't find function <" + std::string(funcName) + ">");

    return {f, ft};
}

Wmem
InstanceWrapper::getMem() const
{
    if (memIdx >= 0)
    {
        auto* e(exports[memIdx]);
        wasm_memory_t* mem = wasm_extern_as_memory(e);
        return {
            .p = reinterpret_cast<std::uint8_t*>(wasm_memory_data(mem)),
            .s = wasm_memory_data_size(mem)};
    }

    wasm_memory_t* mem = nullptr;
    for (int i = 0; i < exports.size(); ++i)
    {
        auto* e(exports[i]);
        if (wasm_extern_kind(e) == WASM_EXTERN_MEMORY)
        {
            memIdx = i;
            mem = wasm_extern_as_memory(e);
            break;
        }
    }

    if (mem == nullptr)
        return {};  // LCOV_EXCL_LINE

    return {
        .p = reinterpret_cast<std::uint8_t*>(wasm_memory_data(mem)),
        .s = wasm_memory_data_size(mem)};
}

std::int64_t
InstanceWrapper::getGas() const
{
    if (store == nullptr)
        return -1;  // LCOV_EXCL_LINE
    std::uint64_t gas = 0;
    wasm_store_get_fuel(store, &gas);
    return static_cast<std::int64_t>(gas);
}

std::int64_t
InstanceWrapper::setGas(std::int64_t gas) const
{
    if (store == nullptr)
        return -1;  // LCOV_EXCL_LINE

    if (gas < 0)
        gas = std::numeric_limits<decltype(gas)>::max();
    wasmi_error_t* err = wasm_store_set_fuel(store, static_cast<std::uint64_t>(gas));
    if (err != nullptr)
    {
        // LCOV_EXCL_START
        printWasmError("Can't set instance gas", nullptr, j);
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
    wasm_byte_vec_t const code{.size = wasmBin.size(), .data = (char*)(wasmBin.data())};
    ModulePtr m = ModulePtr(wasm_module_new(s.get(), &code), &wasm_module_delete);
    if (!m)
        throw std::runtime_error("can't create module");

    return m;
}

// LCOV_EXCL_START
ModuleWrapper::ModuleWrapper() : module(nullptr, &wasm_module_delete)
{
}

ModuleWrapper::ModuleWrapper(ModuleWrapper&& o) : module(nullptr, &wasm_module_delete)
{
    *this = std::move(o);
}
// LCOV_EXCL_STOP

ModuleWrapper::ModuleWrapper(
    StorePtr& s,
    Bytes const& wasmBin,
    bool instantiate,
    ImportVec const& imports,
    beast::Journal j)
    : module(init(s, wasmBin, j)), j(j)
{
    wasm_module_exports(module.get(), exportTypes.get());
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

    module = std::move(o.module);
    instanceWrap = std::move(o.instanceWrap);
    exportTypes = std::move(o.exportTypes);
    j = o.j;

    return *this;
}

ModuleWrapper::
operator bool() const
{
    return instanceWrap;
}

// LCOV_EXCL_STOP

static WasmValtypeVec
makeImpParams(WasmImportFunc const& imp)
{
    auto const paramSize = imp.params.size();
    if (paramSize == 0u)
        return {};

    WasmValtypeVec v(paramSize);

    for (unsigned i = 0; i < paramSize; ++i)
    {
        auto const vt = imp.params[i];
        switch (vt)
        {
            case WasmTypes::WtI32:
                v[i] = wasm_valtype_new_i32();
                break;
            case WasmTypes::WtI64:
                v[i] = wasm_valtype_new_i64();
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
        case WasmTypes::WtI32:
            v[0] = wasm_valtype_new_i32();
            break;
            // LCOV_EXCL_START
        case WasmTypes::WtI64:
            v[0] = wasm_valtype_new_i64();
            break;
        default:
            throw std::runtime_error("invalid return type");
            // LCOV_EXCL_STOP
    }
    return v;
}

WasmExternVec
ModuleWrapper::buildImports(StorePtr& s, ImportVec const& imports) const
{
    WasmImporttypeVec importTypes;
    wasm_module_imports(module.get(), importTypes.get());

    if (importTypes.empty())
        return {};
    if (imports.empty())
        Throw<std::runtime_error>("Empty imports");

    WasmExternVec wimports(importTypes.size());

    unsigned impCnt = 0;
    for (unsigned i = 0; i < importTypes.size(); ++i)
    {
        wasm_importtype_t const* importType = importTypes[i];

        // wasm_name_t const* mn = wasm_importtype_module(importtype);
        // auto modName = std::string_view(mn->data, mn->num_elems);
        wasm_name_t const* fn = wasm_importtype_name(importType);
        auto fieldName = std::string_view(fn->data, fn->size);

        wasm_externkind_t const itype = wasm_externtype_kind(wasm_importtype_type(importType));
        if ((itype) != WASM_EXTERN_FUNC)
        {
            Throw<std::runtime_error>(
                "Invalid import type " + std::to_string(itype));  // LCOV_EXCL_LINE
        }

        // for multi-module support
        // if ((W_ENV != modName) && (W_HOST_LIB != modName))
        //     continue;

        auto const it = imports.find(fieldName);
        if (it == imports.end())
        {
            printWasmError("Import not found: " + std::string(fieldName), nullptr, j);
            continue;  // print all missed import
        }

        auto const& obj = it->second;
        auto const& imp = obj.second;

        WasmValtypeVec params(makeImpParams(imp));
        WasmValtypeVec results(makeImpReturn(imp));

        std::unique_ptr<wasm_functype_t, decltype(&wasm_functype_delete)> const ftype(
            wasm_functype_new(params.get(), results.get()), &wasm_functype_delete);

        params.release();
        results.release();

        wasm_func_t* func = wasm_func_new_with_env(
            s.get(),
            ftype.get(),
            reinterpret_cast<wasm_func_callback_with_env_t>(imp.wrap),
            (void*)&obj,
            nullptr);
        if (func == nullptr)
        {
            Throw<std::runtime_error>(
                "can't create import function " + std::string(imp.name));  // LCOV_EXCL_LINE
        }

        wimports[i] = wasm_func_as_extern(func);
        ++impCnt;
    }

    if (impCnt != importTypes.size())
    {
        printWasmError(
            std::string("Imports not finished: ") + std::to_string(impCnt) + "/" +
                std::to_string(importTypes.size()),
            nullptr,
            j);
        Throw<std::runtime_error>("Missing imports");
    }

    return wimports;
}

FuncInfo
ModuleWrapper::getFunc(std::string_view funcName) const
{
    return instanceWrap.getFunc(funcName, exportTypes);
}

wasm_functype_t*
ModuleWrapper::getFuncType(std::string_view funcName) const
{
    for (size_t i = 0; i < exportTypes.size(); i++)
    {
        auto const* expType(exportTypes[i]);
        wasm_name_t const* name = wasm_exporttype_name(expType);
        wasm_externtype_t const* exnType = wasm_exporttype_type(expType);
        if (wasm_externtype_kind(exnType) == WASM_EXTERN_FUNC &&
            funcName == std::string_view(name->data, name->size))
        {
            return wasm_externtype_as_functype(const_cast<wasm_externtype_t*>(exnType));
        }
    }

    throw std::runtime_error("can't find function <" + std::string(funcName) + ">");
}

Wmem
ModuleWrapper::getMem() const
{
    return instanceWrap.getMem();
}

InstanceWrapper&
ModuleWrapper::getInstance(int)
{
    return instanceWrap;
}

int
ModuleWrapper::addInstance(StorePtr& s, WasmExternVec const& imports)
{
    instanceWrap = {s, module, imports, j};
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
ModuleWrapper::getGas() const
{
    return instanceWrap ? instanceWrap.getGas() : -1;
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
    if (config == nullptr)
    {
        return std::unique_ptr<wasm_engine_t, decltype(&wasm_engine_delete)>{
            nullptr, &wasm_engine_delete};  // LCOV_EXCL_LINE
    }
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

WasmiEngine::WasmiEngine() : engine_(init()), store_(nullptr, &wasm_store_delete)
{
}

int
WasmiEngine::addModule(
    Bytes const& wasmCode,
    bool instantiate,
    ImportVec const& imports,
    int64_t gas)
{
    moduleWrap_.reset();
    store_.reset();  // to free the memory before creating new store
    store_ = {wasm_store_new_with_memory_max_pages(engine_.get(), maxPages), &wasm_store_delete};

    if (gas < 0)
        gas = std::numeric_limits<decltype(gas)>::max();
    wasmi_error_t* err = wasm_store_set_fuel(store_.get(), static_cast<std::uint64_t>(gas));
    if (err != nullptr)
    {
        // LCOV_EXCL_START
        printWasmError("Error setting gas", nullptr, j_);
        wasmi_error_delete(err);
        throw std::runtime_error("can't set gas");
        // LCOV_EXCL_STOP
    }

    moduleWrap_ = std::make_unique<ModuleWrapper>(store_, wasmCode, instantiate, imports, j_);

    if (!moduleWrap_)
        throw std::runtime_error("can't create module wrapper");  // LCOV_EXCL_LINE

    return moduleWrap_ ? 0 : -1;
}

// int
// WasmiEngine::addInstance()
// {
//     return module->addInstance(store.get());
// }

FuncInfo
WasmiEngine::getFunc(std::string_view funcName) const
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
            case WasmTypes::WtI32:
                v.push_back(WASM_I32_VAL(p.of.i32));
                break;
            // LCOV_EXCL_START
            case WasmTypes::WtI64:
                v.push_back(WASM_I64_VAL(p.of.i64));
                break;
            default:
                throw std::runtime_error(
                    "unknown parameter type: " + std::to_string(static_cast<int>(p.type)));
                break;
                // LCOV_EXCL_STOP
        }
    }

    return v;
}

int
WasmiEngine::compareParamTypes(wasm_valtype_vec_t const* ftp, std::vector<wasm_val_t> const& p)
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
WasmiEngine::addParam(std::vector<wasm_val_t>& in, int32_t p)
{
    in.emplace_back();
    auto& el(in.back());
    memset(&el, 0, sizeof(el));
    el = WASM_I32_VAL(p);  // WASM_I32;
}

// LCOV_EXCL_STOP

void
WasmiEngine::addParam(std::vector<wasm_val_t>& in, int64_t p)
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
    uint64_t x = std::chrono::duration_cast<std::chrono::microseconds>(
                     std::chrono::high_resolution_clock::now().time_since_epoch())
                     .count();
    return x;
}
#endif

template <int NR, class... Types>
WasmiResult
WasmiEngine::call(FuncInfo const& f, std::vector<wasm_val_t>& in)
{
    WasmiResult ret(NR);
    wasm_val_vec_t const inv = in.empty() ? wasm_val_vec_t WASM_EMPTY_VEC
                                          : wasm_val_vec_t{.size = in.size(), .data = in.data()};

#ifdef SHOW_CALL_TIME
    auto const start = usecs();
#endif

    wasm_trap_t* trap = wasm_func_call(f.first, &inv, ret.r.get());

#ifdef SHOW_CALL_TIME
    auto const finish = usecs();
    auto const delta_ms = (finish - start) / 1000;
    std::cout << "wasm_func_call: " << delta_ms << "ms" << std::endl;
#endif

    if (trap)
    {
        ret.f = true;
        printWasmError("failure to call func", trap, j_);
    }

    return ret;
}

template <int NR, class... Types>
WasmiResult
WasmiEngine::call(FuncInfo const& f, std::vector<wasm_val_t>& in, std::int32_t p, Types&&... args)
{
    addParam(in, p);
    return call<NR>(f, in, std::forward<Types>(args)...);
}

template <int NR, class... Types>
WasmiResult
WasmiEngine::call(FuncInfo const& f, std::vector<wasm_val_t>& in, std::int64_t p, Types&&... args)
{
    addParam(in, p);
    return call<NR>(f, in, std::forward<Types>(args)...);
}

template <int NR, class... Types>
WasmiResult
WasmiEngine::call(FuncInfo const& f, std::vector<wasm_val_t>& in, Bytes const& p, Types&&... args)
{
    return call<NR>(f, in, p.data(), p.size(), std::forward<Types>(args)...);
}

static inline void
checkImports(ImportVec const& imports, HostFunctions* hfs)
{
    for (auto const& obj : imports)
    {
        if (hfs != obj.second.first)
            Throw<std::runtime_error>("Imports hf unsync");
    }
}

Expected<WasmResult<int32_t>, TER>
WasmiEngine::run(
    Bytes const& wasmCode,
    HostFunctions& hfs,
    int64_t gas,
    std::string_view funcName,
    std::vector<WasmParam> const& params,
    ImportVec const& imports,
    beast::Journal j)
{
    if (gas <= 0)
        return Unexpected<TER>(temBAD_AMOUNT);

    try
    {
        checkImports(imports, &hfs);
        return runHlp(wasmCode, hfs, gas, funcName, params, imports, j);
    }
    catch (std::exception const& e)
    {
        printWasmError(std::string("exception: ") + e.what(), nullptr, j);
    }
    // LCOV_EXCL_START
    catch (...)
    {
        printWasmError(std::string("exception: unknown"), nullptr, j);
    }
    // LCOV_EXCL_STOP
    return Unexpected<TER>(tecFAILED_PROCESSING);
}

namespace {

struct CustomSection
{
    std::string_view name;
    std::span<char const> payload;
};

struct Version
{
    std::string_view name;
    std::string_view version;
};

uint32_t
readLEB128(Bytes const& wasmCode, size_t& offset)
{
    auto result = uint32_t{};
    auto shift = uint32_t{};
    while (offset < wasmCode.size())
    {
        auto byte = wasmCode[offset++];
        result |= static_cast<uint32_t>(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0)
        {
            break;
        }
        shift += 7;
        if (shift >= 32)
        {
            // Drain the rest of the bytes for this leb.
            while (offset < wasmCode.size())
            {
                if ((wasmCode[offset++] & 0x80) == 0)
                {
                    break;
                }
            }
            break;
        }
    }
    return result;
}

template <typename Filter>
void
filterCustomSections(Bytes const& wasmCode, Filter&& filter)
{
    auto offset = size_t{8};  // Skip Magic number and Version

    if (wasmCode.size() <= offset)
    {
        // There is nothing to parse.
        return;
    }

    while (offset < wasmCode.size())
    {
        auto sectionId = wasmCode[offset++];
        auto sectionSize = readLEB128(wasmCode, offset);
        auto nextSection = offset + sectionSize;

        if (nextSection > wasmCode.size())
        {
            break;
        }

        if (sectionId == 0)
        {
            // Wasm custom section marker.
            auto customSection = CustomSection{};
            auto size = readLEB128(wasmCode, offset);

            if (offset + size > wasmCode.size() || offset + size > nextSection)
            {
                return;
            }

            customSection.name =
                std::string_view{reinterpret_cast<char const*>(wasmCode.data()) + offset, size};
            offset += size;

            if (offset >= nextSection)
            {
                offset = nextSection;
                continue;
            }

            size = nextSection - offset;
            customSection.payload = std::span<char const>{
                reinterpret_cast<char const*>(wasmCode.data()) + offset, size};

            if (filter(customSection))
            {
                return;
            }
        }
        offset = nextSection;
    }
}

std::vector<Version>
extractVersionInfo(Bytes const& wasmCode)
{
    static constexpr auto kCommonLib = "xrpl-common-stdlib";
    static constexpr auto kEscrowLib = "xrpl-escrow-stdlib";

    auto versions = std::vector<Version>{};
    filterCustomSections(wasmCode, [&](auto const& section) {
        if (section.name == kCommonLib || section.name == kEscrowLib)
        {
            versions.emplace_back(
                Version{
                    .name = section.name,
                    .version = std::string_view{section.payload.data(), section.payload.size()}});
        }

        // Just read until we have found all the information we are looking for.
        return versions.size() == 2;
    });
    return versions;
}

}  // namespace

Expected<WasmResult<int32_t>, TER>
WasmiEngine::runHlp(
    Bytes const& wasmCode,
    HostFunctions& hfs,
    int64_t gas,
    std::string_view funcName,
    std::vector<WasmParam> const& params,
    ImportVec const& imports,
    beast::Journal j)
{
    // currently only 1 module support, possible parallel UT run
    std::scoped_lock const lg(m_);
    j_ = j;

    if (wasmCode.empty())
        throw std::runtime_error("empty module");
    if (!hfs.checkSelf())
        throw std::runtime_error("hfs isn't clean");

    for (auto const& version : extractVersionInfo(wasmCode))
    {
        j_.debug() << "Module version: " << version.name << " " << version.version << "\n";
    }

    // Create and instantiate the module.
    [[maybe_unused]] int const m = addModule(wasmCode, true, imports, gas);

    if (!moduleWrap_ || !moduleWrap_->instanceWrap)
        throw std::runtime_error("no instance");  // LCOV_EXCL_LINE

    auto clear = [](HostFunctions* p) { p->setRT(nullptr); };
    std::unique_ptr<HostFunctions, decltype(clear)> const clearGuard(&hfs, clear);
    WasmiRuntimeWrapper iw(getRT());
    hfs.setRT(&iw);

    // Call main
    auto const f = getFunc(!funcName.empty() ? funcName : "_start");
    auto const* ftp = wasm_functype_params(f.second);

    // not const because passed directly to VM function (which accept non
    // const)
    auto p = convertParams(params);

    if (int const comp = compareParamTypes(ftp, p); comp >= 0)
        throw std::runtime_error("invalid parameter type #" + std::to_string(comp));

    auto const res = call<1>(f, p);

    if (res.f)
    {
        throw std::runtime_error("<" + std::string(funcName) + "> failure");
    }

    if (res.r.empty())
    {
        throw std::runtime_error(
            "<" + std::string(funcName) + "> return nothing");  // LCOV_EXCL_LINE
    }

    if (res.r[0].kind != WASM_I32)
    {
        throw std::runtime_error(
            "<" + std::string(funcName) +
            "> return type mismatch, ret: " + std::to_string(static_cast<int>(res.r[0].kind)));
    }

    if (gas == -1)
        gas = std::numeric_limits<decltype(gas)>::max();
    WasmResult<int32_t> const ret{.result = res.r[0].of.i32, .cost = gas - moduleWrap_->getGas()};

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
    HostFunctions& hfs,
    std::string_view funcName,
    std::vector<WasmParam> const& params,
    ImportVec const& imports,
    beast::Journal j)
{
    try
    {
        checkImports(imports, &hfs);
        return checkHlp(wasmCode, hfs, funcName, params, imports, j);
    }
    catch (std::exception const& e)
    {
        printWasmError(std::string("exception: ") + e.what(), nullptr, j);
    }
    // LCOV_EXCL_START
    catch (...)
    {
        printWasmError(std::string("exception: unknown"), nullptr, j);
    }
    // LCOV_EXCL_STOP

    return temBAD_WASM;
}

NotTEC
WasmiEngine::checkHlp(
    Bytes const& wasmCode,
    HostFunctions& hfs,
    std::string_view funcName,
    std::vector<WasmParam> const& params,
    ImportVec const& imports,
    beast::Journal j)
{
    // currently only 1 module support, possible parallel UT run
    std::scoped_lock const lg(m_);
    j_ = j;

    // Create and instantiate the module.
    if (wasmCode.empty())
        throw std::runtime_error("empty module");

    int const m = addModule(wasmCode, false, imports, -1);
    if ((m < 0) || !moduleWrap_)
        throw std::runtime_error("no module");  // LCOV_EXCL_LINE

    // Looking for a func and compare parameter types
    auto const f = moduleWrap_->getFuncType(!funcName.empty() ? funcName : "_start");
    auto const* ftp = wasm_functype_params(f);
    auto const p = convertParams(params);

    if (int const comp = compareParamTypes(ftp, p); comp >= 0)
        throw std::runtime_error("invalid parameter type #" + std::to_string(comp));

    return tesSUCCESS;
}

// LCOV_EXCL_START
std::int64_t
WasmiEngine::getGas() const
{
    return moduleWrap_ ? moduleWrap_->getGas() : -1;
}
// LCOV_EXCL_STOP

Wmem
WasmiEngine::getMem() const
{
    return moduleWrap_ ? moduleWrap_->getMem() : Wmem();
}

InstanceWrapper&
WasmiEngine::getRT(int m, int i) const
{
    if (!moduleWrap_)
        throw std::runtime_error("no module");
    return moduleWrap_->getInstance(i);
}

wasm_trap_t*
WasmiEngine::newTrap(std::string const& txt)
{
    static char empty[1] = {0};
    wasm_message_t msg = {.size = 1, .data = empty};

    if (!txt.empty())
        wasm_name_new(&msg, txt.size() + 1, txt.c_str());  // include 0

    wasm_trap_t* trap = wasm_trap_new(store_.get(), &msg);  // NOLINT

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
