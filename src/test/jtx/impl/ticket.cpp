#include <test/jtx/ticket.h>

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/JTx.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/jss.h>

#include <cstdint>

namespace xrpl::test::jtx::ticket {

Json::Value
create(Account const& account, std::uint32_t count)
{
    Json::Value jv;
    jv[jss::Account] = account.human();
    jv[jss::TransactionType] = jss::TicketCreate;
    jv[sfTicketCount.jsonName] = count;
    return jv;
}

void
use::operator()(Env&, JTx& jt) const
{
    jt.fill_seq = false;
    jt[sfSequence.jsonName] = 0u;
    jt[sfTicketSequence.jsonName] = ticketSeq_;
}

}  // namespace xrpl::test::jtx::ticket
