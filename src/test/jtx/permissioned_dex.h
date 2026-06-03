#pragma once

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>

namespace xrpl::test::jtx {

uint256
setupDomain(
    jtx::Env& env,
    std::vector<jtx::Account> const& accounts,
    jtx::Account const& domainOwner = jtx::Account("domainOwner"),
    std::string const& credType = "Cred");

class PermissionedDEX
{
public:
    Account gw;
    Account domainOwner;
    Account alice;
    Account bob;
    Account carol;
    IOU usd;
    uint256 domainID;
    std::string credType;

    PermissionedDEX(Env& env);
};

}  // namespace xrpl::test::jtx
