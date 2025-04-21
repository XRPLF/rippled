//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2025 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <test/jtx.h>
#include <test/jtx/permissioned_dex.h>
#include <test/jtx/permissioned_domains.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/jss.h>

#include <exception>

namespace ripple {
namespace test {
namespace jtx {

PermissionedDEX::PermissionedDEX(Env& env)
    : gw("gateway")
    , domainOwner("domainOwner")
    , alice("alice")
    , bob("bob")
    , carol("carol")
    , USD(gw["USD"])
    , credType("abcde")
{
    // Fund accounts
    env.fund(XRP(100000), domainOwner, alice, bob, carol, gw);
    env.close();

    auto setupTrustline = [&](Account const account) {
        env.trust(USD(1000), account);
        env.close();

        env(pay(gw, account, USD(100)));
        env.close();
    };

    for (auto const& account : {alice, bob, carol, domainOwner})
    {
        setupTrustline(account);
    }

    pdomain::Credentials credentials{{domainOwner, credType}};
    env(pdomain::setTx(domainOwner, credentials));

    auto objects = pdomain::getObjects(domainOwner, env);
    domainID = objects.begin()->first;

    auto setupDomain = [&](Account const account) {
        env(credentials::create(account, domainOwner, credType));
        env.close();
        env(credentials::accept(account, domainOwner, credType));
        env.close();
    };

    for (auto const& account : {alice, bob, carol, gw})
    {
        setupDomain(account);
    }
}

}  // namespace jtx
}  // namespace test
}  // namespace ripple
