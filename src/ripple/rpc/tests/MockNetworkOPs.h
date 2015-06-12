//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2015 Ripple Labs Inc.

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

#ifndef RIPPLE_RPC_MOCKNETWORKOPS_H_INCLUDED
#define RIPPLE_RPC_MOCKNETWORKOPS_H_INCLUDED

#include <ripple/app/misc/NetworkOPs.h>

namespace ripple {
namespace RPC {

class MockNetworkOPs : public NetworkOPs
{
public:
    using NetworkOPs::AccountTx;
    using NetworkOPs::AccountTxs;
    using NetworkOPs::FailHard;
    using NetworkOPs::OperatingMode;

    MockNetworkOPs (beast::Stoppable& parent) : NetworkOPs (parent)
    {
    }

    virtual ~MockNetworkOPs ()
    {
    }

    virtual std::uint32_t getNetworkTimeNC () const
    {
        return {};
    }

    virtual std::uint32_t getCloseTimeNC () const
    {
        return {};
    }

    virtual std::uint32_t getValidationTimeNC ()
    {
        return {};
    }

    virtual void closeTimeOffset (int)
    {
    }

    virtual boost::posix_time::ptime getNetworkTimePT (int& offset) const
    {
        return {};
    }

    virtual std::uint32_t getLedgerID (uint256 const& hash)
    {
        return {};
    }

    virtual std::uint32_t getCurrentLedgerID ()
    {
        return {};
    }

    virtual OperatingMode getOperatingMode () const
    {
        return {};
    }

    virtual std::string strOperatingMode () const
    {
        return {};
    }

    virtual Ledger::pointer getClosedLedger ()
    {
        return {};
    }

    virtual Ledger::pointer getValidatedLedger ()
    {
        return {};
    }

    virtual Ledger::pointer getPublishedLedger ()
    {
        return {};
    }

    virtual Ledger::pointer getCurrentLedger ()
    {
        return {};
    }

    virtual Ledger::pointer getLedgerByHash (uint256 const& hash)
    {
        return {};
    }

    virtual Ledger::pointer getLedgerBySeq (const std::uint32_t seq)
    {
        return {};
    }

    virtual void            missingNodeInLedger (const std::uint32_t seq)
    {
    }

    virtual uint256         getClosedLedgerHash ()
    {
        return {};
    }

    virtual bool haveLedgerRange (std::uint32_t from, std::uint32_t to)
    {
        return {};
    }

    virtual bool haveLedger (std::uint32_t seq)
    {
        return {};
    }

    virtual std::uint32_t getValidatedSeq ()
    {
        return {};
    }

    virtual bool isValidated (std::uint32_t seq)
    {
        return {};
    }

    virtual bool isValidated (std::uint32_t seq, uint256 const& hash)
    {
        return {};
    }

    virtual bool isValidated (Ledger::ref l)
    {
        return {};
    }

    virtual bool getValidatedRange (std::uint32_t& minVal, std::uint32_t& maxVal)
    {
        return {};
    }

    virtual bool getFullValidatedRange (std::uint32_t& minVal, std::uint32_t& maxVal)
    {
        return {};
    }

    virtual STValidation::ref getLastValidation () {
        static STValidation::pointer const stv;
        return stv;
    }

    virtual void setLastValidation (STValidation::ref v)
    {
    }

    using stCallback = std::function<void (Transaction::pointer, TER)>;
    virtual void submitTransaction (Job&, STTx::pointer,
        stCallback callback = stCallback ()) { }
    virtual Transaction::pointer processTransactionCb (Transaction::pointer,
        bool bAdmin, bool bLocal, FailHard failType, stCallback)
    {
        return {};
    }

    virtual Transaction::pointer processTransaction (Transaction::pointer transaction,
        bool bAdmin, bool bLocal, FailHard failType)
    {
        return {};
    }

    virtual Transaction::pointer findTransactionByID (uint256 const& transactionID)
    {
        return {};
    }

