#include <tx/wasm/NFTFixture.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/nft.h>
#include <xrpl/protocol_autogen/transactions/NFTokenMint.h>  // IWYU pragma: keep
#include <xrpl/tx/transactors/nft/NFTokenMint.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>

#include <optional>
#include <string_view>

namespace xrpl::test {

uint256
NFTTest::makeNftId(AccountID const& issuer)
{
    return NFTokenMint::createNFTokenID(kFlags, kFee, issuer, nft::toTaxon(kTaxon), kSequence);
}

uint256
NFTTest::mintNFT(Account const& issuer, std::optional<std::string_view> uri)
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

}  // namespace xrpl::test
