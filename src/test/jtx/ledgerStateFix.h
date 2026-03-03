#pragma once

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
