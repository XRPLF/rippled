//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#include <ripple/app/main/Application.h>
#include <ripple/app/ledger/tests/common_ledger.h>
#include <ripple/app/ledger/TxHold.h>
#include <ripple/app/ledger/LedgerHolder.h>
#include <ripple/core/LoadFeeTrack.h>
#include <ripple/basics/Log.h>
#include <ripple/basics/TestSuite.h>

namespace ripple {
namespace test {

class TxQ_test : public TestSuite
{
    // Acts as a wrapper for to unconditionally
    // get, use, and set a mutable ledger from
    // a LedgerHolder.
    // Obviously, this class is not appropriate
    // for test cases where you might not want to
    // set the ledger back to the LedgerHolder.
    class LedgerHelper
    {
    public:
        LedgerHelper(LedgerHolder& ledgerHolder)
            : ledgerHolder_(ledgerHolder)
        {
            ledger_ = ledgerHolder_.getMutable();
        }
        ~LedgerHelper()
        {
            ledger_->setImmutable();
            ledgerHolder_.set(ledger_);
        }

        operator Ledger::pointer()
        {
            return ledger_;
        }

    private:
        LedgerHolder& ledgerHolder_;
        Ledger::pointer ledger_;
    };

    TestAccount
    createAndQueueAccount(TxQ& txQ,
        TransactionEngineParams params,
        TransactionEngine& engine,
        TestAccount& from,
        std::string const& password, KeyType keyType,
        std::uint64_t amountDrops, std::uint64_t feeDrops)
    {
        auto to = createAccount(password, keyType);
        queuePaymentToApply(txQ, params, engine,
            from, to, amountDrops, feeDrops);

        return to;
    }

    void
    queuePaymentToApply(TxQ& txQ,
        TransactionEngineParams params,
        TransactionEngine& engine,
        TestAccount& from, TestAccount& to,
        std::uint64_t amountDrops, std::uint64_t feeDrops = 10)
    {
        std::shared_ptr<STTx> payment(new STTx(parseTransaction(
            from, getPaymentJson(from, to, amountDrops, feeDrops))));
        auto addResult = txQ.addTransaction(payment, params, engine);
        expectEquals(addResult.first, TxQ::TD_open_ledger);
        expectEquals(addResult.second, tesSUCCESS);
    }

    void
    queuePaymentToHold(TxQ& txQ,
        TransactionEngineParams params,
        TransactionEngine& engine,
        TestAccount& from, TestAccount& to,
        std::uint64_t amountDrops, std::uint64_t feeDrops = 10)
    {
        auto paymentJson = getPaymentJson(from, to,
            amountDrops, feeDrops);
        std::shared_ptr<STTx> payment(new STTx(
            parseTransaction(from, paymentJson)));
        auto addResult = txQ.addTransaction(payment, params, engine);
        expectEquals(addResult.first, TxQ::TD_held);
        expectEquals(addResult.second, TxQ::txnResultHeld);
    }

    void
    simulateConsensus(TxQ& txQ, LoadFeeTrack& loadFeeTrack,
        Ledger::pointer& LCL, LedgerHolder& ledgerHolder,
        size_t expectedTxSetSize)
    {
        auto ledger = ledgerHolder.getMutable();

        auto txSet = close_and_advance(ledger, LCL);
        expectEquals(txSet.size(), expectedTxSetSize);

        // TODO EHENNIS This is stolen from LedgerConsensus::accept
        //              until I understand simulating consensus better.
        //              Yes, it is a kludge.
        // processValidatedLedger is called indirectly by 
        // LedgerMaster::consensusBuilt in 
        // LedgerConsensusImp::accept
        txQ.processValidatedLedger(LCL);
        auto refTxnCost = LCL->getBaseFee();
        /*
        if (oldOL->peekTransactionMap()->getHash().isNonZero())
        {
            WriteLog(lsDEBUG, LedgerConsensus)
                << "Applying transactions from current open ledger";
            applyTransactions(buildTxSet(LCL->peekTransactionMap()),
                ledger, LCL, retriableTransactions, true);
        }
        */

        // Update fee computations.
        updateFeeTracking(ledger, txSet, loadFeeTrack, refTxnCost, nullptr);
        // Stuff the ledger with transactions from the queue.
        TransactionEngine engine(ledger);
        txQ.fillOpenLedger(engine);

        ledger->setImmutable();
        ledgerHolder.set(ledger);
    }

