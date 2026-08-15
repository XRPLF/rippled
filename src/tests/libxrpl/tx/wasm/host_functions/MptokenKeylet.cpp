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

struct MptokenKeyletImpl : WasmImplTest
{
};

TEST_F(MptokenKeyletImpl, MatchesMptokenKeyletFunction)
{
    auto const owner = Account{"owner"};
    ledger.createAccount(owner, XRP(1000));
    auto const anotherAccount = Account{"account"};
    ledger.createAccount(anotherAccount, XRP(1000));

    auto const mpt = makeMptID(1u, owner.id());
    auto const expected = keylet::mptoken(mpt, anotherAccount.id());
    auto const expectedBytes = Bytes{std::begin(expected.key), std::end(expected.key)};
    auto const result = host().mptokenKeylet(mpt, anotherAccount.id());
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, expectedBytes);
}

TEST_F(MptokenKeyletImpl, InvalidMpt)
{
    auto const owner = Account{"owner"};
    ledger.createAccount(owner, XRP(1000));
    auto result = host().mptokenKeylet(MPTID{}, owner.id());
    ASSERT_TRUE(!result.has_value());
    EXPECT_EQ(result.error(), HostFunctionError::InvalidParams);
}

TEST_F(MptokenKeyletImpl, InvalidAccount)
{
    auto const owner = Account{"owner"};
    ledger.createAccount(owner, XRP(1000));

    auto const mpt = makeMptID(1u, owner.id());
    auto result = host().mptokenKeylet(mpt, AccountID{});
    ASSERT_TRUE(!result.has_value());
    EXPECT_EQ(result.error(), HostFunctionError::InvalidAccount);
}

}  // namespace xrpl::test
