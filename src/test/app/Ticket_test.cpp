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

#include <ripple/app/misc/Transaction.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/jss.h>
#include <test/jtx.h>

namespace ripple {

class Ticket_test : public beast::unit_test::suite
{
    /// @brief Validate metadata for a successful CreateTicket transaction.
    ///
    /// @param env current jtx env (tx and meta are extracted using it)
    void
    checkTicketCreateMeta(test::jtx::Env& env)
    {
        using namespace std::string_literals;

        Json::Value const& tx{env.tx()->getJson(JsonOptions::none)};
        {
            std::string const txType =
                tx[sfTransactionType.jsonName].asString();

            if (!BEAST_EXPECTS(
                    txType == jss::TicketCreate,
                    "Unexpected TransactionType: "s + txType))
                return;
        }

        std::uint32_t const count = {tx[sfTicketCount.jsonName].asUInt()};
        if (!BEAST_EXPECTS(
                count >= 1,
                "Unexpected ticket count: "s + std::to_string(count)))
            return;

        std::uint32_t const txSeq = {tx[sfSequence.jsonName].asUInt()};
        std::string const account = tx[sfAccount.jsonName].asString();

        Json::Value const& metadata = env.meta()->getJson(JsonOptions::none);
        if (!BEAST_EXPECTS(
                metadata.isMember(sfTransactionResult.jsonName) &&
                    metadata[sfTransactionResult.jsonName].asString() ==
                        "tesSUCCESS",
                "Not metadata for successful TicketCreate."))
            return;

        BEAST_EXPECT(metadata.isMember(sfAffectedNodes.jsonName));
        BEAST_EXPECT(metadata[sfAffectedNodes.jsonName].isArray());

        bool directoryChanged = false;
        std::uint32_t acctRootFinalSeq = {0};
        std::vector<std::uint32_t> ticketSeqs;
        ticketSeqs.reserve(count);
        for (Json::Value const& node : metadata[sfAffectedNodes.jsonName])
        {
            if (node.isMember(sfModifiedNode.jsonName))
            {
                Json::Value const& modified = node[sfModifiedNode.jsonName];
                std::string const entryType =
                    modified[sfLedgerEntryType.jsonName].asString();
                if (entryType == jss::AccountRoot)
                {
                    auto const& previousFields =
                        modified[sfPreviousFields.jsonName];
                    auto const& finalFields = modified[sfFinalFields.jsonName];
                    {
                        // Verify the account root Sequence did the right thing.
                        std::uint32_t const prevSeq =
                            previousFields[sfSequence.jsonName].asUInt();

                        acctRootFinalSeq =
                            finalFields[sfSequence.jsonName].asUInt();

                        if (txSeq == 0)
                        {
                            // Transaction used a TicketSequence.
                            BEAST_EXPECT(acctRootFinalSeq == prevSeq + count);
                        }
                        else
                        {
                            // Transaction used a (plain) Sequence.
                            BEAST_EXPECT(prevSeq == txSeq);
                            BEAST_EXPECT(
                                acctRootFinalSeq == prevSeq + count + 1);
                        }
                    }

                    std::uint32_t const consumedTickets = {
                        txSeq == 0u ? 1u : 0u};

                    // If...
                    //  1. The TicketCount is 1 and
                    //  2. A ticket was consumed by the ticket create, then
                    //  3. The final TicketCount did not change, so the
                    //     previous TicketCount is not reported.
                    // But, since the count did not change, we know it equals
                    // the final Ticket count.
                    bool const unreportedPrevTicketCount = {
                        count == 1 && txSeq == 0};

                    // Verify the OwnerCount did the right thing
                    if (unreportedPrevTicketCount)
                    {
                        // The number of Tickets should not have changed, so
                        // the previous OwnerCount should not be reported.
                        BEAST_EXPECT(
                            !previousFields.isMember(sfOwnerCount.jsonName));
                    }
                    else
                    {
                        // Verify the OwnerCount did the right thing.
                        std::uint32_t const prevCount = {
                            previousFields[sfOwnerCount.jsonName].asUInt()};

                        std::uint32_t const finalCount = {
                            finalFields[sfOwnerCount.jsonName].asUInt()};

                        BEAST_EXPECT(
                            prevCount + count - consumedTickets == finalCount);
                    }

                    // Verify TicketCount metadata.
                    BEAST_EXPECT(finalFields.isMember(sfTicketCount.jsonName));

                    if (unreportedPrevTicketCount)
                    {
                        // The number of Tickets should not have changed, so
                        // the previous TicketCount should not be reported.
                        BEAST_EXPECT(
                            !previousFields.isMember(sfTicketCount.jsonName));
                    }
                    else
                    {
                        // If the TicketCount was previously present it
                        // should have been greater than zero.
                        std::uint32_t const startCount = {
                            previousFields.isMember(sfTicketCount.jsonName)
                                ? previousFields[sfTicketCount.jsonName]
                                      .asUInt()
                                : 0u};

                        BEAST_EXPECT(
                            (startCount == 0u) ^
                            previousFields.isMember(sfTicketCount.jsonName));

                        BEAST_EXPECT(
                            startCount + count - consumedTickets ==
                            finalFields[sfTicketCount.jsonName]);
                    }
                }
                else if (entryType == jss::DirectoryNode)
                {
                    directoryChanged = true;
                }
                else
                {
                    fail(
                        "Unexpected modified node: "s + entryType,
                        __FILE__,
                        __LINE__);
                }
            }
            else if (node.isMember(sfCreatedNode.jsonName))
            {
                Json::Value const& created = node[sfCreatedNode.jsonName];
                std::string const entryType =
                    created[sfLedgerEntryType.jsonName].asString();
                if (entryType == jss::Ticket)
                {
                    auto const& newFields = created[sfNewFields.jsonName];

                    BEAST_EXPECT(
                        newFields[sfAccount.jsonName].asString() == account);
                    ticketSeqs.push_back(
                        newFields[sfTicketSequence.jsonName].asUInt());
                }
                else if (entryType == jss::DirectoryNode)
                {
                    directoryChanged = true;
                }
                else
                {
                    fail(
                        "Unexpected created node: "s + entryType,
                        __FILE__,
                        __LINE__);
                }
            }
            else if (node.isMember(sfDeletedNode.jsonName))
            {
                Json::Value const& deleted = node[sfDeletedNode.jsonName];
                std::string const entryType =
                    deleted[sfLedgerEntryType.jsonName].asString();

                if (entryType == jss::Ticket)
                {
                    // Verify the transaction's Sequence == 0.
                    BEAST_EXPECT(txSeq == 0);

                    // Verify the account of the deleted ticket.
                    auto const& finalFields = deleted[sfFinalFields.jsonName];
                    BEAST_EXPECT(
                        finalFields[sfAccount.jsonName].asString() == account);

                    // Verify the deleted ticket has the right TicketSequence.
                    BEAST_EXPECT(
                        finalFields[sfTicketSequence.jsonName].asUInt() ==
                        tx[sfTicketSequence.jsonName].asUInt());
                }
            }
            else
            {
                fail(
                    "Unexpected node type in TicketCreate metadata.",
                    __FILE__,
                    __LINE__);
            }
        }
        BEAST_EXPECT(directoryChanged);

        // Verify that all the expected Tickets were created.
        BEAST_EXPECT(ticketSeqs.size() == count);
        std::sort(ticketSeqs.begin(), ticketSeqs.end());
        BEAST_EXPECT(
            std::adjacent_find(ticketSeqs.begin(), ticketSeqs.end()) ==
            ticketSeqs.end());
        BEAST_EXPECT(*ticketSeqs.rbegin() == acctRootFinalSeq - 1);
    }

