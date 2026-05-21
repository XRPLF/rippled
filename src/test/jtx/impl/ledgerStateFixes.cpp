#include <test/jtx/Account.h>
#include <test/jtx/ledgerStateFix.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/tx/transactors/system/LedgerStateFix.h>

#include <cstdint>

namespace xrpl::test::jtx::ledgerStateFix {

// Fix NFTokenPage links on owner's account.  acct pays fee.
json::Value
nftPageLinks(jtx::Account const& acct, jtx::Account const& owner)
{
    json::Value jv;
    jv[sfAccount.jsonName] = acct.human();
    jv[sfLedgerFixType.jsonName] = static_cast<uint16_t>(LedgerStateFix::FixType::NfTokenPageLink);
    jv[sfOwner.jsonName] = owner.human();
    jv[sfTransactionType.jsonName] = jss::LedgerStateFix;
    return jv;
}

// Fix sfExchangeRate on a book directory.  acct pays fee.
json::Value
bookExchangeRate(jtx::Account const& acct, uint256 const& bookDir)
{
    json::Value jv;
    jv[sfAccount.jsonName] = acct.human();
    jv[sfLedgerFixType.jsonName] = static_cast<uint16_t>(LedgerStateFix::FixType::BookExchangeRate);
    jv[sfBookDirectory.jsonName] = to_string(bookDir);
    jv[sfTransactionType.jsonName] = jss::LedgerStateFix;
    return jv;
}

}  // namespace xrpl::test::jtx::ledgerStateFix
