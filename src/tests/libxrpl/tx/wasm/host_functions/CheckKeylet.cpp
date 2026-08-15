#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <helpers/TxTest.h>
#include <tx/wasm/RealHostFixture.h>

#include <expected>
#include <iterator>

namespace xrpl::test {

struct CheckKeyletImpl : WasmImplTest
{
};

TEST_F(CheckKeyletImpl, MatchesCheckKeyletFunction)
{
    auto const owner = Account{"owner"};
    ledger.createAccount(owner, XRP(1000));

    auto const expected = keylet::check(owner.id(), SeqProxy::rawSequence(1u));
    auto const expectedBytes = Bytes{std::begin(expected.key), std::end(expected.key)};
    auto const result = host().checkKeylet(owner.id(), 1u);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, expectedBytes);
}

TEST_F(CheckKeyletImpl, UnsetAccountIsInvalidAccount)
{
    auto const result = host().checkKeylet(AccountID{}, 1u);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), HostFunctionError::InvalidAccount);
}

}  // namespace xrpl::test
