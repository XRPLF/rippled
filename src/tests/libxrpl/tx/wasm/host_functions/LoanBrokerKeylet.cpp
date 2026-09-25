#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <tx/wasm/fixtures/RealHostFixture.h>

namespace xrpl::test {

struct LoanBrokerKeyletImpl : RealHostFixture
{
};

TEST_F(LoanBrokerKeyletImpl, matches_loan_broker_keylet_function)
{
    auto const owner = fund("owner");

    expectKeyletMatches(
        makeHost()->loanBrokerKeylet(owner.id(), 1u),
        keylet::loanBroker(owner.id(), SeqProxy::rawSequence(1u)));
}

TEST_F(LoanBrokerKeyletImpl, invalid_account)
{
    expectError(makeHost()->loanBrokerKeylet(AccountID{}, 1u), HostFunctionError::InvalidAccount);
}

}  // namespace xrpl::test
