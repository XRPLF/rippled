#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <tx/wasm/RealHostFixture.h>

namespace xrpl::test {

struct MptokenKeyletImpl : WasmImplTest
{
};

TEST_F(MptokenKeyletImpl, MatchesMptokenKeyletFunction)
{
    auto const owner = fund("owner");
    auto const anotherAccount = fund("account");

    auto const mpt = makeMptID(1u, owner.id());
    expectKeyletMatches(
        makeHost()->mptokenKeylet(mpt, anotherAccount.id()),
        keylet::mptoken(mpt, anotherAccount.id()));
}

TEST_F(MptokenKeyletImpl, InvalidMpt)
{
    auto const owner = fund("owner");
    expectError(makeHost()->mptokenKeylet(MPTID{}, owner.id()), HostFunctionError::InvalidParams);
}

TEST_F(MptokenKeyletImpl, InvalidAccount)
{
    auto const owner = fund("owner");

    auto const mpt = makeMptID(1u, owner.id());
    expectError(makeHost()->mptokenKeylet(mpt, AccountID{}), HostFunctionError::InvalidAccount);
}

}  // namespace xrpl::test
