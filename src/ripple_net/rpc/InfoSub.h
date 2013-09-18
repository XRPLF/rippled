//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_NET_RPC_INFOSUB_H_INCLUDED
#define RIPPLE_NET_RPC_INFOSUB_H_INCLUDED

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

    typedef boost::shared_ptr<InfoSub>          pointer;

    // VFALCO TODO Standardize on the names of weak / strong pointer typedefs.
    typedef boost::weak_ptr<InfoSub>            wptr;

    typedef const boost::shared_ptr<InfoSub>&   ref;

public:
    /** Abstracts the source of subscription data.
    */
    class Source : public Service
    {
    protected:
        Source (char const* name, Service& parent);

    public:
        // VFALCO TODO Rename the 'rt' parameters to something meaningful.
        virtual void subAccount (ref ispListener,
            const boost::unordered_set<RippleAddress>& vnaAccountIDs,
                uint32 uLedgerIndex, bool rt) = 0;
        
        virtual void unsubAccount (uint64 uListener,
            const boost::unordered_set<RippleAddress>& vnaAccountIDs,
                bool rt) = 0;

        // VFALCO TODO Document the bool return value
        virtual bool subLedger (ref ispListener,
            Json::Value& jvResult) = 0;
        
        virtual bool unsubLedger (uint64 uListener) = 0;

        virtual bool subServer (ref ispListener,
            Json::Value& jvResult) = 0;
        
        virtual bool unsubServer (uint64 uListener) = 0;

        virtual bool subBook (ref ispListener,
            const uint160& currencyPays, const uint160& currencyGets,
                const uint160& issuerPays, const uint160& issuerGets) = 0;
        
        virtual bool unsubBook (uint64 uListener,
            const uint160& currencyPays, const uint160& currencyGets,
                const uint160& issuerPays, const uint160& issuerGets) = 0;

        virtual bool subTransactions (ref ispListener) = 0;
        
        virtual bool unsubTransactions (uint64 uListener) = 0;

        virtual bool subRTTransactions (ref ispListener) = 0;
        
        virtual bool unsubRTTransactions (uint64 uListener) = 0;

        // VFALCO TODO Remove
        //             This was added for one particular partner, it
        //             "pushes" subscription data to a particular URL.
        //
        virtual pointer findRpcSub (const std::string& strUrl) = 0;

        virtual pointer addRpcSub (const std::string& strUrl, ref rspEntry) = 0;
    };

public:
    explicit InfoSub (Source& source);

    virtual ~InfoSub ();

    virtual void send (const Json::Value & jvObj, bool broadcast) = 0;

    // VFALCO NOTE Why is this virtual?
    virtual void send (const Json::Value & jvObj, const std::string & sObj, bool broadcast);

    uint64 getSeq ();

    void onSendEmpty ();

    void insertSubAccountInfo (RippleAddress addr, uint32 uLedgerIndex);

    void clearPathRequest ();

    void setPathRequest (const boost::shared_ptr<PathRequest>& req);

    boost::shared_ptr <PathRequest> const& getPathRequest ();

protected:
    typedef RippleMutex LockType;
    typedef LockType::ScopedLockType ScopedLockType;
    LockType mLock;

private:
    Source& m_source;
    boost::unordered_set <RippleAddress>        mSubAccountInfo;
    boost::unordered_set <RippleAddress>        mSubAccountTransaction;
    boost::shared_ptr <PathRequest>             mPathRequest;

    uint64                                      mSeq;
};

#endif
