#ifndef XRPL_TEST_JTX_TICKET_H_INCLUDED
#define XRPL_TEST_JTX_TICKET_H_INCLUDED

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/owners.h>

#include <cstdint>

namespace ripple {
namespace test {
namespace jtx {

/*
    This shows how the jtx system may be extended to other
    generators, funclets, conditions, and operations,
    without changing the base declarations.
*/

/** Ticket operations */
namespace ticket {

/** Create one of more tickets */
Json::Value
create(Account const& account, std::uint32_t count);

/** Set a ticket sequence on a JTx. */
class use
{
private:
    std::uint32_t ticketSeq_;

public:
    use(std::uint32_t ticketSeq) : ticketSeq_{ticketSeq}
    {
    }

    void
    operator()(Env&, JTx& jt) const;
};

}  // namespace ticket

/** Match the number of tickets on the account. */
using tickets = owner_count<ltTICKET>;

}  // namespace jtx

}  // namespace test
}  // namespace ripple

#endif
