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

#include <BeastConfig.h>
#include <ripple/app/ledger/LedgerCleaner.h>
#include <ripple/app/ledger/InboundLedgers.h>
#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/misc/LoadFeeTrack.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/beast/core/CurrentThreadName.h>

namespace ripple {
namespace detail {

/*

LedgerCleaner

Cleans up the ledger. Specifically, resolves these issues:

1. Older versions could leave the SQLite account and transaction databases in
   an inconsistent state. The cleaner identifies these inconsistencies and
   resolves them.

2. Upon request, checks for missing nodes in a ledger and triggers a fetch.

*/

class LedgerCleanerImp : public LedgerCleaner
{
    Application& app_;
    beast::Journal j_;
    mutable std::mutex mutex_;

    mutable std::condition_variable wakeup_;

    std::thread thread_;

    enum class State : char {
        readyToClean = 0,
        startCleaning,
        cleaning
    };
    State state_ = State::readyToClean;
    bool shouldExit_ = false;

    // The lowest ledger in the range we're checking.
    LedgerIndex  minRange_ = 0;

    // The highest ledger in the range we're checking
    LedgerIndex  maxRange_ = 0;

    // Check all state/transaction nodes
    bool checkNodes_ = false;

    // Rewrite SQL databases
    bool fixTxns_ = false;

    // Number of errors encountered since last success
    int failures_ = 0;

    //--------------------------------------------------------------------------
public:
    LedgerCleanerImp (
        Application& app,
        Stoppable& stoppable,
        beast::Journal journal)
        : LedgerCleaner (stoppable)
        , app_ (app)
        , j_ (journal)
    {
    }

    ~LedgerCleanerImp () override
    {
        if (thread_.joinable())
            LogicError ("LedgerCleanerImp::onStop not called.");
    }

    //--------------------------------------------------------------------------
    //
    // Stoppable
    //
    //--------------------------------------------------------------------------

    void onPrepare () override
    {
    }

    void onStart () override
    {
        thread_ = std::thread {&LedgerCleanerImp::run, this};
    }

    void onStop () override
    {
        JLOG (j_.info()) << "Stopping";
        {
            std::lock_guard<std::mutex> lock (mutex_);
            shouldExit_ = true;
            wakeup_.notify_one();
        }
        thread_.join();
    }

    //--------------------------------------------------------------------------
    //
    // PropertyStream
    //
    //--------------------------------------------------------------------------

    void onWrite (beast::PropertyStream::Map& map) override
    {
        std::lock_guard<std::mutex> lock (mutex_);

        if (maxRange_ == 0)
            map["status"] = "idle";
        else
        {
            map["status"] = "running";
            map["min_ledger"] = minRange_;
            map["max_ledger"] = maxRange_;
            map["check_nodes"] = checkNodes_ ? "true" : "false";
            map["fix_txns"] = fixTxns_ ? "true" : "false";
            if (failures_ > 0)
                map["fail_counts"] = failures_;
        }
    }

    //--------------------------------------------------------------------------
    //
    // LedgerCleaner
    //
    //--------------------------------------------------------------------------

    void doClean (Json::Value const& params) override
    {
        LedgerIndex minRange;
        LedgerIndex maxRange;
        app_.getLedgerMaster().getFullValidatedRange (minRange, maxRange);

        {
            std::lock_guard<std::mutex> lock (mutex_);

            maxRange_ = maxRange;
            minRange_ = minRange;
            checkNodes_ = false;
            fixTxns_ = false;
            failures_ = 0;

            /*
            JSON Parameters:

                All parameters are optional. By default the cleaner cleans
                things it thinks are necessary. This behavior can be modified
                using the following options supplied via JSON RPC:

                "ledger"
                    A single unsigned integer representing an individual
                    ledger to clean.

                "min_ledger", "max_ledger"
                    Unsigned integers representing the starting and ending
                    ledger numbers to clean. If unspecified, clean all ledgers.

                "full"
                    A boolean. When true, means clean everything possible.

                "fix_txns"
                    A boolean value indicating whether or not to fix the
                    transactions in the database as well.

                "check_nodes"
                    A boolean, when set to true means check the nodes.

                "stop"
                    A boolean, when true informs the cleaner to gracefully
                    stop its current activities if any cleaning is taking place.
            */

            // Quick way to fix a single ledger
            if (params.isMember(jss::ledger))
            {
                maxRange_ = params[jss::ledger].asUInt();
                minRange_ = params[jss::ledger].asUInt();
                fixTxns_ = true;
                checkNodes_ = true;
            }

            if (params.isMember(jss::max_ledger))
                 maxRange_ = params[jss::max_ledger].asUInt();

            if (params.isMember(jss::min_ledger))
                minRange_ = params[jss::min_ledger].asUInt();

            if (params.isMember(jss::full))
                fixTxns_ = checkNodes_ = params[jss::full].asBool();

            if (params.isMember(jss::fix_txns))
                fixTxns_ = params[jss::fix_txns].asBool();

            if (params.isMember(jss::check_nodes))
                checkNodes_ = params[jss::check_nodes].asBool();

            if (params.isMember(jss::stop) && params[jss::stop].asBool())
                minRange_ = maxRange_ = 0;

            if (state_ == State::readyToClean)
            {
                state_ = State::startCleaning;
                wakeup_.notify_one();
            }
        }
    }

