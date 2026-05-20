#include <xrpl/basics/Expected.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/ledger/helpers/NFTokenHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/nft.h>
#include <xrpl/tx/wasm/HostFunc.h>
#include <xrpl/tx/wasm/HostFuncImpl.h>
#include <xrpl/tx/wasm/ParamsHelper.h>

#include <cstdint>

namespace xrpl {

// =========================================================
// SECTION: NFT UTILS
// =========================================================

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::getNFT(AccountID const& account, uint256 const& nftId) const
{
    if (!account)
        return Unexpected(HostFunctionError::InvalidAccount);

    if (!nftId)
        return Unexpected(HostFunctionError::InvalidParams);

    auto obj = nft::findToken(ctx_.view(), account, nftId);
    if (!obj)
        return Unexpected(HostFunctionError::LedgerObjNotFound);

    auto objUri = obj->at(~sfURI);
    if (!objUri)
        return Unexpected(HostFunctionError::FieldNotFound);

    Slice const s = objUri->value();
    return Bytes(s.begin(), s.end());
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::getNFTIssuer(uint256 const& nftId) const
{
    auto const issuer = nft::getIssuer(nftId);
    if (!issuer)
        return Unexpected(HostFunctionError::InvalidParams);

    return Bytes{issuer.begin(), issuer.end()};
}

Expected<std::uint32_t, HostFunctionError>
WasmHostFunctionsImpl::getNFTTaxon(uint256 const& nftId) const
{
    return nft::toUInt32(nft::getTaxon(nftId));
}

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::getNFTFlags(uint256 const& nftId) const
{
    return nft::getFlags(nftId);
}

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::getNFTTransferFee(uint256 const& nftId) const
{
    return nft::getTransferFee(nftId);
}

Expected<std::uint32_t, HostFunctionError>
WasmHostFunctionsImpl::getNFTSerial(uint256 const& nftId) const
{
    return nft::getSerial(nftId);
}

}  // namespace xrpl
