#include <test/jtx/acctdelete.h>

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/jss.h>

#include <cstdint>

namespace xrpl::test::jtx {

// Delete account.  If successful transfer remaining XRP to dest.
Json::Value
acctdelete(jtx::Account const& account, jtx::Account const& dest)
{
    Json::Value jv;
    jv[sfAccount.jsonName] = account.human();
    jv[sfDestination.jsonName] = dest.human();
    jv[sfTransactionType.jsonName] = jss::AccountDelete;
    return jv;
}

// Close the ledger until the ledger sequence is large enough to close
// the account.  If margin is specified, close the ledger so `margin`
// more closes are needed
void
incLgrSeqForAccDel(jtx::Env& env, jtx::Account const& acc, std::uint32_t margin)
{
    using namespace jtx;
    auto openLedgerSeq = [](jtx::Env& env) -> std::uint32_t { return env.current()->seq(); };

    int const delta = [&]() -> int {
        if (env.seq(acc) + 255 > openLedgerSeq(env))
            return env.seq(acc) - openLedgerSeq(env) + 255 - margin;
        return 0;
    }();
    env.test.BEAST_EXPECT(margin == 0 || delta >= 0);
    for (int i = 0; i < delta; ++i)
        env.close();
    env.test.BEAST_EXPECT(openLedgerSeq(env) == env.seq(acc) + 255 - margin);
}

}  // namespace xrpl::test::jtx
