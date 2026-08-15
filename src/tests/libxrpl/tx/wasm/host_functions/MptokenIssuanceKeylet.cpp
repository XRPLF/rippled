#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <helpers/TxTest.h>
#include <tx/wasm/RealHostFixture.h>

#include <expected>
#include <iterator>

namespace xrpl::test {

struct MptokenIssuanceKeyletImpl : WasmImplTest
{
};

TEST_F(MptokenIssuanceKeyletImpl, MatchesMptokenIssuanceKeyletFunction)
{
    auto const owner = Account{"owner"};
    ledger.createAccount(owner, XRP(1000));

    auto const expected = keylet::mptokenIssuance(makeMptID(1u, owner.id()));
    auto const expectedBytes = Bytes{std::begin(expected.key), std::end(expected.key)};
    auto const result = host().mptokenIssuanceKeylet(owner.id(), 1u);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, expectedBytes);
}

TEST_F(MptokenIssuanceKeyletImpl, InvalidAccount)
{
    auto result = host().mptokenIssuanceKeylet(AccountID{}, 1u);
    ASSERT_TRUE(!result.has_value());
    EXPECT_EQ(result.error(), HostFunctionError::InvalidAccount);
}

}  // namespace xrpl::test
