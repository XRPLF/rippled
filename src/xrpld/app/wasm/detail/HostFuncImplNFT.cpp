#include <xrpld/app/wasm/HostFuncImpl.h>

#include <xrpl/protocol/STBitString.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/tx/transactors/NFT/NFTokenUtils.h>

namespace xrpl {

// =========================================================
// SECTION: NFT UTILS
// =========================================================

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::getNFT(AccountID const& account, uint256 const& nftId)
{
    if (!account)
        return Unexpected(HostFunctionError::INVALID_ACCOUNT);

    if (!nftId)
        return Unexpected(HostFunctionError::INVALID_PARAMS);

    auto obj = nft::findToken(ctx.view(), account, nftId);
    if (!obj)
        return Unexpected(HostFunctionError::LEDGER_OBJ_NOT_FOUND);

    auto objUri = obj->at(~sfURI);
    if (!objUri)
        return Unexpected(HostFunctionError::FIELD_NOT_FOUND);

    Slice const s = objUri->value();
    return Bytes(s.begin(), s.end());
}

Expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::getNFTIssuer(uint256 const& nftId)
{
    auto const issuer = nft::getIssuer(nftId);
    if (!issuer)
        return Unexpected(HostFunctionError::INVALID_PARAMS);

    return Bytes{issuer.begin(), issuer.end()};
}

Expected<std::uint32_t, HostFunctionError>
WasmHostFunctionsImpl::getNFTTaxon(uint256 const& nftId)
{
    return nft::toUInt32(nft::getTaxon(nftId));
}

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::getNFTFlags(uint256 const& nftId)
{
    return nft::getFlags(nftId);
}

Expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::getNFTTransferFee(uint256 const& nftId)
{
    return nft::getTransferFee(nftId);
}

Expected<std::uint32_t, HostFunctionError>
WasmHostFunctionsImpl::getNFTSerial(uint256 const& nftId)
{
    return nft::getSerial(nftId);
}

}  // namespace xrpl
