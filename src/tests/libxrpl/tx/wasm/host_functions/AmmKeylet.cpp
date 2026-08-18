#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <tx/wasm/RealHostFixture.h>

namespace xrpl::test {

struct AmmKeyletImpl : WasmImplTest
{
};

TEST_F(AmmKeyletImpl, MatchesAmmKeyletFunction)
{
    auto const owner = fund("owner");

    auto usdIssue = Issue{toCurrency("USD"), owner.id()};

    expectKeyletMatches(
        makeHost()->ammKeylet(usdIssue, xrpIssue()), keylet::amm(xrpIssue(), usdIssue));
}

TEST_F(AmmKeyletImpl, InvalidParameters)
{
    auto const owner = fund("owner");

    auto baseMpt = makeMptID(1, owner.id());

    expectError(makeHost()->ammKeylet(xrpIssue(), xrpIssue()), HostFunctionError::InvalidParams);
    expectError(makeHost()->ammKeylet(xrpIssue(), baseMpt), HostFunctionError::InvalidParams);
    expectError(makeHost()->ammKeylet(baseMpt, xrpIssue()), HostFunctionError::InvalidParams);
}

}  // namespace xrpl::test
