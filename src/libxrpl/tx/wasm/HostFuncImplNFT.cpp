#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/ledger/helpers/NFTokenHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBitString.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/protocol/nft.h>
#include <xrpl/tx/wasm/HostFuncImpl.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <cstdint>
#include <expected>

namespace xrpl {

// =========================================================
// SECTION: NFT UTILS
// =========================================================

std::expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::getNFT(AccountID const& account, uint256 const& nftId) const
{
    if (!account)
        return std::unexpected(HostFunctionError::InvalidAccount);

    if (!nftId)
        return std::unexpected(HostFunctionError::InvalidParams);

    auto obj = nft::findToken(ctx_.view(), account, nftId);
    if (!obj)
        return std::unexpected(HostFunctionError::LedgerObjNotFound);

    auto objUri = obj->at(~sfURI);
    if (!objUri)
        return std::unexpected(HostFunctionError::FieldNotFound);

    Slice const s = objUri->value();
    return Bytes(s.begin(), s.end());
}

std::expected<Bytes, HostFunctionError>
WasmHostFunctionsImpl::getNFTIssuer(uint256 const& nftId) const
{
    auto const issuer = nft::getIssuer(nftId);
    if (!issuer)
        return std::unexpected(HostFunctionError::InvalidParams);

    return Bytes{issuer.begin(), issuer.end()};
}

std::expected<std::uint32_t, HostFunctionError>
WasmHostFunctionsImpl::getNFTTaxon(uint256 const& nftId) const
{
    return nft::toUInt32(nft::getTaxon(nftId));
}

std::expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::getNFTFlags(uint256 const& nftId) const
{
    return nft::getFlags(nftId);
}

std::expected<int32_t, HostFunctionError>
WasmHostFunctionsImpl::getNFTTransferFee(uint256 const& nftId) const
{
    return nft::getTransferFee(nftId);
}

std::expected<std::uint32_t, HostFunctionError>
WasmHostFunctionsImpl::getNFTSequence(uint256 const& nftId) const
{
    return nft::getSequence(nftId);
}

}  // namespace xrpl
