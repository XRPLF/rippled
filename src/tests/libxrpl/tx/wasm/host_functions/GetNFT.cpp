#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <tx/wasm/fixtures/NFTFixture.h>
#include <tx/wasm/fixtures/RealHostFixture.h>

#include <string_view>

namespace xrpl::test {

struct GetNFTImpl : NFTTest
{
};

TEST_F(GetNFTImpl, unset_account_is_invalid_account)
{
    auto const issuer = Account{"issuer"};
    expectError(
        makeHost()->getNFT(AccountID{}, makeNftId(issuer.id())), HostFunctionError::InvalidAccount);
}

TEST_F(GetNFTImpl, zero_id_is_invalid_params)
{
    auto const owner = fund("owner");
    expectError(makeHost()->getNFT(owner.id(), uint256{}), HostFunctionError::InvalidParams);
}

TEST_F(GetNFTImpl, missing_token_is_not_found)
{
    auto const owner = fund("owner");
    expectError(
        makeHost()->getNFT(owner.id(), makeNftId(owner.id())),
        HostFunctionError::LedgerObjNotFound);
}

TEST_F(GetNFTImpl, returns_uri)
{
    auto const owner = fund("owner");
    auto const uri = std::string_view{"https://example.com/nft"};
    auto const id = mintNFT(owner, uri);
    expectValue(makeHost()->getNFT(owner.id(), id), RealHostFixture::toBytes(uri));
}

TEST_F(GetNFTImpl, without_uri_field_not_found)
{
    auto const owner = fund("owner");
    auto const id = mintNFT(owner);
    expectError(makeHost()->getNFT(owner.id(), id), HostFunctionError::FieldNotFound);
}

}  // namespace xrpl::test
