#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <tx/wasm/fixtures/RealHostFixture.h>

namespace xrpl::test {

struct OracleKeyletImpl : RealHostFixture
{
};

TEST_F(OracleKeyletImpl, matches_oracle_function)
{
    auto const owner = fund("owner");

    expectKeyletMatches(makeHost()->oracleKeylet(owner.id(), 1u), keylet::oracle(owner.id(), 1u));
}

TEST_F(OracleKeyletImpl, invalid_account)
{
    expectError(makeHost()->oracleKeylet(AccountID{}, 1u), HostFunctionError::InvalidAccount);
}

}  // namespace xrpl::test
