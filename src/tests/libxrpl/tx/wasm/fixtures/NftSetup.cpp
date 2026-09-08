#include <tx/wasm/fixtures/NftSetup.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/nft.h>
#include <xrpl/protocol_autogen/transactions/NFTokenMint.h>  // IWYU pragma: keep
#include <xrpl/tx/transactors/nft/NFTokenMint.h>

#include <helpers/Account.h>
#include <tx/wasm/fixtures/WasmLedger.h>

#include <optional>
#include <string>
#include <string_view>

namespace xrpl::test {

uint256
NftIds::makeNftId(AccountID const& issuer)
{
    return NFTokenMint::createNFTokenID(kFlags, kFee, issuer, nft::toTaxon(kTaxon), kSequence);
}

uint256
mintNft(WasmLedger& fixture, Account const& issuer, std::optional<std::string_view> uri)
{
    auto& ledger = fixture.ledger;
    auto builder = transactions::NFTokenMintBuilder{issuer.id(), 0u};
    if (uri)
        builder.setURI(Slice{uri->data(), uri->size()});
    auto const r = ledger.submit(builder, issuer);
    if (r.ter != tesSUCCESS)
    {
        fixtureFailed(std::string{"minting the NFToken: "} + transToken(r.ter));
    }
    ledger.close();

    // The single minted token lives in the owner's first NFTokenPage.
    auto const& view = ledger.getOpenLedger();
    auto const first = keylet::nftokenPageMin(issuer.id()).key;
    auto const last = keylet::nftokenPageMax(issuer.id()).key;
    auto const pageKey = view.succ(first, last.next());
    if (!pageKey.has_value())
    {
        fixtureFailed("finding the minted token's NFTokenPage");
    }
    auto const page = view.read(Keylet{ltNFTOKEN_PAGE, *pageKey});
    if (page == nullptr)
    {
        fixtureFailed("reading the minted token's NFTokenPage");
    }
    auto const& tokens = page->getFieldArray(sfNFTokens);
    if (tokens.empty())
    {
        fixtureFailed("the NFTokenPage holds no tokens");
    }
    return tokens[0].getFieldH256(sfNFTokenID);
}

}  // namespace xrpl::test