    void checkMetrics(
        TxQ& txQ,
        int expectedCount,
        int expectedPerLedger,
        int expectedMinFeeLevel,
        int expectedMedFeeLevel,
        int expectedCurFeeLevel)
    {
        auto metrics = txQ.getFeeMetrics();
        expectEquals(metrics.referenceFeeLevel, 256);
        expectEquals(metrics.txCount, expectedCount);
        expectEquals(metrics.txPerLedger, expectedPerLedger);
        expectEquals(metrics.minFeeLevel, expectedMinFeeLevel);
        expectEquals(metrics.medFeeLevel, expectedMedFeeLevel);
        expectEquals(metrics.expFeeLevel, expectedCurFeeLevel);
    }


public:
    void run()
    {
        TestSink sink { *this };
        sink.severity(beast::Journal::Severity::kAll);
        beast::Journal journal { sink };
        // We need the LoadFeeTrack object from getApp() because
        // some dependencies (notably Ledger::scaleFeeLoad call
        // back to the app to get the same object.
        // TODO EHENNIS: Inject LoadFeeTrack into Ledger.
        auto& loadFeeTrack = getApp().getFeeTrack();
        auto const oldMinimum = loadFeeTrack.setMinimumTx(3);
        TxQ::Setup txSetup;
        txSetup.ledgersInQueue_ = 1;
        txSetup.minLedgersToComputeSizeLimit_ = 3;
        txSetup.maxLedgerCountsToStore_ = 100;
        auto txQ = make_TxQ(txSetup, loadFeeTrack, journal);
        auto transactionParams = tapOPEN_LEDGER;    // | tapNO_CHECK_SIGN;

        std::uint64_t const xrp = std::mega::num;
        auto master = createAccount("masterpassphrase", KeyType::ed25519);
        Ledger::pointer LCL;
        LedgerHolder ledgerHolder;
        {
            Ledger::pointer ledger;
            std::tie(LCL, ledger) = createGenesisLedger(100000 * xrp, master);
            ledger->setImmutable();
            ledgerHolder.set(ledger);
        }

        expectEquals(LCL->getBaseFee(), 10);
        expectEquals(ledgerHolder.get()->getBaseFee(), 10);

        checkMetrics(*txQ, 0, 3, 256, 256, 256);

        TestAccount alice, bob, charlie, daria;
        int highFeeBob;
        {
            LedgerHelper ledger(ledgerHolder);
            TransactionEngine engine(ledger);

            // Create several accounts while the fee is cheap so they all apply.
            alice = createAndQueueAccount(*txQ,
                transactionParams, engine, master,
                "alice", KeyType::secp256k1, 10000 * xrp, 20);
            bob = createAndQueueAccount(*txQ,
                transactionParams, engine, master,
                "bob", KeyType::ed25519, 2000 * xrp, 15);
            charlie = createAndQueueAccount(*txQ,
                transactionParams, engine, master,
                "charlie", KeyType::ed25519, 3000 * xrp, 10);
            checkMetrics(*txQ, 0, 3, 256, 256, 256);
            daria = createAndQueueAccount(*txQ,
                transactionParams, engine, master,
                "daria", KeyType::secp256k1, 50000 * xrp, 500);
            checkMetrics(*txQ, 0, 3, 256, 256, 256 * 500 * 16 / 9);
        }

        {
            // Not using a LedgerHelper here, because we want to throw
            // the ledger changes away when done.
            auto ledger = ledgerHolder.getMutable();
            TransactionEngine engine(ledger);

            // Alice -> Bob - price starts exploding: held
            queuePaymentToHold(*txQ,
                transactionParams, engine,
                alice, bob, 2000 * xrp);
            checkMetrics(*txQ, 1, 3, 256, 256, 256 * 500 * 16 / 9);

            // Alice -> Charlie - Alice is already in the queue, so can't hold.
            expectEquals(loadFeeTrack.scaleTxnFee(loadFeeTrack.getLoadBase()),
                256 * 500 * 16 / 9);
            std::shared_ptr<STTx> aliceToCharlie(new STTx(getPaymentTx(alice,
                charlie, 500 * xrp)));
            auto addResult = txQ->addTransaction(aliceToCharlie,
                transactionParams, engine);
            expectEquals(addResult.first, TxQ::TD_low_fee);
            expectEquals(addResult.second, TxQ::txnResultLowFee);
            checkMetrics(*txQ, 1, 3, 256, 256, 256 * 500 * 16 / 9);

            // Alice -> Charlie with really high fee - fails because of the item in the TxQ
            expectEquals(loadFeeTrack.scaleTxnFee(loadFeeTrack.getLoadBase()),
                256 * 500 * 16 / 9);
            auto aliceToCharlieHighFeeJson = getPaymentJson(alice, charlie,
                3000 * xrp, 10 * 500 * 16 / 9 + 1);
            std::shared_ptr<STTx> aliceToCharlieHighFee(new STTx(
                parseTransaction(alice, aliceToCharlieHighFeeJson)));
            addResult = txQ->addTransaction(aliceToCharlieHighFee,
                transactionParams, engine);
            expectEquals(addResult.first, TxQ::TD_failed);
            expectEquals(addResult.second, terPRE_SEQ);
            checkMetrics(*txQ, 1, 3, 256, 256, 256 * 500 * 16 / 9);

            // Two transactions for alice failed
            alice.sequence -= 2;
        }

        {
            LedgerHelper ledger(ledgerHolder);
            TransactionEngine engine(ledger);

            // Bob -> Charlie with really high fee - applies
            expectEquals(loadFeeTrack.scaleTxnFee(loadFeeTrack.getLoadBase()),
                256 * 500 * 16 / 9);
            highFeeBob = 10 * 500 * 16 / 9 + 1;
            queuePaymentToApply(*txQ, transactionParams, engine,
                bob, charlie, 500 * xrp, highFeeBob);
            checkMetrics(*txQ, 1, 3, 256, 256, 256 * 500 * 25 / 9);
        }

        {
            auto ledger = ledgerHolder.getMutable();
            TransactionEngine engine(ledger);

            // Daria -> Bob with low fee: hold
            queuePaymentToHold(*txQ, transactionParams,
                engine, daria, bob, 9000 * xrp, 1000);
            checkMetrics(*txQ, 2, 3, 256, 256, 256 * 500 * 25 / 9);
        }

        {
            auto ledger = ledgerHolder.get();

            // Confirm balances
            verifyBalance(ledger, alice, 10000 * xrp);
            verifyBalance(ledger, bob, 1500 * xrp - highFeeBob);
            verifyBalance(ledger, charlie, 3500 * xrp);
            verifyBalance(ledger, daria, 50000 * xrp);
        }

        // Advance the ledger.
        auto lastMedian = 512;
        simulateConsensus(*txQ, loadFeeTrack, LCL, ledgerHolder, 5);
        checkMetrics(*txQ, 0, 5, 256, lastMedian, 256);

        // Verify that the held transactions got applied
        {
            auto ledger = ledgerHolder.get();

            expectEquals(countLedgerNodes(ledger), 2);
            expectEquals(loadFeeTrack.scaleTxnFee(loadFeeTrack.getLoadBase()),
                256);
            verifyBalance(ledger, alice, 8000 * xrp - 10);
            verifyBalance(ledger, bob, 12500 * xrp - highFeeBob);
            verifyBalance(ledger, charlie, 3500 * xrp);
            verifyBalance(ledger, daria, 41000 * xrp - 1000);
        }

        TestAccount elmo, fred, gwen, hank;
        {
            LedgerHelper ledger(ledgerHolder);
            TransactionEngine engine(ledger);

            // Make some more accounts. We'll need them later to abuse the queue.
            checkMetrics(*txQ, 0, 5, 256, lastMedian, 256);
            elmo = createAndQueueAccount(*txQ,
                transactionParams, engine,
                master, "elmo", KeyType::ed25519, 2000 * xrp, 1000);
            fred = createAndQueueAccount(*txQ,
                transactionParams, engine,
                master, "fred", KeyType::secp256k1, 1500 * xrp, 1500);
            gwen = createAndQueueAccount(*txQ,
                transactionParams, engine,
                master, "gwen", KeyType::ed25519, 1000 * xrp, 2000);
            hank = createAndQueueAccount(*txQ,
                transactionParams, engine,
                master, "hank", KeyType::secp256k1, 500 * xrp, 2500);
            checkMetrics(*txQ, 0, 5, 256, lastMedian, 256 * lastMedian * 36 / 25);
        }

        {
            auto ledger = ledgerHolder.getMutable();
            TransactionEngine engine(ledger);

            // Now get a bunch of transactions held.
            queuePaymentToHold(*txQ,
                transactionParams, engine,
                alice, bob, 2 * xrp, 12);
            checkMetrics(*txQ, 1, 5, 256, lastMedian, 256 * lastMedian * 36 / 25);
            queuePaymentToHold(*txQ,
                transactionParams, engine,
                bob, charlie, 10 * xrp, 10);  // won't clear the queue
            queuePaymentToHold(*txQ,
                transactionParams, engine,
                charlie, daria, 3 * xrp, 20);
            queuePaymentToHold(*txQ,
                transactionParams, engine,
                daria, elmo, 50 * xrp, 15);
            queuePaymentToHold(*txQ,
                transactionParams, engine,
                elmo, fred, 100 * xrp, 11);
            queuePaymentToHold(*txQ,
                transactionParams, engine,
                fred, gwen, 15 * xrp, 19);
            queuePaymentToHold(*txQ,
                transactionParams, engine,
                gwen, hank, 40 * xrp, 16);
            queuePaymentToHold(*txQ,
                transactionParams, engine,
                hank, alice, 190 * xrp, 18);
            checkMetrics(*txQ, 8, 5, 256, lastMedian, 256 * lastMedian * 36 / 25);
        }

        {
            auto ledger = ledgerHolder.get();

            verifyBalance(ledger, alice, 8000 * xrp - 10);
            verifyBalance(ledger, bob, 12500 * xrp - highFeeBob);
            verifyBalance(ledger, charlie, 3500 * xrp);
            verifyBalance(ledger, daria, 41000 * xrp - 1000);
            verifyBalance(ledger, elmo, 2000 * xrp);
            verifyBalance(ledger, fred, 1500 * xrp);
            verifyBalance(ledger, gwen, 1000 * xrp);
            verifyBalance(ledger, hank, 500 * xrp);
        }

        // Advance the ledger.
        lastMedian = 32000;
        simulateConsensus(*txQ, loadFeeTrack, LCL, ledgerHolder, 6);
        checkMetrics(*txQ, 1, 6, 256, lastMedian, 256 * lastMedian * 49 / 36);

        {
            auto ledger = ledgerHolder.get();

            // Verify that the held transactions got applied
            expectEquals(countLedgerNodes(ledger), 7);
            // The fee jumps up even more because the last round had so
            // many expensive transactions. The median level ended up at
            // 32000.
            expectEquals(loadFeeTrack.scaleTxnFee(loadFeeTrack.getLoadBase()),
                256 * lastMedian * 49 / 36);
            verifyBalance(ledger, alice, 8188 * xrp - 22);
            verifyBalance(ledger, bob, 12502 * xrp - highFeeBob);
            verifyBalance(ledger, charlie, 3497 * xrp - 20);
            verifyBalance(ledger, daria, 40953 * xrp - 1015);
            verifyBalance(ledger, elmo, 1950 * xrp - 11);
            verifyBalance(ledger, fred, 1585 * xrp - 19);
            verifyBalance(ledger, gwen, 975 * xrp - 16);
            verifyBalance(ledger, hank, 350 * xrp - 18);
        }

        {
            auto ledger = ledgerHolder.getMutable();
            TransactionEngine engine(ledger);

            // Hank sends another payment
            queuePaymentToHold(*txQ,
                transactionParams, engine,
                hank, charlie, 50 * xrp, 10);
            checkMetrics(*txQ, 2, 6, 256, lastMedian, 256 * lastMedian * 49 / 36);
        }

        {
            auto ledger = ledgerHolder.getMutable();
            TransactionEngine engine(ledger);

            // Hank sees his payment got held and bumps the fee, 
            // but doesn't bump it enough
            --hank.sequence;
            queuePaymentToHold(*txQ,
                transactionParams, engine,
                hank, charlie, 50 * xrp, 10000);
            checkMetrics(*txQ, 2, 6, 256, lastMedian, 256 * lastMedian * 49 / 36);
        }

        int highFeeHank;
        {
            LedgerHelper ledger(ledgerHolder);
            TransactionEngine engine(ledger);

            // Hank sees his payment got held and bumps the fee, 
            // because he doesn't want to wait.
            --hank.sequence;
            highFeeHank = 10 * lastMedian * 49 / 36 + 1;
            queuePaymentToApply(*txQ, transactionParams,
                engine, hank, charlie, 50 * xrp, highFeeHank);
            checkMetrics(*txQ, 1, 6, 256, lastMedian, 256 * lastMedian * 64 / 36);
        }

        {
            auto ledger = ledgerHolder.getMutable();
            TransactionEngine engine(ledger);
            // Hank then sends another, less important payment
            // (This will verify that the original payment got removed.)
            queuePaymentToHold(*txQ,
                transactionParams, engine,
                hank, fred, 1 * xrp, 10);
            checkMetrics(*txQ, 2, 6, 256, lastMedian, 256 * lastMedian * 64 / 36);
        }

        {
            auto ledger = ledgerHolder.get();

            verifyBalance(ledger, alice, 8188 * xrp - 22);
            verifyBalance(ledger, bob, 12502 * xrp - highFeeBob);
            verifyBalance(ledger, charlie, 3547 * xrp - 20);
            verifyBalance(ledger, daria, 40953 * xrp - 1015);
            verifyBalance(ledger, elmo, 1950 * xrp - 11);
            verifyBalance(ledger, fred, 1585 * xrp - 19);
            verifyBalance(ledger, gwen, 975 * xrp - 16);
            verifyBalance(ledger, hank, 300 * xrp - highFeeHank - 18);
        }

        // Advance the ledger
        lastMedian = 435;
        simulateConsensus(*txQ, loadFeeTrack, LCL, ledgerHolder, 8);
        {
            auto ledger = ledgerHolder.get();

            // At this point, the queue's size limit should be 6.
            // Verify that bob and hank's payments were applied
            expectEquals(countLedgerNodes(ledger), 2);
            checkMetrics(*txQ, 0, 8, 256, lastMedian, 256);

            verifyBalance(ledger, alice, 8188 * xrp - 22);
            verifyBalance(ledger, bob, 12492 * xrp - highFeeBob - 10);
            verifyBalance(ledger, charlie, 3557 * xrp - 20);
            verifyBalance(ledger, daria, 40953 * xrp - 1015);
            verifyBalance(ledger, elmo, 1950 * xrp - 11);
            verifyBalance(ledger, fred, 1586 * xrp - 19);
            verifyBalance(ledger, gwen, 975 * xrp - 16);
            verifyBalance(ledger, hank, 299 * xrp - highFeeHank - 28);
        }

        {
            LedgerHelper ledger(ledgerHolder);
            TransactionEngine engine(ledger);

            // At this point, the queue should have a limit of 6.
            // Stuff the ledger and queue so we can verify that
            // stuff gets kicked out.
            queuePaymentToApply(*txQ, transactionParams, engine,
                hank, gwen, 10 * xrp);
            queuePaymentToApply(*txQ, transactionParams, engine,
                gwen, fred, 10 * xrp);
            queuePaymentToApply(*txQ, transactionParams, engine,
                fred, elmo, 10 * xrp);
            queuePaymentToApply(*txQ, transactionParams, engine,
                elmo, daria, 10 * xrp);
            queuePaymentToApply(*txQ, transactionParams, engine,
                daria, charlie, 10 * xrp);
            queuePaymentToApply(*txQ, transactionParams, engine,
                charlie, bob, 10 * xrp);
            queuePaymentToApply(*txQ, transactionParams, engine,
                bob, alice, 10 * xrp);

            checkMetrics(*txQ, 0, 8, 256, lastMedian, 256 * 500 * 81 / 64);

            verifyBalance(ledger, alice, 8198 * xrp - 22);
            verifyBalance(ledger, bob, 12492 * xrp - highFeeBob - 20);
            verifyBalance(ledger, charlie, 3557 * xrp - 30);
            verifyBalance(ledger, daria, 40953 * xrp - 1025);
            verifyBalance(ledger, elmo, 1950 * xrp - 21);
            verifyBalance(ledger, fred, 1586 * xrp - 29);
            verifyBalance(ledger, gwen, 975 * xrp - 26);
            verifyBalance(ledger, hank, 289 * xrp - highFeeHank - 38);
        }

        {
            auto ledger = ledgerHolder.getMutable();
            TransactionEngine engine(ledger);
            // Use explicit fees so we deterministically which txn 
            // will get dropped
            queuePaymentToHold(*txQ, transactionParams,
                engine,
                alice, hank, 10 * xrp, 20);
            queuePaymentToHold(*txQ, transactionParams,
                engine,
                hank, gwen, 10 * xrp, 19);
            queuePaymentToHold(*txQ, transactionParams,
                engine,
                gwen, fred, 10 * xrp, 18);
            queuePaymentToHold(*txQ, transactionParams,
                engine,
                fred, elmo, 10 * xrp, 17);
            queuePaymentToHold(*txQ, transactionParams,
                engine,
                elmo, daria, 10 * xrp, 16);
            // This one gets into the queue, but gets dropped when the
            // higher fee one is added later.
            queuePaymentToHold(*txQ, transactionParams,
                engine,
                daria, charlie, 10 * xrp, 15);
            --daria.sequence;

            // Queue is full now.
            checkMetrics(*txQ, 6, 8, 385, lastMedian, 256 * 500 * 81 / 64);

            // Try to add another transaction, it should fail because
            // the queue is full.
            std::shared_ptr<STTx> charlieToBob(new STTx(getPaymentTx(charlie,
                bob, 10 * xrp)));
            auto addResult = txQ->addTransaction(charlieToBob,
                transactionParams, engine);
            expectEquals(addResult.first, TxQ::TD_low_fee);
            expectEquals(addResult.second, TxQ::txnResultLowFee);
            --charlie.sequence;

            // Add another transaction, with a higher fee,
            // Not high enough to get into the ledger, but high 
            // enough to get into the queue (and kick somebody out)
            queuePaymentToHold(*txQ, transactionParams,
                engine,
                charlie, bob, 10 * xrp, 100);

            // Queue is still full, of course, but the min fee has gone up
            checkMetrics(*txQ, 6, 8, 410, lastMedian, 256 * 500 * 81 / 64);
        }

        // Advance the ledger
        lastMedian = 256;
        simulateConsensus(*txQ, loadFeeTrack, LCL, ledgerHolder, 9);
        checkMetrics(*txQ, 0, 9, 256, lastMedian, 256);
        {
            auto ledger = ledgerHolder.get();

            expectEquals(countLedgerNodes(ledger), 6);

            verifyBalance(ledger, alice, 8188 * xrp - 42);
            verifyBalance(ledger, bob, 12502 * xrp - highFeeBob - 20);
            verifyBalance(ledger, charlie, 3547 * xrp - 130);
            verifyBalance(ledger, daria, 40963 * xrp - 1025);
            verifyBalance(ledger, elmo, 1950 * xrp - 37);
            verifyBalance(ledger, fred, 1586 * xrp - 46);
            verifyBalance(ledger, gwen, 975 * xrp - 44);
            verifyBalance(ledger, hank, 289 * xrp - highFeeHank - 57);
        }

        {
            // These should be the last two blocks, no matter what
            // else changes: Create a few more transactions, so that
            // we can be sure that there's one in the queue when the
            // test ends and the TxQ is destructed.

            LedgerHelper ledger(ledgerHolder);
            TransactionEngine engine(ledger);

            // Stuff the ledger.
            queuePaymentToApply(*txQ, transactionParams, engine,
                hank, gwen, 10 * xrp);
            queuePaymentToApply(*txQ, transactionParams, engine,
                gwen, fred, 10 * xrp);
            queuePaymentToApply(*txQ, transactionParams, engine,
                fred, elmo, 10 * xrp);
            queuePaymentToApply(*txQ, transactionParams, engine,
                elmo, daria, 10 * xrp);

            checkMetrics(*txQ, 0, 9, 256, lastMedian, 256 * 500 * 100 / 81);

            verifyBalance(ledger, alice, 8188 * xrp - 42);
            verifyBalance(ledger, bob, 12502 * xrp - highFeeBob - 20);
            verifyBalance(ledger, charlie, 3547 * xrp - 130);
            verifyBalance(ledger, daria, 40973 * xrp - 1025);
            verifyBalance(ledger, elmo, 1950 * xrp - 47);
            verifyBalance(ledger, fred, 1586 * xrp - 56);
            verifyBalance(ledger, gwen, 975 * xrp - 54);
            verifyBalance(ledger, hank, 279 * xrp - highFeeHank - 67);
        }
        {
            auto ledger = ledgerHolder.getMutable();
            TransactionEngine engine(ledger);

            // Queue one straightforward transaction
            queuePaymentToHold(*txQ, transactionParams,
                engine,
                alice, hank, 10 * xrp, 20);

            checkMetrics(*txQ, 1, 9, 256, lastMedian, 256 * 500 * 100 / 81);
        }

        // The loadFeeTrack is global, so we need to reset it
        // as much as possible, else we're going to break other
        // tests.
        loadFeeTrack.onLedger(0, {}, true);
        loadFeeTrack.setMinimumTx(oldMinimum);
        checkMetrics(*txQ, 1, 9, 256, 256, 256);

        pass();
    }
};

BEAST_DEFINE_TESTSUITE(TxQ,app,ripple);
    
}
}
