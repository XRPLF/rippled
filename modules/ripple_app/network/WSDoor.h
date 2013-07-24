//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_WSDOOR_RIPPLEHEADER
#define RIPPLE_WSDOOR_RIPPLEHEADER

class WSDoor : LeakChecked <WSDoor>
{
private:
    websocketpp::server_autotls*    mSEndpoint;

    boost::thread*                  mThread;
    bool                            mPublic;
    std::string                     mIp;
    int                             mPort;

    void        startListening ();

public:

    WSDoor (const std::string& strIp, int iPort, bool bPublic) : mSEndpoint (0), mThread (0), mPublic (bPublic), mIp (strIp), mPort (iPort)
    {
        ;
    }

    void        stop ();

    static WSDoor* createWSDoor (const std::string& strIp, const int iPort, bool bPublic);
};

#endif

// vim:ts=4
