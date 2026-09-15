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

TEST_F(GetNFTImpl, UnsetAccountIsInvalidAccount)
{
    auto const issuer = Account{"issuer"};
    expectError(
        makeHost()->getNFT(AccountID{}, makeNftId(issuer.id())), HostFunctionError::InvalidAccount);
}

TEST_F(GetNFTImpl, ZeroIdIsInvalidParams)
{
    auto const owner = fund("owner");
    expectError(makeHost()->getNFT(owner.id(), uint256{}), HostFunctionError::InvalidParams);
}

TEST_F(GetNFTImpl, MissingTokenIsNotFound)
{
    auto const owner = fund("owner");
    expectError(
        makeHost()->getNFT(owner.id(), makeNftId(owner.id())),
        HostFunctionError::LedgerObjNotFound);
}

TEST_F(GetNFTImpl, ReturnsUri)
{
    auto const owner = fund("owner");
    auto const uri = std::string_view{"https://example.com/nft"};
    auto const id = mintNFT(owner, uri);
    expectValue(makeHost()->getNFT(owner.id(), id), RealHostFixture::toBytes(uri));
}

TEST_F(GetNFTImpl, WithoutUriFieldNotFound)
{
    auto const owner = fund("owner");
    auto const id = mintNFT(owner);
    expectError(makeHost()->getNFT(owner.id(), id), HostFunctionError::FieldNotFound);
}

}  // namespace xrpl::test
