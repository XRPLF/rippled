#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/nft.h>

#include <helpers/Account.h>
#include <tx/wasm/RealHostFixture.h>

#include <cstdint>
#include <optional>
#include <string_view>

namespace xrpl::test {

struct NFTTest : RealHostFixture
{
    static constexpr std::uint16_t kFlags = nft::kFlagTransferable | nft::kFlagBurnable;
    static constexpr std::uint16_t kFee = 314;
    static constexpr std::uint32_t kTaxon = 12345;
    static constexpr std::uint32_t kSequence = 7;

    static uint256
    makeNftId(AccountID const& issuer);

    // Mint a real NFToken owned by `issuer` (taxon 0) and return its id, read back from the
    // owner's NFTokenPage. TxTest applies to the open ledger, which produces no metadata, so
    // the id is recovered from ledger state rather than from the mint's metadata.
    uint256
    mintNFT(Account const& issuer, std::optional<std::string_view> uri = std::nullopt);
};

}  // namespace xrpl::test
