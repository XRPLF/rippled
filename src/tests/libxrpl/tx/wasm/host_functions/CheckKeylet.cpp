#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <tx/wasm/fixtures/RealHostFixture.h>

namespace xrpl::test {

struct CheckKeyletImpl : RealHostFixture
{
};

TEST_F(CheckKeyletImpl, MatchesCheckKeyletFunction)
{
    auto const owner = fund("owner");

    expectKeyletMatches(
        makeHost()->checkKeylet(owner.id(), 1u),
        keylet::check(owner.id(), SeqProxy::rawSequence(1u)));
}

TEST_F(CheckKeyletImpl, UnsetAccountIsInvalidAccount)
{
    expectError(makeHost()->checkKeylet(AccountID{}, 1u), HostFunctionError::InvalidAccount);
}

}  // namespace xrpl::test
