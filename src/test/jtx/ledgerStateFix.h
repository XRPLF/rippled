#pragma once

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>

/** LedgerStateFix operations. */
namespace xrpl::test::jtx::ledgerStateFix {

/** Repair the links in an NFToken directory. */
Json::Value
nftPageLinks(jtx::Account const& acct, jtx::Account const& owner);

}  // namespace xrpl::test::jtx::ledgerStateFix
