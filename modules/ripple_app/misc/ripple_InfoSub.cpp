//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

// This is the primary interface into the "client" portion of the program.
// Code that wants to do normal operations on the network such as
// creating and monitoring accounts, creating transactions, and so on
// should use this interface. The RPC code will primarily be a light wrapper
// over this code.

// Eventually, it will check the node's operating mode (synched, unsynched,
// etectera) and defer to the correct means of processing. The current
// code assumes this node is synched (and will continue to do so until
// there's a functional network.

// VFALCO TODO Figure out how to clean up these globals

InfoSub::InfoSub (Source& source)
    : m_source (source)
    , mLock (this, "InfoSub", __FILE__, __LINE__)
{
    static Atomic <int> s_seq_id;
    mSeq = ++s_seq_id;
}

InfoSub::~InfoSub ()
{
    m_source.unsubTransactions (mSeq);
    m_source.unsubRTTransactions (mSeq);
    m_source.unsubLedger (mSeq);
    m_source.unsubServer (mSeq);
    m_source.unsubAccount (mSeq, mSubAccountInfo, true);
    m_source.unsubAccount (mSeq, mSubAccountInfo, false);
}

void InfoSub::send (const Json::Value& jvObj, const std::string& sObj, bool broadcast)
{
    send (jvObj, broadcast);
}

uint64 InfoSub::getSeq ()
{
    return mSeq;
}

void InfoSub::onSendEmpty ()
{
}

void InfoSub::insertSubAccountInfo (RippleAddress addr, uint32 uLedgerIndex)
{
    ScopedLockType sl (mLock, __FILE__, __LINE__);

    mSubAccountInfo.insert (addr);
}

void InfoSub::clearPathRequest ()
{
    mPathRequest.reset ();
}

void InfoSub::setPathRequest (const boost::shared_ptr<PathRequest>& req)
{
    mPathRequest = req;
}

const boost::shared_ptr<PathRequest>& InfoSub::getPathRequest ()
{
    return mPathRequest;
}
