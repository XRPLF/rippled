#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <tx/wasm/fixtures/RealHostFixture.h>

namespace xrpl::test {

struct DepositPreauthKeyletImpl : RealHostFixture
{
};

TEST_F(DepositPreauthKeyletImpl, MatchesDepositPreauthKeyletFunction)
{
    auto const owner = fund("owner");
    auto const destination = fund("destination");

    expectKeyletMatches(
        makeHost()->depositPreauthKeylet(owner.id(), destination.id()),
        keylet::depositPreauth(owner.id(), destination.id()));
}

TEST_F(DepositPreauthKeyletImpl, CantPreauthToSelf)
{
    auto const owner = fund("owner");

    expectError(
        makeHost()->depositPreauthKeylet(owner.id(), owner.id()), HostFunctionError::InvalidParams);
}

TEST_F(DepositPreauthKeyletImpl, InvalidAccount)
{
    auto const owner = fund("owner");

    auto h = makeHost();
    expectError(
        h->depositPreauthKeylet(AccountID{}, owner.id()), HostFunctionError::InvalidAccount);
    expectError(
        h->depositPreauthKeylet(owner.id(), AccountID{}), HostFunctionError::InvalidAccount);
}

}  // namespace xrpl::test