    //--------------------------------------------------------------------------
    //
    // LedgerCleanerImp
    //
    //--------------------------------------------------------------------------
private:
    void init ()
    {
        JLOG (j_.debug()) << "Initializing";
    }

    void run ()
    {
        beast::setCurrentThreadName ("LedgerCleaner");
        JLOG (j_.debug()) << "Started";

        init();

        while (true)
        {
            {
                std::unique_lock<std::mutex> lock (mutex_);
                wakeup_.wait(lock, [this]()
                    {
                        return (
                            shouldExit_ ||
                            state_ == State::startCleaning);
                    });
                if (shouldExit_)
                    break;

                state_ = State::cleaning;
            }
            doLedgerCleaner();
        }

        stopped();
    }

    // VFALCO TODO This should return boost::optional<uint256>
    LedgerHash getLedgerHash(
        std::shared_ptr<ReadView const>& ledger,
        LedgerIndex index)
    {
        boost::optional<LedgerHash> hash;
        try
        {
            hash = hashOfSeq(*ledger, index, j_);
        }
        catch (SHAMapMissingNode &)
        {
            JLOG (j_.warn()) <<
                "Node missing from ledger " << ledger->info().seq;
            app_.getInboundLedgers().acquire (
                ledger->info().hash, ledger->info().seq,
                InboundLedger::fcGENERIC);
        }
        return hash ? *hash : zero; // kludge
    }

    /** Process a single ledger
        @param ledgerIndex The index of the ledger to process.
        @param ledgerHash  The known correct hash of the ledger.
        @param doNodes Ensure all ledger nodes are in the node db.
        @param doTxns Reprocess (account) transactions to SQL databases.
        @return `true` if the ledger was cleaned.
    */
    bool doLedger(
        LedgerIndex const& ledgerIndex,
        LedgerHash const& ledgerHash,
        bool doNodes,
        bool doTxns)
    {
        auto nodeLedger = app_.getInboundLedgers().acquire (
            ledgerHash, ledgerIndex, InboundLedger::fcGENERIC);
        if (!nodeLedger)
        {
            JLOG (j_.debug()) << "Ledger " << ledgerIndex << " not available";
            app_.getLedgerMaster().clearLedger (ledgerIndex);
            app_.getInboundLedgers().acquire(
                ledgerHash, ledgerIndex, InboundLedger::fcGENERIC);
            return false;
        }

        auto dbLedger = loadByIndex(ledgerIndex, app_);
        if (! dbLedger ||
            (dbLedger->info().hash != ledgerHash) ||
            (dbLedger->info().parentHash != nodeLedger->info().parentHash))
        {
            // Ideally we'd also check for more than one ledger with that index
            JLOG (j_.debug()) <<
                "Ledger " << ledgerIndex << " mismatches SQL DB";
            doTxns = true;
        }

        if(! app_.getLedgerMaster().fixIndex(ledgerIndex, ledgerHash))
        {
            JLOG (j_.debug()) << "ledger " << ledgerIndex
                            << " had wrong entry in history";
            doTxns = true;
        }

        if (doNodes && !nodeLedger->walkLedger(app_.journal ("Ledger")))
        {
            JLOG (j_.debug()) << "Ledger " << ledgerIndex << " is missing nodes";
            app_.getLedgerMaster().clearLedger (ledgerIndex);
            app_.getInboundLedgers().acquire(
                ledgerHash, ledgerIndex, InboundLedger::fcGENERIC);
            return false;
        }

        if (doTxns && !pendSaveValidated(app_, nodeLedger, true, false))
        {
            JLOG (j_.debug()) << "Failed to save ledger " << ledgerIndex;
            return false;
        }

        return true;
    }

