#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/nft.h>

#include <helpers/Account.h>
#include <tx/wasm/fixtures/WasmLedger.h>

#include <cstdint>
#include <optional>
#include <string_view>

// NFToken setup, built on `WasmLedger` rather than on a GTest fixture so a benchmark can mint a
// token without linking a test framework. Tests reach the same helpers through
// `RealHostFixture`, which derives from `WasmLedger`.

namespace xrpl::test {

// The fields baked into `makeNftId`, so a caller can assert an extractor returned the right one.
struct NftIds
{
    static constexpr std::uint16_t kFlags = nft::kFlagTransferable | nft::kFlagBurnable;
    static constexpr std::uint16_t kFee = 314;
    static constexpr std::uint32_t kTaxon = 12345;
    static constexpr std::uint32_t kSequence = 7;

    // A well-formed id carrying the constants above. Computed, not minted: the id-extractor host
    // functions read the id itself and never touch the ledger.
    static uint256
    makeNftId(AccountID const& issuer);
};

// Mint a real NFToken owned by `issuer` (taxon 0) and return its id, read back from the owner's
// NFTokenPage. `TxTest` applies to the open ledger, which produces no metadata, so the id comes
// from ledger state rather than from the mint's metadata.
//
// Throws via `fixtureFailed` if the mint or the page lookup fails; see WasmLedger.h for why that
// is a throw and not an `EXPECT_`.
uint256
mintNft(
    WasmLedger& fixture,
    Account const& issuer,
    std::optional<std::string_view> uri = std::nullopt);

}  // namespace xrpl::test
