#include <xrpl/tx/wasm/HostFuncImpl.h>

#include <xrpl/basics/Slice.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <cstdint>
#include <expected>
#include <string_view>

namespace xrpl {

// =========================================================
// SECTION: WRITE FUNCTION
// =========================================================

std::expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::updateData(Slice const& data)
{
    if (data.size() > kMaxWasmDataLength)
        return std::unexpected(HostFunctionError::DataFieldTooLarge);

    data_ = Bytes(data.begin(), data.end());
    return data_->size();
}

// =========================================================
// SECTION: UTILS
// =========================================================

std::expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::checkSignature(
    Slice const& message,
    Slice const& signature,
    Slice const& pubkey) const
{
    if (!publicKeyType(pubkey))
        return std::unexpected(HostFunctionError::InvalidParams);

    PublicKey const pk(pubkey);
    return verify(pk, message, signature);
}

std::expected<Hash, HostFunctionError>
WasmHostFunctionsImpl::computeSha512HalfHash(Slice const& data) const
{
    auto const hash = sha512Half(data);
    return hash;
}

void
WasmHostFunctionsImpl::trace(std::string_view const& msg, std::string_view const& data) const
{
    log(msg, [&data] { return data; });
}

}  // namespace xrpl