    /// @brief Validate metadata for a ticket using transaction.
    ///
    /// The transaction may have been successful or failed with a tec.
    ///
    /// @param env current jtx env (tx and meta are extracted using it)
    void
    checkTicketConsumeMeta(test::jtx::Env& env)
    {
        Json::Value const& tx{env.tx()->getJson(JsonOptions::none)};

        // Verify that the transaction includes a TicketSequence.

        // Capture that TicketSequence.
        // Capture the Account from the transaction

        // Verify that metadata indicates a tec or a tesSUCCESS.

        // Walk affected nodes:
        //
        //   For each deleted node, see if it is a Ticket node.  If it is
        //   a Ticket Node being deleted, then assert that the...
        //
        //       Account == the transaction Account &&
        //       TicketSequence == the transaction TicketSequence
        //
        //   If a modified node is an AccountRoot, see if it is the transaction
        //   Account.  If it is then verify the TicketCount decreased by one.
        //   If the old TicketCount was 1, then the TicketCount field should be
        //   removed from the final fields of the AccountRoot.
        //
        // After looking at all nodes verify that exactly one Ticket node
        // was deleted.
        BEAST_EXPECT(tx[sfSequence.jsonName].asUInt() == 0);
        std::string const account{tx[sfAccount.jsonName].asString()};
        if (!BEAST_EXPECTS(
                tx.isMember(sfTicketSequence.jsonName),
                "Not metadata for a ticket consuming transaction."))
            return;

        std::uint32_t const ticketSeq{tx[sfTicketSequence.jsonName].asUInt()};

        Json::Value const& metadata{env.meta()->getJson(JsonOptions::none)};
        if (!BEAST_EXPECTS(
                metadata.isMember(sfTransactionResult.jsonName),
                "Metadata is missing TransactionResult."))
            return;

        {
            std::string const transactionResult{
                metadata[sfTransactionResult.jsonName].asString()};
            if (!BEAST_EXPECTS(
                    transactionResult == "tesSUCCESS" ||
                        transactionResult.compare(0, 3, "tec") == 0,
                    transactionResult + " neither tesSUCCESS nor tec"))
                return;
        }

        BEAST_EXPECT(metadata.isMember(sfAffectedNodes.jsonName));
        BEAST_EXPECT(metadata[sfAffectedNodes.jsonName].isArray());

        bool acctRootFound{false};
        std::uint32_t acctRootSeq{0};
        int ticketsRemoved{0};
        for (Json::Value const& node : metadata[sfAffectedNodes.jsonName])
        {
            if (node.isMember(sfModifiedNode.jsonName))
            {
                Json::Value const& modified{node[sfModifiedNode.jsonName]};
                std::string const entryType =
                    modified[sfLedgerEntryType.jsonName].asString();
                if (entryType == "AccountRoot" &&
                    modified[sfFinalFields.jsonName][sfAccount.jsonName]
                            .asString() == account)
                {
                    acctRootFound = true;

                    auto const& previousFields =
                        modified[sfPreviousFields.jsonName];
                    auto const& finalFields = modified[sfFinalFields.jsonName];

                    acctRootSeq = finalFields[sfSequence.jsonName].asUInt();

                    // Check that the TicketCount was present and decremented
                    // by 1.  If it decremented to zero, then the field should
                    // be gone.
                    if (!BEAST_EXPECTS(
                            previousFields.isMember(sfTicketCount.jsonName),
                            "AccountRoot previous is missing TicketCount"))
                        return;

                    std::uint32_t const prevTicketCount =
                        previousFields[sfTicketCount.jsonName].asUInt();

                    BEAST_EXPECT(prevTicketCount > 0);
                    if (prevTicketCount == 1)
                        BEAST_EXPECT(
                            !finalFields.isMember(sfTicketCount.jsonName));
                    else
                        BEAST_EXPECT(
                            finalFields.isMember(sfTicketCount.jsonName) &&
                            finalFields[sfTicketCount.jsonName].asUInt() ==
                                prevTicketCount - 1);
                }
            }
            else if (node.isMember(sfDeletedNode.jsonName))
            {
                Json::Value const& deleted{node[sfDeletedNode.jsonName]};
                std::string const entryType{
                    deleted[sfLedgerEntryType.jsonName].asString()};

                if (entryType == jss::Ticket)
                {
                    // Verify the account of the deleted ticket.
                    BEAST_EXPECT(
                        deleted[sfFinalFields.jsonName][sfAccount.jsonName]
                            .asString() == account);

                    // Verify the deleted ticket has the right TicketSequence.
                    BEAST_EXPECT(
                        deleted[sfFinalFields.jsonName]
                               [sfTicketSequence.jsonName]
                                   .asUInt() == ticketSeq);

                    ++ticketsRemoved;
                }
            }
        }
        BEAST_EXPECT(acctRootFound);
        BEAST_EXPECT(ticketsRemoved == 1);
        BEAST_EXPECT(ticketSeq < acctRootSeq);
    }

