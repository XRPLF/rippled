//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2024 Ripple Labs Inc.

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

#include <xrpld/app/misc/CanonicalTXSet.h>

#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/jss.h>

namespace ripple {
namespace test {

class CanonicalTXSet_test : public beast::unit_test::suite
{
    // Helper function to create a test transaction with sequence
    std::shared_ptr<STTx const>
    makeSeqTx(
        AccountID const& account,
        std::uint32_t seq,
        std::uint32_t salt = 0)
    {
        using namespace jtx;

        STObject tx(sfTransaction);
        tx.setAccountID(sfAccount, account);
        tx.setFieldU32(sfSequence, seq);
        tx.setFieldU16(sfTransactionType, ttPAYMENT);
        tx.setAccountID(sfDestination, AccountID(1));
        tx.setFieldAmount(sfAmount, STAmount(100));
        tx.setFieldAmount(sfFee, STAmount(10));
        tx.setFieldVL(sfSigningPubKey, Slice{});

        // Add salt to make unique transaction IDs
        if (salt != 0)
            tx.setFieldU32(sfSourceTag, salt);

        return std::make_shared<STTx const>(std::move(tx));
    }

    // Helper function to create a test transaction with ticket
    std::shared_ptr<STTx const>
    makeTicketTx(
        AccountID const& account,
        std::uint32_t ticketSeq,
        std::uint32_t salt = 0)
    {
        using namespace jtx;

        STObject tx(sfTransaction);
        tx.setAccountID(sfAccount, account);
        tx.setFieldU32(sfSequence, 0);
        tx.setFieldU32(sfTicketSequence, ticketSeq);
        tx.setFieldU16(sfTransactionType, ttPAYMENT);
        tx.setAccountID(sfDestination, AccountID(1));
        tx.setFieldAmount(sfAmount, STAmount(100));
        tx.setFieldAmount(sfFee, STAmount(10));
        tx.setFieldVL(sfSigningPubKey, Slice{});

        // Add salt to make unique transaction IDs
        if (salt != 0)
            tx.setFieldU32(sfSourceTag, salt);

        return std::make_shared<STTx const>(std::move(tx));
    }

    void
    testInsertAndIteration(bool hasFix)
    {
        testcase("Insert and Iteration");

        AccountID alice{1};
        AccountID bob{2};
        AccountID carol{3};
        AccountID dave{4};

        std::vector<uint256> ledgerHashes = {
            uint256(
                "9FCD278D5D77B4D5AF88EB9F0B2028C188975F7C75B548A137339EB6CF8C9A"
                "69"),
            uint256(
                "71FF372D8189A93B70D1705D698A34FF7315131CAC6E043D1CE20FE26FC323"
                "2A"),
        };

        std::vector<std::vector<AccountID>> goodData = {
            {{carol}, {alice}, {dave}, {bob}},
            {{bob}, {carol}, {dave}, {alice}},
        };

        std::vector<std::vector<AccountID>> badData = {
            {{dave}, {alice}, {bob}, {carol}},
            {{dave}, {alice}, {bob}, {carol}},
        };

        for (int i = 0; i < 2; ++i)
        {
            CanonicalTXSet set(ledgerHashes[i], hasFix);
            auto tx1 = makeSeqTx(alice, 100, 1);
            auto tx2 = makeTicketTx(bob, 100, 2);
            auto tx3 = makeTicketTx(carol, 100, 3);
            auto tx4 = makeTicketTx(dave, 100, 4);
            set.insert(tx4);  // dave
            set.insert(tx1);  // alice
            set.insert(tx3);  // carol
            set.insert(tx2);  // bob

            BEAST_EXPECT(set.size() == 4);

            // Iterate and check the canonical order
            std::vector<AccountID> orderedAccounts;
            for (auto it = set.begin(); it != set.end(); ++it)
            {
                auto accountID = it->second->getAccountID(sfAccount);
                orderedAccounts.push_back(accountID);
            }

            auto const& testData = hasFix ? goodData : badData;
            BEAST_EXPECT(orderedAccounts.size() == 4);
            BEAST_EXPECT(orderedAccounts[0] == testData[i][0]);
            BEAST_EXPECT(orderedAccounts[1] == testData[i][1]);
            BEAST_EXPECT(orderedAccounts[2] == testData[i][2]);
            BEAST_EXPECT(orderedAccounts[3] == testData[i][3]);
        }
    }

