#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <tx/wasm/fixtures/RealHostFixture.h>

namespace xrpl::test {

struct AccountKeyletImpl : RealHostFixture
{
};

TEST_F(AccountKeyletImpl, matches_account_keylet_function)
{
    auto const owner = fund("owner");

    expectKeyletMatches(makeHost()->accountKeylet(owner), keylet::account(owner.id()));
}

TEST_F(AccountKeyletImpl, non_existent_account_still_computes_keylet)
{
    auto const nobody = Account{"nobody"};
    expectKeyletMatches(makeHost()->accountKeylet(nobody), keylet::account(nobody.id()));
}

TEST_F(AccountKeyletImpl, unset_account_is_invalid_account)
{
    expectError(makeHost()->accountKeylet(AccountID{}), HostFunctionError::InvalidAccount);
}

}  // namespace xrpl::test