    void
    testTicketNotEnabled()
    {
        testcase("Feature Not Enabled");

        using namespace test::jtx;
        Env env{*this, supported_amendments() - featureTicketBatch};

        env(ticket::create(env.master, 1), ter(temDISABLED));
        env.close();
        env.require(owners(env.master, 0), tickets(env.master, 0));

        env(noop(env.master), ticket::use(1), ter(temMALFORMED));

        // Close enough ledgers that the previous transactions are no
        // longer retried.
        for (int i = 0; i < 8; ++i)
            env.close();

        env.enableFeature(featureTicketBatch);
        env.close();
        env.require(owners(env.master, 0), tickets(env.master, 0));

        std::uint32_t ticketSeq{env.seq(env.master) + 1};
        env(ticket::create(env.master, 2));
        checkTicketCreateMeta(env);
        env.close();
        env.require(owners(env.master, 2), tickets(env.master, 2));

        env(noop(env.master), ticket::use(ticketSeq++));
        checkTicketConsumeMeta(env);
        env.close();
        env.require(owners(env.master, 1), tickets(env.master, 1));

        env(fset(env.master, asfDisableMaster),
            ticket::use(ticketSeq++),
            ter(tecNO_ALTERNATIVE_KEY));
        checkTicketConsumeMeta(env);
        env.close();
        env.require(owners(env.master, 0), tickets(env.master, 0));
    }

