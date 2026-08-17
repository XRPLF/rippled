#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <tx/wasm/RealHostFixture.h>

namespace xrpl::test {

struct VaultKeyletImpl : WasmImplTest
{
};

TEST_F(VaultKeyletImpl, MatchesVaultFunction)
{
    auto const owner = fund("owner");

    expectKeyletMatches(
        makeHost()->vaultKeylet(owner.id(), 1u),
        keylet::vault(owner.id(), SeqProxy::rawSequence(1u)));
}

TEST_F(VaultKeyletImpl, InvalidAccount)
{
    expectError(makeHost()->vaultKeylet(AccountID{}, 1u), HostFunctionError::InvalidAccount);
}

}  // namespace xrpl::test