    virtual int findTransactionsByDestination (std::list<Transaction::pointer>&,
        RippleAddress const& destinationAccount, std::uint32_t startLedgerSeq,
            std::uint32_t endLedgerSeq, int maxTransactions)
    {
        return {};
    }

    virtual AccountState::pointer getAccountState (Ledger::ref lrLedger,
        RippleAddress const& accountID)
    {
        return {};
    }

    virtual STVector256 getDirNodeInfo (Ledger::ref lrLedger,
        uint256 const& uRootIndex, std::uint64_t& uNodePrevious,
                                   std::uint64_t& uNodeNext)
    {
        return {};
    }

    virtual Json::Value getOwnerInfo (Ledger::pointer lpLedger,
        RippleAddress const& naAccount)
    {
        return {};
    }

    virtual void getBookPage (
        bool bAdmin,
        Ledger::pointer lpLedger,
        Book const& book,
        Account const& uTakerID,
        bool const bProof,
        const unsigned int iLimit,
        Json::Value const& jvMarker,
        Json::Value& jvResult)
    {
    }

    virtual void processTrustedProposal (LedgerProposal::pointer proposal,
        std::shared_ptr<protocol::TMProposeSet> set,
            RippleAddress const& nodePublic)
    {
    }

    virtual bool recvValidation (STValidation::ref val,
        std::string const& source)
    {
        return {};
    }

    virtual void takePosition (int seq,
                               std::shared_ptr<SHAMap> const& position)
    {
    }

    virtual void mapComplete (uint256 const& hash,
                              std::shared_ptr<SHAMap> const& map)
    {
    }

    virtual void makeFetchPack (Job&, std::weak_ptr<Peer> peer,
        std::shared_ptr<protocol::TMGetObjectByHash> request,
        uint256 wantLedger, std::uint32_t uUptime)
    {
    }

    virtual bool shouldFetchPack (std::uint32_t seq)
    {
        return {};
    }

    virtual void gotFetchPack (bool progress, std::uint32_t seq)
    {
    }

    virtual void addFetchPack (
        uint256 const& hash, std::shared_ptr< Blob >& data)
    {
    }

    virtual bool getFetchPack (uint256 const& hash, Blob& data)
    {
        return {};
    }

    virtual int getFetchSize ()
    {
        return {};
    }

    virtual void sweepFetchPack ()
    {
    }

    virtual void endConsensus (bool correctLCL)
    {
    }

    virtual void setStandAlone ()
    {
    }

    virtual void setStateTimer ()
    {
    }

    virtual void newLCL (
        int proposers, int convergeTime, uint256 const& ledgerHash)
    {
    }

    virtual void needNetworkLedger ()
    {
    }

    virtual void clearNeedNetworkLedger ()
    {
    }

    virtual bool isNeedNetworkLedger ()
    {
        return {};
    }

    virtual bool isFull ()
    {
        return {};
    }

    virtual void setProposing (bool isProposing, bool isValidating)
    {
    }

    virtual bool isProposing ()
    {
        return {};
    }

    virtual bool isValidating ()
    {
        return {};
    }

    virtual bool isAmendmentBlocked ()
    {
        return {};
    }

    virtual void setAmendmentBlocked ()
    {
    }

    virtual void consensusViewChange ()
    {
    }

    virtual std::uint32_t getLastCloseTime ()
    {
        return {};
    }

    virtual void setLastCloseTime (std::uint32_t t)
    {
    }

    virtual Json::Value getConsensusInfo ()
    {
        return {};
    }

    virtual Json::Value getServerInfo (bool human, bool admin)
    {
        return {};
    }

    virtual void clearLedgerFetch ()
    {
    }

    virtual Json::Value getLedgerFetchInfo ()
    {
        return {};
    }

    virtual std::uint32_t acceptLedger ()
    {
        return {};
    }

    using Proposals = hash_map <NodeID, std::deque<LedgerProposal::pointer>>;
    virtual Proposals& peekStoredProposals () {
        static Proposals proposals;
        return proposals;
    }

