//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_WSDOOR_RIPPLEHEADER
#define RIPPLE_WSDOOR_RIPPLEHEADER

class WSDoor : protected Thread, LeakChecked <WSDoor>
{
public:
    WSDoor (std::string const& strIp, int iPort, bool bPublic);

    ~WSDoor ();

    void stop ();

private:
    void run ();

private:
    typedef RippleRecursiveMutex LockType;
    typedef LockType::ScopedLockType ScopedLockType;
    LockType m_endpointLock;

    ScopedPointer <websocketpp::server_autotls> m_endpoint;
    bool                            mPublic;
    std::string                     mIp;
    int                             mPort;
};

#endif

// vim:ts=4
