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

#include <xrpld/ledger/Dir.h>

#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Firewall.h>
#include <xrpl/protocol/jss.h>

namespace ripple {
namespace test {
struct Firewall_test : public beast::unit_test::suite
{
    static std::size_t
    ownerDirCount(ReadView const& view, jtx::Account const& acct)
    {
        ripple::Dir const ownerDir(view, keylet::ownerDir(acct.id()));
        return std::distance(ownerDir.begin(), ownerDir.end());
    };

    static std::pair<uint256, std::shared_ptr<SLE const>>
    firewallKeyAndSle(ReadView const& view, jtx::Account const& account)
    {
        auto const k = keylet::firewall(account);
        return {k.key, view.read(k)};
    }

    void
    verifyFirewallSle(
        ReadView const& view,
        jtx::Account const& account,
        jtx::Account const& issuer,
        std::optional<STAmount> const& amount = std::nullopt,
        std::optional<uint32_t> const& timePeriod = std::nullopt,
        std::optional<uint32_t> const& timeStart = std::nullopt,
        std::optional<STAmount> const& totalOut = std::nullopt)
    {
        auto [key, sle] = firewallKeyAndSle(view, account);
        BEAST_EXPECT((*sle)[sfOwner] == account.id());
        BEAST_EXPECT((*sle)[sfIssuer] == issuer.id());
        if (amount)
        {
            std::cout << "amount: " << *amount << std::endl;
            BEAST_EXPECT((*sle)[sfAmount] == *amount);
        }
        if (timePeriod)
        {
            std::cout << "timePeriod: " << *timePeriod << std::endl;
            BEAST_EXPECT((*sle)[sfTimePeriod] == *timePeriod);
        }
        if (timeStart)
        {
            std::cout << "timeStart: " << *timeStart << std::endl;
            BEAST_EXPECT((*sle)[sfTimeStart] == *timeStart);
        }
        // if (totalOut)
        // {
        //     std::cout << "totalOut: " << *totalOut << std::endl;
        //     BEAST_EXPECT((*sle)[sfTimeValue] == *totalOut);
        // }
    }

