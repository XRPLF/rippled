//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2017 Ripple Labs Inc.

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

#include <ripple/protocol/Feature.h>
#include <ripple/protocol/jss.h>
#include <test/jtx.h>

namespace ripple {
namespace test {

struct TemporalTxValidity_test : public beast::unit_test::suite
{
    std::uint32_t
    nettime(jtx::Env& env)
    {
        return static_cast<std::uint32_t>(
            env.closed()->info().closeTime.time_since_epoch().count());
    }

    void
    run() override
    {
        using namespace jtx;

        Account const alice{"alice"};
        Account const bob{"bob"};

        {
            testcase("TemporalTxValidity: Amendment Not Enabled");

            Env env(*this, supported_amendments() - featureTemporalTxValidity);
            env.fund(XRP(1000000), alice);
            env.close();

            {  // Nothing special
                env(pay(alice, bob, XRP(5000)), ter(tesSUCCESS));
                env.close();
            }

            {  // NotValidAfter field - not supported
                auto tx = pay(alice, bob, XRP(5000));
                tx[jss::NotValidAfter] = 20034;
                env(tx, ter(temMALFORMED));
                env.close();
            }

            {  // NotValidBefore field - not supported
                auto tx = pay(alice, bob, XRP(5000));
                tx[jss::NotValidBefore] = 21576;
                env(tx, ter(temMALFORMED));
                env.close();
            }

            {  // NotValidBefore field - not supported
                auto tx = pay(alice, bob, XRP(5000));
                tx[jss::NotValidAfter] = 20034;
                tx[jss::NotValidBefore] = 21576;
                env(tx, ter(temMALFORMED));
            }
        }

        {
            testcase("Temporal Validity: Amendment Enabled");

            Env env(*this, supported_amendments() | featureTemporalTxValidity);
            env.fund(XRP(1000000), alice);
            env.close(std::chrono::seconds{60});

            {  // Nothing special
                env(pay(alice, bob, XRP(5000)), ter(tesSUCCESS));
                env.close(std::chrono::seconds{60});
            }

            {  // Invalid: before >= after
                auto tx = pay(alice, bob, XRP(5001));
                tx[jss::NotValidBefore] = nettime(env) + 10;
                tx[jss::NotValidAfter] = nettime(env) - 10;
                env(tx, ter(temBAD_TEMPORAL_VALIDITY));
                env.close(std::chrono::seconds{60});
            }

            {  // Too soon: the transaction can't execute yet
                auto tx = pay(alice, bob, XRP(5002));
                tx[jss::NotValidBefore] = nettime(env) + 10;
                env(tx, ter(tefTOO_EARLY));
                env.close(std::chrono::seconds{60});
            }

            {  // Too late: the transaction can't execute anymore
                auto tx = pay(alice, bob, XRP(5003));
                tx[jss::NotValidAfter] = nettime(env) - 10;
                env(tx, ter(tefTOO_LATE));
                env.close(std::chrono::seconds{60});
            }

            { // Executes within validity period
                auto tx = pay(alice, bob, XRP(5004));
                tx[jss::NotValidBefore] = nettime(env) - 10;
                tx[jss::NotValidAfter] = nettime(env) + 10;
                env(tx, ter(tesSUCCESS));
                env.close();
            }
        }
    }
};

BEAST_DEFINE_TESTSUITE(TemporalTxValidity, app, ripple);

}  // namespace test
}  // namespace ripple
