#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <tx/wasm/fixtures/RealHostFixture.h>

namespace xrpl::test {

struct DidKeyletImpl : RealHostFixture
{
};

TEST_F(DidKeyletImpl, MatchesDidKeyletFunction)
{
    auto const owner = fund("owner");

    expectKeyletMatches(makeHost()->didKeylet(owner.id()), keylet::did(owner.id()));
}

TEST_F(DidKeyletImpl, InvalidAccount)
{
    expectError(makeHost()->didKeylet(AccountID{}), HostFunctionError::InvalidAccount);
}

}  // namespace xrpl::test
