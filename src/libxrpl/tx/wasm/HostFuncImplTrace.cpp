#include <xrpl/basics/Slice.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/tx/wasm/HostFunc.h>
#include <xrpl/tx/wasm/HostFuncImpl.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <boost/algorithm/hex.hpp>

#include <cstdint>
#include <expected>
#include <iterator>
#include <string>
#include <string_view>

#ifdef _DEBUG
// #define DEBUG_OUTPUT 1
#endif

namespace xrpl {

std::expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::trace(std::string_view const& msg, Slice const& data, bool asHex) const
{
    if (!asHex)
    {
        log(msg, [&data] {
            return std::string_view(reinterpret_cast<char const*>(data.data()), data.size());
        });
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

std::expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::traceNum(std::string_view const& msg, int64_t data) const
{
    log(msg, [data] { return data; });
    return 0;
}

std::expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::traceAccount(std::string_view const& msg, AccountID const& account) const
{
    log(msg, [&account] { return toBase58(account); });
    return 0;
}

std::expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::traceFloat(std::string_view const& msg, Slice const& data) const
{
    log(msg, [&data] { return wasm_float::floatToString(data); });
    return 0;
}

std::expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::traceAmount(std::string_view const& msg, STAmount const& amount) const
{
    log(msg, [&amount] { return amount.getFullText(); });
    return 0;
}

}  // namespace xrpl
