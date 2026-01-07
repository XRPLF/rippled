#ifndef XRPL_TEST_JTX_LEDGER_STATE_FIX_H_INCLUDED
#define XRPL_TEST_JTX_LEDGER_STATE_FIX_H_INCLUDED

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>

namespace xrpl {
namespace test {
namespace jtx {

/** LedgerStateFix operations. */
namespace ledgerStateFix {

/** Repair the links in an NFToken directory. */
Json::Value
nftPageLinks(jtx::Account const& acct, jtx::Account const& owner);

}  // namespace ledgerStateFix

}  // namespace jtx

}  // namespace test
}  // namespace xrpl

#endif
