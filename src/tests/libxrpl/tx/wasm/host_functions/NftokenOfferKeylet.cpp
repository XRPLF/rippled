#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <tx/wasm/RealHostFixture.h>

namespace xrpl::test {

struct NftokenOfferKeyletImpl : RealHostFixture
{
};

TEST_F(NftokenOfferKeyletImpl, MatchesNftokenOfferFunction)
{
    auto const owner = fund("owner");

    expectKeyletMatches(
        makeHost()->nftokenOfferKeylet(owner.id(), 1u),
        keylet::nftokenOffer(owner.id(), SeqProxy::rawSequence(1u)));
}

TEST_F(NftokenOfferKeyletImpl, InvalidAccount)
{
    expectError(makeHost()->nftokenOfferKeylet(AccountID{}, 1u), HostFunctionError::InvalidAccount);
}

}  // namespace xrpl::test
