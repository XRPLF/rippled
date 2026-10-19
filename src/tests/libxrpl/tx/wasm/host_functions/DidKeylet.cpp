#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <tx/wasm/fixtures/RealHostFixture.h>

#include <xrpl_host_functions.h>

namespace xrpl::test {

struct DidKeyletImpl : RealHostFixture
{
};

TEST_F(DidKeyletImpl, matches_did_keylet_function)
{
    auto const owner = fund("owner");

    expectKeyletMatches(makeHost()->didKeylet(owner.id()), keylet::did(owner.id()));
}

TEST_F(DidKeyletImpl, invalid_account)
{
    expectError(makeHost()->didKeylet(AccountID{}), HostFunctionError::InvalidAccount);
}

}  // namespace xrpl::test
