//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_INFOSUB_H
#define RIPPLE_INFOSUB_H

// Operations that clients may wish to perform against the network
// Master operational handler, server sequencer, network tracker

class PathRequest;

class InfoSub
    : public CountedObject <InfoSub>
{
public:
    char const* getCountedObjectName () { return "InfoSub"; }

    typedef boost::shared_ptr<InfoSub>          pointer;

    // VFALCO TODO Standardize on the names of weak / strong pointer typedefs.
    typedef boost::weak_ptr<InfoSub>            wptr;

    typedef const boost::shared_ptr<InfoSub>&   ref;

public:
    InfoSub ();

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
    // VFALCO TODO make accessor for this member
    boost::mutex                                mLockInfo;

private:
    // VFALCO TODO Move these globals to class instance
    static uint64                               sSeq;
    static boost::mutex                         sSeqLock;

    boost::unordered_set <RippleAddress>        mSubAccountInfo;
    boost::unordered_set <RippleAddress>        mSubAccountTransaction;
    boost::shared_ptr <PathRequest>             mPathRequest;

    uint64                                      mSeq;
};

#endif
