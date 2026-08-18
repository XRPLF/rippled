#include <xrpl/basics/Slice.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <tx/wasm/RealHostFixture.h>

namespace xrpl::test {

struct CredentialKeyletImpl : WasmImplTest
{
};

TEST_F(CredentialKeyletImpl, MatchesCredentialKeyletFunction)
{
    auto const owner = fund("owner");

    auto const credTypeStr = std::string{"test"};
    auto const credType = Slice{credTypeStr.data(), credTypeStr.size()};

    expectKeyletMatches(
        makeHost()->credentialKeylet(owner.id(), owner.id(), credType),
        keylet::credential(owner.id(), owner.id(), credType));
}

TEST_F(CredentialKeyletImpl, CredentialTypeStringTooLong)
{
    auto const owner = fund("owner");

    auto constexpr credTypeStr = std::string_view{
        "abcdefghijklmnopqrstuvwxyz01234567890qwertyuiop[]"
        "asdfghjkl;'zxcvbnm8237tr28weufwldebvfv8734t07p"};
    static_assert(credTypeStr.size() > kMaxCredentialTypeLength);
    auto const credType = Slice{credTypeStr.data(), credTypeStr.size()};

    expectError(
        makeHost()->credentialKeylet(owner.id(), owner.id(), credType),
        HostFunctionError::InvalidParams);
}

TEST_F(CredentialKeyletImpl, InvalidAccount)
{
    auto const owner = fund("owner");

    auto const credTypeStr = std::string{"test"};
    auto const credType = Slice{credTypeStr.data(), credTypeStr.size()};

    auto h = makeHost();
    expectError(
        h->credentialKeylet(AccountID{}, owner.id(), credType), HostFunctionError::InvalidAccount);

    expectError(
        h->credentialKeylet(owner.id(), AccountID{}, credType), HostFunctionError::InvalidAccount);
}

}  // namespace xrpl::test
