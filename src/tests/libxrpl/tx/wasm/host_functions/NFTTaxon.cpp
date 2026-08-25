
#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <tx/wasm/NFTFixture.h>
#include <tx/wasm/RealHostFixture.h>

namespace xrpl::test {

struct NFTTaxonImpl : NFTTest
{
};

TEST_F(NFTTaxonImpl, TaxonDecodesFromId)
{
    auto const issuer = Account{"issuer"};
    expectValue(makeHost()->getNFTTaxon(makeNftId(issuer.id())), kTaxon);
}

}  // namespace xrpl::test
