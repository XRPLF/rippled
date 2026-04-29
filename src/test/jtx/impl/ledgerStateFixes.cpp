#include <test/jtx/Account.h>
#include <test/jtx/ledgerStateFix.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/tx/transactors/system/LedgerStateFix.h>

#include <cstdint>

namespace xrpl::test::jtx::ledgerStateFix {

// Fix NFTokenPage links on owner's account.  acct pays fee.
Json::Value
nftPageLinks(jtx::Account const& acct, jtx::Account const& owner)
{
    Json::Value jv;
    jv[sfAccount.jsonName] = acct.human();
    jv[sfLedgerFixType.jsonName] =
        static_cast<std::uint16_t>(LedgerStateFix::FixType::nfTokenPageLink);
    jv[sfOwner.jsonName] = owner.human();
    jv[sfTransactionType.jsonName] = jss::LedgerStateFix;
    return jv;
}

}  // namespace xrpl::test::jtx::ledgerStateFix
