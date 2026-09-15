#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <tx/wasm/fixtures/RealHostFixture.h>

namespace xrpl::test {

struct PermissionedDomainKeyletImpl : RealHostFixture
{
};

TEST_F(PermissionedDomainKeyletImpl, MatchesPermissionedDomainFunction)
{
    auto const owner = fund("owner");

    expectKeyletMatches(
        makeHost()->permissionedDomainKeylet(owner.id(), 1u),
        keylet::permissionedDomain(owner.id(), SeqProxy::rawSequence(1u)));
}

TEST_F(PermissionedDomainKeyletImpl, InvalidAccount)
{
    expectError(
        makeHost()->permissionedDomainKeylet(AccountID{}, 1u), HostFunctionError::InvalidAccount);
}

}  // namespace xrpl::test
