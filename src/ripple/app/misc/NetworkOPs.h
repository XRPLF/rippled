﻿//------------------------------------------------------------------------------
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

#include <ripple/app/consensus/RCLCxPeerPos.h>
#include <ripple/app/ledger/Ledger.h>
#include <ripple/core/JobQueue.h>
#include <ripple/ledger/ReadView.h>
#include <ripple/net/InfoSub.h>
#include <ripple/protocol/STValidation.h>
#include <ripple/protocol/messages.h>
#include <boost/asio.hpp>
#include <deque>
#include <memory>
#include <tuple>

namespace ripple {

// Operations that clients may wish to perform against the network
// Master operational handler, server sequencer, network tracker

class Peer;
class LedgerMaster;
class Transaction;
class ValidatorKeys;

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

/** Specifies the mode under which the server believes it's operating.

    This has implications about how the server processes transactions and
    how it responds to requests (e.g. account balance request).

    @note Other code relies on the numerical values of these constants; do
          not change them without verifying each use and ensuring that it is
          not a breaking change.
*/
enum class OperatingMode {
    DISCONNECTED = 0,  //!< not ready to process requests
    CONNECTED = 1,     //!< convinced we are talking to the network
    SYNCING = 2,       //!< fallen slightly behind
    TRACKING = 3,      //!< convinced we agree with the network
    FULL = 4           //!< we have the ledger and can even validate
};

namespace RPC {
enum class SubmitSync;
}

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
class NetworkOPs : public InfoSub::Source
{
public:
    using clock_type = beast::abstract_clock<std::chrono::steady_clock>;

    enum class FailHard : unsigned char { no, yes };
    static inline FailHard
    doFailHard(bool noMeansDont)
    {
        return noMeansDont ? FailHard::yes : FailHard::no;
    }

public:
    ~NetworkOPs() override = default;

    virtual void
    stop() = 0;

    //--------------------------------------------------------------------------
    //
    // Network information
    //

    virtual OperatingMode
    getOperatingMode() const = 0;
    virtual std::string
    strOperatingMode(OperatingMode const mode, bool const admin = false)
        const = 0;
    virtual std::string
    strOperatingMode(bool const admin = false) const = 0;

    //--------------------------------------------------------------------------
    //
    // Transaction processing
    //

    // must complete immediately
    virtual void
    submitTransaction(std::shared_ptr<STTx const> const&) = 0;

    /** Process a transaction.
     *
     * The transaction has been submitted either from the peer network or
     * from a client. For client submissions, there are 3 distinct behaviors:
     *   1) sync (default): process transactions in a batch immediately,
     *       and return only once the transaction has been processed.
     *   2) async: Put transaction into the batch for the next processing
     *       interval and return immediately.
     *   3) wait: Put transaction into the batch for the next processing
     *       interval and return only after it is processed.
     *
     * @param transaction Transaction object.
     * @param bUnlimited Whether a privileged client connection submitted it.
     * @param sync Client submission synchronous behavior type requested.
     * @param bLocal Whether submitted by client (local) or peer.
     * @param failType Whether to fail hard or not.
     */
    virtual void
    processTransaction(
        std::shared_ptr<Transaction>& transaction,
        bool bUnlimited,
        RPC::SubmitSync sync,
        bool bLocal,
        FailHard failType) = 0;

    /** Apply transactions in batches.
     *
     * Only a single batch unless drain is set. This is to optimize performance
     * because there is significant overhead in applying each batch, whereas
     * processing an individual transaction is fast.
     *
     * Setting the drain parameter is relevant for some transaction
     * processing unit tests that expect all submitted transactions to
     * be processed synchronously.
     *
     * @param drain Whether to process batches until none remain.
     * @return Whether any transactions were processed.
     */
    virtual bool
    transactionBatch(bool drain) = 0;

    //--------------------------------------------------------------------------
    //
    // Owner functions
    //

    virtual Json::Value
    getOwnerInfo(
        std::shared_ptr<ReadView const> lpLedger,
        AccountID const& account) = 0;

    //--------------------------------------------------------------------------
    //
    // Book functions
    //