    void
    testTicketCreatePreflightFail()
    {
        testcase("Create Tickets that fail Preflight");

        using namespace test::jtx;
        Env env{*this, supported_amendments() | featureTicketBatch};

        Account const master{env.master};

        // Exercise boundaries on count.
        env(ticket::create(master, 0), ter(temINVALID_COUNT));
        env(ticket::create(master, 251), ter(temINVALID_COUNT));

        // Exercise fees.
        std::uint32_t const ticketSeq_A{env.seq(master) + 1};
        env(ticket::create(master, 1), fee(XRP(10)));
        checkTicketCreateMeta(env);
        env.close();
        env.require(owners(master, 1), tickets(master, 1));

        env(ticket::create(master, 1), fee(XRP(-1)), ter(temBAD_FEE));

        // Exercise flags.
        std::uint32_t const ticketSeq_B{env.seq(master) + 1};
        env(ticket::create(master, 1), txflags(tfFullyCanonicalSig));
        checkTicketCreateMeta(env);
        env.close();
        env.require(owners(master, 2), tickets(master, 2));

        env(ticket::create(master, 1), txflags(tfSell), ter(temINVALID_FLAG));
        env.close();
        env.require(owners(master, 2), tickets(master, 2));

        // We successfully created 1 ticket earlier.  Verify that we can
        // create 250 tickets in one shot.  We must consume one ticket first.
        env(noop(master), ticket::use(ticketSeq_A));
        checkTicketConsumeMeta(env);
        env.close();
        env.require(owners(master, 1), tickets(master, 1));

        env(ticket::create(master, 250), ticket::use(ticketSeq_B));
        checkTicketCreateMeta(env);
        env.close();
        env.require(owners(master, 250), tickets(master, 250));
    }

