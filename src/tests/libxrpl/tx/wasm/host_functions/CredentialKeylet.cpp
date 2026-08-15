#include <xrpl/basics/Slice.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <helpers/TxTest.h>
#include <tx/wasm/RealHostFixture.h>

#include <expected>
#include <iterator>

namespace xrpl::test {

struct CredentialKeyletImpl : WasmImplTest
{
};

TEST_F(CredentialKeyletImpl, MatchesCredentialKeyletFunction)
{
    auto const owner = Account{"owner"};
    ledger.createAccount(owner, XRP(1000));

    auto const credTypeStr = std::string{"test"};
    auto const credType = Slice{credTypeStr.data(), credTypeStr.size()};

    auto const expected = keylet::credential(owner.id(), owner.id(), credType);
    auto const expectedBytes = Bytes{std::begin(expected.key), std::end(expected.key)};
    auto const result = host().credentialKeylet(owner.id(), owner.id(), credType);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, expectedBytes);
}

TEST_F(CredentialKeyletImpl, CredentialTypeStringTooLong)
{
    auto const owner = Account{"owner"};
    ledger.createAccount(owner, XRP(1000));

    auto constexpr credTypeStr = std::string_view{
        "abcdefghijklmnopqrstuvwxyz01234567890qwertyuiop[]"
        "asdfghjkl;'zxcvbnm8237tr28weufwldebvfv8734t07p"};
    static_assert(credTypeStr.size() > kMaxCredentialTypeLength);
    auto const credType = Slice{credTypeStr.data(), credTypeStr.size()};

    auto const result = host().credentialKeylet(owner.id(), owner.id(), credType);
    ASSERT_TRUE(!result.has_value());
    EXPECT_EQ(result.error(), HostFunctionError::InvalidParams);
}

TEST_F(CredentialKeyletImpl, InvalidAccount)
{
    auto const owner = Account{"owner"};
    ledger.createAccount(owner, XRP(1000));

    auto const credTypeStr = std::string{"test"};
    auto const credType = Slice{credTypeStr.data(), credTypeStr.size()};

    auto result = host().credentialKeylet(AccountID{}, owner.id(), credType);
    ASSERT_TRUE(!result.has_value());
    EXPECT_EQ(result.error(), HostFunctionError::InvalidAccount);

    result = host().credentialKeylet(owner.id(), AccountID{}, credType);
    ASSERT_TRUE(!result.has_value());
    EXPECT_EQ(result.error(), HostFunctionError::InvalidAccount);
}

}  // namespace xrpl::test
