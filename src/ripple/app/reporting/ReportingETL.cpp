//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2020 Ripple Labs Inc.

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

#include <ripple/app/rdb/backend/RelationalDBInterfacePostgres.h>
#include <ripple/app/reporting/ReportingETL.h>

#include <ripple/beast/core/CurrentThreadName.h>
#include <ripple/json/json_reader.h>
#include <ripple/json/json_writer.h>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <iostream>
#include <string>
#include <variant>

namespace ripple {

namespace detail {
/// Convenience function for printing out basic ledger info
std::string
toString(LedgerInfo const& info)
{
    std::stringstream ss;
    ss << "LedgerInfo { Sequence : " << info.seq
       << " Hash : " << strHex(info.hash) << " TxHash : " << strHex(info.txHash)
       << " AccountHash : " << strHex(info.accountHash)
       << " ParentHash : " << strHex(info.parentHash) << " }";
    return ss.str();
}
}  // namespace detail

void
ReportingETL::consumeLedgerData(
    std::shared_ptr<Ledger>& ledger,
    ThreadSafeQueue<std::shared_ptr<SLE>>& writeQueue)
{
    std::shared_ptr<SLE> sle;
    size_t num = 0;
    while (!stopping_ && (sle = writeQueue.pop()))
    {
        assert(sle);
        if (!ledger->exists(sle->key()))
            ledger->rawInsert(sle);

        if (flushInterval_ != 0 && (num % flushInterval_) == 0)
        {
            JLOG(journal_.debug()) << "Flushing! key = " << strHex(sle->key());
            ledger->stateMap().flushDirty(hotACCOUNT_NODE);
        }
        ++num;
    }
}

std::vector<AccountTransactionsData>
ReportingETL::insertTransactions(
    std::shared_ptr<Ledger>& ledger,
    org::xrpl::rpc::v1::GetLedgerResponse& data)
{
    std::vector<AccountTransactionsData> accountTxData;
    for (auto& txn : data.transactions_list().transactions())
    {
        auto& raw = txn.transaction_blob();

        SerialIter it{raw.data(), raw.size()};
        STTx sttx{it};

        auto txSerializer = std::make_shared<Serializer>(sttx.getSerializer());

        TxMeta txMeta{
            sttx.getTransactionID(), ledger->info().seq, txn.metadata_blob()};

        auto metaSerializer =
            std::make_shared<Serializer>(txMeta.getAsObject().getSerializer());

        JLOG(journal_.trace())
            << __func__ << " : "
            << "Inserting transaction = " << sttx.getTransactionID();
        uint256 nodestoreHash = ledger->rawTxInsertWithHash(
            sttx.getTransactionID(), txSerializer, metaSerializer);
        accountTxData.emplace_back(txMeta, std::move(nodestoreHash), journal_);
    }
    return accountTxData;
}

std::shared_ptr<Ledger>
ReportingETL::loadInitialLedger(uint32_t startingSequence)
{
    // check that database is actually empty
    auto ledger = std::const_pointer_cast<Ledger>(
        app_.getLedgerMaster().getValidatedLedger());
    if (ledger)
    {
        JLOG(journal_.fatal()) << __func__ << " : "
                               << "Database is not empty";
        assert(false);
        return {};
    }

    // fetch the ledger from the network. This function will not return until
    // either the fetch is successful, or the server is being shutdown. This
    // only fetches the ledger header and the transactions+metadata
    std::optional<org::xrpl::rpc::v1::GetLedgerResponse> ledgerData{
        fetchLedgerData(startingSequence)};
    if (!ledgerData)
        return {};

    LedgerInfo lgrInfo =
        deserializeHeader(makeSlice(ledgerData->ledger_header()), true);

    JLOG(journal_.debug()) << __func__ << " : "
                           << "Deserialized ledger header. "
                           << detail::toString(lgrInfo);

    ledger =
        std::make_shared<Ledger>(lgrInfo, app_.config(), app_.getNodeFamily());
    ledger->stateMap().clearSynching();
    ledger->txMap().clearSynching();

#ifdef RIPPLED_REPORTING
    std::vector<AccountTransactionsData> accountTxData =
        insertTransactions(ledger, *ledgerData);
#endif

    auto start = std::chrono::system_clock::now();

    ThreadSafeQueue<std::shared_ptr<SLE>> writeQueue;
    std::thread asyncWriter{[this, &ledger, &writeQueue]() {
        consumeLedgerData(ledger, writeQueue);
    }};

    // download the full account state map. This function downloads full ledger
    // data and pushes the downloaded data into the writeQueue. asyncWriter
    // consumes from the queue and inserts the data into the Ledger object.
    // Once the below call returns, all data has been pushed into the queue
    loadBalancer_.loadInitialLedger(startingSequence, writeQueue);

    // null is used to respresent the end of the queue
    std::shared_ptr<SLE> null;
    writeQueue.push(null);
    // wait for the writer to finish
    asyncWriter.join();

    if (!stopping_)
    {
        flushLedger(ledger);
        if (app_.config().reporting())
        {
#ifdef RIPPLED_REPORTING
            dynamic_cast<RelationalDBInterfacePostgres*>(
                &app_.getRelationalDBInterface())
                ->writeLedgerAndTransactions(ledger->info(), accountTxData);
#endif
        }
    }
    auto end = std::chrono::system_clock::now();
    JLOG(journal_.debug()) << "Time to download and store ledger = "
                           << ((end - start).count()) / 1000000000.0;
    return ledger;
}

void
ReportingETL::flushLedger(std::shared_ptr<Ledger>& ledger)
{
    JLOG(journal_.debug()) << __func__ << " : "
                           << "Flushing ledger. "
                           << detail::toString(ledger->info());
    // These are recomputed in setImmutable
    auto& accountHash = ledger->info().accountHash;
    auto& txHash = ledger->info().txHash;
    auto& ledgerHash = ledger->info().hash;

    ledger->setImmutable(app_.config(), false);
    auto start = std::chrono::system_clock::now();

    auto numFlushed = ledger->stateMap().flushDirty(hotACCOUNT_NODE);

    auto numTxFlushed = ledger->txMap().flushDirty(hotTRANSACTION_NODE);

    {
        Serializer s(128);
        s.add32(HashPrefix::ledgerMaster);
        addRaw(ledger->info(), s);
        app_.getNodeStore().store(
            hotLEDGER,
            std::move(s.modData()),
            ledger->info().hash,
            ledger->info().seq);
    }

    app_.getNodeStore().sync();

    auto end = std::chrono::system_clock::now();

    JLOG(journal_.debug()) << __func__ << " : "
                           << "Flushed " << numFlushed
                           << " nodes to nodestore from stateMap";
    JLOG(journal_.debug()) << __func__ << " : "
                           << "Flushed " << numTxFlushed
                           << " nodes to nodestore from txMap";

    JLOG(journal_.debug()) << __func__ << " : "
                           << "Flush took "
                           << (end - start).count() / 1000000000.0
                           << " seconds";

    if (numFlushed == 0)
    {
        JLOG(journal_.fatal()) << __func__ << " : "
                               << "Flushed 0 nodes from state map";
        assert(false);
    }
    if (numTxFlushed == 0)
    {
        JLOG(journal_.warn()) << __func__ << " : "
                              << "Flushed 0 nodes from tx map";
    }

    // Make sure calculated hashes are correct
    if (ledger->stateMap().getHash().as_uint256() != accountHash)
    {
        JLOG(journal_.fatal())
            << __func__ << " : "
            << "State map hash does not match. "
            << "Expected hash = " << strHex(accountHash) << "Actual hash = "
            << strHex(ledger->stateMap().getHash().as_uint256());
        Throw<std::runtime_error>("state map hash mismatch");
    }

    if (ledger->txMap().getHash().as_uint256() != txHash)
    {
        JLOG(journal_.fatal())
            << __func__ << " : "
            << "Tx map hash does not match. "
            << "Expected hash = " << strHex(txHash) << "Actual hash = "
            << strHex(ledger->txMap().getHash().as_uint256());
        Throw<std::runtime_error>("tx map hash mismatch");
    }

    if (ledger->info().hash != ledgerHash)
    {
        JLOG(journal_.fatal())
            << __func__ << " : "
            << "Ledger hash does not match. "
            << "Expected hash = " << strHex(ledgerHash)
            << "Actual hash = " << strHex(ledger->info().hash);
        Throw<std::runtime_error>("ledger hash mismatch");
    }

    JLOG(journal_.info()) << __func__ << " : "
                          << "Successfully flushed ledger! "
                          << detail::toString(ledger->info());
}

void
ReportingETL::publishLedger(std::shared_ptr<Ledger>& ledger)
{
    app_.getOPs().pubLedger(ledger);

    setLastPublish();
}

bool
ReportingETL::publishLedger(uint32_t ledgerSequence, uint32_t maxAttempts)
{
    JLOG(journal_.info()) << __func__ << " : "
                          << "Attempting to publish ledger = "
                          << ledgerSequence;
    size_t numAttempts = 0;
    while (!stopping_)
    {
        auto ledger = app_.getLedgerMaster().getLedgerBySeq(ledgerSequence);

        if (!ledger)
        {
            JLOG(journal_.warn())
                << __func__ << " : "
                << "Trying to publish. Could not find ledger with sequence = "
                << ledgerSequence;
            // We try maxAttempts times to publish the ledger, waiting one
            // second in between each attempt.
            // If the ledger is not present in the database after maxAttempts,
            // we attempt to take over as the writer. If the takeover fails,
            // doContinuousETL will return, and this node will go back to
            // publishing.
            // If the node is in strict read only mode, we simply
            // skip publishing this ledger and return false indicating the
            // publish failed
            if (numAttempts >= maxAttempts)
            {
                JLOG(journal_.error()) << __func__ << " : "
                                       << "Failed to publish ledger after "
                                       << numAttempts << " attempts.";
                if (!readOnly_)
                {
                    JLOG(journal_.info()) << __func__ << " : "
                                          << "Attempting to become ETL writer";
                    return false;
                }
                else
                {
                    JLOG(journal_.debug())
                        << __func__ << " : "
                        << "In strict read-only mode. "
                        << "Skipping publishing this ledger. "
                        << "Beginning fast forward.";
                    return false;
                }
            }
            else
            {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                ++numAttempts;
            }
            continue;
        }

        publishStrand_.post([this, ledger, fname = __func__]() {
            app_.getOPs().pubLedger(ledger);
            setLastPublish();
            JLOG(journal_.info())
                << fname << " : "
                << "Published ledger. " << detail::toString(ledger->info());
        });
        return true;
    }
    return false;
}

std::optional<org::xrpl::rpc::v1::GetLedgerResponse>
ReportingETL::fetchLedgerData(uint32_t idx)
{
    JLOG(journal_.debug()) << __func__ << " : "
                           << "Attempting to fetch ledger with sequence = "
                           << idx;

    std::optional<org::xrpl::rpc::v1::GetLedgerResponse> response =
        loadBalancer_.fetchLedger(idx, false);
    JLOG(journal_.trace()) << __func__ << " : "
                           << "GetLedger reply = " << response->DebugString();
    return response;
}

std::optional<org::xrpl::rpc::v1::GetLedgerResponse>
ReportingETL::fetchLedgerDataAndDiff(uint32_t idx)
{
    JLOG(journal_.debug()) << __func__ << " : "
                           << "Attempting to fetch ledger with sequence = "
                           << idx;

    std::optional<org::xrpl::rpc::v1::GetLedgerResponse> response =
        loadBalancer_.fetchLedger(idx, true);
    JLOG(journal_.trace()) << __func__ << " : "
                           << "GetLedger reply = " << response->DebugString();
    return response;
}

std::pair<std::shared_ptr<Ledger>, std::vector<AccountTransactionsData>>
ReportingETL::buildNextLedger(
    std::shared_ptr<Ledger>& next,
    org::xrpl::rpc::v1::GetLedgerResponse& rawData)
{
    JLOG(journal_.info()) << __func__ << " : "
                          << "Beginning ledger update";

    LedgerInfo lgrInfo =
        deserializeHeader(makeSlice(rawData.ledger_header()), true);

    JLOG(journal_.debug()) << __func__ << " : "
                           << "Deserialized ledger header. "
                           << detail::toString(lgrInfo);

    next->setLedgerInfo(lgrInfo);

    next->stateMap().clearSynching();
    next->txMap().clearSynching();

    std::vector<AccountTransactionsData> accountTxData{
        insertTransactions(next, rawData)};

    JLOG(journal_.debug())
        << __func__ << " : "
        << "Inserted all transactions. Number of transactions  = "
        << rawData.transactions_list().transactions_size();

    for (auto& obj : rawData.ledger_objects().objects())
    {
        auto key = uint256::fromVoid(obj.key().data());
        auto& data = obj.data();

        // indicates object was deleted
        if (data.size() == 0)
        {
            JLOG(journal_.trace()) << __func__ << " : "
                                   << "Erasing object = " << key;
            if (next->exists(key))
                next->rawErase(key);
        }
        else
        {
            SerialIter it{data.data(), data.size()};
            std::shared_ptr<SLE> sle = std::make_shared<SLE>(it, key);

            if (next->exists(key))
            {
                JLOG(journal_.trace()) << __func__ << " : "
                                       << "Replacing object = " << key;
                next->rawReplace(sle);
            }
            else
            {
                JLOG(journal_.trace()) << __func__ << " : "
                                       << "Inserting object = " << key;
                next->rawInsert(sle);
            }
        }
    }
    JLOG(journal_.debug())
        << __func__ << " : "
        << "Inserted/modified/deleted all objects. Number of objects = "
        << rawData.ledger_objects().objects_size();

    if (!rawData.skiplist_included())
    {
        next->updateSkipList();
        JLOG(journal_.warn())
            << __func__ << " : "
            << "tx process is not sending skiplist. This indicates that the tx "
               "process is parsing metadata instead of doing a SHAMap diff. "
               "Make sure tx process is running the same code as reporting to "
               "use SHAMap diff instead of parsing metadata";
    }

    JLOG(journal_.debug()) << __func__ << " : "
                           << "Finished ledger update. "
                           << detail::toString(next->info());
    return {std::move(next), std::move(accountTxData)};
}

// Database must be populated when this starts
std::optional<uint32_t>
ReportingETL::runETLPipeline(uint32_t startSequence)
{
    /*
     * Behold, mortals! This function spawns three separate threads, which talk
     * to each other via 2 different thread safe queues and 1 atomic variable.
     * All threads and queues are function local. This function returns when all
     * of the threads exit. There are two termination conditions: the first is
     * if the load thread encounters a write conflict. In this case, the load
     * thread sets writeConflict, an atomic bool, to true, which signals the
     * other threads to stop. The second termination condition is when the
     * entire server is shutting down, which is detected in one of three ways:
     * 1. isStopping() returns true if the server is shutting down
     * 2. networkValidatedLedgers_.waitUntilValidatedByNetwork returns
     * false, signaling the wait was aborted.
     * 3. fetchLedgerDataAndDiff returns an empty optional, signaling the fetch
     * was aborted.
     * In all cases, the extract thread detects this condition,
     * and pushes an empty optional onto the transform queue. The transform
     * thread, upon popping an empty optional, pushes an empty optional onto the
     * load queue, and then returns. The load thread, upon popping an empty
     * optional, returns.
     */

    JLOG(journal_.debug()) << __func__ << " : "
                           << "Starting etl pipeline";
    writing_ = true;

    std::shared_ptr<Ledger> parent = std::const_pointer_cast<Ledger>(
        app_.getLedgerMaster().getLedgerBySeq(startSequence - 1));
    if (!parent)
    {
        assert(false);
        Throw<std::runtime_error>("runETLPipeline: parent ledger is null");
    }

    std::atomic_bool writeConflict = false;
    std::optional<uint32_t> lastPublishedSequence;
    constexpr uint32_t maxQueueSize = 1000;

    ThreadSafeQueue<std::optional<org::xrpl::rpc::v1::GetLedgerResponse>>
        transformQueue{maxQueueSize};

    std::thread extracter{[this,
                           &startSequence,
                           &writeConflict,
                           &transformQueue]() {
        beast::setCurrentThreadName("rippled: ReportingETL extract");
        uint32_t currentSequence = startSequence;

        // there are two stopping conditions here.
        // First, if there is a write conflict in the load thread, the ETL
        // mechanism should stop.
        // The other stopping condition is if the entire server is shutting
        // down. This can be detected in a variety of ways. See the comment
        // at the top of the function
        while (networkValidatedLedgers_.waitUntilValidatedByNetwork(
                   currentSequence) &&
               !writeConflict && !isStopping())
        {
            auto start = std::chrono::system_clock::now();
            std::optional<org::xrpl::rpc::v1::GetLedgerResponse> fetchResponse{
                fetchLedgerDataAndDiff(currentSequence)};
            auto end = std::chrono::system_clock::now();

            auto time = ((end - start).count()) / 1000000000.0;
            auto tps =
                fetchResponse->transactions_list().transactions_size() / time;

            JLOG(journal_.debug()) << "Extract phase time = " << time
                                   << " . Extract phase tps = " << tps;
            // if the fetch is unsuccessful, stop. fetchLedger only returns
            // false if the server is shutting down, or if the ledger was
            // found in the database (which means another process already
            // wrote the ledger that this process was trying to extract;
            // this is a form of a write conflict). Otherwise,
            // fetchLedgerDataAndDiff will keep trying to fetch the
            // specified ledger until successful
            if (!fetchResponse)
            {
                break;
            }

            transformQueue.push(std::move(fetchResponse));
            ++currentSequence;
        }
        // empty optional tells the transformer to shut down
        transformQueue.push({});
    }};

    ThreadSafeQueue<std::optional<std::pair<
        std::shared_ptr<Ledger>,
        std::vector<AccountTransactionsData>>>>
        loadQueue{maxQueueSize};
    std::thread transformer{[this,
                             &parent,
                             &writeConflict,
                             &loadQueue,
                             &transformQueue]() {
        beast::setCurrentThreadName("rippled: ReportingETL transform");

        assert(parent);
        parent = std::make_shared<Ledger>(*parent, NetClock::time_point{});
        while (!writeConflict)
        {
            std::optional<org::xrpl::rpc::v1::GetLedgerResponse> fetchResponse{
                transformQueue.pop()};
            // if fetchResponse is an empty optional, the extracter thread has
            // stopped and the transformer should stop as well
            if (!fetchResponse)
            {
                break;
            }
            if (isStopping())
                continue;

            auto start = std::chrono::system_clock::now();
            auto [next, accountTxData] =
                buildNextLedger(parent, *fetchResponse);
            auto end = std::chrono::system_clock::now();

            auto duration = ((end - start).count()) / 1000000000.0;
            JLOG(journal_.debug()) << "transform time = " << duration;
            // The below line needs to execute before pushing to the queue, in
            // order to prevent this thread and the loader thread from accessing
            // the same SHAMap concurrently
            parent = std::make_shared<Ledger>(*next, NetClock::time_point{});
            loadQueue.push(
                std::make_pair(std::move(next), std::move(accountTxData)));
        }
        // empty optional tells the loader to shutdown
        loadQueue.push({});
    }};

    std::thread loader{
        [this, &lastPublishedSequence, &loadQueue, &writeConflict]() {
            beast::setCurrentThreadName("rippled: ReportingETL load");
            size_t totalTransactions = 0;
            double totalTime = 0;
            while (!writeConflict)
            {
                std::optional<std::pair<
                    std::shared_ptr<Ledger>,
                    std::vector<AccountTransactionsData>>>
                    result{loadQueue.pop()};
                // if result is an empty optional, the transformer thread has
                // stopped and the loader should stop as well
                if (!result)
                    break;
                if (isStopping())
                    continue;

                auto& ledger = result->first;
                auto& accountTxData = result->second;

                auto start = std::chrono::system_clock::now();
                // write to the key-value store
                flushLedger(ledger);

                auto mid = std::chrono::system_clock::now();
            // write to RDBMS
            // if there is a write conflict, some other process has already
            // written this ledger and has taken over as the ETL writer
#ifdef RIPPLED_REPORTING
                if (!dynamic_cast<RelationalDBInterfacePostgres*>(
                         &app_.getRelationalDBInterface())
                         ->writeLedgerAndTransactions(
                             ledger->info(), accountTxData))
                    writeConflict = true;
#endif
                auto end = std::chrono::system_clock::now();

                if (!writeConflict)
                {
                    publishLedger(ledger);
                    lastPublishedSequence = ledger->info().seq;
                }
                // print some performance numbers
                auto kvTime = ((mid - start).count()) / 1000000000.0;
                auto relationalTime = ((end - mid).count()) / 1000000000.0;

                size_t numTxns = accountTxData.size();
                totalTime += kvTime;
                totalTransactions += numTxns;
                JLOG(journal_.info())
                    << "Load phase of etl : "
                    << "Successfully published ledger! Ledger info: "
                    << detail::toString(ledger->info())
                    << ". txn count = " << numTxns
                    << ". key-value write time = " << kvTime
                    << ". relational write time = " << relationalTime
                    << ". key-value tps = " << numTxns / kvTime
                    << ". relational tps = " << numTxns / relationalTime
                    << ". total key-value tps = "
                    << totalTransactions / totalTime;
            }
        }};

    // wait for all of the threads to stop
    loader.join();
    extracter.join();
    transformer.join();
    writing_ = false;

    JLOG(journal_.debug()) << __func__ << " : "
                           << "Stopping etl pipeline";

    return lastPublishedSequence;
}

// main loop. The software begins monitoring the ledgers that are validated
// by the nework. The member networkValidatedLedgers_ keeps track of the
// sequences of ledgers validated by the network. Whenever a ledger is validated
// by the network, the software looks for that ledger in the database. Once the
// ledger is found in the database, the software publishes that ledger to the
// ledgers stream. If a network validated ledger is not found in the database
// after a certain amount of time, then the software attempts to take over
// responsibility of the ETL process, where it writes new ledgers to the
// database. The software will relinquish control of the ETL process if it
// detects that another process has taken over ETL.
void
ReportingETL::monitor()
{
    auto ledger = std::const_pointer_cast<Ledger>(
        app_.getLedgerMaster().getValidatedLedger());
    if (!ledger)
    {
        JLOG(journal_.info()) << __func__ << " : "
                              << "Database is empty. Will download a ledger "
                                 "from the network.";
        if (startSequence_)
        {
            JLOG(journal_.info())
                << __func__ << " : "
                << "ledger sequence specified in config. "
                << "Will begin ETL process starting with ledger "
                << *startSequence_;
            ledger = loadInitialLedger(*startSequence_);
        }
        else
        {
            JLOG(journal_.info())
                << __func__ << " : "
                << "Waiting for next ledger to be validated by network...";
            std::optional<uint32_t> mostRecentValidated =
                networkValidatedLedgers_.getMostRecent();
            if (mostRecentValidated)
            {
                JLOG(journal_.info()) << __func__ << " : "
                                      << "Ledger " << *mostRecentValidated
                                      << " has been validated. "
                                      << "Downloading...";
                ledger = loadInitialLedger(*mostRecentValidated);
            }
            else
            {
                JLOG(journal_.info()) << __func__ << " : "
                                      << "The wait for the next validated "
                                      << "ledger has been aborted. "
                                      << "Exiting monitor loop";
                return;
            }
        }
    }
    else
    {
        if (startSequence_)
        {
            Throw<std::runtime_error>(
                "start sequence specified but db is already populated");
        }
        JLOG(journal_.info())
            << __func__ << " : "
            << "Database already populated. Picking up from the tip of history";
    }
    if (!ledger)
    {
        JLOG(journal_.error())
            << __func__ << " : "
            << "Failed to load initial ledger. Exiting monitor loop";
        return;
    }
    else
    {
        publishLedger(ledger);
    }
    uint32_t nextSequence = ledger->info().seq + 1;

    JLOG(journal_.debug()) << __func__ << " : "
                           << "Database is populated. "
                           << "Starting monitor loop. sequence = "
                           << nextSequence;
    while (!stopping_ &&
           networkValidatedLedgers_.waitUntilValidatedByNetwork(nextSequence))
    {
        JLOG(journal_.info()) << __func__ << " : "
                              << "Ledger with sequence = " << nextSequence
                              << " has been validated by the network. "
                              << "Attempting to find in database and publish";
        // Attempt to take over responsibility of ETL writer after 10 failed
        // attempts to publish the ledger. publishLedger() fails if the
        // ledger that has been validated by the network is not found in the
        // database after the specified number of attempts. publishLedger()
        // waits one second between each attempt to read the ledger from the
        // database
        //
        // In strict read-only mode, when the software fails to find a
        // ledger in the database that has been validated by the network,
        // the software will only try to publish subsequent ledgers once,
        // until one of those ledgers is found in the database. Once the
        // software successfully publishes a ledger, the software will fall
        // back to the normal behavior of trying several times to publish
        // the ledger that has been validated by the network. In this
        // manner, a reporting processing running in read-only mode does not
        // need to restart if the database is wiped.
        constexpr size_t timeoutSeconds = 10;
        bool success = publishLedger(nextSequence, timeoutSeconds);
        if (!success)
        {
            JLOG(journal_.warn())
                << __func__ << " : "
                << "Failed to publish ledger with sequence = " << nextSequence
                << " . Beginning ETL";
            // doContinousETLPipelined returns the most recent sequence
            // published empty optional if no sequence was published
            std::optional<uint32_t> lastPublished =
                runETLPipeline(nextSequence);
            JLOG(journal_.info()) << __func__ << " : "
                                  << "Aborting ETL. Falling back to publishing";
            // if no ledger was published, don't increment nextSequence
            if (lastPublished)
                nextSequence = *lastPublished + 1;
        }
        else
        {
            ++nextSequence;
        }
    }
}

void
ReportingETL::monitorReadOnly()
{
    JLOG(journal_.debug()) << "Starting reporting in strict read only mode";
    std::optional<uint32_t> mostRecent =
        networkValidatedLedgers_.getMostRecent();
    if (!mostRecent)
        return;
    uint32_t sequence = *mostRecent;
    bool success = true;
    while (!stopping_ &&
           networkValidatedLedgers_.waitUntilValidatedByNetwork(sequence))
    {
        success = publishLedger(sequence, success ? 30 : 1);
        ++sequence;
    }
}

void
ReportingETL::doWork()
{
    worker_ = std::thread([this]() {
        beast::setCurrentThreadName("rippled: ReportingETL worker");
        if (readOnly_)
            monitorReadOnly();
        else
            monitor();
    });
}

ReportingETL::ReportingETL(Application& app)
    : app_(app)
    , journal_(app.journal("ReportingETL"))
    , publishStrand_(app_.getIOService())
    , loadBalancer_(*this)
{
    // if present, get endpoint from config
    if (app_.config().exists("reporting"))
    {
#ifndef RIPPLED_REPORTING
        Throw<std::runtime_error>(
            "Config file specifies reporting, but software was not built with "
            "-Dreporting=1. To use reporting, configure CMake with "
            "-Dreporting=1");
#endif
        if (!app_.config().useTxTables())
            Throw<std::runtime_error>(
                "Reporting requires tx tables. Set use_tx_tables=1 in config "
                "file, under [ledger_tx_tables] section");
        Section section = app_.config().section("reporting");

        JLOG(journal_.debug()) << "Parsing config info";

        auto& vals = section.values();
        for (auto& v : vals)
        {
            JLOG(journal_.debug()) << "val is " << v;
            Section source = app_.config().section(v);

            auto optIp = source.get("source_ip");
            if (!optIp)
                continue;

            auto optWsPort = source.get("source_ws_port");
            if (!optWsPort)
                continue;

            auto optGrpcPort = source.get("source_grpc_port");
            if (!optGrpcPort)
            {
                // add source without grpc port
                // used in read-only mode to detect when new ledgers have
                // been validated. Used for publishing
                if (app_.config().reportingReadOnly())
                    loadBalancer_.add(*optIp, *optWsPort);
                continue;
            }

            loadBalancer_.add(*optIp, *optWsPort, *optGrpcPort);
        }

        // this is true iff --reportingReadOnly was passed via command line
        readOnly_ = app_.config().reportingReadOnly();

        // if --reportingReadOnly was not passed via command line, check config
        // file. Command line takes precedence
        if (!readOnly_)
        {
            auto const optRO = section.get("read_only");
            if (optRO)
            {
                readOnly_ = (*optRO == "true" || *optRO == "1");
                app_.config().setReportingReadOnly(readOnly_);
            }
        }

        // lambda throws a useful message if string to integer conversion fails
        auto asciiToIntThrows =
            [](auto& dest, std::string const& src, char const* onError) {
                char const* const srcEnd = src.data() + src.size();
                auto [ptr, err] = std::from_chars(src.data(), srcEnd, dest);

                if (err == std::errc())
                    // skip whitespace at end of string
                    while (ptr != srcEnd &&
                           std::isspace(static_cast<unsigned char>(*ptr)))
                        ++ptr;

                // throw if
                //  o conversion error or
                //  o entire string is not consumed
                if (err != std::errc() || ptr != srcEnd)
                    Throw<std::runtime_error>(onError + src);
            };

        // handle command line arguments
        if (app_.config().START_UP == Config::StartUpType::FRESH && !readOnly_)
        {
            asciiToIntThrows(
                *startSequence_,
                app_.config().START_LEDGER,
                "Expected integral START_LEDGER command line argument. Got: ");
        }
        // if not passed via command line, check config for start sequence
        if (!startSequence_)
        {
            auto const optStartSeq = section.get("start_sequence");
            if (optStartSeq)
                asciiToIntThrows(
                    *startSequence_,
                    *optStartSeq,
                    "Expected integral start_sequence config entry. Got: ");
        }

        auto const optFlushInterval = section.get("flush_interval");
        if (optFlushInterval)
            asciiToIntThrows(
                flushInterval_,
                *optFlushInterval,
                "Expected integral flush_interval config entry.  Got: ");

        auto const optNumMarkers = section.get("num_markers");
        if (optNumMarkers)
            asciiToIntThrows(
                numMarkers_,
                *optNumMarkers,
                "Expected integral num_markers config entry.  Got: ");
    }
}

}  // namespace ripple
