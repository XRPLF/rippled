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

#ifndef RIPPLE_APP_MISC_NETWORKOPS_H_INCLUDED
#define RIPPLE_APP_MISC_NETWORKOPS_H_INCLUDED

#include <ripple/core/JobQueue.h>
#include <ripple/protocol/STValidation.h>
#include <ripple/app/ledger/Ledger.h>
#include <ripple/app/ledger/LedgerProposal.h>
#include <ripple/net/InfoSub.h>
#include <beast/cxx14/memory.h> // <memory>
#include <beast/threads/Stoppable.h>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <deque>
#include <tuple>

#include "ripple.pb.h"

namespace ripple {

// Operations that clients may wish to perform against the network
// Master operational handler, server sequencer, network tracker

class Peer;
class LedgerConsensus;
class LedgerMaster;

// This is the primary interface into the "client" portion of the program.
// Code that wants to do normal operations on the network such as
// creating and monitoring accounts, creating transactions, and so on
// should use this interface. The RPC code will primarily be a light wrapper
// over this code.
//
// Eventually, it will check the node's operating mode (synched, unsynched,
// etectera) and defer to the correct means of processing. The current
// code assumes this node is synched (and will continue to do so until
// there's a functional network.
//
/** Provides server functionality for clients.

    Clients include backend applications, local commands, and connected
    clients. This class acts as a proxy, fulfilling the command with local
    data if possible, or asking the network and returning the results if
    needed.

    A backend application or local client can trust a local instance of
    rippled / NetworkOPs. However, client software connecting to non-local
    instances of rippled will need to be hardened to protect against hostile
    or unreliable servers.
*/
class NetworkOPs
    : public InfoSub::Source
{
protected:
    explicit NetworkOPs (Stoppable& parent);

public:
    typedef beast::abstract_clock <std::chrono::steady_clock> clock_type;

    enum Fault
    {
        // exceptions these functions can throw
        IO_ERROR    = 1,
        NO_NETWORK  = 2,
    };

    enum OperatingMode
    {
        // how we process transactions or account balance requests
        omDISCONNECTED  = 0,    // not ready to process requests
        omCONNECTED     = 1,    // convinced we are talking to the network
        omSYNCING       = 2,    // fallen slightly behind
        omTRACKING      = 3,    // convinced we agree with the network
        omFULL          = 4     // we have the ledger and can even validate
    };

    // VFALCO TODO Fix OrderBookDB to not need this unrelated type.
    //
    typedef hash_map <std::uint64_t, InfoSub::wptr> SubMapType;

public:
    virtual ~NetworkOPs () = 0;

    //--------------------------------------------------------------------------
    //
    // Network information
    //

    // Our best estimate of wall time in seconds from 1/1/2000
    virtual std::uint32_t getNetworkTimeNC () const = 0;
    // Our best estimate of current ledger close time
    virtual std::uint32_t getCloseTimeNC () const = 0;
    // Use *only* to timestamp our own validation
    virtual std::uint32_t getValidationTimeNC () = 0;
    virtual void closeTimeOffset (int) = 0;
    virtual boost::posix_time::ptime getNetworkTimePT (int& offset) const = 0;
    virtual std::uint32_t getLedgerID (uint256 const& hash) = 0;
    virtual std::uint32_t getCurrentLedgerID () = 0;

    virtual OperatingMode getOperatingMode () const = 0;
    virtual std::string strOperatingMode () const = 0;
    virtual Ledger::pointer getClosedLedger () = 0;
    virtual Ledger::pointer getValidatedLedger () = 0;
    virtual Ledger::pointer getPublishedLedger () = 0;
    virtual Ledger::pointer getCurrentLedger () = 0;
    virtual Ledger::pointer getLedgerByHash (uint256 const& hash) = 0;
    virtual Ledger::pointer getLedgerBySeq (const std::uint32_t seq) = 0;
    virtual void            missingNodeInLedger (const std::uint32_t seq) = 0;

    virtual uint256         getClosedLedgerHash () = 0;

    // Do we have this inclusive range of ledgers in our database
    virtual bool haveLedgerRange (std::uint32_t from, std::uint32_t to) = 0;
    virtual bool haveLedger (std::uint32_t seq) = 0;
    virtual std::uint32_t getValidatedSeq () = 0;
    virtual bool isValidated (std::uint32_t seq) = 0;
    virtual bool isValidated (std::uint32_t seq, uint256 const& hash) = 0;
    virtual bool isValidated (Ledger::ref l) = 0;
    virtual bool getValidatedRange (std::uint32_t& minVal, std::uint32_t& maxVal) = 0;
    virtual bool getFullValidatedRange (std::uint32_t& minVal, std::uint32_t& maxVal) = 0;

    virtual STValidation::ref getLastValidation () = 0;
    virtual void setLastValidation (STValidation::ref v) = 0;

    //--------------------------------------------------------------------------
    //
    // Transaction processing
    //

    // must complete immediately
    // VFALCO TODO Make this a TxCallback structure
    typedef std::function<void (Transaction::pointer, TER)> stCallback;
    virtual void submitTransaction (Job&, STTx::pointer,
        stCallback callback = stCallback ()) = 0;
    virtual Transaction::pointer submitTransactionSync (Transaction::ref tpTrans,
        bool bAdmin, bool bLocal, bool bFailHard, bool bSubmit) = 0;
    virtual Transaction::pointer processTransactionCb (Transaction::pointer,
        bool bAdmin, bool bLocal, bool bFailHard, stCallback) = 0;
    virtual Transaction::pointer processTransaction (Transaction::pointer transaction,
        bool bAdmin, bool bLocal, bool bFailHard) = 0;
    virtual Transaction::pointer findTransactionByID (uint256 const& transactionID) = 0;
    virtual int findTransactionsByDestination (std::list<Transaction::pointer>&,
        RippleAddress const& destinationAccount, std::uint32_t startLedgerSeq,
            std::uint32_t endLedgerSeq, int maxTransactions) = 0;

    //--------------------------------------------------------------------------
    //
    // Account functions
    //

    virtual AccountState::pointer getAccountState (Ledger::ref lrLedger,
        RippleAddress const& accountID) = 0;

    //--------------------------------------------------------------------------
    //
    // Directory functions
    //

    virtual STVector256 getDirNodeInfo (Ledger::ref lrLedger,
        uint256 const& uRootIndex, std::uint64_t& uNodePrevious,
                                   std::uint64_t& uNodeNext) = 0;

    //--------------------------------------------------------------------------
    //
    // Owner functions
    //

    virtual Json::Value getOwnerInfo (Ledger::pointer lpLedger,
        RippleAddress const& naAccount) = 0;

    //--------------------------------------------------------------------------
    //
    // Book functions
    //

    virtual void getBookPage (
        bool bAdmin,
        Ledger::pointer lpLedger,
        Book const& book,
        Account const& uTakerID,
        bool const bProof,
        const unsigned int iLimit,
        Json::Value const& jvMarker,
        Json::Value& jvResult) = 0;

    //--------------------------------------------------------------------------

    // ledger proposal/close functions
    virtual void processTrustedProposal (LedgerProposal::pointer proposal,
        std::shared_ptr<protocol::TMProposeSet> set, RippleAddress nodePublic,
            uint256 checkLedger, bool sigGood) = 0;

    virtual bool recvValidation (STValidation::ref val,
        std::string const& source) = 0;

    virtual void takePosition (int seq,
                               std::shared_ptr<SHAMap> const& position) = 0;

    virtual void mapComplete (uint256 const& hash,
                              std::shared_ptr<SHAMap> const& map) = 0;

    // Fetch packs
    virtual void makeFetchPack (Job&, std::weak_ptr<Peer> peer,
        std::shared_ptr<protocol::TMGetObjectByHash> request,
        uint256 wantLedger, std::uint32_t uUptime) = 0;

    virtual bool shouldFetchPack (std::uint32_t seq) = 0;
    virtual void gotFetchPack (bool progress, std::uint32_t seq) = 0;
    virtual void addFetchPack (
        uint256 const& hash, std::shared_ptr< Blob >& data) = 0;
    virtual bool getFetchPack (uint256 const& hash, Blob& data) = 0;
    virtual int getFetchSize () = 0;
    virtual void sweepFetchPack () = 0;

    // network state machine
    virtual void endConsensus (bool correctLCL) = 0;
    virtual void setStandAlone () = 0;
    virtual void setStateTimer () = 0;

    virtual void newLCL (
        int proposers, int convergeTime, uint256 const& ledgerHash) = 0;
    // VFALCO TODO rename to setNeedNetworkLedger
    virtual void needNetworkLedger () = 0;
    virtual void clearNeedNetworkLedger () = 0;
    virtual bool isNeedNetworkLedger () = 0;
    virtual bool isFull () = 0;
    virtual void setProposing (bool isProposing, bool isValidating) = 0;
    virtual bool isProposing () = 0;
    virtual bool isValidating () = 0;
    virtual bool isAmendmentBlocked () = 0;
    virtual void setAmendmentBlocked () = 0;
    virtual void consensusViewChange () = 0;
    virtual int getPreviousProposers () = 0;
    virtual int getPreviousConvergeTime () = 0;
    virtual std::uint32_t getLastCloseTime () = 0;
    virtual void setLastCloseTime (std::uint32_t t) = 0;

    virtual Json::Value getConsensusInfo () = 0;
    virtual Json::Value getServerInfo (bool human, bool admin) = 0;
    virtual void clearLedgerFetch () = 0;
    virtual Json::Value getLedgerFetchInfo () = 0;
    virtual std::uint32_t acceptLedger () = 0;

    typedef hash_map <NodeID, std::deque<LedgerProposal::pointer>> Proposals;
    virtual Proposals& peekStoredProposals () = 0;

    virtual void storeProposal (LedgerProposal::ref proposal,
        RippleAddress const& peerPublic) = 0;

    virtual uint256 getConsensusLCL () = 0;

    virtual void reportFeeChange () = 0;

    virtual void updateLocalTx (Ledger::ref newValidLedger) = 0;
    virtual void addLocalTx (Ledger::ref openLedger, STTx::ref txn) = 0;
    virtual std::size_t getLocalTxCount () = 0;

    //Helper function to generate SQL query to get transactions
    virtual std::string transactionsSQL (std::string selection,
        RippleAddress const& account, std::int32_t minLedger, std::int32_t maxLedger,
        bool descending, std::uint32_t offset, int limit, bool binary,
            bool count, bool bAdmin) = 0;

    // client information retrieval functions
    typedef std::pair<Transaction::pointer, TransactionMetaSet::pointer>
    AccountTx;

    typedef std::vector<AccountTx> AccountTxs;

    virtual AccountTxs getAccountTxs (
        RippleAddress const& account,
        std::int32_t minLedger, std::int32_t maxLedger,  bool descending,
        std::uint32_t offset, int limit, bool bAdmin) = 0;

    virtual AccountTxs getTxsAccount (
        RippleAddress const& account,
        std::int32_t minLedger, std::int32_t maxLedger, bool forward,
        Json::Value& token, int limit, bool bAdmin) = 0;

    typedef std::tuple<std::string, std::string, std::uint32_t>
    txnMetaLedgerType;

    typedef std::vector<txnMetaLedgerType> MetaTxsList;

    virtual MetaTxsList getAccountTxsB (RippleAddress const& account,
        std::int32_t minLedger, std::int32_t maxLedger,  bool descending,
            std::uint32_t offset, int limit, bool bAdmin) = 0;

    virtual MetaTxsList getTxsAccountB (RippleAddress const& account,
        std::int32_t minLedger, std::int32_t maxLedger,  bool forward,
        Json::Value& token, int limit, bool bAdmin) = 0;

    virtual std::vector<RippleAddress> getLedgerAffectedAccounts (
        std::uint32_t ledgerSeq) = 0;

    //--------------------------------------------------------------------------
    //
    // Monitoring: publisher side
    //
    virtual void pubLedger (Ledger::ref lpAccepted) = 0;
    virtual void pubProposedTransaction (Ledger::ref lpCurrent,
        STTx::ref stTxn, TER terResult) = 0;
};

//------------------------------------------------------------------------------

std::unique_ptr<NetworkOPs>
make_NetworkOPs (NetworkOPs::clock_type& clock, bool standalone,
    std::size_t network_quorum, JobQueue& job_queue, LedgerMaster& ledgerMaster,
    beast::Stoppable& parent, beast::Journal journal);

} // ripple

#endif