    void
    testErase()
    {
        testcase("Erase");

        CanonicalTXSet set(uint256(42));

        AccountID alice(1);
        auto tx1 = makeSeqTx(alice, 100, 1);
        auto tx2 = makeSeqTx(alice, 101, 2);
        auto tx3 = makeSeqTx(alice, 102, 3);

        set.insert(tx1);
        set.insert(tx2);
        set.insert(tx3);
        BEAST_EXPECT(set.size() == 3);

        // Find and erase a transaction
        auto it = set.begin();
        while (it != set.end() && it->second != tx2)
            ++it;

        BEAST_EXPECT(it != set.end());
        BEAST_EXPECT(it->second == tx2);

        it = set.erase(it);
        BEAST_EXPECT(set.size() == 2);

        // Verify tx2 is gone
        bool foundTx1 = false;
        bool foundTx2 = false;
        bool foundTx3 = false;

        for (auto const& item : set)
        {
            if (item.second == tx1)
                foundTx1 = true;
            if (item.second == tx2)
                foundTx2 = true;
            if (item.second == tx3)
                foundTx3 = true;
        }

        BEAST_EXPECT(foundTx1);
        BEAST_EXPECT(!foundTx2);
        BEAST_EXPECT(foundTx3);
    }

    void
    testReset()
    {
        testcase("Reset");

        CanonicalTXSet set(uint256(42));
        BEAST_EXPECT(set.key() == uint256(42));

        AccountID alice(1);
        auto tx1 = makeSeqTx(alice, 100, 1);
        auto tx2 = makeSeqTx(alice, 101, 2);

        set.insert(tx1);
        set.insert(tx2);
        BEAST_EXPECT(set.size() == 2);

        // Reset with new salt
        set.reset(uint256(99));
        BEAST_EXPECT(set.key() == uint256(99));
        BEAST_EXPECT(set.empty());
        BEAST_EXPECT(set.size() == 0);
    }

    void
    testPopAcctTransactionSequence()
    {
        testcase("Pop account transaction - sequences");

        CanonicalTXSet set(uint256(42));
        AccountID alice(1);
        AccountID bob(2);

        // Insert transactions with sequences
        auto tx1 = makeSeqTx(alice, 100, 1);
        auto tx2 = makeSeqTx(alice, 101, 2);
        auto tx3 = makeSeqTx(alice, 102, 3);
        auto tx4 = makeSeqTx(alice, 104, 4);  // Gap in sequence
        auto tx5 = makeSeqTx(bob, 200, 5);

        set.insert(tx1);
        set.insert(tx2);
        set.insert(tx3);
        set.insert(tx4);
        set.insert(tx5);

        // Create a "processed" transaction (not in set) with seq 99
        auto processedTx = makeSeqTx(alice, 99, 99);

        // Pop consecutive sequences
        auto popped = set.popAcctTransaction(processedTx);
        BEAST_EXPECT(popped == tx1);    // Returns tx with seq 100
        BEAST_EXPECT(set.size() == 4);  // tx1 removed

        // Now "process" tx1 (seq 100) to get tx2 (seq 101)
        popped = set.popAcctTransaction(tx1);
        BEAST_EXPECT(popped == tx2);    // Returns tx with seq 101
        BEAST_EXPECT(set.size() == 3);  // tx2 removed

        // Now "process" tx2 (seq 101) to get tx3 (seq 102)
        popped = set.popAcctTransaction(tx2);
        BEAST_EXPECT(popped == tx3);    // Returns tx with seq 102
        BEAST_EXPECT(set.size() == 2);  // tx3 removed

        // Now "process" tx3 (seq 102) - gap at 103, so no return
        popped = set.popAcctTransaction(tx3);
        BEAST_EXPECT(
            !popped);  // Gap in sequence (103 missing), returns nullptr
        BEAST_EXPECT(set.size() == 2);  // Nothing removed (tx4 and tx5 remain)
    }

    void
    testPopAcctTransactionTickets()
    {
        testcase("Pop account transaction - tickets");

        CanonicalTXSet set(uint256(42));
        AccountID alice(1);

        // Insert transactions with tickets
        auto tx1 = makeTicketTx(alice, 100, 1);
        auto tx2 = makeTicketTx(alice, 105, 2);
        auto tx3 = makeTicketTx(alice, 103, 3);

        set.insert(tx1);
        set.insert(tx2);
        set.insert(tx3);
        BEAST_EXPECT(set.size() == 3);

        // Create a "processed" ticket transaction (not in set)
        // This represents a transaction that was just processed
        auto processedTx = makeTicketTx(alice, 95, 99);

        // Pop ticket transactions (should return lowest ticket ID)
        auto popped = set.popAcctTransaction(processedTx);
        BEAST_EXPECT(popped == tx1);    // Ticket 100 is the lowest
        BEAST_EXPECT(set.size() == 2);  // tx1 removed

        // Now "process" tx1 (ticket 100) to get the next lowest ticket
        popped = set.popAcctTransaction(tx1);
        BEAST_EXPECT(popped == tx3);    // Ticket 103 is next lowest
        BEAST_EXPECT(set.size() == 1);  // tx3 removed

        // Now "process" tx3 (ticket 103) to get the next ticket
        popped = set.popAcctTransaction(tx3);
        BEAST_EXPECT(popped == tx2);    // Ticket 105 is the last one
        BEAST_EXPECT(set.size() == 0);  // tx2 removed, set is empty

        // Try to pop when set is empty
        popped = set.popAcctTransaction(tx2);
        BEAST_EXPECT(!popped);          // No more transactions
        BEAST_EXPECT(set.size() == 0);  // Still empty
    }

