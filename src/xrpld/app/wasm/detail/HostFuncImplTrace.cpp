#include <xrpld/app/wasm/HostFuncImpl.h>

#include <xrpl/protocol/STBitString.h>
#include <xrpl/protocol/digest.h>

#ifdef _DEBUG
// #define DEBUG_OUTPUT 1
#endif

namespace xrpl {

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::trace(std::string_view const& msg, Slice const& data, bool asHex)
{
    if (!asHex)
    {
        log(msg, [&data] { return std::string_view(reinterpret_cast<char const*>(data.data()), data.size()); });
    }
    else
    {
        log(msg, [&data] {
            std::string hex;
            hex.reserve(data.size() * 2);
            boost::algorithm::hex(data.begin(), data.end(), std::back_inserter(hex));
            return hex;
        });
    }

    return 0;
}

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::traceNum(std::string_view const& msg, int64_t data)
{
    log(msg, [data] { return data; });
    return 0;
}

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::traceAccount(std::string_view const& msg, AccountID const& account)
{
    log(msg, [&account] { return toBase58(account); });
    return 0;
}

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::traceFloat(std::string_view const& msg, Slice const& data)
{
    log(msg, [&data] { return wasm_float::floatToString(data); });
    return 0;
}

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::traceAmount(std::string_view const& msg, STAmount const& amount)
{
    log(msg, [&amount] { return amount.getFullText(); });
    return 0;
}

}  // namespace xrpl
