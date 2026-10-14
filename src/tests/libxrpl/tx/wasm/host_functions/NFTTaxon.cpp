
#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <tx/wasm/fixtures/NFTFixture.h>
#include <tx/wasm/fixtures/RealHostFixture.h>

namespace xrpl::test {

struct NFTTaxonImpl : NFTTest
{
};

TEST_F(NFTTaxonImpl, taxon_decodes_from_id)
{
    auto const issuer = Account{"issuer"};
    expectValue(makeHost()->getNFTTaxon(makeNftId(issuer.id())), kTaxon);
}

}  // namespace xrpl::test
