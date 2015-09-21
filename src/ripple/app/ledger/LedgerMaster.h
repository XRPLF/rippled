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

#ifndef RIPPLE_APP_LEDGER_LEDGERMASTER_H_INCLUDED
#define RIPPLE_APP_LEDGER_LEDGERMASTER_H_INCLUDED

#include <ripple/app/main/Application.h>
#include <ripple/app/ledger/Ledger.h>
#include <ripple/app/ledger/LedgerHolder.h>
#include <ripple/basics/chrono.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/protocol/RippleLedgerHash.h>
#include <ripple/protocol/STValidation.h>
#include <beast/insight/Collector.h>
#include <beast/threads/Stoppable.h>
#include <beast/threads/UnlockGuard.h>
#include <beast/utility/PropertyStream.h>

#include "ripple.pb.h"

namespace ripple {

class Peer;

struct LedgerReplay
{
    std::map< int, std::shared_ptr<STTx const> > txns_;
    std::uint32_t closeTime_;
    int closeFlags_;
    Ledger::pointer prevLedger_;
};

// Tracks the current ledger and any ledgers in the process of closing
// Tracks ledger history
// Tracks held transactions

// VFALCO TODO Rename to Ledgers
//        It sounds like this holds all the ledgers...
//
class LedgerMaster
    : public beast::Stoppable
{
protected:
    explicit LedgerMaster (Stoppable& parent);

public:
    using callback = std::function <void (Ledger::ref)>;

public:
    using LockType = RippleRecursiveMutex;
    using ScopedLockType = std::unique_lock <LockType>;
    using ScopedUnlockType = beast::GenericScopedUnlock <LockType>;

    virtual ~LedgerMaster () = default;

    virtual LedgerIndex getCurrentLedgerIndex () = 0;
    virtual LedgerIndex getValidLedgerIndex () = 0;

    virtual bool isCompatible (Ledger::pointer,
        beast::Journal::Stream, const char* reason) = 0;

    virtual LockType& peekMutex () = 0;

    // The current ledger is the ledger we believe new transactions should go in
    virtual std::shared_ptr<ReadView const> getCurrentLedger () = 0;

    // The finalized ledger is the last closed/accepted ledger
    virtual Ledger::pointer getClosedLedger () = 0;

    // The validated ledger is the last fully validated ledger
    virtual Ledger::pointer getValidatedLedger () = 0;

    // The Rules are in the last fully validated ledger if there is one.
    virtual Rules getValidatedRules() = 0;

    // This is the last ledger we published to clients and can lag the validated
    // ledger
    virtual Ledger::ref getPublishedLedger () = 0;

    virtual bool isValidLedger(LedgerInfo const&) = 0;

    virtual int getPublishedLedgerAge () = 0;
    virtual int getValidatedLedgerAge () = 0;
    virtual bool isCaughtUp(std::string& reason) = 0;

    virtual int getMinValidations () = 0;

    virtual void setMinValidations (int v, bool strict) = 0;

    virtual std::uint32_t getEarliestFetch () = 0;

    virtual bool storeLedger (Ledger::pointer) = 0;
    virtual void forceValid (Ledger::pointer) = 0;

    virtual void setFullLedger (
        Ledger::pointer ledger, bool isSynchronous, bool isCurrent) = 0;

    virtual void switchLCL (Ledger::pointer lastClosed) = 0;

    virtual void failedSave(std::uint32_t seq, uint256 const& hash) = 0;

    virtual std::string getCompleteLedgers () = 0;

    virtual void applyHeldTransactions () = 0;

    /** Get a ledger's hash by sequence number using the cache
    */
    virtual uint256 getHashBySeq (std::uint32_t index) = 0;

    /** Walk to a ledger's hash using the skip list
    */
    virtual uint256 walkHashBySeq (std::uint32_t index) = 0;
    virtual uint256 walkHashBySeq (
        std::uint32_t index, Ledger::ref referenceLedger) = 0;

    virtual Ledger::pointer getLedgerBySeq (std::uint32_t index) = 0;

    virtual Ledger::pointer getLedgerByHash (uint256 const& hash) = 0;

    virtual void setLedgerRangePresent (
        std::uint32_t minV, std::uint32_t maxV) = 0;

    virtual uint256 getLedgerHash(
        std::uint32_t desiredSeq, Ledger::ref knownGoodLedger) = 0;

    virtual void addHeldTransaction (Transaction::ref trans) = 0;
    virtual void fixMismatch (Ledger::ref ledger) = 0;

    virtual bool haveLedger (std::uint32_t seq) = 0;
    virtual void clearLedger (std::uint32_t seq) = 0;
    virtual bool getValidatedRange (
        std::uint32_t& minVal, std::uint32_t& maxVal) = 0;
    virtual bool getFullValidatedRange (
        std::uint32_t& minVal, std::uint32_t& maxVal) = 0;

    virtual void tune (int size, int age) = 0;
    virtual void sweep () = 0;
    virtual float getCacheHitRate () = 0;

    virtual void checkAccept (Ledger::ref ledger) = 0;
    virtual void checkAccept (uint256 const& hash, std::uint32_t seq) = 0;
    virtual void consensusBuilt (Ledger::ref ledger) = 0;

    virtual LedgerIndex getBuildingLedger () = 0;
    virtual void setBuildingLedger (LedgerIndex index) = 0;

    virtual void tryAdvance () = 0;
    virtual void newPathRequest () = 0;
    virtual bool isNewPathRequest () = 0;
    virtual void newOrderBookDB () = 0;

    virtual bool fixIndex (
        LedgerIndex ledgerIndex, LedgerHash const& ledgerHash) = 0;
    virtual void doLedgerCleaner(Json::Value const& parameters) = 0;

    virtual beast::PropertyStream::Source& getPropertySource () = 0;

    virtual void clearPriorLedgers (LedgerIndex seq) = 0;

    virtual void clearLedgerCachePrior (LedgerIndex seq) = 0;

    // ledger replay
    virtual void takeReplay (std::unique_ptr<LedgerReplay> replay) = 0;
    virtual std::unique_ptr<LedgerReplay> releaseReplay () = 0;

    // Fetch Packs
    virtual
    void gotFetchPack (
        bool progress,
        std::uint32_t seq) = 0;

    virtual
    void addFetchPack (
        uint256 const& hash,
        std::shared_ptr<Blob>& data) = 0;

    virtual
    bool getFetchPack (
        uint256 const& hash,
        Blob& data) = 0;

    virtual
    void makeFetchPack (
        std::weak_ptr<Peer> const& wPeer,
        std::shared_ptr<protocol::TMGetObjectByHash> const& request,
        uint256 haveLedgerHash,
        std::uint32_t uUptime) = 0;

    virtual
    std::size_t getFetchPackCacheSize () const = 0;
};

std::unique_ptr <LedgerMaster>
make_LedgerMaster (
    Application& app,
    Stopwatch& stopwatch,
    beast::Stoppable& parent,
    beast::insight::Collector::ptr const& collector,
    beast::Journal journal);

} // ripple

#endif
