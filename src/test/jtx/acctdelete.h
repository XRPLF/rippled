#ifndef XRPL_TEST_JTX_ACCTDELETE_H_INCLUDED
#define XRPL_TEST_JTX_ACCTDELETE_H_INCLUDED

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>

#include <xrpl/beast/unit_test/suite.h>

namespace ripple {
namespace test {
namespace jtx {

/** Delete account.  If successful transfer remaining XRP to dest. */
Json::Value
acctdelete(Account const& account, Account const& dest);

// Close the ledger until the ledger sequence is large enough to close
// the account.  If margin is specified, close the ledger so `margin`
// more closes are needed
void
incLgrSeqForAccDel(
    jtx::Env& env,
    jtx::Account const& acc,
    std::uint32_t margin = 0);

}  // namespace jtx

}  // namespace test
}  // namespace ripple

#endif
