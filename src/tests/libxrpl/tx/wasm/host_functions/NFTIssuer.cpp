#include <xrpl/protocol/AccountID.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <tx/wasm/fixtures/NFTFixture.h>
#include <tx/wasm/fixtures/RealHostFixture.h>

#include <string_view>

namespace xrpl::test {

struct NFTIssuerImpl : NFTTest
{
};

TEST_F(NFTIssuerImpl, IssuerDecodesFromId)
{
    auto const issuer = Account{"issuer"};
    expectValue(
        makeHost()->getNFTIssuer(makeNftId(issuer.id())), RealHostFixture::toBytes(issuer.id()));
}

TEST_F(NFTIssuerImpl, IssuerZeroIsInvalidParams)
{
    expectError(makeHost()->getNFTIssuer(makeNftId(AccountID{})), HostFunctionError::InvalidParams);
}

}  // namespace xrpl::test
