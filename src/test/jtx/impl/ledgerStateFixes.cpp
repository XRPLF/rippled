#include <test/jtx/ledgerStateFix.h>

#include <xrpld/app/tx/detail/LedgerStateFix.h>

#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

namespace ripple {
namespace test {
namespace jtx {

namespace ledgerStateFix {

// Fix NFTokenPage links on owner's account.  acct pays fee.
Json::Value
nftPageLinks(jtx::Account const& acct, jtx::Account const& owner)
{
    Json::Value jv;
    jv[sfAccount.jsonName] = acct.human();
    jv[sfLedgerFixType.jsonName] = LedgerStateFix::nfTokenPageLink;
    jv[sfOwner.jsonName] = owner.human();
    jv[sfTransactionType.jsonName] = jss::LedgerStateFix;
    return jv;
}

}  // namespace ledgerStateFix

}  // namespace jtx
}  // namespace test
}  // namespace ripple