    virtual void
    getBookPage(
        std::shared_ptr<ReadView const>& lpLedger,
        Book const& book,
        AccountID const& uTakerID,
        bool const bProof,
        unsigned int iLimit,
        Json::Value const& jvMarker,
        Json::Value& jvResult) = 0;

    //--------------------------------------------------------------------------

    // ledger proposal/close functions
    virtual bool
    processTrustedProposal(RCLCxPeerPos peerPos) = 0;

    virtual bool
    recvValidation(
        std::shared_ptr<STValidation> const& val,
        std::string const& source) = 0;

    virtual void
    mapComplete(std::shared_ptr<SHAMap> const& map, bool fromAcquire) = 0;

    // network state machine
    virtual bool
    beginConsensus(uint256 const& netLCL) = 0;
    virtual void
    endConsensus() = 0;
    virtual void
    setStandAlone() = 0;
    virtual void
    setStateTimer() = 0;
    virtual void
    setBatchApplyTimer() = 0;

    virtual void
    setNeedNetworkLedger() = 0;
    virtual void
    clearNeedNetworkLedger() = 0;
    virtual bool
    isNeedNetworkLedger() = 0;
    virtual bool
    isFull() = 0;
    virtual void
    setMode(OperatingMode om) = 0;
    virtual bool
    isBlocked() = 0;
    virtual bool
    isAmendmentBlocked() = 0;
    virtual void
    setAmendmentBlocked() = 0;
    virtual bool
    isAmendmentWarned() = 0;
    virtual void
    setAmendmentWarned() = 0;
    virtual void
    clearAmendmentWarned() = 0;
    virtual bool
    isUNLBlocked() = 0;
    virtual void
    setUNLBlocked() = 0;
    virtual void
    clearUNLBlocked() = 0;
    virtual void
    consensusViewChange() = 0;

    virtual Json::Value
    getConsensusInfo() = 0;
    virtual Json::Value
    getServerInfo(bool human, bool admin, bool counters) = 0;
    virtual void
    clearLedgerFetch() = 0;
    virtual Json::Value
    getLedgerFetchInfo() = 0;

    /** Accepts the current transaction tree, return the new ledger's sequence

        This API is only used via RPC with the server in STANDALONE mode and
        performs a virtual consensus round, with all the transactions we are
        proposing being accepted.
    */
    virtual std::uint32_t
    acceptLedger(
        std::optional<std::chrono::milliseconds> consensusDelay =
            std::nullopt) = 0;

    virtual void
    reportFeeChange() = 0;

    virtual void
    updateLocalTx(ReadView const& newValidLedger) = 0;
    virtual std::size_t
    getLocalTxCount() = 0;

    //--------------------------------------------------------------------------
    //
    // Monitoring: publisher side
    //
    virtual void
    pubLedger(std::shared_ptr<ReadView const> const& lpAccepted) = 0;
    virtual void
    pubProposedTransaction(
        std::shared_ptr<ReadView const> const& ledger,
        std::shared_ptr<STTx const> const& transaction,
        TER result) = 0;
    virtual void
    pubValidation(std::shared_ptr<STValidation> const& val) = 0;

    virtual void
    forwardValidation(Json::Value const& jvObj) = 0;
    virtual void
    forwardManifest(Json::Value const& jvObj) = 0;
    virtual void
    forwardProposedTransaction(Json::Value const& jvObj) = 0;
    virtual void
    forwardProposedAccountTransaction(Json::Value const& jvObj) = 0;

    virtual void
    stateAccounting(Json::Value& obj) = 0;
};

//------------------------------------------------------------------------------

std::unique_ptr<NetworkOPs>
make_NetworkOPs(
    Application& app,
    NetworkOPs::clock_type& clock,
    bool standalone,
    std::size_t minPeerCount,
    bool start_valid,
    JobQueue& job_queue,
    LedgerMaster& ledgerMaster,
    ValidatorKeys const& validatorKeys,
    boost::asio::io_service& io_svc,
    beast::Journal journal,
    beast::insight::Collector::ptr const& collector);

}  // namespace ripple

#endif