    void
    testTicketCreatePreclaimFail()
    {
        testcase("Create Tickets that fail Preclaim");

        using namespace test::jtx;
        {
            // Create tickets on a non-existent account.
            Env env{*this, supported_amendments() | featureTicketBatch};
            Account alice{"alice"};
            env.memoize(alice);

            env(ticket::create(alice, 1),
                json(jss::Sequence, 1),
                ter(terNO_ACCOUNT));
        }
        {
            // Exceed the threshold where tickets can no longer be
            // added to an account.
            Env env{*this, supported_amendments() | featureTicketBatch};
            Account alice{"alice"};

            env.fund(XRP(100000), alice);

            std::uint32_t ticketSeq{env.seq(alice) + 1};
            env(ticket::create(alice, 250));
            checkTicketCreateMeta(env);
            env.close();
            env.require(owners(alice, 250), tickets(alice, 250));

            // Note that we can add one more ticket while consuming a ticket
            // because the final result is still 250 tickets.
            env(ticket::create(alice, 1), ticket::use(ticketSeq + 0));
            checkTicketCreateMeta(env);
            env.close();
            env.require(owners(alice, 250), tickets(alice, 250));

            // Adding one more ticket will exceed the threshold.
            env(ticket::create(alice, 2),
                ticket::use(ticketSeq + 1),
                ter(tecDIR_FULL));
            env.close();
            env.require(owners(alice, 249), tickets(alice, 249));

            // Now we can successfully add one more ticket.
            env(ticket::create(alice, 2), ticket::use(ticketSeq + 2));
            checkTicketCreateMeta(env);
            env.close();
            env.require(owners(alice, 250), tickets(alice, 250));

            // Since we're at 250, we can't add another ticket using a
            // sequence.
            env(ticket::create(alice, 1), ter(tecDIR_FULL));
            env.close();
            env.require(owners(alice, 250), tickets(alice, 250));
        }
        {
            // Explore exceeding the ticket threshold from another angle.
            Env env{*this, supported_amendments() | featureTicketBatch};
            Account alice{"alice"};

            env.fund(XRP(100000), alice);
            env.close();

            std::uint32_t ticketSeq_AB{env.seq(alice) + 1};
            env(ticket::create(alice, 2));
            checkTicketCreateMeta(env);
            env.close();
            env.require(owners(alice, 2), tickets(alice, 2));

            // Adding 250 tickets (while consuming one) will exceed the
            // threshold.
            env(ticket::create(alice, 250),
                ticket::use(ticketSeq_AB + 0),
                ter(tecDIR_FULL));
            env.close();
            env.require(owners(alice, 1), tickets(alice, 1));

            // Adding 250 tickets (without consuming one) will exceed the
            // threshold.
            env(ticket::create(alice, 250), ter(tecDIR_FULL));
            env.close();
            env.require(owners(alice, 1), tickets(alice, 1));

            // Alice can now add 250 tickets while consuming one.
            env(ticket::create(alice, 250), ticket::use(ticketSeq_AB + 1));
            checkTicketCreateMeta(env);
            env.close();
            env.require(owners(alice, 250), tickets(alice, 250));
        }
    }

    void
    testTicketInsufficientReserve()
    {
        testcase("Create Ticket Insufficient Reserve");

        using namespace test::jtx;
        Env env{*this, supported_amendments() | featureTicketBatch};
        Account alice{"alice"};

        // Fund alice not quite enough to make the reserve for a Ticket.
        env.fund(env.current()->fees().accountReserve(1) - drops(1), alice);
        env.close();

        env(ticket::create(alice, 1), ter(tecINSUFFICIENT_RESERVE));
        env.close();
        env.require(owners(alice, 0), tickets(alice, 0));

        // Give alice enough to exactly meet the reserve for one Ticket.
        env(
            pay(env.master,
                alice,
                env.current()->fees().accountReserve(1) - env.balance(alice)));
        env.close();

        env(ticket::create(alice, 1));
        checkTicketCreateMeta(env);
        env.close();
        env.require(owners(alice, 1), tickets(alice, 1));

        // Give alice not quite enough to make the reserve for a total of
        // 250 Tickets.
        env(
            pay(env.master,
                alice,
                env.current()->fees().accountReserve(250) - drops(1) -
                    env.balance(alice)));
        env.close();

        // alice doesn't quite have the reserve for a total of 250
        // Tickets, so the transaction fails.
        env(ticket::create(alice, 249), ter(tecINSUFFICIENT_RESERVE));
        env.close();
        env.require(owners(alice, 1), tickets(alice, 1));

        // Give alice enough so she can make the reserve for all 250
        // Tickets.
        env(pay(
            env.master,
            alice,
            env.current()->fees().accountReserve(250) - env.balance(alice)));
        env.close();

        std::uint32_t const ticketSeq{env.seq(alice) + 1};
        env(ticket::create(alice, 249));
        checkTicketCreateMeta(env);
        env.close();
        env.require(owners(alice, 250), tickets(alice, 250));
        BEAST_EXPECT(ticketSeq + 249 == env.seq(alice));
    }

