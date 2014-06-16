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

#ifndef RIPPLE_NET_RPC_INFOSUB_H_INCLUDED
#define RIPPLE_NET_RPC_INFOSUB_H_INCLUDED

namespace ripple {

// Operations that clients may wish to perform against the network
// Master operational handler, server sequencer, network tracker

class PathRequest;

/** Manages a client's subscription to data feeds.
*/
class InfoSub
    : public CountedObject <InfoSub>
{
public:
    static char const* getCountedObjectName () { return "InfoSub"; }

    typedef std::shared_ptr<InfoSub>          pointer;

    // VFALCO TODO Standardize on the names of weak / strong pointer typedefs.
    typedef std::weak_ptr<InfoSub>            wptr;

    typedef const std::shared_ptr<InfoSub>&   ref;

    typedef Resource::Consumer Consumer;

public:
    /** Abstracts the source of subscription data.
    */
    class Source : public beast::Stoppable
    {
    protected:
        Source (char const* name, beast::Stoppable& parent);

    public:
        // VFALCO TODO Rename the 'rt' parameters to something meaningful.
        virtual void subAccount (ref ispListener,
            const boost::unordered_set<RippleAddress>& vnaAccountIDs,
                std::uint32_t uLedgerIndex, bool rt) = 0;
        
        virtual void unsubAccount (std::uint64_t uListener,
            const boost::unordered_set<RippleAddress>& vnaAccountIDs,
                bool rt) = 0;

        // VFALCO TODO Document the bool return value
        virtual bool subLedger (ref ispListener,
            Json::Value& jvResult) = 0;
        
        virtual bool unsubLedger (std::uint64_t uListener) = 0;

        virtual bool subServer (ref ispListener,
            Json::Value& jvResult) = 0;
        
        virtual bool unsubServer (std::uint64_t uListener) = 0;

        virtual bool subBook (ref ispListener,
            RippleCurrency const& currencyPays, RippleCurrency const& currencyGets,
                RippleIssuer const& issuerPays, RippleIssuer const& issuerGets) = 0;
        
        virtual bool unsubBook (std::uint64_t uListener,
            RippleCurrency const& currencyPays, RippleCurrency const& currencyGets,
                RippleIssuer const& issuerPays, RippleIssuer const& issuerGets) = 0;

        virtual bool subTransactions (ref ispListener) = 0;
        
        virtual bool unsubTransactions (std::uint64_t uListener) = 0;

        virtual bool subRTTransactions (ref ispListener) = 0;
        
        virtual bool unsubRTTransactions (std::uint64_t uListener) = 0;

        // VFALCO TODO Remove
        //             This was added for one particular partner, it
        //             "pushes" subscription data to a particular URL.
        //
        virtual pointer findRpcSub (const std::string& strUrl) = 0;

        virtual pointer addRpcSub (const std::string& strUrl, ref rspEntry) = 0;
    };

public:
    InfoSub (Source& source, Consumer consumer);

    virtual ~InfoSub ();

    Consumer& getConsumer();

    virtual void send (const Json::Value & jvObj, bool broadcast) = 0;

    // VFALCO NOTE Why is this virtual?
    virtual void send (const Json::Value & jvObj, const std::string & sObj, bool broadcast);

    std::uint64_t getSeq ();

    void onSendEmpty ();

    void insertSubAccountInfo (RippleAddress addr, std::uint32_t uLedgerIndex);

    void clearPathRequest ();

    void setPathRequest (const std::shared_ptr<PathRequest>& req);

    std::shared_ptr <PathRequest> const& getPathRequest ();

protected:
    typedef RippleMutex LockType;
    typedef std::lock_guard <LockType> ScopedLockType;
    LockType mLock;

private:
    Consumer m_consumer;
    Source& m_source;
    boost::unordered_set <RippleAddress>        mSubAccountInfo;
    boost::unordered_set <RippleAddress>        mSubAccountTransaction;
    std::shared_ptr <PathRequest>             mPathRequest;

    std::uint64_t                               mSeq;
};

} // ripple

#endif
