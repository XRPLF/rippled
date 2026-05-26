#pragma once

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>

/** LedgerStateFix operations. */
namespace xrpl::test::jtx::ledgerStateFix {

/** Repair the links in an NFToken directory. */
json::Value
nftPageLinks(jtx::Account const& acct, jtx::Account const& owner);

/** Repair sfExchangeRate on a book directory's first page. */
json::Value
bookExchangeRate(jtx::Account const& acct, uint256 const& bookDir);

}  // namespace xrpl::test::jtx::ledgerStateFix