    void
    testUsingTickets()
    {
        testcase("Using Tickets");

        using namespace test::jtx;
        Env env{*this, supported_amendments() | featureTicketBatch};
        Account alice{"alice"};

        env.fund(XRP(10000), alice);
        env.close();

        // Successfully create tickets (using a sequence)
        std::uint32_t const ticketSeq_AB{env.seq(alice) + 1};
        env(ticket::create(alice, 2));
        checkTicketCreateMeta(env);
        env.close();
        env.require(owners(alice, 2), tickets(alice, 2));
        BEAST_EXPECT(ticketSeq_AB + 2 == env.seq(alice));

        // You can use a ticket to create one ticket ...
        std::uint32_t const ticketSeq_C{env.seq(alice)};
        env(ticket::create(alice, 1), ticket::use(ticketSeq_AB + 0));
        checkTicketCreateMeta(env);
        env.close();
        env.require(owners(alice, 2), tickets(alice, 2));
        BEAST_EXPECT(ticketSeq_C + 1 == env.seq(alice));

        // ... you can use a ticket to create multiple tickets ...
        std::uint32_t const ticketSeq_DE{env.seq(alice)};
        env(ticket::create(alice, 2), ticket::use(ticketSeq_AB + 1));
        checkTicketCreateMeta(env);
        env.close();
        env.require(owners(alice, 3), tickets(alice, 3));
        BEAST_EXPECT(ticketSeq_DE + 2 == env.seq(alice));

        // ... and you can use a ticket for other things.
        env(noop(alice), ticket::use(ticketSeq_DE + 0));
        checkTicketConsumeMeta(env);
        env.close();
        env.require(owners(alice, 2), tickets(alice, 2));
        BEAST_EXPECT(ticketSeq_DE + 2 == env.seq(alice));

        env(pay(alice, env.master, XRP(20)), ticket::use(ticketSeq_DE + 1));
        checkTicketConsumeMeta(env);
        env.close();
        env.require(owners(alice, 1), tickets(alice, 1));
        BEAST_EXPECT(ticketSeq_DE + 2 == env.seq(alice));

        env(trust(alice, env.master["USD"](20)), ticket::use(ticketSeq_C));
        checkTicketConsumeMeta(env);
        env.close();
        env.require(owners(alice, 1), tickets(alice, 0));
        BEAST_EXPECT(ticketSeq_DE + 2 == env.seq(alice));

        // Attempt to use a ticket that has already been used.
        env(noop(alice), ticket::use(ticketSeq_C), ter(tefNO_TICKET));
        env.close();

        // Attempt to use a ticket from the future.
        std::uint32_t const ticketSeq_F{env.seq(alice) + 1};
        env(noop(alice), ticket::use(ticketSeq_F), ter(terPRE_TICKET));
        env.close();

        // Now create the ticket.  The retry will consume the new ticket.
        env(ticket::create(alice, 1));
        checkTicketCreateMeta(env);
        env.close();
        env.require(owners(alice, 1), tickets(alice, 0));
        BEAST_EXPECT(ticketSeq_F + 1 == env.seq(alice));

        // Try a transaction that combines consuming a ticket with
        // AccountTxnID.
        std::uint32_t const ticketSeq_G{env.seq(alice) + 1};
        env(ticket::create(alice, 1));
        checkTicketCreateMeta(env);
        env.close();

        env(noop(alice),
            ticket::use(ticketSeq_G),
            json(R"({"AccountTxnID": "0"})"),
            ter(temINVALID));
        env.close();
        env.require(owners(alice, 2), tickets(alice, 1));
    }

