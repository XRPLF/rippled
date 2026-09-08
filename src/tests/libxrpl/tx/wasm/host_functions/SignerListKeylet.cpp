#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <tx/wasm/RealHostFixture.h>

namespace xrpl::test {

struct SignerListKeyletImpl : RealHostFixture
{
};

TEST_F(SignerListKeyletImpl, MatchesSignerListFunction)
{
    auto const owner = fund("owner");

    expectKeyletMatches(makeHost()->signerListKeylet(owner.id()), keylet::signerList(owner.id()));
}

TEST_F(SignerListKeyletImpl, InvalidAccount)
{
    expectError(makeHost()->signerListKeylet(AccountID{}), HostFunctionError::InvalidAccount);
}

}  // namespace xrpl::test
