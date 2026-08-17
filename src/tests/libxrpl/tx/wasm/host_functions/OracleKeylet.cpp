#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <tx/wasm/RealHostFixture.h>

namespace xrpl::test {

struct OracleKeyletImpl : WasmImplTest
{
};

TEST_F(OracleKeyletImpl, MatchesOracleFunction)
{
    auto const owner = fund("owner");

    expectKeyletMatches(makeHost()->oracleKeylet(owner.id(), 1u), keylet::oracle(owner.id(), 1u));
}

TEST_F(OracleKeyletImpl, InvalidAccount)
{
    expectError(makeHost()->oracleKeylet(AccountID{}, 1u), HostFunctionError::InvalidAccount);
}

}  // namespace xrpl::test
