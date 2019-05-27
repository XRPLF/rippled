//------------------------------------------------------------------------------
/*
  This file is part of rippled: https://github.com/ripple/rippled
  Copyright (c) 2012-2016 Ripple Labs Inc.

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
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/jss.h>

namespace ripple {

class Ticket_test : public beast::unit_test::suite
{
    static auto constexpr idOne =
      "00000000000000000000000000000000"
      "00000000000000000000000000000001";

    /// @brief validate metadata for a create/cancel ticket transaction and
    /// return the 3 or 4 nodes that make-up the metadata (AffectedNodes)
    ///
    /// @param env current jtx env (meta will be extracted from it)
    ///
    /// @param other_target flag to indicate whether a Target different
    /// from the Account was specified for the ticket (when created)
    ///
    /// @param expiration flag to indicate a cancellation with expiration which
    /// causes two of the affected nodes to be swapped (in order).
    ///
    /// @retval std::array size 4 of json object values representing
    /// each meta node entry. When the transaction was a cancel with differing
    /// target and account, there will be 4 complete items, otherwise the last
    /// entry will be an empty object
    auto
    checkTicketMeta(
        test::jtx::Env& env,
        bool other_target = false,
        bool expiration = false)
    {
        using namespace std::string_literals;
        auto const& tx = env.tx ()->getJson (JsonOption::none);
        bool is_cancel = tx[jss::TransactionType] == "TicketCancel";

        auto const& jvm = env.meta ()->getJson (JsonOption::none);
        std::array<Json::Value, 4> retval;

        // these are the affected nodes that we expect for
        // a few different scenarios.
        // tuple is index, field name, and label (LedgerEntryType)
        std::vector<
            std::tuple<std::size_t, std::string, std::string>
        > expected_nodes;

        if (is_cancel && other_target)
        {
            expected_nodes = {
                std::make_tuple(0, sfModifiedNode.fieldName, "AccountRoot"s),
                std::make_tuple(
                    expiration ? 2: 1, sfModifiedNode.fieldName, "AccountRoot"s),
                std::make_tuple(
                    expiration ? 1: 2, sfDeletedNode.fieldName, "Ticket"s),
                std::make_tuple(3, sfDeletedNode.fieldName, "DirectoryNode"s)
            };
        }
        else
        {
            expected_nodes = {
                std::make_tuple(0, sfModifiedNode.fieldName, "AccountRoot"s),
                std::make_tuple(1,
                    is_cancel ?
                        sfDeletedNode.fieldName : sfCreatedNode.fieldName,
                    "Ticket"s),
                std::make_tuple(2,
                    is_cancel ?
                        sfDeletedNode.fieldName : sfCreatedNode.fieldName,
                 "DirectoryNode"s)
            };
        }

        BEAST_EXPECT(jvm.isMember (sfAffectedNodes.fieldName));
        BEAST_EXPECT(jvm[sfAffectedNodes.fieldName].isArray());
        BEAST_EXPECT(
            jvm[sfAffectedNodes.fieldName].size() == expected_nodes.size());

        // verify the actual metadata against the expected
        for (auto const& it : expected_nodes)
        {
            auto const& idx = std::get<0>(it);
            auto const& field = std::get<1>(it);
            auto const& type = std::get<2>(it);
            BEAST_EXPECT(jvm[sfAffectedNodes.fieldName][idx].isMember(field));
            retval[idx] = jvm[sfAffectedNodes.fieldName][idx][field];
            BEAST_EXPECT(retval[idx][sfLedgerEntryType.fieldName] == type);
        }

        return retval;
    }

    void testTicketNotEnabled ()
    {
        testcase ("Feature Not Enabled");

        using namespace test::jtx;
        Env env {*this, FeatureBitset{}};

        env (ticket::create (env.master), ter(temDISABLED));
        env (ticket::cancel (env.master, idOne), ter (temDISABLED));
    }

    void testTicketCancelNonexistent ()
    {
        testcase ("Cancel Nonexistent");

        using namespace test::jtx;
        Env env {*this, supported_amendments().set(featureTickets)};
        env (ticket::cancel (env.master, idOne), ter (tecNO_ENTRY));
    }

    void testTicketCreatePreflightFail ()
    {
        testcase ("Create/Cancel Ticket with Bad Fee, Fail Preflight");

        using namespace test::jtx;
        Env env {*this, supported_amendments().set(featureTickets)};

        env (ticket::create (env.master), fee (XRP (-1)), ter (temBAD_FEE));
        env (ticket::cancel (env.master, idOne), fee (XRP (-1)), ter (temBAD_FEE));
    }

    void testTicketCreateNonexistent ()
    {
        testcase ("Create Tickets with Nonexistent Accounts");

        using namespace test::jtx;
        Env env {*this, supported_amendments().set(featureTickets)};
        Account alice {"alice"};
        env.memoize (alice);

        env (ticket::create (env.master, alice), ter(tecNO_TARGET));

        env (ticket::create (alice, env.master),
            json (jss::Sequence, 1),
            ter (terNO_ACCOUNT));
    }

    void testTicketToSelf ()
    {
        testcase ("Create Tickets with Same Account and Target");

        using namespace test::jtx;
        Env env {*this, supported_amendments().set(featureTickets)};

        env (ticket::create (env.master, env.master));
        auto cr = checkTicketMeta (env);
        auto const& jticket = cr[1];

        BEAST_EXPECT(jticket[sfLedgerIndex.fieldName] ==
            "7F58A0AE17775BA3404D55D406DD1C2E91EADD7AF3F03A26877BCE764CCB75E3");
        BEAST_EXPECT(jticket[sfNewFields.fieldName][jss::Account] ==
            env.master.human());
        BEAST_EXPECT(jticket[sfNewFields.fieldName][jss::Sequence] == 1);
        //verify that there's no `Target` saved in the ticket
        BEAST_EXPECT(! jticket[sfNewFields.fieldName].
            isMember(sfTarget.fieldName));
    }

    void testTicketCancelByCreator ()
    {
        testcase ("Create Ticket and Then Cancel by Creator");

        using namespace test::jtx;
        Env env {*this, supported_amendments().set(featureTickets)};

        // create and verify
        env (ticket::create (env.master));
        auto cr = checkTicketMeta (env);
        auto const& jacct = cr[0];
        auto const& jticket = cr[1];
        BEAST_EXPECT(
            jacct[sfPreviousFields.fieldName][sfOwnerCount.fieldName] == 0);
        BEAST_EXPECT(
            jacct[sfFinalFields.fieldName][sfOwnerCount.fieldName] == 1);
        BEAST_EXPECT(jticket[sfNewFields.fieldName][jss::Sequence] ==
            jacct[sfPreviousFields.fieldName][jss::Sequence]);
        BEAST_EXPECT(jticket[sfLedgerIndex.fieldName] ==
            "7F58A0AE17775BA3404D55D406DD1C2E91EADD7AF3F03A26877BCE764CCB75E3");
        BEAST_EXPECT(jticket[sfNewFields.fieldName][jss::Account] ==
            env.master.human());

        // cancel
        env (ticket::cancel(env.master, jticket[sfLedgerIndex.fieldName].asString()));
        auto crd = checkTicketMeta (env);
        auto const& jacctd = crd[0];
        BEAST_EXPECT(jacctd[sfFinalFields.fieldName][jss::Sequence] == 3);
        BEAST_EXPECT(
            jacctd[sfFinalFields.fieldName][sfOwnerCount.fieldName] == 0);
    }

    void testTicketInsufficientReserve ()
    {
        testcase ("Create Ticket Insufficient Reserve");

        using namespace test::jtx;
        Env env {*this, supported_amendments().set(featureTickets)};
        Account alice {"alice"};

        env.fund (env.current ()->fees ().accountReserve (0), alice);
        env.close ();

        env (ticket::create (alice), ter (tecINSUFFICIENT_RESERVE));
    }

    void testTicketCancelByTarget ()
    {
        testcase ("Create Ticket and Then Cancel by Target");

        using namespace test::jtx;
        Env env {*this, supported_amendments().set(featureTickets)};
        Account alice {"alice"};

        env.fund (XRP (10000), alice);
        env.close ();

        // create and verify
        env (ticket::create (env.master, alice));
        auto cr = checkTicketMeta (env, true);
        auto const& jacct = cr[0];
        auto const& jticket = cr[1];
        BEAST_EXPECT(
            jacct[sfFinalFields.fieldName][sfOwnerCount.fieldName] == 1);
        BEAST_EXPECT(jticket[sfLedgerEntryType.fieldName] == "Ticket");
        BEAST_EXPECT(jticket[sfLedgerIndex.fieldName] ==
            "C231BA31A0E13A4D524A75F990CE0D6890B800FF1AE75E51A2D33559547AC1A2");
        BEAST_EXPECT(jticket[sfNewFields.fieldName][jss::Account] ==
            env.master.human());
        BEAST_EXPECT(jticket[sfNewFields.fieldName][sfTarget.fieldName] ==
            alice.human());
        BEAST_EXPECT(jticket[sfNewFields.fieldName][jss::Sequence] == 2);

        // cancel using the target account
        env (ticket::cancel(alice, jticket[sfLedgerIndex.fieldName].asString()));
        auto crd = checkTicketMeta (env, true);
        auto const& jacctd = crd[0];
        auto const& jdir = crd[2];
        BEAST_EXPECT(
            jacctd[sfFinalFields.fieldName][sfOwnerCount.fieldName] == 0);
        BEAST_EXPECT(jdir[sfLedgerIndex.fieldName] ==
            jticket[sfLedgerIndex.fieldName]);
        BEAST_EXPECT(jdir[sfFinalFields.fieldName][jss::Account] ==
            env.master.human());
        BEAST_EXPECT(jdir[sfFinalFields.fieldName][sfTarget.fieldName] ==
            alice.human());
        BEAST_EXPECT(jdir[sfFinalFields.fieldName][jss::Flags] == 0);
        BEAST_EXPECT(jdir[sfFinalFields.fieldName][sfOwnerNode.fieldName] ==
            "0000000000000000");
        BEAST_EXPECT(jdir[sfFinalFields.fieldName][jss::Sequence] == 2);
    }

    void testTicketWithExpiration ()
    {
        testcase ("Create Ticket with Future Expiration");

        using namespace test::jtx;
        Env env {*this, supported_amendments().set(featureTickets)};

        // create and verify
        using namespace std::chrono_literals;
        uint32_t expire =
            (env.timeKeeper ().closeTime () + 60s)
            .time_since_epoch ().count ();
        env (ticket::create (env.master, expire));
        auto cr = checkTicketMeta (env);
        auto const& jacct = cr[0];
        auto const& jticket = cr[1];
        BEAST_EXPECT(
            jacct[sfPreviousFields.fieldName][sfOwnerCount.fieldName] == 0);
        BEAST_EXPECT(
            jacct[sfFinalFields.fieldName][sfOwnerCount.fieldName] == 1);
        BEAST_EXPECT(jticket[sfNewFields.fieldName][jss::Sequence] ==
            jacct[sfPreviousFields.fieldName][jss::Sequence]);
        BEAST_EXPECT(
            jticket[sfNewFields.fieldName][sfExpiration.fieldName] == expire);
    }

    void testTicketZeroExpiration ()
    {
        testcase ("Create Ticket with Zero Expiration");

        using namespace test::jtx;
        Env env {*this, supported_amendments().set(featureTickets)};

        // create and verify
        env (ticket::create (env.master, 0u), ter (temBAD_EXPIRATION));
    }

    void testTicketWithPastExpiration ()
    {
        testcase ("Create Ticket with Past Expiration");

        using namespace test::jtx;
        Env env {*this, supported_amendments().set(featureTickets)};

        env.timeKeeper ().adjustCloseTime (days {2});
        env.close ();

        // create and verify
        uint32_t expire = 60;
        env (ticket::create (env.master, expire));
        // in the case of past expiration, we only get
        // one meta node entry returned
        auto const& jvm = env.meta ()->getJson (JsonOption::none);
        BEAST_EXPECT(jvm.isMember(sfAffectedNodes.fieldName));
        BEAST_EXPECT(jvm[sfAffectedNodes.fieldName].isArray());
        BEAST_EXPECT(jvm[sfAffectedNodes.fieldName].size() == 1);
        BEAST_EXPECT(jvm[sfAffectedNodes.fieldName][0u].
            isMember(sfModifiedNode.fieldName));
        auto const& jacct =
            jvm[sfAffectedNodes.fieldName][0u][sfModifiedNode.fieldName];
        BEAST_EXPECT(
            jacct[sfLedgerEntryType.fieldName] == "AccountRoot");
        BEAST_EXPECT(jacct[sfFinalFields.fieldName][jss::Account] ==
            env.master.human());
    }

    void testTicketAllowExpiration ()
    {
        testcase ("Create Ticket and Allow to Expire");

        using namespace test::jtx;
        Env env {*this, supported_amendments().set(featureTickets)};

        // create and verify
        uint32_t expire =
            (env.timeKeeper ().closeTime () + std::chrono::hours {3})
            .time_since_epoch().count();
        env (ticket::create (env.master, expire));
        auto cr = checkTicketMeta (env);
        auto const& jacct = cr[0];
        auto const& jticket = cr[1];
        BEAST_EXPECT(
            jacct[sfPreviousFields.fieldName][sfOwnerCount.fieldName] == 0);
        BEAST_EXPECT(
            jacct[sfFinalFields.fieldName][sfOwnerCount.fieldName] == 1);
        BEAST_EXPECT(
            jticket[sfNewFields.fieldName][sfExpiration.fieldName] == expire);
        BEAST_EXPECT(jticket[sfLedgerIndex.fieldName] ==
            "7F58A0AE17775BA3404D55D406DD1C2E91EADD7AF3F03A26877BCE764CCB75E3");

        Account alice {"alice"};
        env.fund (XRP (10000), alice);
        env.close ();

        // now try to cancel with alice account, which should not work
        auto jv = ticket::cancel(alice, jticket[sfLedgerIndex.fieldName].asString());
        env (jv, ter (tecNO_PERMISSION));

        // advance the ledger time to as to trigger expiration
        env.timeKeeper ().adjustCloseTime (days {3});
        env.close ();

        // now try again - the cancel succeeds because ticket has expired
        env (jv);
        auto crd = checkTicketMeta (env, true, true);
        auto const& jticketd = crd[1];
        BEAST_EXPECT(
            jticketd[sfFinalFields.fieldName][sfExpiration.fieldName] == expire);
    }

public:
    void run () override
    {
        testTicketNotEnabled ();
        testTicketCancelNonexistent ();
        testTicketCreatePreflightFail ();
        testTicketCreateNonexistent ();
        testTicketToSelf ();
        testTicketCancelByCreator ();
        testTicketInsufficientReserve ();
        testTicketCancelByTarget ();
        testTicketWithExpiration ();
        testTicketZeroExpiration ();
        testTicketWithPastExpiration ();
        testTicketAllowExpiration ();
    }
};

BEAST_DEFINE_TESTSUITE (Ticket, tx, ripple);

}  // ripple

