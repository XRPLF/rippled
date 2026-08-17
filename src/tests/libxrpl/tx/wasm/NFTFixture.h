#pragma once

#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/nft.h>
#include <xrpl/protocol_autogen/transactions/NFTokenMint.h>
#include <xrpl/tx/transactors/nft/NFTokenMint.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <helpers/TxTest.h>
#include <tx/wasm/RealHostFixture.h>

#include <cstdint>
#include <optional>
#include <string_view>

namespace xrpl::test {

struct NFTTest : WasmImplTest
{
    static constexpr std::uint16_t kFlags = nft::kFlagTransferable | nft::kFlagBurnable;
    static constexpr std::uint16_t kFee = 314;
    static constexpr std::uint32_t kTaxon = 12345;
    static constexpr std::uint32_t kSequence = 7;

    static uint256
    makeNftId(AccountID const& issuer)
    {
        return NFTokenMint::createNFTokenID(kFlags, kFee, issuer, nft::toTaxon(kTaxon), kSequence);
    }

    // Mint a real NFToken owned by `issuer` (taxon 0) and return its id, read back from the
    // owner's NFTokenPage. TxTest applies to the open ledger, which produces no metadata, so
    // the id is recovered from ledger state rather than from the mint's metadata.
    uint256
    mintNFT(Account const& issuer, std::optional<std::string_view> uri = std::nullopt)
    {
        auto builder = transactions::NFTokenMintBuilder{issuer.id(), 0u};
        if (uri)
            builder.setURI(Slice{uri->data(), uri->size()});
        auto const r = ledger.submit(builder, issuer);
        EXPECT_EQ(r.ter, tesSUCCESS) << transToken(r.ter);
        ledger.close();

        // The single minted token lives in the owner's first NFTokenPage.
        auto const& view = ledger.getOpenLedger();
        auto const first = keylet::nftokenPageMin(issuer.id()).key;
        auto const last = keylet::nftokenPageMax(issuer.id()).key;
        auto const pageKey = view.succ(first, last.next());
        EXPECT_TRUE(pageKey.has_value());
        auto const page = pageKey ? view.read(Keylet{ltNFTOKEN_PAGE, *pageKey}) : nullptr;
        EXPECT_NE(page, nullptr);
        if (!page)
            return uint256{};
        auto const& tokens = page->getFieldArray(sfNFTokens);
        EXPECT_FALSE(tokens.empty());
        return tokens.empty() ? uint256{} : tokens[0].getFieldH256(sfNFTokenID);
    }
};

}  // namespace xrpl::test
