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
#include <ripple/app/ledger/InboundLedgers.h>
#include <ripple/app/ledger/Ledger.h>
#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/ledger/impl/LedgerCleaner.h>
#include <ripple/app/main/Application.h>
#include <ripple/core/LoadFeeTrack.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/protocol/Protocol.h>
#include <ripple/protocol/RippleLedgerHash.h>
#include <beast/threads/Thread.h>
#include <beast/cxx14/memory.h> // <memory>
#include <thread>

namespace ripple {

/*

LedgerCleaner

Cleans up the ledger. Specifically, resolves these issues:

1. Older versions could leave the SQLite account and transaction databases in
   an inconsistent state. The cleaner identifies these inconsistencies and
   resolves them.

2. Upon request, checks for missing nodes in a ledger and triggers a fetch.

*/

class LedgerCleanerImp
    : public LedgerCleaner
    , public beast::Thread
{
public:
    struct State
    {
        State()
            : minRange (0)
            , maxRange (0)
            , checkNodes (false)
            , fixTxns (false)
            , failures (0)
        {
        }

        // The lowest ledger in the range we're checking.
        LedgerIndex  minRange;

        // The highest ledger in the range we're checking
        LedgerIndex  maxRange;

        // Check all state/transaction nodes
        bool checkNodes;

        // Rewrite SQL databases
        bool fixTxns;

        // Number of errors encountered since last success
        int failures;
    };

    using SharedState = beast::SharedData <State>;

    Application& app_;
    SharedState m_state;
    beast::Journal m_journal;

    //--------------------------------------------------------------------------

    LedgerCleanerImp (
        Application& app,
        Stoppable& stoppable,
        beast::Journal journal)
        : LedgerCleaner (stoppable)
        , Thread ("LedgerCleaner")
        , app_ (app)
        , m_journal (journal)
    {
    }

    ~LedgerCleanerImp ()
    {
        stopThread ();
    }

    //--------------------------------------------------------------------------
    //
    // Stoppable
    //
    //--------------------------------------------------------------------------

    void onPrepare ()
    {
    }

    void onStart ()
    {
        startThread();
    }

    void onStop ()
    {
        m_journal.info << "Stopping";
        signalThreadShouldExit();
        notify();
    }

    //--------------------------------------------------------------------------
    //
    // PropertyStream
    //
    //--------------------------------------------------------------------------

    void onWrite (beast::PropertyStream::Map& map)
    {
        SharedState::Access state (m_state);

        if (state->maxRange == 0)
            map["status"] = "idle";
        else
        {
            map["status"] = "running";
            map["min_ledger"] = state->minRange;
            map["max_ledger"] = state->maxRange;
            map["check_nodes"] = state->checkNodes ? "true" : "false";
            map["fix_txns"] = state->fixTxns ? "true" : "false";
            if (state->failures > 0)
                map["fail_counts"] = state->failures;
        }
    }

    //--------------------------------------------------------------------------
    //
    // LedgerCleaner
    //
    //--------------------------------------------------------------------------

    void doClean (Json::Value const& params)
    {
        LedgerIndex minRange;
        LedgerIndex maxRange;
        app_.getLedgerMaster().getFullValidatedRange (minRange, maxRange);

        {
            SharedState::Access state (m_state);

            state->maxRange = maxRange;
            state->minRange = minRange;
            state->checkNodes = false;
            state->fixTxns = false;
            state->failures = 0;

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
                state->maxRange = params[jss::ledger].asUInt();
                state->minRange = params[jss::ledger].asUInt();
                state->fixTxns = true;
                state->checkNodes = true;
            }

            if (params.isMember(jss::max_ledger))
                 state->maxRange = params[jss::max_ledger].asUInt();

            if (params.isMember(jss::min_ledger))
                state->minRange = params[jss::min_ledger].asUInt();

            if (params.isMember(jss::full))
                state->fixTxns = state->checkNodes = params[jss::full].asBool();

            if (params.isMember(jss::fix_txns))
                state->fixTxns = params[jss::fix_txns].asBool();

            if (params.isMember(jss::check_nodes))
                state->checkNodes = params[jss::check_nodes].asBool();

            if (params.isMember(jss::stop) && params[jss::stop].asBool())
                state->minRange = state->maxRange = 0;
        }

        notify();
    }

    //--------------------------------------------------------------------------
    //
    // LedgerCleanerImp
    //
    //--------------------------------------------------------------------------

    void init ()
    {
        m_journal.debug << "Initializing";
    }

