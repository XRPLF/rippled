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
#ifdef DEBUG_OUTPUT
    auto j = getJournal().error();
#else
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

    return msg.size() + data.size() * (asHex ? 2 : 1);
}

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::traceNum(std::string_view const& msg, int64_t data)
{
#ifdef DEBUG_OUTPUT
    auto j = getJournal().error();
#else
    auto j = getJournal().trace();
#endif
    j << "HF TRACE NUM(" << leKey.key << "): " << msg << " " << data;
    return msg.size() + sizeof(data);
}

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::traceAccount(
    std::string_view const& msg,
    AccountID const& account)
{
#ifdef DEBUG_OUTPUT
    auto j = getJournal().error();
#else
    auto j = getJournal().trace();
#endif

    auto const accountStr = toBase58(account);

    j << "HF TRACE ACCOUNT(" << leKey.key << "): " << msg << " " << accountStr;
    return msg.size() + accountStr.size();
}

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::traceFloat(
    std::string_view const& msg,
    Slice const& data)
{
#ifdef DEBUG_OUTPUT
    auto j = getJournal().error();
#else
    auto j = getJournal().trace();
#endif
    auto const s = floatToString(data);
    j << "HF TRACE FLOAT(" << leKey.key << "): " << msg << " " << s;
    return msg.size() + s.size();
}

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::traceAmount(
    std::string_view const& msg,
    STAmount const& amount)
{
#ifdef DEBUG_OUTPUT
    auto j = getJournal().error();
#else
    auto j = getJournal().trace();
#endif
    auto const amountStr = amount.getFullText();
    j << "HF TRACE AMOUNT(" << leKey.key << "): " << msg << " " << amountStr;
    return msg.size() + amountStr.size();
}

}  // namespace xrpl