    void
    testFirewallSetPreflightCreate(FeatureBitset features)
    {
        testcase("firewall set preflight create");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        Account const alice = Account("alice");
        Account const bob = Account("bob");
        Account const carol = Account("carol");

        // temDISABLED: Amendment not enabled
        {
            auto const amend = features - featureFirewall;
            Env env{*this, amend};
            env.fund(XRP(1000), alice, bob, carol);
            env.close();

            env(firewall::set(alice),
                firewall::backup(bob),
                firewall::counter_party(carol),
                ter(temDISABLED));
        }

        // Basic Preflight1 Checks
        {
            Env env{*this, features};
            env.fund(XRP(1000), alice, bob, carol);
            env.close();

            // temINVALID_FLAG: Invalid flags set
            auto jt = firewall::set(alice);
            env(jt,
                firewall::backup(bob),
                firewall::counter_party(carol),
                txflags(tfClose),
                ter(temINVALID_FLAG));

            // temBAD_FEE: Invalid fee amount (test with 0 fee)
            env(firewall::set(alice),
                fee(XRP(-1)),
                firewall::backup(bob),
                firewall::counter_party(carol),
                ter(temBAD_FEE));
        }

        // CREATE - Required Fields
        {
            Env env{*this, features};
            env.fund(XRP(1000), alice, bob, carol);
            env.close();

            // temMALFORMED: FirewallSet: sfCounterParty is required for
            // creation
            env(firewall::set(alice), firewall::backup(bob), ter(temMALFORMED));

            // temMALFORMED: FirewallSet: sfBackup is required for creation
            env(firewall::set(alice),
                firewall::counter_party(carol),
                ter(temMALFORMED));
        }

        // CREATE - Forbidden Fields
        {
            Env env{*this, features};
            env.fund(XRP(1000), alice, bob, carol);
            env.close();

            // temMALFORMED: FirewallSet: sfFirewallSigners not allowed for
            // creation
            env(firewall::set(alice),
                firewall::counter_party(carol),
                firewall::backup(bob),
                firewall::sig(alice),
                ter(temMALFORMED));
        }

        // CREATE - FirewallRules Validation (Data Validation)
        {
            Env env{*this, features};
            env.fund(XRP(1000), alice, bob, carol);
            env.close();

            // temMALFORMED: FirewallSet: sfFirewallRules must not be empty if
            // present
            auto jt = firewall::set(alice);
            jt[jss::FirewallRules] = Json::arrayValue;
            env(jt,
                firewall::backup(bob),
                firewall::counter_party(carol),
                ter(temMALFORMED));

            // // temMALFORMED: FirewallSet: sfFirewallRules must not be larger
            // than 8
            env(firewall::set(alice),
                firewall::backup(bob),
                firewall::counter_party(carol),
                firewall::rule(
                    ltACCOUNT_ROOT,
                    sfBalance,
                    FirewallOperator::GREATER_THAN_EQUAL,
                    XRP(1).value().getJson(JsonOptions::none)),
                firewall::rule(
                    ltACCOUNT_ROOT,
                    sfBalance,
                    FirewallOperator::LESS_THAN,
                    XRP(2).value().getJson(JsonOptions::none)),
                firewall::rule(
                    ltACCOUNT_ROOT,
                    sfBalance,
                    FirewallOperator::EQUAL,
                    XRP(3).value().getJson(JsonOptions::none)),
                firewall::rule(
                    ltACCOUNT_ROOT,
                    sfBalance,
                    FirewallOperator::GREATER_THAN,
                    XRP(4).value().getJson(JsonOptions::none)),
                firewall::rule(
                    ltACCOUNT_ROOT,
                    sfBalance,
                    FirewallOperator::LESS_THAN_EQUAL,
                    XRP(5).value().getJson(JsonOptions::none)),
                firewall::rule(
                    ltACCOUNT_ROOT,
                    sfBalance,
                    FirewallOperator::EQUAL,
                    XRP(6).value().getJson(JsonOptions::none)),
                firewall::rule(
                    ltACCOUNT_ROOT,
                    sfBalance,
                    FirewallOperator::GREATER_THAN_EQUAL,
                    XRP(7).value().getJson(JsonOptions::none)),
                firewall::rule(
                    ltACCOUNT_ROOT,
                    sfBalance,
                    FirewallOperator::LESS_THAN,
                    XRP(8).value().getJson(JsonOptions::none)),
                firewall::rule(
                    ltACCOUNT_ROOT,
                    sfBalance,
                    FirewallOperator::LESS_THAN,
                    XRP(9).value().getJson(JsonOptions::none)),
                ter(temMALFORMED));
        }

        // CREATE - Ledger Entry Type and Field Code Validation
        {
            Env env{*this, features};
            env.fund(XRP(1000), alice, bob, carol);
            env.close();

            // temMALFORMED: FirewallSet: sfLedgerEntryType X is not supported
            env(firewall::set(alice),
                firewall::backup(bob),
                firewall::counter_party(carol),
                firewall::rule(
                    ltRIPPLE_STATE,
                    sfBalance,
                    FirewallOperator::GREATER_THAN_EQUAL,
                    XRP(1).value().getJson(JsonOptions::none)),
                ter(temMALFORMED));

            // temMALFORMED: FirewallSet: sfFieldCode X is not supported
            env(firewall::set(alice),
                firewall::backup(bob),
                firewall::counter_party(carol),
                firewall::rule(
                    ltACCOUNT_ROOT,
                    sfSequence,
                    FirewallOperator::GREATER_THAN_EQUAL,
                    XRP(1).value().getJson(JsonOptions::none)),
                ter(temMALFORMED));
        }

        // CREATE - Comparison Operator Validation
        {
            Env env{*this, features};
            env.fund(XRP(1000), alice, bob, carol);
            env.close();

            // temMALFORMED: FirewallSet: sfComparisonOperator X is not
            // supported (< 1)
            auto jt = firewall::set(alice);
            jt[jss::FirewallRules][0u][jss::FirewallRule]
              [sfLedgerEntryType.jsonName] = ltACCOUNT_ROOT;
            jt[jss::FirewallRules][0u][jss::FirewallRule]
              [sfFieldCode.jsonName] = sfBalance.fieldCode;
            jt[jss::FirewallRules][0u][jss::FirewallRule]
              [sfComparisonOperator.jsonName] = 0;
            jt[jss::FirewallRules][0u][jss::FirewallRule][sfFirewallValue]
              [jss::type] = "AMOUNT";
            jt[jss::FirewallRules][0u][jss::FirewallRule][sfFirewallValue]
              [jss::value] = XRP(10).value().getJson(JsonOptions::none);
            env(jt,
                firewall::backup(bob),
                firewall::counter_party(carol),
                ter(temMALFORMED));

            // temMALFORMED: FirewallSet: sfComparisonOperator X is not
            // supported (> 5)
            jt = firewall::set(alice);
            jt[jss::FirewallRules][0u][jss::FirewallRule]
              [sfLedgerEntryType.jsonName] = ltACCOUNT_ROOT;
            jt[jss::FirewallRules][0u][jss::FirewallRule]
              [sfFieldCode.jsonName] = sfBalance.fieldCode;
            jt[jss::FirewallRules][0u][jss::FirewallRule]
              [sfComparisonOperator.jsonName] = 6;
            jt[jss::FirewallRules][0u][jss::FirewallRule][sfFirewallValue]
              [jss::type] = "AMOUNT";
            jt[jss::FirewallRules][0u][jss::FirewallRule][sfFirewallValue]
              [jss::value] = XRP(10).value().getJson(JsonOptions::none);
            env(jt,
                firewall::backup(bob),
                firewall::counter_party(carol),
                ter(temMALFORMED));
        }

        // // CREATE - STData Validation in Rules
        // {
        //     Env env{*this, features};
        //     env.fund(XRP(1000), alice, bob, carol);
        //     env.close();

        //     // temMALFORMED: Invalid STData type for field (wrong type for
        //     balance field) auto jt = firewall::set(alice);
        //     jt[jss::FirewallRules][0u][jss::FirewallRule][sfLedgerEntryType.jsonName]
        //     = ltACCOUNT_ROOT;
        //     jt[jss::FirewallRules][0u][jss::FirewallRule][sfFieldCode.jsonName]
        //     = sfBalance.fieldCode;
        //     jt[jss::FirewallRules][0u][jss::FirewallRule][sfComparisonOperator.jsonName]
        //     = 4;
        //     jt[jss::FirewallRules][0u][jss::FirewallRule][sfFirewallValue][jss::type]
        //     = "UINT32";
        //     jt[jss::FirewallRules][0u][jss::FirewallRule][sfFirewallValue][jss::value]
        //     = 1000; env(jt, firewall::backup(bob),
        //     firewall::counter_party(carol), ter(temMALFORMED));

        //     // temMALFORMED: Malformed JSON in firewall value
        //     jt = firewall::set(alice);
        //     jt[jss::FirewallRules][0u][jss::FirewallRule][sfLedgerEntryType.jsonName]
        //     = ltACCOUNT_ROOT;
        //     jt[jss::FirewallRules][0u][jss::FirewallRule][sfFieldCode.jsonName]
        //     = sfBalance.fieldCode;
        //     jt[jss::FirewallRules][0u][jss::FirewallRule][sfComparisonOperator.jsonName]
        //     = 4;
        //     jt[jss::FirewallRules][0u][jss::FirewallRule][sfFirewallValue] =
        //     "invalid_json"; env(jt, firewall::backup(bob),
        //     firewall::counter_party(carol), ter(temMALFORMED));
        // }

        // CREATE - Edge Cases
        {
            Env env{*this, features};
            env.fund(XRP(1000), alice, bob, carol);
            env.close();

            // // Test with exactly 8 rules (maximum allowed) - should succeed
            // auto jt = firewall::set(alice);
            // for (int i = 0; i < 8; ++i)
            // {
            //     jt[jss::FirewallRules][i][jss::FirewallRule]
            //       [sfLedgerEntryType.jsonName] = ltACCOUNT_ROOT;
            //     jt[jss::FirewallRules][i][jss::FirewallRule]
            //       [sfFieldCode.jsonName] = sfBalance.fieldCode;
            //     jt[jss::FirewallRules][i][jss::FirewallRule]
            //       [sfComparisonOperator.jsonName] = 4;
            //     jt[jss::FirewallRules][i][jss::FirewallRule][sfFirewallValue]
            //       [jss::type] = "AMOUNT";
            //     jt[jss::FirewallRules][i][jss::FirewallRule][sfFirewallValue]
            //       [jss::value] = XRP(10 +
            //       i).value().getJson(JsonOptions::none);
            // }
            // env(jt,
            //     firewall::backup(bob),
            //     firewall::counter_party(carol),
            //     ter(tesSUCCESS));

            // Test with all comparison operators (1-5)
            // for (int op = 1; op <= 5; ++op)
            // {
            //     jt = firewall::set(alice);
            //     jt[jss::FirewallRules][0u][jss::FirewallRule]
            //       [sfLedgerEntryType.jsonName] = ltACCOUNT_ROOT;
            //     jt[jss::FirewallRules][0u][jss::FirewallRule]
            //       [sfFieldCode.jsonName] = sfBalance.fieldCode;
            //     jt[jss::FirewallRules][0u][jss::FirewallRule]
            //       [sfComparisonOperator.jsonName] = op;
            //     jt[jss::FirewallRules][0u][jss::FirewallRule][sfFirewallValue]
            //       [jss::type] = "AMOUNT";
            //     jt[jss::FirewallRules][0u][jss::FirewallRule][sfFirewallValue]
            //       [jss::value] = XRP(10).value().getJson(JsonOptions::none);
            //     env(jt,
            //         firewall::backup(bob),
            //         firewall::counter_party(carol),
            //         ter(tesSUCCESS));
            // }

            // Test with boundary values (0 amount)
            // jt = firewall::set(alice);
            // jt[jss::FirewallRules][0u][jss::FirewallRule]
            //   [sfLedgerEntryType.jsonName] = ltACCOUNT_ROOT;
            // jt[jss::FirewallRules][0u][jss::FirewallRule]
            //   [sfFieldCode.jsonName] = sfBalance.fieldCode;
            // jt[jss::FirewallRules][0u][jss::FirewallRule]
            //   [sfComparisonOperator.jsonName] = 4;
            // jt[jss::FirewallRules][0u][jss::FirewallRule][sfFirewallValue]
            //   [jss::type] = "AMOUNT";
            // jt[jss::FirewallRules][0u][jss::FirewallRule][sfFirewallValue]
            //   [jss::value] = XRP(0).value().getJson(JsonOptions::none);
            // env(jt,
            //     firewall::backup(bob),
            //     firewall::counter_party(carol),
            //     ter(tesSUCCESS));

            // // Test with maximum XRP amount
            // jt = firewall::set(alice);
            // jt[jss::FirewallRules][0u][jss::FirewallRule]
            //   [sfLedgerEntryType.jsonName] = ltACCOUNT_ROOT;
            // jt[jss::FirewallRules][0u][jss::FirewallRule]
            //   [sfFieldCode.jsonName] = sfBalance.fieldCode;
            // jt[jss::FirewallRules][0u][jss::FirewallRule]
            //   [sfComparisonOperator.jsonName] = 4;
            // jt[jss::FirewallRules][0u][jss::FirewallRule][sfFirewallValue]
            //   [jss::type] = "AMOUNT";
            // jt[jss::FirewallRules][0u][jss::FirewallRule][sfFirewallValue]
            //   [jss::value] =
            //       STAmount(STAmount::cMaxNative).getJson(JsonOptions::none);
            // env(jt,
            //     firewall::backup(bob),
            //     firewall::counter_party(carol),
            //     ter(tesSUCCESS));
        }

        // tesSUCCESS: Valid create with CounterParty and Backup only
        {
            Env env{*this, features};
            env.fund(XRP(1000), alice, bob, carol);
            env.close();

            env(firewall::set(alice),
                firewall::backup(bob),
                firewall::counter_party(carol),
                ter(tesSUCCESS));
            env.close();
        }

        // tesSUCCESS: Valid create with CounterParty, Backup, and 1 rule
        {
            Env env{*this, features};
            env.fund(XRP(1000), alice, bob, carol);
            env.close();

            env(firewall::set(alice),
                firewall::backup(bob),
                firewall::counter_party(carol),
                firewall::rule(
                    ltACCOUNT_ROOT,
                    sfBalance,
                    FirewallOperator::GREATER_THAN_EQUAL,
                    XRP(10).value().getJson(JsonOptions::none)),
                ter(tesSUCCESS));
            env.close();
        }

        // tesSUCCESS: Valid create with time period rule
        {
            Env env{*this, features};
            env.fund(XRP(1000), alice, bob, carol);
            env.close();

            env(firewall::set(alice),
                firewall::backup(bob),
                firewall::counter_party(carol),
                firewall::rule(
                    ltACCOUNT_ROOT,
                    sfBalance,
                    FirewallOperator::LESS_THAN,
                    XRP(100).value().getJson(JsonOptions::none),
                    3600),  // 1 hour time period
                ter(tesSUCCESS));
            env.close();
        }
    }

