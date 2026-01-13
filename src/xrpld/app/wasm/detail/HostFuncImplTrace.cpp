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
#ifdef DEBUG_OUTPUT
    auto& j = std::cerr;
#else
    if (!getJournal().active(beast::severities::kTrace))
        return ret;
    auto j = getJournal().trace();
#endif

    if (!asHex)
    {
        j << "HF TRACE (" << leKey.key << "): " << msg << " "
          << std::string_view(
                 reinterpret_cast<char const*>(data.data()), data.size());
    }
    else
    {
        std::string hex;
        hex.reserve(data.size() * 2);
        boost::algorithm::hex(
            data.begin(), data.end(), std::back_inserter(hex));
        j << "HF DEV TRACE (" << leKey.key << "): " << msg << " " << hex;
    }

#ifdef DEBUG_OUTPUT
    j << std::endl;
#endif

    return ret;
}

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::traceNum(std::string_view const& msg, int64_t data)
{
    auto const ret = msg.size() + sizeof(data);
#ifdef DEBUG_OUTPUT
    auto& j = std::cerr;
#else
    if (!getJournal().active(beast::severities::kTrace))
        return ret;
    auto j = getJournal().trace();
#endif

    j << "HF TRACE NUM(" << leKey.key << "): " << msg << " " << data;

#ifdef DEBUG_OUTPUT
    j << std::endl;
#endif

    return ret;
}

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::traceAccount(
    std::string_view const& msg,
    AccountID const& account)
{
    auto const ret = msg.size() + account.size();
#ifdef DEBUG_OUTPUT
    auto& j = std::cerr;
#else
    if (!getJournal().active(beast::severities::kTrace))
        return ret;
    auto j = getJournal().trace();
#endif

    auto const accountStr = toBase58(account);

    j << "HF TRACE ACCOUNT(" << leKey.key << "): " << msg << " " << accountStr;

#ifdef DEBUG_OUTPUT
    j << std::endl;
#endif

    return ret;
}

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::traceFloat(
    std::string_view const& msg,
    Slice const& data)
{
    auto const ret = msg.size() + data.size();
#ifdef DEBUG_OUTPUT
    auto& j = std::cerr;
#else
    if (!getJournal().active(beast::severities::kTrace))
        return ret;
    auto j = getJournal().trace();
#endif
    auto const s = wasm_float::floatToString(data);
    j << "HF TRACE FLOAT(" << leKey.key << "): " << msg << " " << s;

#ifdef DEBUG_OUTPUT
    j << std::endl;
#endif

    return ret;
}

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::traceAmount(
    std::string_view const& msg,
    STAmount const& amount)
{
    auto const ret = msg.size();
#ifdef DEBUG_OUTPUT
    auto& j = std::cerr;
#else
    if (!getJournal().active(beast::severities::kTrace))
        return ret;
    auto j = getJournal().trace();
#endif
    auto const amountStr = amount.getFullText();
    j << "HF TRACE AMOUNT(" << leKey.key << "): " << msg << " " << amountStr;

#ifdef DEBUG_OUTPUT
    j << std::endl;
#endif

    return ret;
}

}  // namespace xrpl
