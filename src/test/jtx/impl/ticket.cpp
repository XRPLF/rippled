#include <test/jtx/ticket.h>

#include <xrpl/protocol/jss.h>

namespace ripple {
namespace test {
namespace jtx {

namespace ticket {

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

}  // namespace ticket

}  // namespace jtx
}  // namespace test
}  // namespace ripple