    void
    testFirewallSetPreflightUpdate(FeatureBitset features)
    {
        testcase("firewall set preflight update");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        Account const alice = Account("alice");
        Account const bob = Account("bob");
        Account const carol = Account("carol");
        Account const dave = Account("dave");

        // UPDATE - Required Fields
        {
            Env env{*this, features};
            env.fund(XRP(1000), alice, bob, carol, dave);
            env.close();

            // Create a firewall first
            env(firewall::set(alice),
                firewall::backup(bob),
                firewall::counter_party(carol),
                ter(tesSUCCESS));
            env.close();

            auto [firewallKey, firewallSle] =
                firewallKeyAndSle(*env.current(), alice);

            // temMALFORMED: FirewallSet: sfFirewallSigners required for updates
            auto const seq = env.seq(alice);
            auto const fee = firewall::calcFirewallFee(env, 1);
            auto jt = firewall::set(alice, firewallKey, seq, fee);
            env(jt, ter(temMALFORMED));

            // temMALFORMED: FirewallSet: sfFirewallID required for updates
            // (when missing)
            env(firewall::set(alice), firewall::sig(carol), ter(temMALFORMED));
        }

        // UPDATE - Forbidden Fields
        {
            Env env{*this, features};
            env.fund(XRP(1000), alice, bob, carol, dave);
            env.close();

            // Create a firewall first
            env(firewall::set(alice),
                firewall::backup(bob),
                firewall::counter_party(carol),
                ter(tesSUCCESS));
            env.close();

            auto [firewallKey, firewallSle] =
                firewallKeyAndSle(*env.current(), alice);

            // temMALFORMED: FirewallSet: sfBackup not allowed for updates
            auto const seq = env.seq(alice);
            auto const fee = firewall::calcFirewallFee(env, 1);
            auto jt = firewall::set(alice, firewallKey, seq, fee);
            env(jt,
                firewall::backup(dave),
                firewall::sig(carol),
                ter(temMALFORMED));
        }

        // UPDATE - FirewallSigners Validation
        {
            Env env{*this, features};
            env.fund(XRP(1000), alice, bob, carol, dave);
            env.close();

            // Create a firewall first
            env(firewall::set(alice),
                firewall::backup(bob),
                firewall::counter_party(carol),
                ter(tesSUCCESS));
            env.close();

            auto [firewallKey, firewallSle] =
                firewallKeyAndSle(*env.current(), alice);
            auto const seq = env.seq(alice);
            auto const fee = firewall::calcFirewallFee(env, 1);

            // temMALFORMED: FirewallSet: sfFirewallSigners cannot be empty
            auto jt = firewall::set(alice, firewallKey, seq, fee);
            jt[sfFirewallSigners] = Json::arrayValue;  // Empty array
            env(jt, ter(temMALFORMED));

            // temMALFORMED: FirewallSet: sfFirewallSigners cannot include the
            // outer account
            jt = firewall::set(alice, firewallKey, seq, fee);
            env(jt, firewall::sig(alice), ter(temMALFORMED));

            // temBAD_SIGNATURE: FirewallSet: invalid firewall signature
            jt = firewall::set(alice, firewallKey, seq, fee);
            jt[sfFirewallSigners][0u][sfFirewallSigner][jss::Account] =
                carol.human();
            jt[sfFirewallSigners][0u][sfFirewallSigner][jss::SigningPubKey] =
                strHex(carol.pk().slice());
            jt[sfFirewallSigners][0u][sfFirewallSigner][jss::TxnSignature] =
                "deadbeef";
            env(jt, ter(temBAD_SIGNATURE));
        }

        // tesSUCCESS: Valid update with proper single signature
        {
            Env env{*this, features};
            env.fund(XRP(1000), alice, bob, carol, dave);
            env.close();

            // Create a firewall first
            env(firewall::set(alice),
                firewall::backup(bob),
                firewall::counter_party(carol),
                ter(tesSUCCESS));
            env.close();

            auto [firewallKey, firewallSle] =
                firewallKeyAndSle(*env.current(), alice);

            auto const seq = env.seq(alice);
            auto const fee = firewall::calcFirewallFee(env, 1);
            auto jt = firewall::set(alice, firewallKey, seq, fee);
            env(jt, firewall::sig(carol), ter(tesSUCCESS));
            env.close();
        }

        // tesSUCCESS: Valid update with new CounterParty
        {
            Env env{*this, features};
            env.fund(XRP(1000), alice, bob, carol, dave);
            env.close();

            // Create a firewall first
            env(firewall::set(alice),
                firewall::backup(bob),
                firewall::counter_party(carol),
                ter(tesSUCCESS));
            env.close();

            auto [firewallKey, firewallSle] =
                firewallKeyAndSle(*env.current(), alice);

            auto const seq = env.seq(alice);
            auto const fee = firewall::calcFirewallFee(env, 1);
            auto jt = firewall::set(alice, firewallKey, seq, fee);
            jt[sfCounterParty.jsonName] = dave.human();
            env(jt, firewall::sig(carol), ter(tesSUCCESS));
        }

        // tesSUCCESS: Valid update with new FirewallRules
        {
            Env env{*this, features};
            env.fund(XRP(1000), alice, bob, carol, dave);
            env.close();

            // Create a firewall first
            env(firewall::set(alice),
                firewall::backup(bob),
                firewall::counter_party(carol),
                ter(tesSUCCESS));
            env.close();

            auto [firewallKey, firewallSle] =
                firewallKeyAndSle(*env.current(), alice);

            auto const seq = env.seq(alice);
            auto const fee = firewall::calcFirewallFee(env, 1);
            auto jt = firewall::set(alice, firewallKey, seq, fee);
            env(jt,
                firewall::rule(
                    ltACCOUNT_ROOT,
                    sfBalance,
                    FirewallOperator::GREATER_THAN,
                    XRP(5).value().getJson(JsonOptions::none)),
                firewall::sig(carol),
                ter(tesSUCCESS));
        }

        // tesSUCCESS: Valid update with both CounterParty and FirewallRules
        {
            Env env{*this, features};
            env.fund(XRP(1000), alice, bob, carol, dave);
            env.close();

            // Create a firewall first
            env(firewall::set(alice),
                firewall::backup(bob),
                firewall::counter_party(carol),
                ter(tesSUCCESS));
            env.close();

            auto [firewallKey, firewallSle] =
                firewallKeyAndSle(*env.current(), alice);

            auto const seq = env.seq(alice);
            auto const fee = firewall::calcFirewallFee(env, 1);
            auto jt = firewall::set(alice, firewallKey, seq, fee);
            jt[sfCounterParty.jsonName] = dave.human();
            env(jt,
                firewall::rule(
                    ltACCOUNT_ROOT,
                    sfBalance,
                    FirewallOperator::LESS_THAN,
                    XRP(200).value().getJson(JsonOptions::none)),
                firewall::sig(carol),
                ter(tesSUCCESS));
        }
    }

