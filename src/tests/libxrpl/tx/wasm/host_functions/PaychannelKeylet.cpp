#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <tx/wasm/RealHostFixture.h>

namespace xrpl::test {

struct PaychannelKeyletImpl : RealHostFixture
{
};

TEST_F(PaychannelKeyletImpl, MatchesPaychannelFunction)
{
    auto const owner = fund("owner");
    auto const destination = fund("destination");

    expectKeyletMatches(
        makeHost()->paychannelKeylet(owner.id(), destination.id(), 1u),
        keylet::payChannel(owner.id(), destination.id(), SeqProxy::rawSequence(1u)));
}

TEST_F(PaychannelKeyletImpl, CantUseSelf)
{
    auto const owner = fund("owner");

    expectError(
        makeHost()->paychannelKeylet(owner.id(), owner.id(), 1u), HostFunctionError::InvalidParams);
}

TEST_F(PaychannelKeyletImpl, InvalidAccount)
{
    auto const owner = fund("owner");

    auto h = makeHost();

    expectError(
        h->paychannelKeylet(AccountID{}, owner.id(), 1u), HostFunctionError::InvalidAccount);

    expectError(
        h->paychannelKeylet(owner.id(), AccountID{}, 1u), HostFunctionError::InvalidAccount);
}

}  // namespace xrpl::test
