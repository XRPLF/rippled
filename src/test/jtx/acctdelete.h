#pragma once

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>

#include <xrpl/json/json_value.h>

#include <cstdint>

namespace xrpl::test::jtx {

/** Delete account.  If successful transfer remaining XRP to dest. */
json::Value
acctdelete(Account const& account, Account const& dest);

// Close the ledger until the ledger sequence is large enough to close
// the account.  If margin is specified, close the ledger so `margin`
// more closes are needed
void
incLgrSeqForAccDel(jtx::Env& env, jtx::Account const& acc, std::uint32_t margin = 0);

}  // namespace xrpl::test::jtx
