
#include <test/jtx/permissioned_dex.h>

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/credentials.h>
#include <test/jtx/pay.h>
#include <test/jtx/permissioned_domains.h>

#include <xrpl/basics/base_uint.h>

#include <string>
#include <vector>

namespace xrpl::test::jtx {

uint256
setupDomain(
    jtx::Env& env,
    std::vector<jtx::Account> const& accounts,
    jtx::Account const& domainOwner,
    std::string const& credType)
{
    using namespace jtx;
    env.fund(XRP(100000), domainOwner);
    env.close();

    pdomain::Credentials const credentials{{domainOwner, credType}};
    env(pdomain::setTx(domainOwner, credentials));

    auto const objects = pdomain::getObjects(domainOwner, env);
    auto const domainID = objects.begin()->first;

    for (auto const& account : accounts)
    {
        env(credentials::create(account, domainOwner, credType));
        env.close();
        env(credentials::accept(account, domainOwner, credType));
        env.close();
    }
    return domainID;
}

PermissionedDEX::PermissionedDEX(Env& env)
    : gw("permdex-gateway")
    , domainOwner("permdex-domainOwner")
    , alice("permdex-alice")
    , bob("permdex-bob")
    , carol("permdex-carol")
    , usd(gw["USD"])
    , credType("permdex-abcde")
{
    // Fund accounts
    env.fund(XRP(100000), alice, bob, carol, gw);
    env.close();

    domainID = setupDomain(env, {alice, bob, carol, gw}, domainOwner, credType);

    for (auto const& account : {alice, bob, carol, domainOwner})
    {
        env.trust(usd(1000), account);
        env.close();

        env(pay(gw, account, usd(100)));
        env.close();
    }
}

}  // namespace xrpl::test::jtx
