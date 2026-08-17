#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <tx/wasm/RealHostFixture.h>

namespace xrpl::test {

struct OfferKeyletImpl : WasmImplTest
{
};

TEST_F(OfferKeyletImpl, MatchesOfferFunction)
{
    auto const owner = fund("owner");

    expectKeyletMatches(
        makeHost()->offerKeylet(owner.id(), 1u),
        keylet::offer(owner.id(), SeqProxy::rawSequence(1u)));
}

TEST_F(OfferKeyletImpl, InvalidAccount)
{
    expectError(makeHost()->offerKeylet(AccountID{}, 1u), HostFunctionError::InvalidAccount);
}

}  // namespace xrpl::test
