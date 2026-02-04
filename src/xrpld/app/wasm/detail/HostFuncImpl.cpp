#include <xrpld/app/wasm/HostFuncImpl.h>

#include <xrpl/protocol/STBitString.h>
#include <xrpl/protocol/digest.h>

namespace xrpl {

// =========================================================
// SECTION: WRITE FUNCTION
// =========================================================

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::updateData(Slice const& data)
{
    if (data.size() > maxWasmDataLength)
    {
        return Unexpected(HostFunctionError::DATA_FIELD_TOO_LARGE);
    }
    data_ = Bytes(data.begin(), data.end());
    return data_->size();
}

// =========================================================
// SECTION: UTILS
// =========================================================

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::checkSignature(Slice const& message, Slice const& signature, Slice const& pubkey)
{
    if (!publicKeyType(pubkey))
        return Unexpected(HostFunctionError::INVALID_PARAMS);

    PublicKey const pk(pubkey);
    return verify(pk, message, signature);
}

Expected<Hash, HostFunctionError>
WasmHostFunctionsImpl::computeSha512HalfHash(Slice const& data)
{
    auto const hash = sha512Half(data);
    return hash;
}

}  // namespace xrpl