    void
    testFirewallSetPreclaimCreate(FeatureBitset features)
    {
        testcase("firewall set preclaim create");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        Account const alice = Account("alice");
        Account const bob = Account("bob");
        Account const carol = Account("carol");
        Account const dave = Account("dave");

        // CREATE - Duplicate Check
        {
            Env env{*this, features};
            env.fund(XRP(1000), alice, bob, carol);
            env.close();

            // Create a firewall first
            env(firewall::set(alice),
                firewall::backup(bob),
                firewall::counter_party(carol),
                ter(tesSUCCESS));
            env.close();

            // tecDUPLICATE: FirewallSet: Firewall already exists for account
            env(firewall::set(alice),
                firewall::backup(bob),
                firewall::counter_party(carol),
                ter(tecDUPLICATE));
        }

        // CREATE - CounterParty Account Existence
        {
            Env env{*this, features};
            env.fund(XRP(1000), alice, bob);
            env.close();
            env.memoize(carol);

            // tecNO_DST: FirewallSet: CounterParty account does not exist
            env(firewall::set(alice),
                firewall::backup(bob),
                firewall::counter_party(carol),  // carol was never funded
                ter(tecNO_DST));
        }

        // CREATE - Backup Account Existence
        {
            Env env{*this, features};
            env.fund(XRP(1000), alice, carol);
            env.close();
            env.memoize(bob);

            // tecNO_DST: FirewallSet: Backup account does not exist
            env(firewall::set(alice),
                firewall::backup(bob),  // bob was never funded
                firewall::counter_party(carol),
                ter(tecNO_DST));
        }

        // // CREATE - Reserve Requirements
        // {
        //     Env env{*this, features};
        //     env.fund(XRP(1000), alice, bob, carol);
        //     env.close();

        //     // Drain alice's balance to near reserve
        //     auto const reserve = env.current()->fees().accountReserve(0);
        //     auto const baseFee = env.current()->fees().base;
        //     auto const firewallReserve =
        //     env.current()->fees().accountReserve(2); // +2 for firewall +
        //     preauth

        //     // Leave just enough for current reserve but not enough for
        //     firewall + preauth env(pay(alice, bob,
        //         alice.balance(*env.current()) - reserve - baseFee));
        //     env.close();

        //     // tecINSUFFICIENT_RESERVE: Insufficient reserve for Firewall +
        //     WithdrawPreauth env(firewall::set(alice),
        //         firewall::backup(bob),
        //         firewall::counter_party(carol),
        //         ter(tecINSUFFICIENT_RESERVE));
        // }

        // CREATE - Valid cases with rules
        {
            Env env{*this, features};
            env.fund(XRP(1000), alice, bob, carol);
            env.close();

            // tesSUCCESS: Valid preclaim with firewall rules
            env(firewall::set(alice),
                firewall::backup(bob),
                firewall::counter_party(carol),
                firewall::rule(
                    ltACCOUNT_ROOT,
                    sfBalance,
                    FirewallOperator::GREATER_THAN_EQUAL,
                    XRP(10).value().getJson(JsonOptions::none)),
                ter(tesSUCCESS));
        }
    }

