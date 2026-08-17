#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <tx/wasm/RealHostFixture.h>

namespace xrpl::test {

struct TicketKeyletImpl : WasmImplTest
{
};

TEST_F(TicketKeyletImpl, MatchesTicketFunction)
{
    auto const owner = fund("owner");

    expectKeyletMatches(
        makeHost()->ticketKeylet(owner.id(), 1u),
        keylet::ticket(owner.id(), SeqProxy::rawTicket(1u)));
}

TEST_F(TicketKeyletImpl, InvalidAccount)
{
    expectError(makeHost()->ticketKeylet(AccountID{}, 1u), HostFunctionError::InvalidAccount);
}

}  // namespace xrpl::test
