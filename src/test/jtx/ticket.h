#pragma once

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/JTx.h>
#include <test/jtx/owners.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/LedgerFormats.h>

#include <cstdint>

namespace xrpl::test::jtx {

/*
    This shows how the jtx system may be extended to other
    generators, funclets, conditions, and operations,
    without changing the base declarations.
*/

/** Ticket operations */
namespace ticket {

/** Create one of more tickets */
json::Value
create(Account const& account, std::uint32_t count);

/** Set a ticket sequence on a JTx. */
class Use
{
private:
    std::uint32_t ticketSeq_;

public:
    Use(std::uint32_t ticketSeq) : ticketSeq_{ticketSeq}
    {
    }

    void
    operator()(Env&, JTx& jt) const;
};

}  // namespace ticket

/** Match the number of tickets on the account. */
using tickets = OwnerCount<ltTICKET>;

}  // namespace xrpl::test::jtx
