#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <tx/wasm/RealHostFixture.h>

namespace xrpl::test {

struct DelegateKeyletImpl : WasmImplTest
{
};

TEST_F(DelegateKeyletImpl, MatchesDelegateKeyletFunction)
{
    auto const owner = fund("owner");
    auto const delegate = fund("delegate");

    expectKeyletMatches(
        makeHost()->delegateKeylet(owner.id(), delegate.id()),
        keylet::delegate(owner.id(), delegate.id()));
}

TEST_F(DelegateKeyletImpl, CantDelegateToSelf)
{
    auto const owner = fund("owner");

    expectError(
        makeHost()->delegateKeylet(owner.id(), owner.id()), HostFunctionError::InvalidParams);
}

TEST_F(DelegateKeyletImpl, InvalidAccount)
{
    auto const owner = fund("owner");

    auto h = makeHost();
    expectError(h->delegateKeylet(AccountID{}, owner.id()), HostFunctionError::InvalidAccount);
    expectError(h->delegateKeylet(owner.id(), AccountID{}), HostFunctionError::InvalidAccount);
}

}  // namespace xrpl::test
