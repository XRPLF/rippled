#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <tx/wasm/fixtures/RealHostFixture.h>

namespace xrpl::test {

struct SignerListKeyletImpl : RealHostFixture
{
};

TEST_F(SignerListKeyletImpl, matches_signer_list_function)
{
    auto const owner = fund("owner");

    expectKeyletMatches(makeHost()->signerListKeylet(owner.id()), keylet::signerList(owner.id()));
}

TEST_F(SignerListKeyletImpl, invalid_account)
{
    expectError(makeHost()->signerListKeylet(AccountID{}), HostFunctionError::InvalidAccount);
}

}  // namespace xrpl::test