    /** Returns the hash of the specified ledger.
        @param ledgerIndex The index of the desired ledger.
        @param referenceLedger [out] An optional known good subsequent ledger.
        @return The hash of the ledger. This will be all-bits-zero if not found.
    */
    LedgerHash getHash(
        LedgerIndex const& ledgerIndex,
        std::shared_ptr<ReadView const>& referenceLedger)
    {
        LedgerHash ledgerHash;

        if (!referenceLedger || (referenceLedger->info().seq < ledgerIndex))
        {
            referenceLedger = app_.getLedgerMaster().getValidatedLedger();
            if (!referenceLedger)
            {
                JLOG (j_.warn()) << "No validated ledger";
                return ledgerHash; // Nothing we can do. No validated ledger.
            }
        }

        if (referenceLedger->info().seq >= ledgerIndex)
        {
            // See if the hash for the ledger we need is in the reference ledger
            ledgerHash = getLedgerHash(referenceLedger, ledgerIndex);
            if (ledgerHash.isZero())
            {
                // No. Try to get another ledger that might have the hash we
                // need: compute the index and hash of a ledger that will have
                // the hash we need.
                LedgerIndex refIndex = getCandidateLedger (ledgerIndex);
                LedgerHash refHash = getLedgerHash (referenceLedger, refIndex);

                bool const nonzero (refHash.isNonZero ());
                assert (nonzero);
                if (nonzero)
                {
                    // We found the hash and sequence of a better reference
                    // ledger.
                    referenceLedger =
                        app_.getInboundLedgers().acquire(
                            refHash, refIndex, InboundLedger::fcGENERIC);
                    if (referenceLedger)
                        ledgerHash = getLedgerHash(
                            referenceLedger, ledgerIndex);
                }
            }
        }
        else
            JLOG (j_.warn()) << "Validated ledger is prior to target ledger";

        return ledgerHash;
    }

    /** Run the ledger cleaner. */
    void doLedgerCleaner()
    {
        auto shouldExit = [this]()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return shouldExit_;
        };

        std::shared_ptr<ReadView const> goodLedger;

        while (! shouldExit())
        {
            LedgerIndex ledgerIndex;
            LedgerHash ledgerHash;
            bool doNodes;
            bool doTxns;

            while (app_.getFeeTrack().isLoadedLocal())
            {
                JLOG (j_.debug()) << "Waiting for load to subside";
                std::this_thread::sleep_for(std::chrono::seconds(5));
                if (shouldExit())
                    return;
            }

            {
                std::lock_guard<std::mutex> lock (mutex_);
                if ((minRange_ > maxRange_) ||
                    (maxRange_ == 0) || (minRange_ == 0))
                {
                    minRange_ = maxRange_ = 0;
                    state_ = State::readyToClean;
                    return;
                }
                ledgerIndex = maxRange_;
                doNodes = checkNodes_;
                doTxns = fixTxns_;
            }

            ledgerHash = getHash(ledgerIndex, goodLedger);

            bool fail = false;
            if (ledgerHash.isZero())
            {
                JLOG (j_.info()) << "Unable to get hash for ledger "
                               << ledgerIndex;
                fail = true;
            }
            else if (!doLedger(ledgerIndex, ledgerHash, doNodes, doTxns))
            {
                JLOG (j_.info()) << "Failed to process ledger " << ledgerIndex;
                fail = true;
            }

            if (fail)
            {
                {
                    std::lock_guard<std::mutex> lock (mutex_);
                    ++failures_;
                }
                // Wait for acquiring to catch up to us
                std::this_thread::sleep_for(std::chrono::seconds(2));
            }
            else
            {
                {
                    std::lock_guard<std::mutex> lock (mutex_);
                    if (ledgerIndex == minRange_)
                        ++minRange_;
                    if (ledgerIndex == maxRange_)
                        --maxRange_;
                    failures_ = 0;
                }
                // Reduce I/O pressure and wait for acquiring to catch up to us
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }

        }
    }
};

//------------------------------------------------------------------------------

LedgerCleaner::LedgerCleaner (Stoppable& parent)
    : Stoppable ("LedgerCleaner", parent)
    , beast::PropertyStream::Source ("ledgercleaner")
{
}

LedgerCleaner::~LedgerCleaner ()
{
}

std::unique_ptr<LedgerCleaner>
make_LedgerCleaner (Application& app,
    Stoppable& parent, beast::Journal journal)
{
    return std::make_unique<LedgerCleanerImp>(app, parent, journal);
}

} // detail
} // ripple