    void run ()
    {
        m_journal.debug << "Started";

        init ();

        while (! this->threadShouldExit())
        {
            this->wait ();
            if (! this->threadShouldExit())
            {
                doLedgerCleaner();
            }
        }

        stopped();
    }

    // VFALCO TODO This should return boost::optional<uint256>
    LedgerHash getLedgerHash(Ledger::pointer ledger, LedgerIndex index)
    {
        boost::optional<LedgerHash> hash;
        try
        {
            hash = hashOfSeq(*ledger, index, m_journal);
        }
        catch (SHAMapMissingNode &)
        {
            m_journal.warning <<
                "Node missing from ledger " << ledger->info().seq;
            app_.getInboundLedgers().acquire (
                ledger->getHash(), ledger->info().seq,
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
        Ledger::pointer nodeLedger =
            app_.getInboundLedgers().acquire (
                ledgerHash, ledgerIndex, InboundLedger::fcGENERIC);
        if (!nodeLedger)
        {
            m_journal.debug << "Ledger " << ledgerIndex << " not available";
            return false;
        }

        Ledger::pointer dbLedger = loadByIndex(ledgerIndex, app_);
        if (! dbLedger ||
            (dbLedger->getHash() != ledgerHash) ||
            (dbLedger->info().parentHash != nodeLedger->info().parentHash))
        {
            // Ideally we'd also check for more than one ledger with that index
            m_journal.debug <<
                "Ledger " << ledgerIndex << " mismatches SQL DB";
            doTxns = true;
        }

        if(! app_.getLedgerMaster().fixIndex(ledgerIndex, ledgerHash))
        {
            m_journal.debug << "ledger " << ledgerIndex
                            << " had wrong entry in history";
            doTxns = true;
        }

        if (doNodes && !nodeLedger->walkLedger())
        {
            m_journal.debug << "Ledger " << ledgerIndex << " is missing nodes";
            app_.getInboundLedgers().acquire(
                ledgerHash, ledgerIndex, InboundLedger::fcGENERIC);
            return false;
        }

        if (doTxns && !pendSaveValidated(app_, nodeLedger, true, false))
        {
            m_journal.debug << "Failed to save ledger " << ledgerIndex;
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
        Ledger::pointer& referenceLedger)
    {
        LedgerHash ledgerHash;

        if (!referenceLedger || (referenceLedger->info().seq < ledgerIndex))
        {
            referenceLedger = app_.getLedgerMaster().getValidatedLedger();
            if (!referenceLedger)
            {
                m_journal.warning << "No validated ledger";
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
            m_journal.warning << "Validated ledger is prior to target ledger";

        return ledgerHash;
    }

    /** Run the ledger cleaner. */
    void doLedgerCleaner()
    {
        Ledger::pointer goodLedger;

        while (! this->threadShouldExit())
        {
            LedgerIndex ledgerIndex;
            LedgerHash ledgerHash;
            bool doNodes;
            bool doTxns;

            while (app_.getFeeTrack().isLoadedLocal())
            {
                m_journal.debug << "Waiting for load to subside";
                std::this_thread::sleep_for(std::chrono::seconds(5));
                if (this->threadShouldExit ())
                    return;
            }

            {
                SharedState::Access state (m_state);
                if ((state->minRange > state->maxRange) ||
                    (state->maxRange == 0) || (state->minRange == 0))
                {
                    state->minRange = state->maxRange = 0;
                    return;
                }
                ledgerIndex = state->maxRange;
                doNodes = state->checkNodes;
                doTxns = state->fixTxns;
            }

            ledgerHash = getHash(ledgerIndex, goodLedger);

            bool fail = false;
            if (ledgerHash.isZero())
            {
                m_journal.info << "Unable to get hash for ledger "
                               << ledgerIndex;
                fail = true;
            }
            else if (!doLedger(ledgerIndex, ledgerHash, doNodes, doTxns))
            {
                m_journal.info << "Failed to process ledger " << ledgerIndex;
                fail = true;
            }

            if (fail)
            {
                {
                    SharedState::Access state (m_state);
                    ++state->failures;
                }
                // Wait for acquiring to catch up to us
                std::this_thread::sleep_for(std::chrono::seconds(2));
            }
            else
            {
                {
                    SharedState::Access state (m_state);
                    if (ledgerIndex == state->minRange)
                        ++state->minRange;
                    if (ledgerIndex == state->maxRange)
                        --state->maxRange;
                    state->failures = 0;
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
    beast::Stoppable& parent, beast::Journal journal)
{
    return std::make_unique<LedgerCleanerImp>(app, parent, journal);
}

} // ripple
