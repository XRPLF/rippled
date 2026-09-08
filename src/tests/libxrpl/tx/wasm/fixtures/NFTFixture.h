#pragma once

#include <xrpl/basics/base_uint.h>

#include <helpers/Account.h>
#include <tx/wasm/fixtures/NftSetup.h>
#include <tx/wasm/fixtures/RealHostFixture.h>

#include <optional>
#include <string_view>

// The NFToken helpers with a real ledger and GTest attached, for the `host_functions/NFT*` tests.
// The ledger-only versions are in NftSetup.h, which links no test framework.

namespace xrpl::test {

struct NFTTest : RealHostFixture, NftIds
{
    uint256
    mintNFT(Account const& issuer, std::optional<std::string_view> uri = std::nullopt)
    {
        return mintNft(*this, issuer, uri);
    }
};

}  // namespace xrpl::test
