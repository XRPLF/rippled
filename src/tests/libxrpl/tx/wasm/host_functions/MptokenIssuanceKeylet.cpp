#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <tx/wasm/RealHostFixture.h>

namespace xrpl::test {

struct MptokenIssuanceKeyletImpl : WasmImplTest
{
};

TEST_F(MptokenIssuanceKeyletImpl, MatchesMptokenIssuanceKeyletFunction)
{
    auto const owner = fund("owner");

    expectKeyletMatches(
        makeHost()->mptokenIssuanceKeylet(owner.id(), 1u),
        keylet::mptokenIssuance(makeMptID(1u, owner.id())));
}

TEST_F(MptokenIssuanceKeyletImpl, InvalidAccount)
{
    expectError(
        makeHost()->mptokenIssuanceKeylet(AccountID{}, 1u), HostFunctionError::InvalidAccount);
}

}  // namespace xrpl::test