    void
    testPopAcctTransactionMixed()
    {
        testcase("Pop account transaction - mixed sequences and tickets");

        CanonicalTXSet set(uint256(42));
        AccountID alice(1);

        // Insert mix of sequence and ticket transactions
        auto tx1 = makeSeqTx(alice, 100, 1);
        auto tx2 = makeSeqTx(alice, 101, 2);
        auto tx3 = makeTicketTx(alice, 50, 3);   // Lower ticket
        auto tx4 = makeTicketTx(alice, 150, 4);  // Higher ticket

        set.insert(tx1);
        set.insert(tx2);
        set.insert(tx3);
        set.insert(tx4);
        BEAST_EXPECT(set.size() == 4);

        // Create a "processed" transaction with seq 99 (not in set)
        // This represents the last processed sequential transaction
        auto processedTx = makeSeqTx(alice, 99, 99);

        // Sequences should be processed first (in order)
        auto popped = set.popAcctTransaction(processedTx);
        BEAST_EXPECT(popped == tx1);  // Gets seq 100
        BEAST_EXPECT(set.size() == 3);

        // Use tx1 (just processed) to get the next one
        popped = set.popAcctTransaction(tx1);
        BEAST_EXPECT(popped == tx2);  // Gets seq 101
        BEAST_EXPECT(set.size() == 2);

        // After seq 101, there are no more sequential transactions
        // So now it should move to tickets (lowest first)
        popped = set.popAcctTransaction(tx2);
        BEAST_EXPECT(popped == tx3);  // Gets ticket 50 (lowest)
        BEAST_EXPECT(set.size() == 1);

        // Use tx3 (ticket 50) to get the next ticket
        popped = set.popAcctTransaction(tx3);
        BEAST_EXPECT(popped == tx4);  // Gets ticket 150 (only one left)
        BEAST_EXPECT(set.size() == 0);

        // Try to pop when empty
        popped = set.popAcctTransaction(tx4);
        BEAST_EXPECT(!popped);  // No more transactions
        BEAST_EXPECT(set.size() == 0);
    }

    void
    testDuplicateTransactions()
    {
        testcase("Duplicate transactions");

        CanonicalTXSet set(uint256(42));

        AccountID alice(1);

        // Create identical transactions
        auto tx1 = makeSeqTx(alice, 100, 1);
        auto tx2 = makeSeqTx(alice, 100, 1);  // Same parameters

        set.insert(tx1);
        set.insert(tx2);

        // Map should have unique keys
        BEAST_EXPECT(set.size() == 1);

        // The first insert wins with std::map
        auto it = set.begin();
        BEAST_EXPECT(it->second == tx1);  // Should be tx1, not tx2

        // Verify they have the same transaction ID
        BEAST_EXPECT(tx1->getTransactionID() == tx2->getTransactionID());
    }

    void
    testEmptyPop()
    {
        testcase("Empty pop");

        CanonicalTXSet set(uint256(42));

        AccountID alice(1);
        auto tx1 = makeSeqTx(alice, 100, 1);

        // Try to pop from empty set
        auto popped = set.popAcctTransaction(tx1);
        BEAST_EXPECT(!popped);
        BEAST_EXPECT(set.empty());
    }

    void
    testLargeGapInSequence()
    {
        testcase("Large gap in sequence");

        CanonicalTXSet set(uint256(42));

        AccountID alice(1);

        auto tx1 = makeSeqTx(alice, 100, 1);
        auto tx2 = makeSeqTx(alice, 200, 2);  // Large gap

        set.insert(tx1);
        set.insert(tx2);

        auto popped = set.popAcctTransaction(tx1);
        BEAST_EXPECT(!popped);  // Gap too large, no consecutive sequence
        BEAST_EXPECT(set.size() == 2);
    }

    void
    run() override
    {
        // testInsertAndIteration(false);
        testInsertAndIteration(true);
        // testErase();
        // testReset();
        // testPopAcctTransactionSequence();
        // testPopAcctTransactionTickets();
        // testPopAcctTransactionMixed();
        // testDuplicateTransactions();
        // testEmptyPop();
        // testLargeGapInSequence();
    }
};

BEAST_DEFINE_TESTSUITE(CanonicalTXSet, app, ripple);

}  // namespace test
}  // namespace ripple