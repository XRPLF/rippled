#include <test/jtx.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/jss.h>

#include <exception>

namespace ripple {
namespace test {
namespace jtx {

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

    pdomain::Credentials credentials{{domainOwner, credType}};
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
    , USD(gw["USD"])
    , credType("permdex-abcde")
{
    // Fund accounts
    env.fund(XRP(100000), alice, bob, carol, gw);
    env.close();

    domainID = setupDomain(env, {alice, bob, carol, gw}, domainOwner, credType);

    for (auto const& account : {alice, bob, carol, domainOwner})
    {
        env.trust(USD(1000), account);
        env.close();

        env(pay(gw, account, USD(100)));
        env.close();
    }
}

}  // namespace jtx
}  // namespace test
}  // namespace ripple