    virtual void storeProposal (LedgerProposal::ref proposal,
        RippleAddress const& peerPublic)
    {
    }

    virtual uint256 getConsensusLCL ()
    {
        return {};
    }

    virtual void reportFeeChange ()
    {
    }

    virtual void updateLocalTx (Ledger::ref newValidLedger)
    {
    }

    virtual void addLocalTx (Ledger::ref openLedger, STTx::ref txn)
    {
    }

    virtual std::size_t getLocalTxCount ()
    {
        return {};
    }

    virtual std::string transactionsSQL (std::string selection,
        RippleAddress const&, std::int32_t minLedger, std::int32_t maxLedger,
        bool descending, std::uint32_t offset, int limit, bool binary,
            bool count, bool bAdmin)
    {
        return {};
    }

    virtual AccountTxs getAccountTxs (
        RippleAddress const& account,
        std::int32_t minLedger, std::int32_t maxLedger,  bool descending,
        std::uint32_t offset, int limit, bool bAdmin)
    {
        return {};
    }

    virtual AccountTxs getTxsAccount (
        RippleAddress const& account,
        std::int32_t minLedger, std::int32_t maxLedger, bool forward,
        Json::Value& token, int limit, bool bAdmin)
    {
        return {};
    }

    virtual MetaTxsList getAccountTxsB (RippleAddress const& account,
        std::int32_t minLedger, std::int32_t maxLedger,  bool descending,
            std::uint32_t offset, int limit, bool bAdmin)
    {
        return {};
    }

    virtual MetaTxsList getTxsAccountB (RippleAddress const& account,
        std::int32_t minLedger, std::int32_t maxLedger,  bool forward,
        Json::Value& token, int limit, bool bAdmin)
    {
        return {};
    }

    virtual std::vector<RippleAddress> getLedgerAffectedAccounts (
        std::uint32_t ledgerSeq)
    {
        return {};
    }

    virtual void pubLedger (Ledger::ref lpAccepted)
    {
    }

    virtual void pubProposedTransaction (Ledger::ref lpCurrent,
        STTx::ref stTxn, TER terResult)
    {
    }

    virtual void subAccount (InfoSub::ref ispListener,
        const hash_set<RippleAddress>& vnaAccountIDs,
        bool realTime)
    {
    }

    virtual void unsubAccount (InfoSub::ref isplistener,
        const hash_set<RippleAddress>& vnaAccountIDs,
        bool realTime)
    {
    }

    virtual void unsubAccountInternal (std::uint64_t uListener,
        const hash_set<RippleAddress>& vnaAccountIDs,
        bool realTime)
    {
    }

    virtual bool subLedger (InfoSub::ref ispListener, Json::Value& jvResult)
    {
        return {};
    }

    virtual bool unsubLedger (std::uint64_t uListener)
    {
        return {};
    }

    virtual bool subServer (InfoSub::ref ispListener, Json::Value& jvResult,
        bool admin)
    {
        return {};
    }

    virtual bool unsubServer (std::uint64_t uListener)
    {
        return {};
    }

    virtual bool subBook (InfoSub::ref ispListener, Book const&)
    {
        return {};
    }

    virtual bool unsubBook (std::uint64_t uListener, Book const&)
    {
        return {};
    }

    virtual bool subTransactions (InfoSub::ref ispListener)
    {
        return {};
    }

    virtual bool unsubTransactions (std::uint64_t uListener)
    {
        return {};
    }

    virtual bool subRTTransactions (InfoSub::ref ispListener)
    {
        return {};
    }

    virtual bool unsubRTTransactions (std::uint64_t uListener)
    {
        return {};
    }

    virtual InfoSub::pointer findRpcSub (std::string const& strUrl)
    {
        return {};
    }

    virtual InfoSub::pointer addRpcSub (std::string const& strUrl, InfoSub::ref)
    {
        return {};
    }
};

} // RPC
} // ripple

#endif