    void
    testFirewallSetPreclaimUpdate(FeatureBitset features)
    {
        testcase("firewall set preclaim update");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        Account const alice = Account("alice");
        Account const bob = Account("bob");
        Account const carol = Account("carol");
        Account const dave = Account("dave");
        Account const eve = Account("eve");

        // UPDATE - Firewall Existence
        {
            Env env{*this, features};
            env.fund(XRP(1000), alice, bob, carol, dave);
            env.close();

            // tecNO_TARGET: FirewallSet: Firewall not found
            auto const seq = env.seq(alice);
            auto const fee = firewall::calcFirewallFee(env, 1);
            auto jt = firewall::set(
                alice, uint256{1}, seq, fee);  // Non-existent firewall ID
            env(jt, firewall::sig(carol), ter(tecNO_TARGET));
        }

        // UPDATE - Permission Check
        {
            Env env{*this, features};
            env.fund(XRP(1000), alice, bob, carol, dave);
            env.close();

            // Create a firewall for alice
            env(firewall::set(alice),
                firewall::backup(bob),
                firewall::counter_party(carol),
                ter(tesSUCCESS));
            env.close();

            auto [firewallKey, firewallSle] =
                firewallKeyAndSle(*env.current(), alice);

            // tecNO_PERMISSION: FirewallSet: Account is not the firewall owner
            auto const seq = env.seq(dave);
            auto const fee = firewall::calcFirewallFee(env, 1);
            auto jt = firewall::set(
                dave,
                firewallKey,
                seq,
                fee);  // dave trying to update alice's firewall
            env(jt, firewall::sig(carol), ter(tecNO_PERMISSION));
        }

        // UPDATE - New CounterParty Validation - Same as existing
        {
            Env env{*this, features};
            env.fund(XRP(1000), alice, bob, carol, dave);
            env.close();

            // Create a firewall for alice
            env(firewall::set(alice),
                firewall::backup(bob),
                firewall::counter_party(carol),
                ter(tesSUCCESS));
            env.close();

            auto [firewallKey, firewallSle] =
                firewallKeyAndSle(*env.current(), alice);

            // tecDUPLICATE: FirewallSet: sfCounterParty must not be the same as
            // existing CounterParty
            auto const seq = env.seq(alice);
            auto const fee = firewall::calcFirewallFee(env, 1);
            auto jt = firewall::set(alice, firewallKey, seq, fee);
            jt[sfCounterParty.jsonName] = carol.human();  // Same as existing
            env(jt, firewall::sig(carol), ter(tecDUPLICATE));
        }

        // UPDATE - New CounterParty Validation - Account does not exist
        {
            Env env{*this, features};
            env.fund(XRP(1000), alice, bob, carol, dave);
            env.close();

            // Create a firewall for alice
            env(firewall::set(alice),
                firewall::backup(bob),
                firewall::counter_party(carol),
                ter(tesSUCCESS));
            env.close();

            auto [firewallKey, firewallSle] =
                firewallKeyAndSle(*env.current(), alice);

            // tecNO_DST: FirewallSet: New CounterParty account does not exist
            auto const seq = env.seq(alice);
            auto const fee = firewall::calcFirewallFee(env, 1);
            auto jt = firewall::set(alice, firewallKey, seq, fee);
            jt[sfCounterParty.jsonName] = eve.human();  // eve was never funded
            env(jt, firewall::sig(carol), ter(tecNO_DST));
        }

        // UPDATE - Valid cases - Update CounterParty only
        {
            Env env{*this, features};
            env.fund(XRP(1000), alice, bob, carol, dave);
            env.close();

            // Create a firewall for alice
            env(firewall::set(alice),
                firewall::backup(bob),
                firewall::counter_party(carol),
                ter(tesSUCCESS));
            env.close();

            auto [firewallKey, firewallSle] =
                firewallKeyAndSle(*env.current(), alice);

            // tesSUCCESS: Valid update changing CounterParty
            auto const seq = env.seq(alice);
            auto const fee = firewall::calcFirewallFee(env, 1);
            auto jt = firewall::set(alice, firewallKey, seq, fee);
            jt[sfCounterParty.jsonName] =
                dave.human();  // Different from existing carol
            env(jt, firewall::sig(carol), ter(tesSUCCESS));
        }

        // UPDATE - Valid cases - Update rules only
        {
            Env env{*this, features};
            env.fund(XRP(1000), alice, bob, carol, dave);
            env.close();

            // Create a firewall for alice
            env(firewall::set(alice),
                firewall::backup(bob),
                firewall::counter_party(carol),
                ter(tesSUCCESS));
            env.close();

            auto [firewallKey, firewallSle] =
                firewallKeyAndSle(*env.current(), alice);

            // tesSUCCESS: Valid update changing only rules
            auto const seq = env.seq(alice);
            auto const fee = firewall::calcFirewallFee(env, 1);
            auto jt = firewall::set(alice, firewallKey, seq, fee);
            env(jt,
                firewall::rule(
                    ltACCOUNT_ROOT,
                    sfBalance,
                    FirewallOperator::GREATER_THAN,
                    XRP(50).value().getJson(JsonOptions::none)),
                firewall::sig(carol),
                ter(tesSUCCESS));
        }

        // UPDATE - Valid cases - Update both CounterParty and rules
        {
            Env env{*this, features};
            env.fund(XRP(1000), alice, bob, carol, dave);
            env.close();

            // Create a firewall for alice
            env(firewall::set(alice),
                firewall::backup(bob),
                firewall::counter_party(carol),
                ter(tesSUCCESS));
            env.close();

            auto [firewallKey, firewallSle] =
                firewallKeyAndSle(*env.current(), alice);

            // tesSUCCESS: Valid update changing both CounterParty and rules
            auto const seq = env.seq(alice);
            auto const fee = firewall::calcFirewallFee(env, 1);
            auto jt = firewall::set(alice, firewallKey, seq, fee);
            jt[sfCounterParty.jsonName] = dave.human();
            env(jt,
                firewall::rule(
                    ltACCOUNT_ROOT,
                    sfBalance,
                    FirewallOperator::LESS_THAN,
                    XRP(100).value().getJson(JsonOptions::none)),
                firewall::sig(carol),  // Current counterparty signs
                ter(tesSUCCESS));
        }

        // UPDATE - Valid cases - No changes (update with no new fields)
        {
            Env env{*this, features};
            env.fund(XRP(1000), alice, bob, carol, dave);
            env.close();

            // Create a firewall for alice
            env(firewall::set(alice),
                firewall::backup(bob),
                firewall::counter_party(carol),
                ter(tesSUCCESS));
            env.close();

            auto [firewallKey, firewallSle] =
                firewallKeyAndSle(*env.current(), alice);

            // tesSUCCESS: Valid update with no changes (just signature)
            auto const seq = env.seq(alice);
            auto const fee = firewall::calcFirewallFee(env, 1);
            auto jt = firewall::set(alice, firewallKey, seq, fee);
            env(jt, firewall::sig(carol), ter(tesSUCCESS));
        }
    }

    void
    testWithFeats(FeatureBitset features)
    {
        testFirewallSetPreflightCreate(features);
        testFirewallSetPreflightUpdate(features);
        testFirewallSetPreclaimCreate(features);
        testFirewallSetPreclaimUpdate(features);
    }

public:
    void
    run() override
    {
        using namespace test::jtx;
        FeatureBitset const all{testable_amendments()};
        testWithFeats(all);
    }
};

BEAST_DEFINE_TESTSUITE(Firewall, app, ripple);
}  // namespace test
}  // namespace ripple