    void
    testTransactionDatabaseWithTickets()
    {
        // The Transaction database keeps each transaction's sequence number
        // in an entry (called "FromSeq").  Until the introduction of tickets
        // each sequence stored for a given account would always be unique.
        // With the advent of tickets there could be lots of entries
        // with zero.
        //
        // We really don't expect those zeros to cause any problems since
        // there are no indexes that use "FromSeq".  But it still seems
        // prudent to exercise this a bit to see if tickets cause any obvious
        // harm.
        testcase("Transaction Database With Tickets");

        using namespace test::jtx;
        Env env{*this, supported_amendments() | featureTicketBatch};
        Account alice{"alice"};

        env.fund(XRP(10000), alice);
        env.close();

        // Lambda that returns the hash of the most recent transaction.
        auto getTxID = [&env, this]() -> uint256 {
            std::shared_ptr<STTx const> tx{env.tx()};
            if (!BEAST_EXPECTS(tx, "Transaction not found"))
                Throw<std::invalid_argument>("Invalid transaction ID");

            return tx->getTransactionID();
        };

        // A note about the metadata created by these transactions.
        //
        // We _could_ check the metadata on these transactions.  However
        // checking the metadata has the side effect of advancing the ledger.
        // So if we check the metadata we don't get to look at several
        // transactions in the same ledger.  Therefore a specific choice was
        // made to not check the metadata on these transactions.

        // Successfully create several tickets (using a sequence).
        std::uint32_t ticketSeq{env.seq(alice)};
        static constexpr std::uint32_t ticketCount{10};
        env(ticket::create(alice, ticketCount));
        uint256 const txHash_1{getTxID()};

        // Just for grins use the tickets in reverse from largest to smallest.
        ticketSeq += ticketCount;
        env(noop(alice), ticket::use(--ticketSeq));
        uint256 const txHash_2{getTxID()};

        env(pay(alice, env.master, XRP(200)), ticket::use(--ticketSeq));
        uint256 const txHash_3{getTxID()};

        env(deposit::auth(alice, env.master), ticket::use(--ticketSeq));
        uint256 const txHash_4{getTxID()};

        // Close the ledger so we look at transactions from a couple of
        // different ledgers.
        env.close();

        env(pay(alice, env.master, XRP(300)), ticket::use(--ticketSeq));
        uint256 const txHash_5{getTxID()};

        env(pay(alice, env.master, XRP(400)), ticket::use(--ticketSeq));
        uint256 const txHash_6{getTxID()};

        env(deposit::unauth(alice, env.master), ticket::use(--ticketSeq));
        uint256 const txHash_7{getTxID()};

        env(noop(alice), ticket::use(--ticketSeq));
        uint256 const txHash_8{getTxID()};

        env.close();

        // Checkout what's in the Transaction database.  We go straight
        // to the database.  Most of our interfaces cache transactions
        // in memory.  So if we use normal interfaces we would get the
        // transactions from memory rather than from the database.

        // Lambda to verify a transaction pulled from the Transaction database.
        auto checkTxFromDB = [&env, this](
                                 uint256 const& txID,
                                 std::uint32_t ledgerSeq,
                                 std::uint32_t txSeq,
                                 boost::optional<std::uint32_t> ticketSeq,
                                 TxType txType) {
            error_code_i txErrCode{rpcSUCCESS};
            std::shared_ptr<Transaction> const tx{
                Transaction::load(txID, env.app(), txErrCode)};
            BEAST_EXPECT(txErrCode == rpcSUCCESS);
            BEAST_EXPECT(tx->getLedger() == ledgerSeq);

            std::shared_ptr<STTx const> const& sttx{tx->getSTransaction()};
            BEAST_EXPECT((*sttx)[sfSequence] == txSeq);
            if (ticketSeq)
                BEAST_EXPECT((*sttx)[sfTicketSequence] == *ticketSeq);
            BEAST_EXPECT((*sttx)[sfTransactionType] == txType);
        };

        //   txID ledgerSeq txSeq ticketSeq          txType
        checkTxFromDB(txHash_1, 4, 4, {}, ttTICKET_CREATE);
        checkTxFromDB(txHash_2, 4, 0, 13, ttACCOUNT_SET);
        checkTxFromDB(txHash_3, 4, 0, 12, ttPAYMENT);
        checkTxFromDB(txHash_4, 4, 0, 11, ttDEPOSIT_PREAUTH);

        checkTxFromDB(txHash_5, 5, 0, 10, ttPAYMENT);
        checkTxFromDB(txHash_6, 5, 0, 9, ttPAYMENT);
        checkTxFromDB(txHash_7, 5, 0, 8, ttDEPOSIT_PREAUTH);
        checkTxFromDB(txHash_8, 5, 0, 7, ttACCOUNT_SET);
    }

public:
    void
    run() override
    {
        testTicketNotEnabled();
        testTicketCreatePreflightFail();
        testTicketCreatePreclaimFail();
        testTicketInsufficientReserve();
        testUsingTickets();
        testTransactionDatabaseWithTickets();
    }
};

BEAST_DEFINE_TESTSUITE(Ticket, tx, ripple);

}  // namespace ripple
