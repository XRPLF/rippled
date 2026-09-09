
#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <tx/wasm/fixtures/NFTFixture.h>
#include <tx/wasm/fixtures/RealHostFixture.h>

#include <cstdint>

namespace xrpl::test {

struct NFTSequenceImpl : NFTTest
{
};

TEST_F(NFTSequenceImpl, SequenceDecodesFromId)
{
    auto const issuer = Account{"issuer"};
    expectValue(makeHost()->getNFTSequence(makeNftId(issuer.id())), kSequence);
}

TEST_F(NFTSequenceImpl, SequenceShouldBeZeroWithZeroNftId)
{
    expectValue(makeHost()->getNFTSequence(uint256{}), std::int32_t{});
}

}  // namespace xrpl::test
