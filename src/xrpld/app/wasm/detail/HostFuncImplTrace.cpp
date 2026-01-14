#include <xrpld/app/wasm/HostFuncImpl.h>

#include <xrpl/protocol/STBitString.h>
#include <xrpl/protocol/digest.h>

#ifdef _DEBUG
// #define DEBUG_OUTPUT 1
#endif

namespace xrpl {

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::trace(
    std::string_view const& msg,
    Slice const& data,
    bool asHex)
{
    auto const ret = msg.size() + data.size() * (asHex ? 2 : 1);

    if (!asHex)
    {
        log(msg,
            std::string_view(
                reinterpret_cast<char const*>(data.data()), data.size()));
    }
    else
    {
        std::string hex;
        hex.reserve(data.size() * 2);
        boost::algorithm::hex(
            data.begin(), data.end(), std::back_inserter(hex));
        log(msg, hex);
    }

    return ret;
}

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::traceNum(std::string_view const& msg, int64_t data)
{
    auto const ret = msg.size() + sizeof(data);
    log(msg, data);
    return ret;
}

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::traceAccount(
    std::string_view const& msg,
    AccountID const& account)
{
    auto const ret = msg.size() + account.size();
    auto const accountStr = toBase58(account);
    log(msg, accountStr);
    return ret;
}

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::traceFloat(
    std::string_view const& msg,
    Slice const& data)
{
    auto const ret = msg.size() + data.size();
    auto const s = wasm_float::floatToString(data);
    log(msg, s);
    return ret;
}

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::traceAmount(
    std::string_view const& msg,
    STAmount const& amount)
{
    auto const ret = msg.size();
    auto const amountStr = amount.getFullText();
    log(msg, amountStr);
    return ret;
}

}  // namespace xrpl
